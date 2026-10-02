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
extern SvtJxsErrorType_t svt_jxs_block_on_semaphore_timed_wait(Handle_t semaphore_handle);

#if defined(__linux__)
#define SVT_JXS_TW_DEFAULT_BUDGET_US 1000
#define SVT_JXS_TW_DEFAULT_SLICE_US  50
#define SVT_JXS_TW_DEFAULT_SLACK_NS  1000
#define SVT_JXS_TW_MAX_US            1000000 /* budget, slice and slack are clamped to 1 s */

typedef struct SvtJxsTimedWaitConfig {
    int64_t budget_ns; /* 0 when the timed wait is disabled */
    int64_t slice_ns;
    unsigned long slack_ns;
} SvtJxsTimedWaitConfig;

/* Fills *config from the SVT_JXS_TW_US, SVT_JXS_TW_SLICE_US and SVT_JXS_TW_SLACK_NS strings (NULL
 * when unset). Exposed for the unit tests; the library reads the environment once per process. */
extern void svt_jxs_tw_parse_config(const char *budget_us, const char *slice_us, const char *slack_ns,
                                    SvtJxsTimedWaitConfig *config);

#ifdef BUILD_TESTING
/* What the timed waits of the calling thread did, so tests can check which path ran. */
typedef struct SvtJxsTimedWaitStats {
    uint32_t slice_timeouts; /* timed slices that ended without a wake-up */
    uint32_t fallbacks;      /* waits whose budget ran out before the wake-up was seen */
} SvtJxsTimedWaitStats;

/* Replaces the process-wide config read from the environment. Only call it while no thread waits. */
void svt_jxs_tw_set_config_for_testing(const SvtJxsTimedWaitConfig *config);
void svt_jxs_tw_get_stats_for_testing(SvtJxsTimedWaitStats *stats);
void svt_jxs_tw_reset_stats_for_testing(void);
/* Waits of any thread that started the timed slices (deadline set) since the last reset, so a test
 * can act only once a waiter has its deadline. */
uint32_t svt_jxs_tw_get_waits_entered_for_testing(void);
#endif // BUILD_TESTING
#endif

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
    // timed_wait - waiters do the timed wait before blocking (see SvtThreads.c). Set by the
    //   owner after svt_jxs_create_cond_var(); only the decoder sets it.
    uint8_t timed_wait;
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
