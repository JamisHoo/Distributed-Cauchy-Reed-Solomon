#ifndef __EC_GF8_H__
#define __EC_GF8_H__

#if defined(__SSE4_2__) && defined(__AVX__)
#define USE_AVX
#endif

#ifdef USE_AVX

#include <immintrin.h>
#define encode_t __m256

#define XOR1(A) (A)
#define XOR2(A,B) _mm256_xor_ps(A,B)
#define XOR3(A,B,C) _mm256_xor_ps(_mm256_xor_ps(A,B),C)
#define XOR4(A,B,C,D) _mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(A,B),C),D)
#define XOR5(A,B,C,D,E) _mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(A,B),C),D),E)
#define XOR6(A,B,C,D,E,F) _mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(A,B),C),D),E),F)
#define XOR7(A,B,C,D,E,F,G) _mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(A,B),C),D),E),F),G)
#define XOR8(A,B,C,D,E,F,G,H) _mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(A,B),C),D),E),F),G),H)
#define XOR9(A,B,C,D,E,F,G,H,I) _mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(_mm256_xor_ps(A,B),C),D),E),F),G),H),I)

#else

#define encode_t uint64_t

#define XOR1(A) (A)
#define XOR2(A,B) (A ^ B)
#define XOR3(A,B,C) (A ^ B ^ C)
#define XOR4(A,B,C,D) (A ^ B ^ C ^ D)
#define XOR5(A,B,C,D,E) (A ^ B ^ C ^ D ^ E)
#define XOR6(A,B,C,D,E,F) (A ^ B ^ C ^ D ^ E ^ F)
#define XOR7(A,B,C,D,E,F,G) (A ^ B ^ C ^ D ^ E ^ F ^ G)
#define XOR8(A,B,C,D,E,F,G,H) (A ^ B ^ C ^ D ^ E ^ F ^ G ^ H)
#define XOR9(A,B,C,D,E,F,G,H,I) (A ^ B ^ C ^ D ^ E ^ F ^ G ^ H ^ I)

#endif

#define EC_GF_BITS 8
#define EC_GF_MOD 0x11D
#define EC_GF_SIZE (1 << EC_GF_BITS)
#define EC_GF_WORD_SIZE sizeof(encode_t)

#include <inttypes.h>
#include <stddef.h>

extern void (* ec_gf_muladd[])(uint8_t* out, uint8_t* in, size_t width);

#endif /* __EC_GF8_H__ */
