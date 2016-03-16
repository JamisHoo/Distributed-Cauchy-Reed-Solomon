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
//struct rdma_private_data brick_pdata[NUM_BRICKS];

struct rdma_event_channel* cm_channel;
struct rdma_event_channel* brick_cm_channel[NUM_BRICKS];
struct rdma_cm_id* cm_id;
//struct rdma_cm_id* brick_cm_id[NUM_BRICKS];
struct rdma_cm_event* event;
//struct rdma_cm_event* brick_event[NUM_BRICKS];
struct rdma_conn_param conn_param = { };
//struct rdma_conn_param brick_conn_param[NUM_BRICKS] = { };
struct ibv_pd* pd;
//struct ibv_pd* brick_pd[NUM_BRICKS];
struct ibv_comp_channel* comp_chan;
//struct ibv_comp_channel* brick_comp_chan[NUM_BRICKS];
struct ibv_cq* cq;
//struct ibv_cq* brick_cq[NUM_BRICKS];
struct ibv_cq* evt_cq;
//struct ibv_cq* brick_evt_cq[NUM_BRICKS];
struct ibv_mr* mr_data;
struct ibv_mr* mr_output[NUM_BRICKS];
struct ibv_qp_init_attr qp_attr = { };
//struct ibv_qp_init_attr brick_qp_attr[NUM_BRICKS] = { };
struct ibv_sge sge_data;
//struct ibv_sge sge_send[NUM_BRICKS];
//struct ibv_send_wr send_wr[NUM_BRICKS] = { };
//struct ibv_send_wr* bad_send_wr[NUM_BRICKS];
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

void network_init() {
    /* Set up RDMA CM structures */

    cm_channel = rdma_create_event_channel();
    assert(cm_channel);

    err = rdma_create_id(cm_channel, &cm_id, 0, RDMA_PS_TCP);
    assert(err == 0);

    for (i = 0; i < NUM_BRICKS; ++i) {
        brick_cm_channel[i] = rdma_create_event_channel();
        assert(brick_cm_channel[i]);

        err = rdma_create_id(brick_cm_channel[i], &brick_cm_id[i], 0, RDMA_PS_TCP);
        assert(err == 0);
    }

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

    /*
    for (i = 0; i < NUM_BRICKS; ++i) {
        n = getaddrinfo(brick_ip[i], brick_port_string[i], &hints, &res);
        assert(n >= 0);
        
        for (t = res; t; t = t->ai_next) {
            err = rdma_resolve_addr(brick_cm_id[i], 0, t->ai_addr, RESOLVE_TIMEOUT_MS);
            if (!err) break;
        }
        assert(err == 0);

        err = rdma_get_cm_event(brick_cm_channel[i], &brick_event[i]);
        assert(err == 0);
        assert(brick_event[i]->event == RDMA_CM_EVENT_ADDR_RESOLVED);
        rdma_ack_cm_event(brick_event[i]);

        err = rdma_resolve_route(brick_cm_id[i], RESOLVE_TIMEOUT_MS);
        assert(err == 0);

        err = rdma_get_cm_event(brick_cm_channel[i], &brick_event[i]);
        assert(err == 0);
        assert(brick_event[i]->event == RDMA_CM_EVENT_ROUTE_RESOLVED);
        rdma_ack_cm_event(brick_event[i]);
    }
    */

    /* Create verbs objects now that we know which device to use */

    pd = ibv_alloc_pd(cm_id->verbs);
    assert(pd);

    comp_chan = ibv_create_comp_channel(cm_id->verbs);
    assert(comp_chan);

    cq = ibv_create_cq(cm_id->verbs, 10, 0, comp_chan, 0);
    assert(cq);

    err = ibv_req_notify_cq(cq, 0);
    assert(err == 0);

    mr_data = ibv_reg_mr(pd, data, DATA_SIZE / NUM_CLIENTS, 
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

    /*
    for (i = 0; i < NUM_BRICKS; ++i) {
        brick_pd[i] = ibv_alloc_pd(brick_cm_id[i]->verbs);
        assert(brick_pd[i]);

        brick_comp_chan[i] = ibv_create_comp_channel(brick_cm_id[i]->verbs);
        assert(brick_comp_chan[i]);

        brick_cq[i] = ibv_create_cq(brick_cm_id[i]->verbs, 10, 0, brick_comp_chan[i], 0);
        assert(brick_cq[i]);

        err = ibv_req_notify_cq(brick_cq[i], 0);
        assert(err == 0);

        mr_output[i] = ibv_reg_mr(brick_pd[i], output[i], DATA_SIZE / NUM_CLIENTS / COLUMN,
                                  IBV_ACCESS_LOCAL_WRITE);
        assert(mr_output[i]);

        brick_qp_attr[i].cap.max_send_wr = 10;
        brick_qp_attr[i].cap.max_send_sge = 10;
        brick_qp_attr[i].cap.max_recv_wr = 10;
        brick_qp_attr[i].cap.max_recv_sge = 10;
        brick_qp_attr[i].send_cq = brick_cq[i];
        brick_qp_attr[i].recv_cq = brick_cq[i];
        brick_qp_attr[i].qp_type = IBV_QPT_RC;

        err = rdma_create_qp(brick_cm_id[i], brick_pd[i], &brick_qp_attr[i]);
        assert(err == 0);
    }
    */
    /* Post receive for data before connecting */

    sge_data.addr = (uintptr_t)data;
    sge_data.length = DATA_SIZE / NUM_CLIENTS;
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

    /*
    for (i = 0; i < NUM_BRICKS; ++i) {
        brick_conn_param[i].private_data = &client_pdata;
        brick_conn_param[i].private_data_len = sizeof(client_pdata);
        brick_conn_param[i].initiator_depth = 1;
        brick_conn_param[i].retry_count = 7;
    }
    */

    /* Connect to bricks */

    /*
    for (i = 0; i < NUM_BRICKS; ++i) {
        err = rdma_connect(brick_cm_id[i], &brick_conn_param[i]);
        assert(err == 0);

        err = rdma_get_cm_event(brick_cm_channel[i], &brick_event[i]);
        assert(err == 0);
        assert(brick_event[i]->event == RDMA_CM_EVENT_ESTABLISHED);

        memcpy(&brick_pdata[i], brick_event[i]->param.conn.private_data, sizeof(brick_pdata[i]));
        assert(brick_pdata[i].index == i);
        rdma_ack_cm_event(brick_event[i]);

        printf("Client %d and Brick %d connected. \n", server_pdata.index, brick_pdata[i].index);
    }
    */
}

void network_release() {
    ibv_dereg_mr(mr_data);
    /*
    for (i = 0; i < NUM_BRICKS; ++i)
        ibv_dereg_mr(mr_output[i]);
    */
    
    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(cm_channel);
    
    /*
    for (i = 0; i < NUM_BRICKS; ++i) {
        rdma_destroy_id(brick_cm_id[i]);
        rdma_destroy_event_channel(brick_cm_channel[i]);
    }
    */
}

int main(int argc, char** argv) {
    network_init()

    network_release()
}
