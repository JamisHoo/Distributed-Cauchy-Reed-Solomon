#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#include "ec-method.h"
#include "thpool.h"

#ifdef VECTOR
#include <immintrin.h>
#endif


static uint32_t bit[EC_GF_BITS];
static uint32_t GfPow[EC_GF_SIZE << 1];
static uint32_t GfLog[EC_GF_SIZE << 1];

static threadpool thpool;
static int processor_count;

typedef struct {
    size_t size;
    uint32_t columns, row;
    uint8_t* in;
    uint8_t* out;
} ec_encode_param_t;

typedef struct {
    size_t size;
    uint32_t columns;
    uint32_t total_rows;
    uint8_t* in;
    uint32_t* rows;
    uint8_t** out;
    size_t out_offset;
} ec_encode_batch_param_t;

typedef struct {
    size_t size;
    uint32_t columns;
    uint32_t* rows;
    uint8_t** in;
    uint8_t* out;
    size_t in_offset;

    size_t n_extra;
    int* recv_index;
    int* col_index;
    int* row_index;
    uint64_t* C;
    uint64_t* D;
    uint64_t* E;
    uint64_t* F;
} ec_decode_param_t;

void ec_method_initialize(int processor_count_) {
    uint32_t i;
    uint32_t carry_mask;

    bit[0] = 0x01;
    for (i = 1; i < EC_GF_BITS; ++i)
        bit[i] = bit[i - 1] << 1;
    carry_mask = bit[EC_GF_BITS - 1] << 1;

    GfPow[0] = 1;
    for (i = 1; i < EC_GF_SIZE - 1 + EC_GF_BITS - 1; ++i) {
        GfPow[i] = GfPow[i - 1] << 1;
        if (GfPow[i] & carry_mask)
            GfPow[i] ^= EC_GF_MOD;
    }

    GfLog[0] = -1;
    for (i = 0; i < EC_GF_SIZE - 1; ++i)
        GfLog[GfPow[i]] = i;

    thpool = thpool_init(processor_count_);
    processor_count = processor_count_;
}

size_t ec_method_encode_impl(size_t size, uint32_t columns, uint32_t row, 
                             uint8_t* in, uint8_t* out) {
    uint32_t i;
    uint32_t col, row_eqn, col_eqn, seg_num;
    uint32_t ExpFE;

    size /= EC_METHOD_CHUNK_SIZE * columns;

    if (row < columns) {
        in += EC_METHOD_CHUNK_SIZE * row;

        for (i = 0; i < size; ++i) {
            memcpy(out, in, EC_METHOD_CHUNK_SIZE);
            out += EC_METHOD_CHUNK_SIZE;
            in += EC_METHOD_CHUNK_SIZE * columns;
        }
        
        return size * EC_METHOD_CHUNK_SIZE;
    }

    memset(out, 0x00, size * EC_METHOD_CHUNK_SIZE);

    for (i = 0; i < size; ++i) {
        // for each message column
        for (col = 0; col < columns; ++col) {
            ExpFE = (EC_GF_SIZE - 1 - GfLog[(row - columns) ^ col ^ bit[EC_GF_BITS - 1]]) % (EC_GF_SIZE - 1);
            ec_gf_muladd[GfPow[ExpFE]](out, in);
            in += EC_METHOD_CHUNK_SIZE;
        }
        out += EC_METHOD_CHUNK_SIZE;
    }
    
    return size * EC_METHOD_CHUNK_SIZE;
}

size_t ec_method_batch_encode_impl(size_t size, uint32_t columns, 
                                   uint32_t total_rows, uint32_t* rows, 
                                   uint8_t* in, uint8_t** out, size_t out_offset) {
    uint32_t i, r;
    uint32_t col, row_eqn, col_eqn, seg_num;
    uint32_t ExpFE;

    uint8_t* in_ptr = in;
    uint8_t* out_ptrs[EC_METHOD_MAX_FRAGMENTS];
    for (i = 0; i < total_rows; ++i)
        out_ptrs[i] = out[i] + out_offset;

    size /= EC_METHOD_CHUNK_SIZE * columns;

    for (i = 0; i < size; ++i) {
        for (r = 0; r < total_rows; ++r) {
            if (rows[r] < columns) {
                memcpy(out_ptrs[r], in_ptr + rows[r] * EC_METHOD_CHUNK_SIZE, EC_METHOD_CHUNK_SIZE);
            } else {
                memset(out_ptrs[r], 0x00, EC_METHOD_CHUNK_SIZE);

                // for each message column
                for (col = 0; col < columns; ++col) {
                    ExpFE = (EC_GF_SIZE - 1 - GfLog[(rows[r] - columns) ^ col ^ bit[EC_GF_BITS - 1]]) % (EC_GF_SIZE - 1);
                    ec_gf_muladd[GfPow[ExpFE]](out_ptrs[r], in_ptr);
                    in_ptr += EC_METHOD_CHUNK_SIZE;
                }
                in_ptr -= EC_METHOD_CHUNK_SIZE * columns;
            }
            out_ptrs[r] += EC_METHOD_CHUNK_SIZE;
        }
        in_ptr += EC_METHOD_CHUNK_SIZE * columns;
    }
    return size * EC_METHOD_CHUNK_SIZE;
}

