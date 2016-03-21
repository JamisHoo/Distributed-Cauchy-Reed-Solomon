#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <infiniband/arch.h>
#include <rdma/rdma_cma.h>

#include "nodes.h"

struct rdma_private_data server_pdata[NUM_CLIENTS];
struct rdma_private_data client_pdata[NUM_CLIENTS];

struct rdma_event_channel* cm_channel;
struct rdma_cm_id* listen_id;
struct rdma_cm_id* cm_id[NUM_CLIENTS];
struct rdma_cm_event* event[NUM_CLIENTS];
struct rdma_conn_param conn_param[NUM_CLIENTS] = { };
struct ibv_pd* pd[NUM_CLIENTS];
struct ibv_comp_channel* comp_chan[NUM_CLIENTS];
struct ibv_cq* cq[NUM_CLIENTS];
struct ibv_cq* evt_cq[NUM_CLIENTS];
struct ibv_mr* mr_data[NUM_CLIENTS];
struct ibv_mr* mr_ack_buffer[NUM_CLIENTS];
struct ibv_qp_init_attr qp_attr[NUM_CLIENTS] = { };
struct ibv_sge sge_send[NUM_CLIENTS];
struct ibv_sge sge_recv[NUM_CLIENTS];
struct ibv_send_wr send_wr[NUM_CLIENTS] = { };
struct ibv_send_wr* bad_send_wr[NUM_CLIENTS];
struct ibv_recv_wr recv_wr[NUM_CLIENTS] = { };
struct ibv_recv_wr* bad_recv_wr[NUM_CLIENTS];
struct ibv_wc wc;

void* cq_context;
struct sockaddr_in sockin;
int i, n, err;

uint8_t* data;
size_t data_size[NUM_CLIENTS];
size_t data_offset[NUM_CLIENTS];
char data_filename[PATH_MAX];
uint64_t ack_buffer[NUM_CLIENTS];

void data_init() {
    data = (uint8_t*)malloc(BUFFER_SIZE * NUM_CLIENTS);

    FILE* pfile;
    size_t file_size, client_size, size;

    sprintf(data_filename, "data");
    pfile = fopen(data_filename, "rb");
    fseek(pfile, 0L, SEEK_END);
    file_size = ftell(pfile);
    fseek(pfile, 0L, SEEK_SET);

    client_size = file_size / NUM_CLIENTS;
    if (client_size % ENCODE_BLOCK_SIZE) {
        client_size = (1 + client_size / ENCODE_BLOCK_SIZE) * ENCODE_BLOCK_SIZE;
        
        if (client_size > BUFFER_BODY_SIZE)
            client_size -= ENCODE_BLOCK_SIZE;
    }

    n = 0;
    for (i = 0; i < NUM_CLIENTS - 1; ++i) {
        data_size[i] = client_size;
        data_offset[i] = n;
        n += data_size[i];
    }
    data_offset[NUM_CLIENTS - 1] = n;
    data_size[NUM_CLIENTS - 1] = file_size - n;

    assert(data_size[NUM_CLIENTS - 1] <= BUFFER_BODY_SIZE);

    printf("file size == %lu \n", file_size);
    for (i = 0; i < NUM_CLIENTS; ++i)
        printf("data fragment %d: offset = %lu, size = %lu \n", i, data_offset[i], data_size[i]);

    for (i = 0; i < NUM_CLIENTS; ++i) {
        *(uint64_t*)(data + BUFFER_SIZE * i) = htonll(data_size[i]);
        *(uint64_t*)(data + BUFFER_SIZE * i + sizeof(uint64_t)) = htonll(data_offset[i]);
        sprintf(data + BUFFER_SIZE * i + 2 * sizeof(uint64_t), "data_%x", i);
        size = fread(data + BUFFER_SIZE * i + BUFFER_HEADER_SIZE,
                     data_size[i], 1, pfile);
        assert(size == 1);
    }
    
    fclose(pfile);
}

void data_release() {
    free(data);
}

