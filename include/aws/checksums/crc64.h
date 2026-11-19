#ifndef AWS_CHECKSUMS_CRC64_H
#define AWS_CHECKSUMS_CRC64_H
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/checksums/crc64_exports.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The entry point function to perform a CRC64-NVME (a.k.a. CRC64-Rocksoft) computation.
 * Selects a suitable implementation based on hardware capabilities.
 * Pass 0 in the previous_crc64 parameter as an initial value unless continuing
 * to update a running crc in a subsequent call.
 * There are many variants of CRC64 algorithms. This CRC64 variant is bit-reflected (based on
 * the non bit-reflected polynomial 0xad93d23594c93659) and inverts the CRC input and output bits.
 */
AWS_CRC64_API uint64_t aws_checksums_crc64nvme(const uint8_t *input, int length, uint64_t previous_crc64);

/**
 * The entry point function to perform a CRC64-NVME (a.k.a. CRC64-Rocksoft) computation.
 * Supports buffer lengths up to size_t max.
 * Selects a suitable implementation based on hardware capabilities.
 * Pass 0 in the previous_crc64 parameter as an initial value unless continuing
 * to update a running crc in a subsequent call.
 */
AWS_CRC64_API uint64_t aws_checksums_crc64nvme_ex(const uint8_t *input, size_t length, uint64_t previous_crc64);

/**
 * Combines two CRC64-NVME (CRC64-Rocksoft) checksums computed over separate data blocks.
 * This is equivalent to computing the CRC64-NVME of the concatenated data blocks without
 * having to re-scan the data.
 *
 * Given:
 *   crc1 = CRC64_NVME(data_block_A)
 *   crc2 = CRC64_NVME(data_block_B)
 *
 * This function computes:
 *   result = CRC64_NVME(data_block_A || data_block_B)
 *
 * @param crc1 The CRC64-NVME checksum of the first data block
 * @param crc2 The CRC64-NVME checksum of the second data block
 * @param len2 The length (in bytes) of the original data that produced crc2.
 *             This is NOT the size of the checksum (which is always 8 bytes),
 *             but rather the size of the data block that was checksummed.
 * @return The combined CRC64-NVME checksum as if computed over the concatenated data
 */
AWS_CRC64_API uint64_t aws_checksums_crc64nvme_combine(uint64_t crc1, uint64_t crc2, uint64_t len2);

/**
 * Force resolution of the CPU-feature-dependent function pointers used by the CRC64 entry points.
 * Concurrent first use is safe, and repeated calls are idempotent.
 */
AWS_CRC64_API void aws_checksums_crc64_init(void);

/**
 * The portable (table driven) CRC64-NVME implementation. Exposed mainly for testing and for callers
 * that want to bypass hardware acceleration; prefer aws_checksums_crc64nvme() for normal use.
 */
AWS_CRC64_API uint64_t aws_checksums_crc64nvme_sw(const uint8_t *input, int length, uint64_t previous_crc64);

/**
 * The portable implementation of aws_checksums_crc64nvme_combine().
 */
AWS_CRC64_API uint64_t aws_checksums_crc64nvme_combine_sw(uint64_t crc1, uint64_t crc2, uint64_t len2);

#ifdef __cplusplus
}
#endif

#endif /* AWS_CHECKSUMS_CRC64_H */