static void* ec_method_single_encode(void* param) {
    ec_encode_param_t* ec_param = (ec_encode_param_t*)param;
    size_t size = ec_param->size;
    uint32_t columns = ec_param->columns;
    uint32_t row = ec_param->row;
    uint8_t* in = ec_param->in;
    uint8_t* out = ec_param->out;

    ec_method_encode_impl(size, columns, row, in, out);

    return 0;
}

static void* ec_method_batch_single_encode(void* param) {
    ec_encode_batch_param_t* ec_param = (ec_encode_batch_param_t*)param;
    size_t size = ec_param->size;
    uint32_t columns = ec_param->columns;
    uint32_t total_rows = ec_param->total_rows;
    uint8_t* in = ec_param->in;
    uint8_t** out = ec_param->out;
    size_t out_offset = ec_param->out_offset;
    uint32_t* rows = ec_param->rows;

    ec_method_batch_encode_impl(size, columns, total_rows, rows, in, out, out_offset);

    return 0;
}

size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row,
                        uint8_t* in, uint8_t* out) {
    uint32_t i;
    size_t original_size = size;
    uint8_t* in_ptr = in;
    uint8_t* out_ptr = out;
    ec_encode_param_t* params = malloc(sizeof(ec_encode_param_t) * processor_count);
    size /= EC_METHOD_CHUNK_SIZE * columns;

    for (i = 0; i < processor_count; ++i) {
        params[i] = (ec_encode_param_t) {
            .size = (size / processor_count + (i < (size % processor_count))) * EC_METHOD_CHUNK_SIZE * columns,
            .columns = columns,
            .row = row,
            .in = in_ptr,
            .out = out_ptr
        };
        in_ptr += params[i].size;
        out_ptr += params[i].size / columns;
#if AUTO_THPOOL
        if (original_size < THR_THPOOL)
            ec_method_single_encode((void*)(params + i));
        else 
            thpool_add_work(thpool, ec_method_single_encode, (void*)(params + i));
#else
        thpool_add_work(thpool, ec_method_single_encode, (void*)(params + i));
#endif
    }
#if AUTO_THPOOL
    if (original_size >= THR_THPOOL)
        thpool_wait(thpool);
#else
    thpool_wait(thpool);
#endif

    free(params);
    return size * EC_METHOD_CHUNK_SIZE;
} 

size_t ec_method_batch_encode(size_t size, uint32_t columns, uint32_t total_rows,
                              uint32_t* rows, uint8_t* in, uint8_t** out) {
    uint32_t i;
    size_t original_size = size;
    size_t out_offset = 0;
    ec_encode_batch_param_t* params = malloc(sizeof(ec_encode_batch_param_t) * processor_count);
    size /= EC_METHOD_CHUNK_SIZE * columns;

    pthread_t *threads = malloc(sizeof(pthread_t)*processor_count);
    pthread_attr_t attr;
    cpu_set_t cpus;
    pthread_attr_init(&attr);

#if AUTO_THPOOL
    if (original_size < THR_THPOOL) {
        params[0] = (ec_encode_batch_param_t) {
            .size = original_size,
            .columns = columns,
            .total_rows = total_rows,
            .rows = rows,
            .in = in,
            .out = out,
            .out_offset = out_offset
        };
        ec_method_batch_single_encode((void*)params);
    } else {
#endif
        for (i = 0; i < processor_count; ++i) {
            params[i] = (ec_encode_batch_param_t) {
                .size = (size / processor_count + (i < (size % processor_count))) * EC_METHOD_CHUNK_SIZE * columns,
                .columns = columns,
                .total_rows = total_rows,
                .rows = rows,
                .in = in,
                .out = out,
                .out_offset = out_offset
            };
            in += params[i].size;
            out_offset += params[i].size / columns;

            CPU_ZERO(&cpus);
            CPU_SET(i, &cpus);
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
            pthread_create(threads + i, &attr, ec_method_batch_single_encode, (void*)(params + i));
        }

        for(i = 0; i < processor_count; ++i) 
            pthread_join(threads[i], NULL);
#if AUTO_THPOOL
    }
#endif

    free(threads);
    free(params);
    return size * EC_METHOD_CHUNK_SIZE;
}

