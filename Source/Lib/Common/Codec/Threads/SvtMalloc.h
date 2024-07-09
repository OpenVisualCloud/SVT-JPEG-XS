/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/
#ifndef _SVT_MALLOC_H_
#define _SVT_MALLOC_H_
#include <stdlib.h>
#include <stdio.h>

#include "Definitions.h"

#ifndef NDEBUG
#define DEBUG_MEMORY_USAGE
#endif

#define svt_print_alloc_fail(a, b) svt_jxs_print_alloc_fail_impl(a, b)
void svt_jxs_print_alloc_fail_impl(const char* file, int line);

#ifdef DEBUG_MEMORY_USAGE
void svt_jxs_print_memory_usage();
void svt_jxs_increase_component_count();
void svt_jxs_decrease_component_count();
void svt_jxs_add_mem_entry_impl(void* ptr, PointerType_t type, size_t count, const char* file, uint32_t line);
void svt_jxs_remove_mem_entry(void* ptr, PointerType_t type);

#define svt_add_mem_entry(a, b, c, d, e) svt_jxs_add_mem_entry_impl(a, b, c, d, e)

#define SVT_ADD_MEM_ENTRY(p, type, count) svt_add_mem_entry(p, type, count, __FILE__, __LINE__)
#define SVT_REMOVE_MEM_ENTRY(p, type)     svt_jxs_remove_mem_entry(p, type);

#else
#define svt_jxs_print_memory_usage() \
    do {                         \
    } while (0)
#define svt_jxs_increase_component_count() \
    do {                               \
    } while (0)
#define svt_jxs_decrease_component_count() \
    do {                               \
    } while (0)
#define SVT_ADD_MEM_ENTRY(p, type, count) \
    do {                                  \
    } while (0)
#define SVT_REMOVE_MEM_ENTRY(p, type) \
    do {                              \
    } while (0)

#endif //DEBUG_MEMORY_USAGE

#define SVT_NO_THROW_ADD_MEM(p, size, type)           \
    do {                                              \
        if (!p)                                       \
            svt_print_alloc_fail(__FILE__, __LINE__); \
        else                                          \
            SVT_ADD_MEM_ENTRY(p, type, size);         \
    } while (0)

#define SVT_CHECK_MEM(p)                             \
    do {                                             \
        if (!p)                                      \
            return SvtJxsErrorInsufficientResources; \
    } while (0)

#define SVT_ADD_MEM(p, size, type)           \
    do {                                     \
        SVT_NO_THROW_ADD_MEM(p, size, type); \
        SVT_CHECK_MEM(p);                    \
    } while (0)

#define SVT_NO_THROW_MALLOC(pointer, size)                          \
    do {                                                            \
        void* malloced_p = malloc(size);                            \
        SVT_NO_THROW_ADD_MEM(malloced_p, size, POINTER_TYPE_N_PTR); \
        pointer = malloced_p;                                       \
    } while (0)

#define SVT_MALLOC(pointer, size)           \
    do {                                    \
        SVT_NO_THROW_MALLOC(pointer, size); \
        SVT_CHECK_MEM(pointer);             \
    } while (0)

#define SVT_NO_THROW_CALLOC(pointer, count, size)                       \
    do {                                                                \
        pointer = calloc(count, size);                                  \
        SVT_NO_THROW_ADD_MEM(pointer, count* size, POINTER_TYPE_C_PTR); \
    } while (0)

#define SVT_CALLOC(pointer, count, size)           \
    do {                                           \
        SVT_NO_THROW_CALLOC(pointer, count, size); \
        SVT_CHECK_MEM(pointer);                    \
    } while (0)

#define SVT_FREE(pointer)                                  \
    do {                                                   \
        SVT_REMOVE_MEM_ENTRY(pointer, POINTER_TYPE_N_PTR); \
        free(pointer);                                     \
        pointer = NULL;                                    \
    } while (0)

#define SVT_MALLOC_ARRAY(pa, count)              \
    do {                                         \
        SVT_MALLOC(pa, sizeof(*(pa)) * (count)); \
    } while (0)