void network_init() {
    /* Set up RDMA CM structures */

    cm_channel = rdma_create_event_channel();
    assert(cm_channel);

    err = rdma_create_id(cm_channel, &listen_id, 0, RDMA_PS_TCP);
    assert(err == 0);

    memset(&sockin, 0x00, sizeof(sockin));
    sockin.sin_family = AF_INET;
    sockin.sin_port = htons(server_port); 
    sockin.sin_addr.s_addr = INADDR_ANY;

    /* Bind to local port and listen for connection request */

    err = rdma_bind_addr(listen_id, (struct sockaddr*)&sockin);
    assert(err == 0);

    err = rdma_listen(listen_id, NUM_CLIENTS);
    assert(err == 0);

    for (i = 0; i < NUM_CLIENTS; ++i) {
        err = rdma_get_cm_event(cm_channel, &event[i]);
        assert(err == 0);
        assert(event[i]->event == RDMA_CM_EVENT_CONNECT_REQUEST);
        cm_id[i] = event[i]->id;
        memcpy(&client_pdata[i], event[i]->param.conn.private_data, sizeof(client_pdata[i]));
        rdma_ack_cm_event(event[i]);


        /* Create verbs objects now that we know which device to use */

        pd[i] = ibv_alloc_pd(cm_id[i]->verbs);
        assert(pd[i]);

        comp_chan[i] = ibv_create_comp_channel(cm_id[i]->verbs);
        assert(comp_chan[i]);
        
        cq[i] = ibv_create_cq(cm_id[i]->verbs, 10, 0, comp_chan[i], 0);
        assert(cq[i]);

        err = ibv_req_notify_cq(cq[i], 0);
        assert(err == 0);

        mr_data[i] = ibv_reg_mr(pd[i], data + BUFFER_SIZE * i, 
                                BUFFER_SIZE, 
                                IBV_ACCESS_LOCAL_WRITE); 
        assert(mr_data[i]);

        mr_ack_buffer[i] = ibv_reg_mr(pd[i], ack_buffer + i, 
                                      sizeof(ack_buffer[i]),
                                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        assert(mr_ack_buffer[i]);


        qp_attr[i].cap.max_send_wr = 10;
        qp_attr[i].cap.max_send_sge = 10;
        qp_attr[i].cap.max_recv_wr = 10;
        qp_attr[i].cap.max_recv_sge = 10;
        qp_attr[i].send_cq = cq[i];
        qp_attr[i].recv_cq = cq[i];
        qp_attr[i].qp_type = IBV_QPT_RC;
        
        err = rdma_create_qp(cm_id[i], pd[i], &qp_attr[i]);
        assert(err == 0);

        /* Construct connection params */
        server_pdata[i].index = i;
        server_pdata[i].ack_buffer_va = htonll((uintptr_t)ack_buffer + i);
        server_pdata[i].ack_buffer_rkey = htonl(mr_ack_buffer[i]->rkey);

        conn_param[i].responder_resources = 1;
        conn_param[i].private_data = &server_pdata[i];
        conn_param[i].private_data_len = sizeof(server_pdata[i]);
    }


    for (i = 0; i < NUM_CLIENTS; ++i) {
        /* Accept connection */

        err = rdma_accept(cm_id[i], &conn_param[i]);
        assert(err == 0);

        err = rdma_get_cm_event(cm_channel, &event[i]);
        assert(err == 0);
        assert(event[i]->event == RDMA_CM_EVENT_ESTABLISHED);

        rdma_ack_cm_event(event[i]);
    }
}

void network_release() {
    for (i = 0; i < NUM_CLIENTS; ++i) {
        ibv_dereg_mr(mr_data[i]);
        ibv_dereg_mr(mr_ack_buffer[i]);
        rdma_destroy_id(cm_id[i]);
    }

    rdma_destroy_id(listen_id);
    rdma_destroy_event_channel(cm_channel);
}

void post_send_data(int i, uint8_t* buffer, size_t length) {
    /* Send data to Client i */
    sge_send[i].addr = (uintptr_t)buffer;
    sge_send[i].length = length;
    sge_send[i].lkey = mr_data[i]->lkey;

    send_wr[i].wr_id = 1;
    send_wr[i].opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    send_wr[i].send_flags = IBV_SEND_SIGNALED;
    send_wr[i].sg_list = &sge_send[i];
    send_wr[i].num_sge = 1;
    send_wr[i].wr.rdma.rkey = ntohl(client_pdata[i].data_rkey);
    send_wr[i].wr.rdma.remote_addr = ntohll(client_pdata[i].data_va);

    err = ibv_post_send(cm_id[i]->qp, &send_wr[i], &bad_send_wr[i]);
    assert(err == 0);

    /* Wait for send completion */
    err = ibv_get_cq_event(comp_chan[i], &evt_cq[i], &cq_context); 
    assert(err == 0);

    ibv_ack_cq_events(evt_cq[i], 1);

    err = ibv_req_notify_cq(cq[i], 0);
    assert(err == 0);

    n = ibv_poll_cq(cq[i], 1, &wc);
    assert(n >= 1);
    assert(wc.status == IBV_WC_SUCCESS);
}

void post_receive_ack(int i) {
    /* Post receive for ack */
    ack_buffer[i] = 0;
    sge_recv[i].addr = (uintptr_t)(ack_buffer + i);
    sge_recv[i].length = sizeof(ack_buffer[i]);
    sge_recv[i].lkey = mr_ack_buffer[i]->lkey;

    recv_wr[i].wr_id = 1;
    recv_wr[i].sg_list = &sge_recv[i];
    recv_wr[i].num_sge = 1;
    
    err = ibv_post_recv(cm_id[i]->qp, &recv_wr[i], &bad_recv_wr[i]);
    assert(err == 0);
}

void wait_ack(int i) {
    /* Wait for ack */
    err = ibv_get_cq_event(comp_chan[i], &evt_cq[i], &cq_context);
    assert(err == 0);

    ibv_ack_cq_events(evt_cq[i], 1);

    err = ibv_req_notify_cq(cq[i], 0);
    assert(err == 0);

    n = ibv_poll_cq(cq[i], 1, &wc);
    assert(n >= 1);
    assert(wc.status == IBV_WC_SUCCESS);
}

int main(int argc, char** argv) {
    data_init();
    network_init();

    while (1) {
        printf("Press to start");
        getchar();

        for (i = 0; i < NUM_CLIENTS; ++i) {
            post_receive_ack(i);
            post_send_data(i, data + BUFFER_SIZE * i, BUFFER_HEADER_SIZE + data_size[i]);
            wait_ack(i);
            printf("%d ack \n", i);
        }

        break;
    }
    
    network_release();
    data_release();
}
