# aws-crc64

A self-contained C library providing **CRC64-NVME** (a.k.a. CRC64-Rocksoft), extracted from
[aws-checksums](https://github.com/awslabs/aws-checksums). It carries the same hardware-accelerated
kernels (Intel PCLMULQDQ/AVX-512, ARMv8 PMULL) and the same portable slice-by-8 fallback, but has
**no dependency on `aws-c-common`**. It uses the C library and platform thread support.

CRC64-NVME is bit-reflected, based on the polynomial `0xad93d23594c93659`, and inverts the CRC
input and output bits. It is the checksum used by Amazon S3's `x-amz-checksum-crc64nvme`.

## Usage

```c
#include <aws/checksums/crc64.h>

uint64_t crc = aws_checksums_crc64nvme((const uint8_t *)"123456789", 9, 0);
/* crc == 0xae8b14860a799888 */
```

### API

| Function | Purpose |
| --- | --- |
| `aws_checksums_crc64nvme(input, length, previous_crc64)` | Main entry point. Dispatches to the fastest kernel available on the running CPU. Pass `0` as `previous_crc64` to start, or a running value to continue. |
| `aws_checksums_crc64nvme_ex(input, length, previous_crc64)` | Same, but takes a `size_t` length so buffers larger than `INT_MAX` work. |
| `aws_checksums_crc64nvme_combine(crc1, crc2, len2)` | Combine the CRCs of two blocks into the CRC of their concatenation, without rescanning. `len2` is the **byte length of the second block**, not the checksum size. |
| `aws_checksums_crc64_init(void)` | Optional. Resolves the CPU-dispatch function pointers up front. Idempotent. |
| `aws_checksums_crc64nvme_sw(...)` / `..._combine_sw(...)` | The portable implementations, exposed for testing and for callers that want to bypass hardware acceleration. |

Function names match upstream aws-checksums so existing call sites port over unchanged.

### Thread safety

All functions are safe to call concurrently. The runtime dispatch is initialized once on first
use. Call `aws_checksums_crc64_init()` to resolve it eagerly during startup.

## Building

A recent toolchain is assumed (CMake 4.0+, any current GCC/Clang/MSVC): the SIMD kernels are
always compiled on their target architecture, there is no compile-time feature probing.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `AWS_CRC64_BUILD_SHARED` | `ON` | Build the shared library (`aws-crc64_shared`). On Windows the DLL carries version info from `aws-crc64.rc`. |
| `AWS_CRC64_BUILD_STATIC` | `ON` | Build the static library (`aws-crc64_static`). |
| `AWS_CRC64_INSTALL` | `OFF` | Install the libraries and public headers. No CMake package config is generated. |

Both library editions can be built in one pass. Exports are handled automatically:
the shared target compiles with `AWS_CRC64_EXPORTS`, the static target propagates
`AWS_CRC64_STATIC_DEFINE` to its consumers.

## Consuming

Vendor the sources in-tree and link one of the targets:

```cmake
add_subdirectory(third_party/aws-crc64)
target_link_libraries(my_app PRIVATE aws-crc64::aws-crc64_shared) # or aws-crc64::aws-crc64_static
```

## Implementation selection

The kernels that need special instruction sets live in their own translation units, and only
those units are compiled with the corresponding instruction set flags. At runtime the library
checks CPU features (CPUID on x86-64, `getauxval`/`sysctlbyname`/`IsProcessorFeaturePresent`
on ARM64) and only enters a kernel the running CPU actually supports.

| Kernel | Built when | Requires (runtime) |
| --- | --- | --- |
| `crc64nvme_intel_avx512` | targeting x86-64 | AVX-512F + AVX-512VL + VPCLMULQDQ, with OS `XCR0` state enabled |
| `crc64nvme_intel_clmul` | targeting x86-64 | CLMUL + AVX2 + SSE4.2, with OS `XCR0` state enabled |
| `crc64nvme_arm_pmull` | targeting ARM64 | PMULL + AES/crypto |
| `crc64nvme_sw` | always | always |

`aws_checksums_crc64nvme_combine` uses the PMULL kernel on ARM64 and the portable path elsewhere.

## Layout

```
include/aws/checksums/crc64.h          public API
include/aws/checksums/crc64_exports.h  visibility / dllexport macros
source/crc64_priv.h                    internal header (replaces the aws-c-common pieces)
source/crc64.c                         folding constants + CPU dispatch
source/crc64_cpuid.c                   runtime CPU feature detection
source/crc64_sw.c                      portable slice-by-8 kernel + combine
source/crc64_util.c                    carry-less multiply mod P
source/intel/crc64nvme_clmul.c         SSE4.2 + PCLMULQDQ kernel
source/intel/crc64nvme_avx512.c        AVX-512 + VPCLMULQDQ kernel
source/arm/crc64_arm.c                 ARMv8 PMULL kernel + combine
aws-crc64.rc                           Windows version resource (shared library only)
```

## Differences from upstream aws-checksums

- No `aws-c-common` dependency. `cpuid.h`, `macros.h`, `byte_order.h`, and the CMake feature-test
  modules are replaced by `source/crc64_priv.h`, `source/crc64_cpuid.c`, and the `CMakeLists.txt`.
- Only CRC64-NVME is included; CRC32, CRC32C, and xxHash are not.
- `aws_checksums_multiply_mod_p_reflected` was rewritten without `__uint128_t`. Upstream falls back
  to an O(len2) bit-shifting loop when the compiler has no 128-bit integers (notably MSVC), which
  makes `combine` extremely slow for large blocks; here every target gets the O(log len2) path.
- The AVX-512 runtime check additionally requires AVX-512VL, which the kernel genuinely uses.

## License

Apache-2.0, unchanged from aws-checksums. See [LICENSE](LICENSE).
