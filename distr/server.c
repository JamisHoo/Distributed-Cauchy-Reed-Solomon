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
struct ibv_qp_init_attr qp_attr[NUM_CLIENTS] = { };
struct ibv_sge sge_send[NUM_CLIENTS];
struct ibv_send_wr send_wr[NUM_CLIENTS] = { };
struct ibv_send_wr* bad_send_wr[NUM_CLIENTS];
struct ibv_wc wc;

void* cq_context;
struct sockaddr_in sin;
int i, n, err;


void network_init() {
    /* Set up RDMA CM structures */

    cm_channel = rdma_create_event_channel();
    assert(cm_channel);

    err = rdma_create_id(cm_channel, &listen_id, 0, RDMA_PS_TCP);
    assert(err == 0);

    memset(&sin, 0x00, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(server_port); 
    sin.sin_addr.s_addr = INADDR_ANY;

    /* Bind to local port and listen for connection request */

    err = rdma_bind_addr(listen_id, (struct sockaddr*)&sin);
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

        mr_data[i] = ibv_reg_mr(pd[i], data + DATA_SIZE / NUM_CLIENTS * i, 
                                DATA_SIZE / NUM_CLIENTS, 
                                IBV_ACCESS_LOCAL_WRITE); 
        assert(mr_data[i]);


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
        rdma_destroy_id(cm_id[i]);
    }

    rdma_destroy_id(listen_id);
    rdma_destroy_event_channel(cm_channel);
}

int main(int argc, char** argv) {
    network_init();
    
    network_destroy();
}
