#ifndef NODES_H_
#define NODES_H_

#define NUM_CLIENTS (4)

const int server_port = 5000;
const char server_port_string[] = "5000";
const char server_ip[] = "192.168.50.1";

#define RESOLVE_TIMEOUT_MS (5000)
#define BUFFER_HEADER_SIZE (1024)
#define BUFFER_BODY_SIZE (1 << 27)
#define BUFFER_SIZE (BUFFER_BODY_SIZE + BUFFER_HEADER_SIZE)
#define COLUMN (16)
#define ROW (COLUMN / 4 * 5)

#define NUM_BRICKS (ROW)

#define ENCODE_BLOCK_SIZE (1024) // TODO

const int brick_port[NUM_BRICKS] = { 5011, 5012, 5013, 5014, 5015 };
const char* brick_port_string[NUM_BRICKS] = { "5011", "5012", "5013", "5014", "5015" };
const char* brick_ip[NUM_BRICKS] = { "192.168.50.1", "192.168.50.1", "192.168.50.1", "192.168.50.1", "192.168.50.1" };

struct rdma_private_data {
    int index;
    uint64_t data_va;
    uint64_t ack_buffer_va;
    uint32_t data_rkey;
    uint32_t ack_buffer_rkey;
};

#endif /* NODES_H_ */

