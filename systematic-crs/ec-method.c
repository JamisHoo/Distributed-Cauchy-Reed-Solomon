#include <string.h>
#include <pthread.h>

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

    uint64_t* in_ptr = (uint64_t*)in;
    uint64_t* out_ptr = (uint64_t*)out;
    
    for (i = 0; i < size; ++i) {
        // for each message column
        for (col = 0; col < columns; ++col) {
            ExpFE = (EC_GF_SIZE - 1 - GfLog[(row - columns) ^ col ^ bit[EC_GF_BITS - 1]]) % (EC_GF_SIZE - 1);

            // for each row in a single galois field element matrix
            for (row_eqn = 0; row_eqn < EC_GF_BITS; ++row_eqn) {
                
                // for each bit in a single galios field element matrix row
                for (col_eqn = 0; col_eqn < EC_GF_BITS; ++col_eqn) {

                    // if this bit is 1
                    if (GfPow[ExpFE + row_eqn] & bit[col_eqn]) {
                        
                        // for each word
#ifdef VECTOR
                        for (seg_num = 0; seg_num < EC_METHOD_WORD_SIZE / sizeof(__m256); ++seg_num)
                            ((__m256*)out_ptr)[seg_num] = _mm256_xor_ps(((__m256*)out_ptr)[seg_num], ((__m256*)in_ptr)[seg_num]);
#else
                        for (seg_num = 0; seg_num < EC_METHOD_WIDTH; ++seg_num)
                            out_ptr[seg_num] ^= in_ptr[seg_num];
#endif

                    }
                    in_ptr += EC_METHOD_WIDTH;
                }
                in_ptr -= EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE;
                out_ptr += EC_METHOD_WIDTH;
            }
            in_ptr += EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE;
            out_ptr -= EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE;
        }
        out_ptr += EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE;
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

size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row, 
                        uint8_t* in, uint8_t* out) {
    uint32_t i, j;
    uint8_t* in_ptr = in;
    uint8_t* out_ptr = out;
    size_t original_size = size;
    pthread_t* threads = malloc(sizeof(pthread_t) * processor_count);
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
    free(threads);
    return size * EC_METHOD_CHUNK_SIZE;
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
#ifdef VECTOR
    uint64_t M[EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE * EC_METHOD_MAX_FRAGMENTS] __attribute__((aligned(sizeof(__m256))));
#else
    uint64_t M[EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE * EC_METHOD_MAX_FRAGMENTS];
#endif
    int n_first_recv, n_extra;
    int col_ind, row_ind;
    int col_eqn, row_eqn;
    int i, j, k, l, s, seg_num;
    uint32_t index;
    int32_t ExpFE;

    size_t n_recv = columns;

#ifdef VECTOR
    __m256* M_vec_ptr;
    __m256* out_vec_ptr;
#endif

    uint64_t* out_ptr;
    uint64_t* in_ptrs[EC_METHOD_MAX_FRAGMENTS];
    
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
    
    out_ptr = (uint64_t*)out;
    for (i = 0; i < n_recv; ++i)
        in_ptrs[i] = (uint64_t*)in[i];

    for (s = 0; s < size; ++s) {

        row_ind = 0;
        for (i = 0; i < n_recv; ++i) 
            if (rows[i] >= columns) {
                memcpy(M + row_ind * EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE, in_ptrs[i], EC_METHOD_CHUNK_SIZE);
                ++row_ind;
                if (row_ind >= n_extra) 
                    break;
            }
        
        // Adjust M array according to the equations and the contents of output data

        for (row_ind = 0; row_ind < n_extra; ++row_ind) 
            for (col_ind = 0; col_ind < columns; ++col_ind) 
                if (recv_index[col_ind]) {
                    ExpFE = (EC_GF_SIZE - 1 - GfLog[row_index[row_ind] ^ col_ind ^ bit[EC_GF_BITS - 1]]) % (EC_GF_SIZE - 1);
                    for (row_eqn = 0; row_eqn < EC_GF_BITS; ++row_eqn) {
                        j = EC_METHOD_WIDTH * (row_eqn + row_ind * EC_GF_BITS);
                        for (col_eqn = 0; col_eqn < EC_GF_BITS; ++col_eqn) {
                            k = EC_METHOD_WIDTH * (col_eqn + col_ind * EC_GF_BITS);
                            if (GfPow[ExpFE + row_eqn] & bit[col_eqn]) {
#ifdef VECTOR
                                M_vec_ptr = (__m256*)(M + j);
                                out_vec_ptr = (__m256*)(out_ptr +k);
                                for (seg_num = 0; seg_num < EC_METHOD_WORD_SIZE / sizeof(__m256); ++seg_num)
                                    M_vec_ptr[seg_num] = _mm256_xor_ps(M_vec_ptr[seg_num], out_vec_ptr[seg_num]);
#else
                                for (seg_num = 0; seg_num < EC_METHOD_WIDTH; ++seg_num) 
                                    M[seg_num + j] ^= out_ptr[seg_num + k];
#endif
                            }
                        }
                    }
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
                
                j = col_index[row_ind] * EC_GF_BITS * EC_METHOD_WIDTH;
                for (row_eqn = 0; row_eqn < EC_GF_BITS; ++row_eqn) {
                    k = row_eqn * EC_METHOD_WIDTH + j;
                    for (col_eqn = 0; col_eqn < EC_GF_BITS; ++col_eqn) {
                        l = EC_METHOD_WIDTH * (col_eqn + col_ind * EC_GF_BITS);
                        if (GfPow[ExpFE + row_eqn] & bit[col_eqn]) {
#ifdef VECTOR
                            M_vec_ptr = (__m256*)(M + l);
                            out_vec_ptr = (__m256*)(out_ptr + k);

                            for (seg_num = 0; seg_num < EC_METHOD_WORD_SIZE / sizeof(__m256); ++seg_num)
                            out_vec_ptr[seg_num] = _mm256_xor_ps(out_vec_ptr[seg_num], M_vec_ptr[seg_num]);
#else
                            for (seg_num = 0; seg_num < EC_METHOD_WIDTH; ++seg_num)
                                out_ptr[seg_num + k] ^= M[l++];
#endif
                        }
                    }
                }
            }
        
        out_ptr += EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE * columns;
        for (i = 0; i < n_recv; ++i)
            in_ptrs[i] += EC_METHOD_CHUNK_SIZE / EC_GF_WORD_SIZE;
    }

    return size * columns * EC_METHOD_CHUNK_SIZE;

}
