#ifndef NODES_H_
#define NODES_H_

#include "ec/ec-method.h"

#define NUM_CLIENTS (2)

#define COLUMN (4)
#define ROW (COLUMN + 2)
#define NUM_BRICKS (ROW)

const int server_port = 5000;
const char server_port_string[] = "5000";
const char server_ip[] = "192.168.50.1";

#define RESOLVE_TIMEOUT_MS (5000)

#define BUFFER_HEADER_SIZE (1024)
#define BUFFER_BODY_SIZE (1L << 29)
#define BUFFER_SIZE (BUFFER_BODY_SIZE + BUFFER_HEADER_SIZE)
#define DECODE_BUFFER_BODY_SIZE (BUFFER_BODY_SIZE / COLUMN * ROW)
#define DECODE_BUFFER_SIZE (DECODE_BUFFER_BODY_SIZE + BUFFER_HEADER_SIZE)
// maximum allowed value of DECODE_BUFFER_SIZE is 1GiB

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

