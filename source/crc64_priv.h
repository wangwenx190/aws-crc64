#ifndef AWS_CRC64_PRIV_H
#define AWS_CRC64_PRIV_H

/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 *
 * Internal header for the self-contained CRC64-NVME library. It replaces the pieces of
 * aws-c-common (macros.h, config.h, cpuid.h, byte_order.h) that the original aws-checksums
 * CRC64 sources relied on. Not installed.
 */

#include <aws/checksums/crc64.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#    include <stdlib.h> /* _byteswap_uint64 */
#endif

/*
 * ---------------------------------------------------------------------------
 * Architecture detection
 * ---------------------------------------------------------------------------
 * The AWS_CRC64_HAVE_* feature macros are supplied by the build system (see CMakeLists.txt)
 * and only tell us that a given source file could be *compiled*. The arch macros below tell
 * us what we are actually targeting, and runtime detection tells us what we may *execute*.
 */
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
#    define AWS_CRC64_ARCH_INTEL_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define AWS_CRC64_ARCH_ARM64 1
#endif

/*
 * ---------------------------------------------------------------------------
 * Compiler helpers
 * ---------------------------------------------------------------------------
 */
#if defined(_MSC_VER)
#    define AWS_CRC64_ALIGNED_TYPEDEF(from, to, alignment) typedef __declspec(align(alignment)) from to
#else
#    define AWS_CRC64_ALIGNED_TYPEDEF(from, to, alignment) typedef from to __attribute__((aligned(alignment)))
#endif

#if defined(__GNUC__) || defined(__clang__)
#    define AWS_CRC64_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#    define AWS_CRC64_UNLIKELY(x) (x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ---------------------------------------------------------------------------
 * Runtime CPU feature detection (source/crc64_cpuid.c)
 * ---------------------------------------------------------------------------
 */
enum aws_crc64_cpu_feature {
    AWS_CRC64_CPU_FEATURE_CLMUL = 0,
    AWS_CRC64_CPU_FEATURE_SSE_4_2,
    AWS_CRC64_CPU_FEATURE_AVX2,
    AWS_CRC64_CPU_FEATURE_AVX512,
    AWS_CRC64_CPU_FEATURE_VPCLMULQDQ,
    AWS_CRC64_CPU_FEATURE_ARM_PMULL,
    AWS_CRC64_CPU_FEATURE_ARM_CRYPTO,

    AWS_CRC64_CPU_FEATURE_COUNT
};

bool aws_crc64_cpu_has_feature(enum aws_crc64_cpu_feature feature);

/*
 * ---------------------------------------------------------------------------
 * Byte order helpers
 * ---------------------------------------------------------------------------
 */
static inline bool aws_crc64_is_big_endian(void) {
    const uint16_t z = 0x100;
    return *(const uint8_t *)&z != 0;
}

/* Reverse the bytes in a 64-bit word, but only on big-endian platforms. */
static inline uint64_t aws_crc64_bswap64_if_be(uint64_t x) {
    if (!aws_crc64_is_big_endian()) {
        return x;
    }

    /* Check the builtins first - clang on Windows defines _MSC_VER too. */
#if defined(__clang__) && __clang_major__ >= 3
    return __builtin_bswap64(x);
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)
    return _byteswap_uint64(x);
#else
    return ((x << 56) & 0xff00000000000000ULL) | ((x << 40) & 0x00ff000000000000ULL) |
           ((x << 24) & 0x0000ff0000000000ULL) | ((x << 8) & 0x000000ff00000000ULL) |
           ((x >> 8) & 0x00000000ff000000ULL) | ((x >> 24) & 0x0000000000ff0000ULL) |
           ((x >> 40) & 0x000000000000ff00ULL) | ((x >> 56) & 0x00000000000000ffULL);
#endif
}

/*
 * ---------------------------------------------------------------------------
 * Implementations
 * ---------------------------------------------------------------------------
 * aws_checksums_crc64nvme_sw() and aws_checksums_crc64nvme_combine_sw() are declared in the
 * public header; the accelerated variants are internal.
 */

#if defined(AWS_CRC64_HAVE_CLMUL_INTRINSICS) && defined(AWS_CRC64_ARCH_INTEL_X64)
uint64_t aws_checksums_crc64nvme_intel_clmul(const uint8_t *input, int length, uint64_t previous_crc64);
#endif

#if defined(AWS_CRC64_HAVE_AVX512_INTRINSICS) && defined(AWS_CRC64_ARCH_INTEL_X64)
uint64_t aws_checksums_crc64nvme_intel_avx512(const uint8_t *input, int length, uint64_t previous_crc64);
#endif

#if defined(AWS_CRC64_HAVE_ARM_PMULL) && defined(AWS_CRC64_ARCH_ARM64)
uint64_t aws_checksums_crc64nvme_arm_pmull(const uint8_t *input, int length, uint64_t previous_crc64);

uint64_t aws_checksums_crc64nvme_combine_arm_pmull(uint64_t crc1, uint64_t crc2, uint64_t len2);
#endif

/*
 * ---------------------------------------------------------------------------
 * Pre-computed constants (source/crc64.c)
 * ---------------------------------------------------------------------------
 */
typedef struct {
    uint64_t x2048[8];        /* x^2112 mod P(x) / x^2048 mod P(x) */
    uint64_t x1536[8];        /* x^1600 mod P(x) / x^1536 mod P(x) */
    uint64_t x1024[8];        /* x^1088 mod P(x) / x^1024 mod P(x) */
    uint64_t x512[8];         /* x^576  mod P(x) / x^512  mod P(x) */
    uint64_t x384[2];         /* x^448  mod P(x) / x^384  mod P(x) */
    uint64_t x256[2];         /* x^320  mod P(x) / x^256  mod P(x) */
    uint64_t x128[2];         /* x^192  mod P(x) / x^128  mod P(x) */
    uint64_t mu_poly[2];      /* Barrett mu / polynomial P(x) */
    uint64_t trailing[15][2]; /* Folding constants for 15 possible trailing input data lengths */
    uint64_t shift_factors[16][16][2];
} aws_checksums_crc64_constants_t;

extern uint8_t aws_checksums_masks_shifts[6][16];
extern aws_checksums_crc64_constants_t aws_checksums_crc64nvme_constants;

/*
 * ---------------------------------------------------------------------------
 * Misc helpers (source/crc64_util.c)
 * ---------------------------------------------------------------------------
 */
/**
 * Carry-less multiply of two bit-reflected field elements modulo the bit-reflected CRC64
 * polynomial. `poly` is the low 64 bits of the 65 bit polynomial.
 */
uint64_t aws_checksums_multiply_mod_p_reflected(uint64_t poly, uint64_t a, uint64_t b);

/* Applies a checksum function that takes an int length over a buffer of up to size_t length. */
static inline uint64_t aws_crc64_large_buffer_apply(
    uint64_t (*checksum_fn)(const uint8_t *, int, uint64_t),
    const uint8_t *buffer,
    size_t length,
    uint64_t previous) {

    uint64_t val = previous;
    while (length > (size_t)INT_MAX) {
        val = checksum_fn(buffer, INT_MAX, val);
        buffer += (size_t)INT_MAX;
        length -= (size_t)INT_MAX;
    }
    val = checksum_fn(buffer, (int)length, val);
    return val;
}

#ifdef __cplusplus
}
#endif

#endif /* AWS_CRC64_PRIV_H */
