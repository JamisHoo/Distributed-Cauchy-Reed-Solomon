#ifndef NODES_H_
#define NODES_H_

#include "ec/ec-method.h"

#define NUM_CLIENTS (4)

const int server_port = 5000;
const char server_port_string[] = "5000";
const char server_ip[] = "192.168.50.1";

#define RESOLVE_TIMEOUT_MS (5000)
#define BUFFER_HEADER_SIZE (1024)
#define BUFFER_BODY_SIZE (1L << 29) // maximum allowed value is 512MB
#define BUFFER_SIZE (BUFFER_BODY_SIZE + BUFFER_HEADER_SIZE)
#define COLUMN (4)
#define ROW (COLUMN / 4 * 5)

#define NUM_BRICKS (ROW)

#define ENCODE_BLOCK_SIZE (EC_METHOD_CHUNK_SIZE * COLUMN) 

#define WAIT_ACK_TIMEVAL (1)

struct rdma_private_data {
    int index;
    uint64_t data_va;
    uint64_t ack_buffer_va;
    uint32_t data_rkey;
    uint32_t ack_buffer_rkey;
};

#endif /* NODES_H_ */

