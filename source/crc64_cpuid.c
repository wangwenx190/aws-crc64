/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 *
 * Minimal, dependency free runtime CPU feature detection. Only the features that the CRC64-NVME
 * implementations dispatch on are detected.
 */

#include "crc64_priv.h"

static bool s_detected[AWS_CRC64_CPU_FEATURE_COUNT];
static bool s_detection_performed = false;

/* ------------------------------------------------------------------------- */
/* x86 / x86-64                                                              */
/* ------------------------------------------------------------------------- */
#if defined(AWS_CRC64_ARCH_INTEL_X64) || defined(__i386__) || defined(_M_IX86)

#    if defined(_MSC_VER)
#        include <immintrin.h>
#        include <intrin.h>
#    else
#        include <cpuid.h>
#    endif

static void s_cpuid(uint32_t leaf, uint32_t sub_leaf, uint32_t regs[4]) {
#    if defined(_MSC_VER)
    int out[4] = {0, 0, 0, 0};
    __cpuidex(out, (int)leaf, (int)sub_leaf);
    regs[0] = (uint32_t)out[0];
    regs[1] = (uint32_t)out[1];
    regs[2] = (uint32_t)out[2];
    regs[3] = (uint32_t)out[3];
#    else
    uint32_t a = 0, b = 0, c = 0, d = 0;
    __cpuid_count(leaf, sub_leaf, a, b, c, d);
    regs[0] = a;
    regs[1] = b;
    regs[2] = c;
    regs[3] = d;
#    endif
}

/* Read XCR0. Only valid to call when CPUID.1:ECX.OSXSAVE is set. */
static uint64_t s_xgetbv0(void) {
#    if defined(_MSC_VER)
    return _xgetbv(0);
#    else
    uint32_t eax = 0, edx = 0;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return ((uint64_t)edx << 32) | eax;
#    endif
}

static void s_detect_features(void) {
    uint32_t max_leaf[4] = {0, 0, 0, 0};
    s_cpuid(0, 0, max_leaf);
    if (max_leaf[0] < 1) {
        return;
    }

    uint32_t leaf1[4] = {0, 0, 0, 0};
    s_cpuid(1, 0, leaf1);

    const bool has_pclmulqdq = (leaf1[2] & (1u << 1)) != 0;
    const bool has_sse42 = (leaf1[2] & (1u << 19)) != 0;
    const bool has_osxsave = (leaf1[2] & (1u << 27)) != 0;
    const bool has_avx = (leaf1[2] & (1u << 28)) != 0;

    s_detected[AWS_CRC64_CPU_FEATURE_CLMUL] = has_pclmulqdq;
    s_detected[AWS_CRC64_CPU_FEATURE_SSE_4_2] = has_sse42;

    if (!has_osxsave || !has_avx) {
        /* Without OS support for XSAVE/AVX state, no (V)EX encoded path may be used. */
        return;
    }

    const uint64_t xcr0 = s_xgetbv0();
    /* bit 1: SSE state, bit 2: AVX (YMM) state */
    const bool os_saves_ymm = (xcr0 & 0x6) == 0x6;
    /* bits 5,6,7: opmask, ZMM_Hi256, Hi16_ZMM state */
    const bool os_saves_zmm = os_saves_ymm && ((xcr0 & 0xe0) == 0xe0);

    if (!os_saves_ymm || max_leaf[0] < 7) {
        return;
    }

    uint32_t leaf7[4] = {0, 0, 0, 0};
    s_cpuid(7, 0, leaf7);

    const bool has_avx2 = (leaf7[1] & (1u << 5)) != 0;
    const bool has_avx512f = (leaf7[1] & (1u << 16)) != 0;
    const bool has_avx512vl = (leaf7[1] & (1u << 31)) != 0;
    const bool has_vpclmulqdq = (leaf7[2] & (1u << 10)) != 0;

    s_detected[AWS_CRC64_CPU_FEATURE_AVX2] = has_avx2;
    /* The AVX-512 CRC64 kernel uses both 512-bit (F) and 128-bit masked (VL) intrinsics. */
    s_detected[AWS_CRC64_CPU_FEATURE_AVX512] = os_saves_zmm && has_avx512f && has_avx512vl;
    s_detected[AWS_CRC64_CPU_FEATURE_VPCLMULQDQ] = os_saves_zmm && has_vpclmulqdq;
}

/* ------------------------------------------------------------------------- */
/* ARM64                                                                     */
/* ------------------------------------------------------------------------- */
#elif defined(AWS_CRC64_ARCH_ARM64)

#    if defined(_WIN32)
#        include <windows.h>
#    elif defined(__APPLE__)
#        include <sys/sysctl.h>
#        include <sys/types.h>
#    elif defined(__linux__)
#        include <sys/auxv.h>
#        ifndef AT_HWCAP
#            define AT_HWCAP 16
#        endif
#        ifndef AWS_CRC64_HWCAP_AES
#            define AWS_CRC64_HWCAP_AES (1UL << 3)
#        endif
#        ifndef AWS_CRC64_HWCAP_PMULL
#            define AWS_CRC64_HWCAP_PMULL (1UL << 4)
#        endif
#    elif defined(__FreeBSD__)
#        include <sys/auxv.h>
#        ifndef AT_HWCAP
#            define AT_HWCAP 25
#        endif
#    endif

static void s_detect_features(void) {
#    if defined(__ARM_FEATURE_CRYPTO) || defined(__ARM_FEATURE_AES)
    /* The whole binary is already being built for a target that guarantees these. */
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_PMULL] = true;
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_CRYPTO] = true;
#    elif defined(_WIN32)
    const bool crypto = IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0;
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_PMULL] = crypto;
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_CRYPTO] = crypto;
#    elif defined(__APPLE__)
    /* Every arm64 Apple platform implements the crypto extensions, but ask anyway when we can. */
    int value = 0;
    size_t value_size = sizeof(value);
    bool pmull = true;
    if (sysctlbyname("hw.optional.arm.FEAT_PMULL", &value, &value_size, NULL, 0) == 0) {
        pmull = value != 0;
    }
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_PMULL] = pmull;
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_CRYPTO] = pmull;
#    elif defined(__linux__)
    const unsigned long hwcap = getauxval(AT_HWCAP);
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_PMULL] = (hwcap & AWS_CRC64_HWCAP_PMULL) != 0;
    s_detected[AWS_CRC64_CPU_FEATURE_ARM_CRYPTO] = (hwcap & AWS_CRC64_HWCAP_AES) != 0;
#    elif defined(__FreeBSD__)
    unsigned long hwcap = 0;
    if (elf_aux_info(AT_HWCAP, &hwcap, (int)sizeof(hwcap)) == 0) {
        s_detected[AWS_CRC64_CPU_FEATURE_ARM_PMULL] = (hwcap & (1UL << 4)) != 0;
        s_detected[AWS_CRC64_CPU_FEATURE_ARM_CRYPTO] = (hwcap & (1UL << 3)) != 0;
    }
#    else
    /* Unknown OS - stay on the portable path. */
#    endif
}

/* ------------------------------------------------------------------------- */
/* Everything else                                                           */
/* ------------------------------------------------------------------------- */
#else

static void s_detect_features(void) {}

#endif

bool aws_crc64_cpu_has_feature(enum aws_crc64_cpu_feature feature) {
    if (AWS_CRC64_UNLIKELY(!s_detection_performed)) {
        s_detect_features();
        s_detection_performed = true;
    }

    if ((int)feature < 0 || (int)feature >= AWS_CRC64_CPU_FEATURE_COUNT) {
        return false;
    }
    return s_detected[feature];
}
