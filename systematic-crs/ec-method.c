#include <string.h>

#include "ec-method.h"

#ifdef VECTOR
#include <immintrin.h>
#endif


static uint32_t bit[EC_GF_BITS];
static uint32_t GfPow[EC_GF_SIZE << 1];
static uint32_t GfLog[EC_GF_SIZE << 1];

void ec_method_initialize(void) {
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
}

size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row, 
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
            ec_gf_muladd[GfPow[ExpFE]](out, in, EC_METHOD_WIDTH);
            in += EC_METHOD_CHUNK_SIZE;
        }
        out += EC_METHOD_CHUNK_SIZE;
    }
    
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
    encode_t M[EC_METHOD_WIDTH * EC_GF_BITS * EC_METHOD_MAX_FRAGMENTS] __attribute__((aligned(EC_GF_WORD_SIZE)));
    
    int n_first_recv, n_extra;
    int col_ind, row_ind;
    int col_eqn, row_eqn;
    int i, j, k, l, s, seg_num;
    uint32_t index;
    int32_t ExpFE;

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
    
    uint8_t* out_ptr;
    uint8_t* in_ptrs[EC_METHOD_MAX_FRAGMENTS];
    out_ptr = (uint8_t*)out;
    for (i = 0; i < n_recv; ++i)
        in_ptrs[i] = (uint8_t*)in[i];

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
                    ec_gf_muladd[GfPow[ExpFE]]((uint8_t*)(M + j), out_ptr + k, EC_METHOD_WIDTH);
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
                ec_gf_muladd[GfPow[ExpFE]](out_ptr + j, (uint8_t*)(M + l), EC_METHOD_WIDTH);
            }
        
        out_ptr += EC_METHOD_CHUNK_SIZE * columns;
        for (i = 0; i < n_recv; ++i)
            in_ptrs[i] += EC_METHOD_CHUNK_SIZE;
    }

    return size * columns * EC_METHOD_CHUNK_SIZE;

}