size_t ec_method_decode_impl(size_t size, uint32_t columns, uint32_t* rows, uint8_t** in, uint8_t* out, size_t in_offset,
                             size_t n_extra, int* recv_index, int* row_index, int* col_index,
                             uint64_t* C, uint64_t* D, uint64_t* E, uint64_t* F) {
    int i, j, k, l, s, seg_num;
    int col_ind, row_ind, col_eqn, row_eqn;
    size_t n_recv = columns;
    int32_t ExpFE;
    encode_t M[EC_METHOD_WIDTH * EC_GF_BITS * EC_METHOD_MAX_FRAGMENTS] __attribute__((aligned(EC_GF_WORD_SIZE)));

    uint8_t* out_ptr;
    uint8_t* in_ptrs[EC_METHOD_MAX_FRAGMENTS];

    size /= EC_METHOD_CHUNK_SIZE;

    out_ptr = (uint8_t*)out;
    for (i = 0; i < n_recv; ++i)
        in_ptrs[i] = (uint8_t*)(in[i] + in_offset);

    for (s = 0; s < size; ++s) {

        row_ind = 0;
        for (i = 0; i < n_recv; ++i) 
            if (rows[i] >= columns) {
                memcpy(M + row_ind * EC_GF_BITS * EC_METHOD_WIDTH, in_ptrs[i], EC_METHOD_CHUNK_SIZE);
                ++row_ind;
                if (row_ind >= n_extra) 
                    break;
            }
        
        // Adjust M array according to the equations and the contents of output data

        for (row_ind = 0; row_ind < n_extra; ++row_ind) 
            for (col_ind = 0; col_ind < columns; ++col_ind) 
                if (recv_index[col_ind]) {
                    ExpFE = (EC_GF_SIZE - 1 - GfLog[row_index[row_ind] ^ col_ind ^ bit[EC_GF_BITS - 1]]) % (EC_GF_SIZE - 1);

                    j = EC_METHOD_WIDTH * row_ind * EC_GF_BITS;
                    k = EC_METHOD_WORD_SIZE * col_ind * EC_GF_BITS;
                    ec_gf_muladd[GfPow[ExpFE]]((uint8_t*)(M + j), out_ptr + k);
                }

        // Fill in the recovered information in the message from the inverted
        // matrix and from M

        for (row_ind = 0; row_ind < n_extra; ++row_ind)
            for (col_ind = 0; col_ind < n_extra; ++col_ind) {
                ExpFE = E[col_ind] + F[row_ind] - C[col_ind] - D[row_ind] - 
                        GfLog[row_index[col_ind] ^ col_index[row_ind] ^ bit[EC_GF_BITS - 1]];
                if (ExpFE < 0)
                    ExpFE = EC_GF_SIZE - 1 - ((-ExpFE) % (EC_GF_SIZE - 1));
                ExpFE %= EC_GF_SIZE - 1;

                j = col_index[row_ind] * EC_GF_BITS * EC_METHOD_WORD_SIZE;
                l = col_ind * EC_GF_BITS * EC_METHOD_WIDTH;
                ec_gf_muladd[GfPow[ExpFE]](out_ptr + j, (uint8_t*)(M + l));
            }
        
        out_ptr += EC_METHOD_CHUNK_SIZE * columns;
        for (i = 0; i < n_recv; ++i)
            in_ptrs[i] += EC_METHOD_CHUNK_SIZE;
    }

    return size * columns * EC_METHOD_CHUNK_SIZE;
}

