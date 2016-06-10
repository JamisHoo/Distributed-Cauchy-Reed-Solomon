#ifndef __EC_GF8_H__
#define __EC_GF8_H__

#if defined(__SSE4_2__) && defined(__AVX__)
#define USE_AVX
#endif

#ifdef USE_AVX

#include <immintrin.h>
#define encode_t __m256i

#define XOR2(A,B) _mm256_xor_si256(A,B)
#define XOR3(A,B,C) _mm256_xor_si256(_mm256_xor_si256(A,B),C)
#define XOR4(A,B,C,D) _mm256_xor_si256(_mm256_xor_si256(A,B), _mm256_xor_si256(C,D))
#define XOR5(A,B,C,D,E) _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(A,B), _mm256_xor_si256(C,D)),E)

#else

#define encode_t uint64_t

#define XOR2(A,B) (A ^ B)
#define XOR3(A,B,C) (A ^ B ^ C)
#define XOR4(A,B,C,D) (A ^ B ^ C ^ D)
#define XOR5(A,B,C,D,E) (A ^ B ^ C ^ D ^ E)

#endif

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D
#define EC_GF_SIZE (1 << EC_GF_BITS)
#define EC_GF_WORD_SIZE sizeof(encode_t)

#include <inttypes.h>
#include <stddef.h>

extern void (* ec_gf_muladd[])(uint8_t* out, uint8_t* in);

#endif /* __EC_GF8_H__ */
