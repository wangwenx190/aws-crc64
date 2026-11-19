cmake_minimum_required(VERSION 4.0)

project(aws-crc64
    VERSION 1.0.0
    DESCRIPTION "Self-contained CRC64-NVME (CRC64-Rocksoft) C library"
    LANGUAGES C
)

if(WIN32)
    enable_language(RC)
endif()
if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET "hidden")
if(NOT "x${CMAKE_C_FLAGS}" STREQUAL "x")
    if(MSVC)
        string(REGEX REPLACE " [/-]W[01234] " " " CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
    else()
        string(REGEX REPLACE " -W(all)?(extra)? " " " CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
        string(REGEX REPLACE " -[W]?pedantic " " " CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
    endif()
endif()
string(APPEND CMAKE_C_FLAGS " -w ")

set(__top_level ON)
if("x${CMAKE_SOURCE_DIR}" STREQUAL "x${PROJECT_SOURCE_DIR}")
    message("AWS-CRC64: Building as a top-level project.")
else()
    set(__top_level OFF)
    message("AWS-CRC64: Building as a subproject.")
endif()

option(AWS_CRC64_BUILD_SHARED "Build shared library edition of aws-crc64." ON)
option(AWS_CRC64_BUILD_STATIC "Build static library edition of aws-crc64." ON)
option(AWS_CRC64_INSTALL "Enable installation for aws-crc64." OFF)
if(NOT AWS_CRC64_BUILD_SHARED AND NOT AWS_CRC64_BUILD_STATIC)
    message(SEND_ERROR "You MUST build either the shared library or the static library or both of them!")
endif()

if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" __architecture)
elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID)
    string(TOLOWER "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" __architecture)
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" __architecture)
endif()
set(__x64 OFF)
set(__arm64 OFF)
foreach(__arch IN LISTS __architecture)
    if(__arch MATCHES "^(amd64|x86_64|x64)$")
        set(__x64 ON)
    elseif(__arch MATCHES "^(aarch64|arm64|arm64e)$")
        set(__arm64 ON)
    endif()
endforeach()

function(add_kernel_flags source architecture)
    if(APPLE AND __architecture MATCHES ";")
        foreach(flag IN LISTS ARGN)
            set_property(SOURCE "${source}" APPEND PROPERTY COMPILE_OPTIONS
                "-Xarch_${architecture}" "${flag}")
        endforeach()
    else()
        set_property(SOURCE "${source}" APPEND PROPERTY COMPILE_OPTIONS ${ARGN})
    endif()
endfunction()

# We assume a recent toolchain, so the intrinsics used by the SIMD kernels are always
# available at build time and there is no compile-time feature detection. Whether a
# kernel may actually *run* is still decided at runtime (see source/crc64_cpuid.c).
# The special instruction set flags are scoped to the kernel translation units only.
# Note for MSVC: intrinsics compile fine without any /arch switch, but passing it makes
# the compiler use consistent VEX/EVEX encoding for the whole translation unit instead
# of mixing legacy SSE encodings with VEX/EVEX ones in the hot path.
if(__x64)
    if(MSVC)
        add_kernel_flags(source/intel/crc64nvme_clmul.c x86_64 /arch:AVX2)
        add_kernel_flags(source/intel/crc64nvme_avx512.c x86_64 /arch:AVX512)
    else()
        add_kernel_flags(source/intel/crc64nvme_clmul.c x86_64 -msse4.2 -mpclmul -mavx2)
        add_kernel_flags(source/intel/crc64nvme_avx512.c x86_64
            -msse4.2 -mpclmul -mavx2 -mavx512f -mavx512vl -mvpclmulqdq)
    endif()
endif()
if(__arm64 AND NOT MSVC)
    add_kernel_flags(source/arm/crc64_arm.c arm64 -march=armv8-a+crc+crypto)
    # Use the same ARM instruction scheduling as the upstream AWS kernel.
    add_kernel_flags(source/arm/crc64_arm.c arm64 -mtune=neoverse-v1)
endif()

set(__targets)
if(AWS_CRC64_BUILD_SHARED)
    add_library(${PROJECT_NAME}_shared SHARED)
    add_library(${PROJECT_NAME}::${PROJECT_NAME}_shared ALIAS ${PROJECT_NAME}_shared)
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    target_compile_definitions(${PROJECT_NAME}_shared PRIVATE AWS_CRC64_EXPORTS)
    if(WIN32)
        target_sources(${PROJECT_NAME}_shared PRIVATE ${PROJECT_NAME}.rc)
    endif()
    list(APPEND __targets ${PROJECT_NAME}_shared)
endif()
if(AWS_CRC64_BUILD_STATIC)
    add_library(${PROJECT_NAME}_static STATIC)
    add_library(${PROJECT_NAME}::${PROJECT_NAME}_static ALIAS ${PROJECT_NAME}_static)
    target_compile_definitions(${PROJECT_NAME}_static PUBLIC AWS_CRC64_STATIC_DEFINE)
    list(APPEND __targets ${PROJECT_NAME}_static)
endif()
if(AWS_CRC64_BUILD_SHARED AND NOT AWS_CRC64_BUILD_STATIC)
    add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME}_shared)
endif()
if(AWS_CRC64_BUILD_STATIC AND NOT AWS_CRC64_BUILD_SHARED)
    add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME}_static)
endif()

find_package(Threads REQUIRED)
include(GNUInstallDirs)
file(GLOB_RECURSE __sources LIST_DIRECTORIES false CONFIGURE_DEPENDS
    "./include/aws/checksums/*.h" "./source/*.h" "./source/*.c"
)
foreach(__target IN LISTS __targets)
    target_sources(${__target} PRIVATE ${__sources})
    target_link_libraries(${__target} PRIVATE Threads::Threads)
    target_include_directories(${__target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(__x64)
        target_compile_definitions(${__target} PRIVATE
            AWS_CRC64_HAVE_CLMUL_INTRINSICS
            AWS_CRC64_HAVE_AVX512_INTRINSICS
        )
    endif()
    if(__arm64)
        target_compile_definitions(${__target} PRIVATE AWS_CRC64_HAVE_ARM_PMULL)
    endif()
    if(__top_level)
        if(WIN32)
            target_compile_definitions(${__target} PRIVATE
                UNICODE _UNICODE
                NOMINMAX
                WIN32_LEAN_AND_MEAN
            )
        endif()
        if(MSVC)
            target_compile_options(${__target} PRIVATE
                /utf-8 /permissive-
                $<$<NOT:$<CONFIG:Debug>>:/Gw /Gy /Zc:inline>
            )
            target_link_options(${__target} PRIVATE
                $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>
            )
        else()
            target_compile_options(${__target} PRIVATE
                $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections -fdata-sections>
            )
            if(APPLE)
                target_link_options(${__target} PRIVATE
                    $<$<NOT:$<CONFIG:Debug>>:-Wl,-dead_strip>
                )
            else()
                target_link_options(${__target} PRIVATE
                    $<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>
                )
            endif()
        endif()
    endif()
    set_target_properties(${__target} PROPERTIES VERSION ${PROJECT_VERSION})
    get_target_property(__type ${__target} TYPE)
    if(NOT "x${__type}" STREQUAL "xSTATIC_LIBRARY")
        set_target_properties(${__target} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
    endif()
    if(AWS_CRC64_INSTALL)
        install(TARGETS ${__target}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
endforeach()

if(AWS_CRC64_INSTALL)
    install(
        FILES include/aws/checksums/crc64.h include/aws/checksums/crc64_exports.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aws/checksums
    )
endif()
