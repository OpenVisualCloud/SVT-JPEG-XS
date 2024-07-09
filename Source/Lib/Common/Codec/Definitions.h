/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DEFINITIONS_H_
#define _DEFINITIONS_H_
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GROUP_SIZE              4
#define SIGNIFICANCE_GROUP_SIZE 8

#ifdef _WIN32
#define INLINE __inline
#elif __GNUC__
#define INLINE __inline__
#else
#define INLINE
#endif

#ifdef _WIN32
#define FOPEN(f, s, m) fopen_s(&f, s, m)
#else
#define FOPEN(f, s, m) f = fopen(s, m)
#endif

#if (defined(__GNUC__) && __GNUC__) || defined(__SUNPRO_C)
#define DECLARE_ALIGNED(n, typ, val) typ val __attribute__((aligned(n)))
#elif defined(_WIN32)
#define DECLARE_ALIGNED(n, typ, val) __declspec(align(n)) typ val
#else
#warning No alignment directives known for this compiler.
#define DECLARE_ALIGNED(n, typ, val) typ val
#endif

/* This is a 32 bit pointer and is aligned on a 32 bit word boundary.
*/
typedef void *void_ptr;

/** The Handle_t type is used to define OS object handles for threads,
semaphores, mutexs, etc.
*/
typedef void *Handle_t;

/**
object_ptr is a void_ptr to the object being constructed.
object_init_data_ptr is a void_ptr to a data structure used to initialize the
object.
*/
typedef SvtJxsErrorType_t (*Creator_t)(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr);

typedef enum PointerType {
    POINTER_TYPE_N_PTR = 0,     // malloc'd pointer
    POINTER_TYPE_C_PTR = 1,     // calloc'd pointer
    POINTER_TYPE_A_PTR = 2,     // malloc'd pointer aligned
    POINTER_TYPE_MUTEX = 3,     // mutex
    POINTER_TYPE_SEMAPHORE = 4, // semaphore
    POINTER_TYPE_THREAD = 5,    // thread handle
    POINTER_TYPE_NUM,
} PointerType_t;

#define ALVALUE 64

#define SVT_CREATE_SEMAPHORE(pointer, initial_count, max_count)   \
    do {                                                          \
        pointer = svt_jxs_create_semaphore(initial_count, max_count); \
        SVT_ADD_MEM(pointer, 1, POINTER_TYPE_SEMAPHORE);          \
    } while (0)

#define SVT_DESTROY_SEMAPHORE(pointer)                             \
    do {                                                           \
        if (pointer) {                                             \
            svt_jxs_destroy_semaphore(pointer);                    \
            SVT_REMOVE_MEM_ENTRY(pointer, POINTER_TYPE_SEMAPHORE); \
            pointer = NULL;                                        \
        }                                                          \
    } while (0)

#define SVT_CREATE_MUTEX(pointer)                    \
    do {                                             \
        pointer = svt_jxs_create_mutex();            \
        SVT_ADD_MEM(pointer, 1, POINTER_TYPE_MUTEX); \
    } while (0)

#define SVT_DESTROY_MUTEX(pointer)                             \
    do {                                                       \
        if (pointer) {                                         \
            svt_jxs_destroy_mutex(pointer);                    \
            SVT_REMOVE_MEM_ENTRY(pointer, POINTER_TYPE_MUTEX); \
            pointer = NULL;                                    \
        }                                                      \
    } while (0)

// Common Macros
#define UNUSED(x) (void)(x)

#ifdef __cplusplus
}
#endif
#endif // _DEFINITIONS_H_
