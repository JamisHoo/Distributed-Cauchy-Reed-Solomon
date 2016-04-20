#ifndef __EC_METHOD_H__
#define __EC_METHOD_H__

#include <inttypes.h>
#include <stddef.h>

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D
#define EC_GF_SIZE (1 << EC_GF_BITS)
#define EC_GF_WORD_SIZE sizeof(uint64_t)

#define EC_METHOD_MAX_FRAGMENTS 256
#define EC_METHOD_MAX_NODES (EC_GF_SIZE - 1)
#define EC_METHOD_WORD_SIZE 64
#define EC_METHOD_CHUNK_SIZE (EC_METHOD_WORD_SIZE * EC_GF_BITS)
#define EC_METHOD_WIDTH (EC_METHOD_WORD_SIZE / EC_GF_WORD_SIZE)

#if defined(__SSE4_2__) && defined(__AVX__)
#define VECTOR
#endif

void ec_method_initialize(void);
size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row,
                        uint8_t* in, uint8_t* out);
size_t ec_method_decode(size_t size, uint32_t columns, uint32_t* rows,
                        uint8_t** in, uint8_t* out);

#endif /* __EC_METHOD_H__ */
