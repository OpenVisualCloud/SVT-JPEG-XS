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

#ifndef _SVT_THREAD_H_
#define _SVT_THREAD_H_

#include "Definitions.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Create wrapper functions that hide thread calls,
// semaphores, mutex, etc. These wrappers also hide
// platform specific implementations of these objects.

/**************************************
     * Threads
     **************************************/
extern Handle_t svt_jxs_create_thread(void *(*thread_function)(void *), void *thread_context);

extern SvtJxsErrorType_t svt_jxs_destroy_thread(Handle_t thread_handle);

/**************************************
     * Semaphores
     **************************************/
extern Handle_t svt_jxs_create_semaphore(uint32_t initial_count, uint32_t max_count);

extern SvtJxsErrorType_t svt_jxs_post_semaphore(Handle_t semaphore_handle);

extern SvtJxsErrorType_t svt_jxs_block_on_semaphore(Handle_t semaphore_handle);

extern SvtJxsErrorType_t svt_jxs_destroy_semaphore(Handle_t semaphore_handle);

/**************************************
     * Mutex
     **************************************/
extern Handle_t svt_jxs_create_mutex(void);
extern SvtJxsErrorType_t svt_jxs_release_mutex(Handle_t mutex_handle);
extern SvtJxsErrorType_t svt_jxs_block_on_mutex(Handle_t mutex_handle);
extern SvtJxsErrorType_t svt_jxs_destroy_mutex(Handle_t mutex_handle);
#ifdef _WIN32

#define SVT_CREATE_THREAD(pointer, thread_function, thread_context)   \
    do {                                                              \
        pointer = svt_jxs_create_thread(thread_function, thread_context); \
        SVT_ADD_MEM(pointer, 1, POINTER_TYPE_THREAD);                 \
    } while (0)

#else
#ifndef __USE_GNU
#define __USE_GNU
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <pthread.h>
#if defined(__linux__)
#define SVT_CREATE_THREAD(pointer, thread_function, thread_context)   \
    do {                                                              \
        pointer = svt_jxs_create_thread(thread_function, thread_context); \
        SVT_ADD_MEM(pointer, 1, POINTER_TYPE_THREAD);                 \
    } while (0)
#else
#define SVT_CREATE_THREAD(pointer, thread_function, thread_context)   \
    do {                                                              \
        pointer = svt_jxs_create_thread(thread_function, thread_context); \
        SVT_ADD_MEM(pointer, 1, POINTER_TYPE_THREAD);                 \
    } while (0)
#endif
#endif
#define SVT_DESTROY_THREAD(pointer)                             \
    do {                                                        \
        if (pointer) {                                          \
            svt_jxs_destroy_thread(pointer);                    \
            SVT_REMOVE_MEM_ENTRY(pointer, POINTER_TYPE_THREAD); \
            pointer = NULL;                                     \
        }                                                       \
    } while (0);

#define SVT_CREATE_THREAD_ARRAY(pa, count, thread_function, thread_contexts) \
    do {                                                                     \
        SVT_ALLOC_PTR_ARRAY(pa, count);                                      \
        for (uint32_t i = 0; i < count; i++)                                 \
            SVT_CREATE_THREAD(pa[i], thread_function, thread_contexts[i]);   \
    } while (0)

#define SVT_DESTROY_THREAD_ARRAY(pa, count)      \
    do {                                         \
        if (pa) {                                \
            for (uint32_t i = 0; i < count; i++) \
                SVT_DESTROY_THREAD(pa[i]);       \
            SVT_FREE_PTR_ARRAY(pa, count);       \
        }                                        \
    } while (0)

/*
 Condition variable
*/
typedef struct CondVar {
    int32_t val;
#ifdef _WIN32
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
#else
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
#endif
} CondVar;

SvtJxsErrorType_t svt_jxs_create_cond_var(CondVar *cond_var);
SvtJxsErrorType_t svt_jxs_free_cond_var(CondVar *cond_var);
SvtJxsErrorType_t svt_jxs_set_cond_var(CondVar *cond_var, int32_t new_value);
SvtJxsErrorType_t svt_jxs_add_cond_var(CondVar *cond_var, int32_t add_value);
SvtJxsErrorType_t svt_jxs_wait_cond_var(CondVar *cond_var, int32_t input);

#ifdef __cplusplus
}
#endif
#endif /*_SVT_THREAD_H_*/
