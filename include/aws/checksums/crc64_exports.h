#ifndef AWS_CHECKSUMS_CRC64_EXPORTS_H
#define AWS_CHECKSUMS_CRC64_EXPORTS_H
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/*
 * The shared library target compiles with AWS_CRC64_EXPORTS (private), the static
 * library target propagates AWS_CRC64_STATIC_DEFINE (public). Consumers of the
 * shared library need no define at all and get __declspec(dllimport).
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(AWS_CRC64_STATIC_DEFINE)
#        define AWS_CRC64_API
#    elif defined(AWS_CRC64_EXPORTS)
#        define AWS_CRC64_API __declspec(dllexport)
#    else
#        define AWS_CRC64_API __declspec(dllimport)
#    endif
#else
#    define AWS_CRC64_API __attribute__((visibility("default")))
#endif

#endif /* AWS_CHECKSUMS_CRC64_EXPORTS_H */