void* ec_method_single_decode(void* param) {
    ec_decode_param_t* ec_param = (ec_decode_param_t*)param;
    size_t size = ec_param->size;
    uint32_t columns = ec_param->columns;
    uint32_t* rows = ec_param->rows;
    uint8_t** in = ec_param->in;
    uint8_t* out = ec_param->out;
    size_t in_offset = ec_param->in_offset;
    
    size_t n_extra = ec_param->n_extra;
    int* recv_index = ec_param->recv_index;
    int* col_index = ec_param->col_index;
    int* row_index = ec_param->row_index;
    uint64_t* C = ec_param->C;
    uint64_t* D = ec_param->D;
    uint64_t* E = ec_param->E;
    uint64_t* F = ec_param->F;

    ec_method_decode_impl(size, columns, rows, in, out, in_offset,
                          n_extra, recv_index, row_index, col_index,
                          C, D, E, F);
    return 0;
}


size_t ec_method_decode(size_t size, uint32_t columns, uint32_t* rows, 
                        uint8_t** in, uint8_t* out) {
    int recv_index[EC_METHOD_MAX_FRAGMENTS];
    int col_index[EC_METHOD_MAX_FRAGMENTS];
    int row_index[EC_METHOD_MAX_FRAGMENTS];
    uint64_t C[EC_METHOD_MAX_FRAGMENTS] = { 0 };
    uint64_t D[EC_METHOD_MAX_FRAGMENTS] = { 0 };
    uint64_t E[EC_METHOD_MAX_FRAGMENTS] = { 0 };
    uint64_t F[EC_METHOD_MAX_FRAGMENTS] = { 0 };

    int n_first_recv, n_extra;
    int col_ind, row_ind;
    int i, s;
    uint32_t index;
    size_t in_offset;

    size_t n_recv = columns;

    memset(out, 0x00, size * columns);

    size /= EC_METHOD_CHUNK_SIZE;
    
    n_first_recv = 0;
    for (i = 0; i < columns; ++i)
        recv_index[i] = 0;

    for (i = 0; i < n_recv; ++i) {
        index = rows[i];
        if (index < columns) {
            recv_index[index] = 1;

            for (s = 0; s < size; ++s)
                memcpy(out + EC_METHOD_CHUNK_SIZE * index + s * EC_METHOD_CHUNK_SIZE * columns,
                       in[i] + s * EC_METHOD_CHUNK_SIZE, 
                       EC_METHOD_CHUNK_SIZE);

            n_first_recv++;
        }
    }

    n_extra = columns - n_first_recv;

    col_ind = 0;
    for (i = 0; i < columns; ++i)
        if (!recv_index[i])
            col_index[col_ind++] = i;

    row_ind = 0;
    for (i = 0; i < n_recv; ++i) 
        if (rows[i] >= columns) {
            row_index[row_ind] = rows[i] - columns;

            ++row_ind;
            if (row_ind >= n_extra)
                break;
        }

    // Compute the determinant of the matrix in the finite field and then
    // compute the inverse matrix

    for (row_ind = 0; row_ind < n_extra; ++row_ind)
        for (col_ind = 0; col_ind < n_extra; ++col_ind) {
            if (col_ind != row_ind) {
                C[row_ind] += GfLog[row_index[row_ind] ^ row_index[col_ind]];
                D[col_ind] += GfLog[col_index[row_ind] ^ col_index[col_ind]];
            }
            E[row_ind] += GfLog[row_index[row_ind] ^ col_index[col_ind] ^ bit[EC_GF_BITS - 1]];
            F[col_ind] += GfLog[row_index[row_ind] ^ col_index[col_ind] ^ bit[EC_GF_BITS - 1]];
        }

    ec_decode_param_t* params = malloc(sizeof(ec_decode_param_t) * processor_count);
    in_offset = 0;
    for (i = 0; i < processor_count; ++i) {
        params[i] = (ec_decode_param_t) {
            .size = (size / processor_count + (i < (size % processor_count))) * EC_METHOD_CHUNK_SIZE,
            .columns = columns,
            .rows = rows,
            .in = in,
            .out = out,
            .in_offset = in_offset,
            .n_extra = n_extra,
            .recv_index = recv_index,
            .col_index = col_index,
            .row_index = row_index,
            .C = C, .D = D, .E = E, .F = F
        };
        in_offset += params[i].size;
        out += params[i].size * columns;
        thpool_add_work(thpool, ec_method_single_decode, (void*)(params + i));
    }
    thpool_wait(thpool);
    free(params);

    return size * EC_METHOD_CHUNK_SIZE * columns;
}
