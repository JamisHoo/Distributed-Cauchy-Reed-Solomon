#include <stdbool.h>
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
struct ibv_cq* recv_cq;
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
uint64_t wr_count = 1;
int i, j, n, err;

uint8_t* data;
char data_filename[PATH_MAX];
uint64_t ack_buffer[NUM_CLIENTS];
int num_finished_jobs = 0;

struct Job {
    size_t data_size;
    size_t data_offset;
    uint8_t* data_ptr;
    bool finished;
    bool broken;
    uint64_t wr_id;
    uint64_t timeout;
};

struct Job jobs[NUM_CLIENTS];

void data_init() {
    for (i = 0; i < NUM_CLIENTS; ++i) {
        jobs[i].finished = 0;
        jobs[i].broken = 0;
    }

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

    size = 0;
    for (i = 0; i < NUM_CLIENTS - 1; ++i) {
        jobs[i].data_size = client_size;
        jobs[i].data_offset = size;
        size += jobs[i].data_size;
    }
    jobs[NUM_CLIENTS - 1].data_offset = size;
    jobs[NUM_CLIENTS - 1].data_size = file_size - size;

    if (jobs[NUM_CLIENTS - 1].data_size > BUFFER_BODY_SIZE) {
        printf("File too large\n");
        assert(0);
    }

    printf("file size == %lu \n", file_size);
    for (i = 0; i < NUM_CLIENTS; ++i)
        printf("data fragment %d: offset = %lu, size = %lu \n", i, jobs[i].data_offset, jobs[i].data_size);

    for (i = 0; i < NUM_CLIENTS; ++i) {
        *(uint64_t*)(data + BUFFER_SIZE * i) = htonll(jobs[i].data_size);
        *(uint64_t*)(data + BUFFER_SIZE * i + sizeof(uint64_t)) = htonll(jobs[i].data_offset);
        strcpy(data + BUFFER_SIZE * i + 2 * sizeof(uint64_t), data_filename);
        jobs[i].data_ptr = data + BUFFER_SIZE * i;
        size = fread(jobs[i].data_ptr + BUFFER_HEADER_SIZE,
                     jobs[i].data_size, 1, pfile);
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

        if (!i) {
            recv_cq = ibv_create_cq(cm_id[i]->verbs, NUM_CLIENTS * 3, 0, 0, 0);
            assert(recv_cq);
        }

        cq[i] = ibv_create_cq(cm_id[i]->verbs, 2, 0, comp_chan[i], 0);
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


        qp_attr[i].cap.max_send_wr = 1;
        qp_attr[i].cap.max_send_sge = 1;
        qp_attr[i].cap.max_recv_wr = NUM_CLIENTS;
        qp_attr[i].cap.max_recv_sge = NUM_CLIENTS;
        qp_attr[i].send_cq = cq[i];
        qp_attr[i].recv_cq = recv_cq;
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
        rdma_destroy_qp(cm_id[i]);
        ibv_dereg_mr(mr_data[i]);
        ibv_dereg_mr(mr_ack_buffer[i]);
        ibv_destroy_cq(cq[i]);
        if (!i) ibv_destroy_cq(recv_cq);
        ibv_destroy_comp_channel(comp_chan[i]);
        rdma_destroy_id(cm_id[i]);
    }

    rdma_destroy_id(listen_id);
    rdma_destroy_event_channel(cm_channel);
}

void post_send_data(int i, uint8_t* buffer, size_t length) {
    uint64_t expected_wr_id;

    /* Send data to Client i */
    sge_send[i].addr = (uintptr_t)buffer;
    sge_send[i].length = length;
    sge_send[i].lkey = mr_data[i]->lkey;

    expected_wr_id = wr_count++;
    send_wr[i].wr_id = expected_wr_id;
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
    assert(wc.wr_id == expected_wr_id);
    assert(n >= 1);
    assert(wc.status == IBV_WC_SUCCESS);
}

uint64_t post_receive_ack(int i) {
    uint64_t expected_wr_id = wr_count++;
    /* Post receive for ack */
    ack_buffer[i] = 0;
    sge_recv[i].addr = (uintptr_t)(ack_buffer + i);
    sge_recv[i].length = sizeof(ack_buffer[i]);
    sge_recv[i].lkey = mr_ack_buffer[i]->lkey;

    recv_wr[i].wr_id = expected_wr_id;
    recv_wr[i].sg_list = &sge_recv[i];
    recv_wr[i].num_sge = 1;
    
    err = ibv_post_recv(cm_id[i]->qp, &recv_wr[i], &bad_recv_wr[i]);
    assert(err == 0);

    return expected_wr_id;
}

uint64_t wait_ack() {
    /* Wait for ack */

    n = ibv_poll_cq(recv_cq, 1, &wc);
    if (n == 0) 
        return 0;
    else if (n == 1) 
        return wc.wr_id;
    else 
        assert(0);
}

uint64_t calc_timeout(size_t length) {
    size_t encode_rate = 128 * 1024 * 1024; // 128MB/s
    return 5 + length / encode_rate;
}

int main(int argc, char** argv) {
    uint64_t ack_id;

    data_init();
    network_init();

    printf("Press to start");
    getchar();

    for (i = 0; i < NUM_CLIENTS; ++i) {
        jobs[i].wr_id = post_receive_ack(i);
        jobs[i].timeout = time(0) + calc_timeout(jobs[i].data_size);
        post_send_data(i, jobs[i].data_ptr, BUFFER_HEADER_SIZE + jobs[i].data_size);
    }

    while (1) {
        ack_id = wait_ack();

        if (ack_id == 0) {
            // find timeout
            i = 0; j = 0;
            for (i = 0; i < NUM_CLIENTS; ++i)
                if (jobs[i].broken == 0 && jobs[i].finished == 0 && jobs[i].timeout < time(0)) { 
                    // find a spare client
                    for (j = 0; j < NUM_CLIENTS; ++j) 
                        if (j != i && jobs[j].finished == 1 && jobs[j].broken == 0) 
                            break;
                    break;
                }
            
            if (i < NUM_CLIENTS && j < NUM_CLIENTS) {
                printf("Client %d failed, transfer job to Client %d\n", i, j);
                jobs[i].broken = 1;
                jobs[j].data_size = jobs[i].data_size;
                jobs[j].data_offset = jobs[i].data_offset;
                memcpy(jobs[j].data_ptr, jobs[i].data_ptr, BUFFER_HEADER_SIZE + jobs[j].data_size);
                jobs[j].finished = 0;
                jobs[j].broken = 0;
                jobs[j].wr_id = post_receive_ack(j);
                jobs[j].timeout = time(0) + calc_timeout(jobs[j].data_size);
                post_send_data(j, jobs[j].data_ptr, BUFFER_HEADER_SIZE + jobs[j].data_size);
            } else if (i < NUM_CLIENTS && j == NUM_CLIENTS) {
                printf("Client %d failed, cannot find valid client\n", i);
                sleep(WAIT_ACK_TIMEVAL);
            } else {
                sleep(WAIT_ACK_TIMEVAL);
            }
        } else {
            // wr ack
            for (i = 0; i < NUM_CLIENTS; ++i)
                if (jobs[i].wr_id == ack_id) {
                    jobs[i].finished = 1;
                    ++num_finished_jobs;
                    printf("Client %d finished\n", i);
                    break;
                }
            if (num_finished_jobs == NUM_CLIENTS) break;
        }
    }

    network_release();
    data_release();
}
