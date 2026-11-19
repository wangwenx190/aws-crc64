/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include "crc64_priv.h"

#if defined(_MSC_VER)
#    include <intrin.h>
#endif

/* Count trailing zeros. x must not be 0. */
static inline int s_ctz64(uint64_t x) {
    /* Check the builtins first - clang on Windows defines _MSC_VER too. */
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER)
    unsigned long index = 0;
#    if defined(_M_X64) || defined(_M_ARM64)
    _BitScanForward64(&index, x);
    return (int)index;
#    else
    if (_BitScanForward(&index, (unsigned long)(x & 0xffffffffu))) {
        return (int)index;
    }
    _BitScanForward(&index, (unsigned long)(x >> 32));
    return (int)index + 32;
#    endif
#else
    int n = 0;
    while (!(x & 1)) {
        x >>= 1;
        ++n;
    }
    return n;
#endif
}

/**
 * Carry-less multiply of two bit-reflected field elements modulo the bit-reflected CRC64
 * polynomial.
 *
 * The original aws-checksums implementation used __uint128_t, which is unavailable on MSVC.
 * The 65 bit polynomial always has its x^64 term set, and the running y factor is immediately
 * right shifted after every reduction, so that term never has to be materialized - which keeps
 * the whole computation inside 64 bit registers and portable.
 *
 * @param poly the low 64 bits of the 65 bit bit-reflected polynomial
 */
uint64_t aws_checksums_multiply_mod_p_reflected(uint64_t poly, uint64_t a, uint64_t b) {

    if (!a || !b) {
        return 0;
    }

    uint64_t hi_bit = (uint64_t)1 << 63;
    /* Choose the factor with the most trailing zero bits so the loop can exit soonest */
    const int swap = s_ctz64(b) > s_ctz64(a);
    uint64_t x = swap ? b : a;
    uint64_t y = swap ? a : b;
    uint64_t product = 0;

    /* Loop through the bits in the x factor */
    while (x) {
        /*
         * Every iteration keeps doubling the y factor using right shifts (it's bit-reflected).
         * When the field degree bit is set, first reduce using the field polynomial - the x^64
         * term of the polynomial always lands back in bit 63 after the shift.
         */
        if (y & 1) {
            y = ((y ^ poly) >> 1) | ((uint64_t)1 << 63);
        } else {
            y >>= 1;
        }

        if (x & hi_bit) {
            product ^= y;
            /* Clear the bit in x so the loop will quit when there are no more bits set */
            x ^= hi_bit;
        }

        /* Advance to test the next lowest bit in x */
        hi_bit >>= 1;
    }

    return product;
}
