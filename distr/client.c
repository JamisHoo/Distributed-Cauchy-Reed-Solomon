#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <infiniband/arch.h>
#include <rdma/rdma_cma.h>

#include "nodes.h"

struct rdma_private_data server_pdata;
struct rdma_private_data client_pdata;

struct rdma_event_channel* cm_channel;
struct rdma_event_channel* brick_cm_channel[NUM_BRICKS];
struct rdma_cm_id* cm_id;
struct rdma_cm_event* event;
struct rdma_conn_param conn_param = { };
struct ibv_pd* pd;
struct ibv_comp_channel* comp_chan;
struct ibv_cq* cq;
struct ibv_cq* evt_cq;
struct ibv_mr* mr_data;
struct ibv_mr* mr_output[NUM_BRICKS];
struct ibv_qp_init_attr qp_attr = { };
struct ibv_sge sge_data;
struct ibv_recv_wr recv_wr = { };
struct ibv_recv_wr* bad_recv_wr;
struct ibv_wc wc;
void* cq_context;

struct addrinfo* res, *t;
struct addrinfo hints = {
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM
};
int n, i, err;
size_t size;

uint8_t* data;

void data_init() {
    data = (uint8_t*)malloc(BUFFER_SIZE);
}

void data_release() {
    free(data);
}

void network_init() {
    /* Set up RDMA CM structures */

    cm_channel = rdma_create_event_channel();
    assert(cm_channel);

    err = rdma_create_id(cm_channel, &cm_id, 0, RDMA_PS_TCP);
    assert(err == 0);

    /* Resolve server address and route */

    n = getaddrinfo(server_ip, server_port_string, &hints, &res); 
    assert(n >= 0);

    for (t = res; t; t = t->ai_next) {
        err = rdma_resolve_addr(cm_id, 0, t->ai_addr, RESOLVE_TIMEOUT_MS);
        if (!err) break;
    }
    assert(err == 0);

    err = rdma_get_cm_event(cm_channel, &event);
    assert(err == 0);
    assert(event->event == RDMA_CM_EVENT_ADDR_RESOLVED);

    rdma_ack_cm_event(event);

    err = rdma_resolve_route(cm_id, RESOLVE_TIMEOUT_MS);
    assert(err == 0);

    err = rdma_get_cm_event(cm_channel, &event);
    assert(err == 0);
    assert(event->event == RDMA_CM_EVENT_ROUTE_RESOLVED);
    rdma_ack_cm_event(event);

    /* Create verbs objects now that we know which device to use */

    pd = ibv_alloc_pd(cm_id->verbs);
    assert(pd);

    comp_chan = ibv_create_comp_channel(cm_id->verbs);
    assert(comp_chan);

    cq = ibv_create_cq(cm_id->verbs, 10, 0, comp_chan, 0);
    assert(cq);

    err = ibv_req_notify_cq(cq, 0);
    assert(err == 0);

    mr_data = ibv_reg_mr(pd, data, BUFFER_SIZE, 
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    assert(mr_data);

    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_send_sge = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_recv_sge = 10;
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;

    err = rdma_create_qp(cm_id, pd, &qp_attr);
    assert(err == 0);

    /* Post receive for data before connecting */

    sge_data.addr = (uintptr_t)data;
    sge_data.length = BUFFER_SIZE;
    sge_data.lkey = mr_data->lkey;

    recv_wr.sg_list = &sge_data;
    recv_wr.num_sge = 1;

    err = ibv_post_recv(cm_id->qp, &recv_wr, &bad_recv_wr);
    assert(err == 0);

    /* Construct connection params */

    client_pdata.data_va = htonll((uintptr_t)data);
    client_pdata.data_rkey = htonl(mr_data->rkey);

    conn_param.private_data = &client_pdata;
    conn_param.private_data_len = sizeof(client_pdata);
    conn_param.initiator_depth = 1;
    conn_param.retry_count = 7;

    /* Connect to server */

    err = rdma_connect(cm_id, &conn_param);
    assert(err == 0);

    err = rdma_get_cm_event(cm_channel, &event);
    assert(err == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    memcpy(&server_pdata, event->param.conn.private_data, sizeof(server_pdata));
    rdma_ack_cm_event(event);

    printf("My index == %d\n", server_pdata.index);

    /* Construct connection params */

    client_pdata.index = server_pdata.index;
}

void network_release() {
    ibv_dereg_mr(mr_data);
    
    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(cm_channel);
}

void post_receive() {
    /* Post receive for data */
    sge_data.addr = (uintptr_t)data;
    sge_data.length = BUFFER_SIZE;
    sge_data.lkey = mr_data->lkey;

    recv_wr.sg_list = &sge_data;
    recv_wr.num_sge = 1;

    err = ibv_post_recv(cm_id->qp, &recv_wr, &bad_recv_wr);
    assert(err == 0);
}

int wait_receive() {
    /* Wait for receive completion */
    err = ibv_get_cq_event(comp_chan, &evt_cq, &cq_context);
    if (err) return 1;

    ibv_ack_cq_events(evt_cq, 1);

    err = ibv_req_notify_cq(cq, 0);
    if (err) return 1;

    n = ibv_poll_cq(cq, 1, &wc);
    if (n < 0) return 1;
    if (wc.status != IBV_WC_SUCCESS) return 1;
    
    return 0;
}

void handle_data() {
    FILE* pfile;
    char filename[128];
    size_t size;

    sprintf(filename, "data%d", client_pdata.index);
    pfile = fopen(filename, "wb");
    size = fwrite(data, BUFFER_SIZE, 1, pfile); 
    assert(size == 1);
    fclose(pfile);
}

int main(int argc, char** argv) {
    data_init();
    network_init();

    while (1) {
        post_receive();

        wait_receive();

        handle_data();

        break;
    }

    network_release();
    data_release();
}