#define SVT_REALLOC_ARRAY(pa, count)                      \
    do {                                                  \
        size_t size = sizeof(*(pa)) * (count);            \
        void* p = realloc(pa, size);                      \
        if (p) {                                          \
            SVT_REMOVE_MEM_ENTRY(pa, POINTER_TYPE_N_PTR); \
        }                                                 \
        SVT_ADD_MEM(p, size, POINTER_TYPE_N_PTR);         \
        pa = p;                                           \
    } while (0)

#define SVT_CALLOC_ARRAY(pa, count)           \
    do {                                      \
        SVT_CALLOC(pa, count, sizeof(*(pa))); \
    } while (0)

#define SVT_FREE_ARRAY(pa) SVT_FREE(pa);

#define SVT_ALLOC_PTR_ARRAY(pa, count)        \
    do {                                      \
        SVT_CALLOC(pa, count, sizeof(*(pa))); \
    } while (0)

#define SVT_FREE_PTR_ARRAY(pa, count)          \
    do {                                       \
        if (pa) {                              \
            for (size_t i = 0; i < count; i++) \
                SVT_FREE(pa[i]);               \
            SVT_FREE(pa);                      \
        }                                      \
    } while (0)

#define SVT_MALLOC_2D(p2d, width, height)             \
    do {                                              \
        SVT_MALLOC_ARRAY(p2d, width);                 \
        SVT_MALLOC_ARRAY(p2d[0], (width) * (height)); \
        for (size_t w = 1; w < (width); w++)          \
            p2d[w] = p2d[0] + w * (height);           \
    } while (0)

#define SVT_CALLOC_2D(p2d, width, height)             \
    do {                                              \
        SVT_MALLOC_ARRAY(p2d, width);                 \
        SVT_CALLOC_ARRAY(p2d[0], (width) * (height)); \
        for (size_t w = 1; w < (width); w++)          \
            p2d[w] = p2d[0] + w * (height);           \
    } while (0)

#define SVT_FREE_2D(p2d)            \
    do {                            \
        if (p2d)                    \
            SVT_FREE_ARRAY(p2d[0]); \
        SVT_FREE_ARRAY(p2d);        \
    } while (0)

#ifdef _WIN32
#define SVT_MALLOC_ALIGNED(pointer, size)               \
    do {                                                \
        pointer = _aligned_malloc(size, ALVALUE);       \
        SVT_ADD_MEM(pointer, size, POINTER_TYPE_A_PTR); \
    } while (0)

#define SVT_FREE_ALIGNED(pointer)                          \
    do {                                                   \
        SVT_REMOVE_MEM_ENTRY(pointer, POINTER_TYPE_A_PTR); \
        _aligned_free(pointer);                            \
        pointer = NULL;                                    \
    } while (0)
#else
#define SVT_MALLOC_ALIGNED(pointer, size)                           \
    do {                                                            \
        if (posix_memalign((void**)&(pointer), ALVALUE, size) != 0) \
            return SvtJxsErrorInsufficientResources;                \
        SVT_ADD_MEM(pointer, size, POINTER_TYPE_A_PTR);             \
    } while (0)

#define SVT_FREE_ALIGNED(pointer)                          \
    do {                                                   \
        SVT_REMOVE_MEM_ENTRY(pointer, POINTER_TYPE_A_PTR); \
        free(pointer);                                     \
        pointer = NULL;                                    \
    } while (0)
#endif

#define SVT_MALLOC_ALIGNED_ARRAY(pa, count) SVT_MALLOC_ALIGNED(pa, sizeof(*(pa)) * (count))

#define SVT_CALLOC_ALIGNED_ARRAY(pa, count)              \
    do {                                                 \
        SVT_MALLOC_ALIGNED(pa, sizeof(*(pa)) * (count)); \
        memset(pa, 0, sizeof(*(pa)) * (count));          \
    } while (0)

#define SVT_FREE_ALIGNED_ARRAY(pa) SVT_FREE_ALIGNED(pa)

#endif /*_SVT_MALLOC_H_*/
