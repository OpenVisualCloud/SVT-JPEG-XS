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

// Summary:
// Threads contains wrappers functions that hide
// platform specific objects such as threads, semaphores,
// and mutexs.  The goal is to eliminate platform #define
// in the code.

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define POINTER_TYPE_THREAD_SANITIZER_ENABLED 1
#endif
#endif

#ifndef POINTER_TYPE_THREAD_SANITIZER_ENABLED
#define POINTER_TYPE_THREAD_SANITIZER_ENABLED 0
#endif

/****************************************
 * Universal Includes
 ****************************************/
#include <stdlib.h>
#include "SvtThreads.h"
#include "SvtLog.h"
/****************************************
  * Win32 Includes
  ****************************************/
#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#endif // _WIN32
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif
#if PRINTF_TIME
#include <time.h>
#ifdef _WIN32
void printfTime(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SVT_LOG("  [%i ms]\t", ((int32_t)clock()));
    vprintf(fmt, args);
    va_end(args);
}
#endif
#endif

/****************************************
 * svt_jxs_create_thread
 ****************************************/
Handle_t svt_jxs_create_thread(void *(*thread_function)(void *), void *thread_context) {
    Handle_t thread_handle = NULL;

#ifdef _WIN32

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    thread_handle = (Handle_t)CreateThread(NULL,                                    // default security attributes
                                           0,                                       // default stack size
                                           (LPTHREAD_START_ROUTINE)thread_function, // function to be tied to the new thread
                                           thread_context,                          // context to be tied to the new thread
                                           0,                                       // thread active when created
                                           NULL);                                   // new thread ID
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif //__GNUC__

#else
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) {
        SVT_ERROR("Failed to initalize thread attributes\n");
        return NULL;
    }
    size_t stack_size;
    if (pthread_attr_getstacksize(&attr, &stack_size)) {
        SVT_ERROR("Failed to get thread stack size\n");
        pthread_attr_destroy(&attr);
        return NULL;
    }
    // 1 MiB in bytes for now since we can't easily change the stack size after creation
    const size_t min_stack_size = 1024 * 1024;
    if (stack_size < min_stack_size && pthread_attr_setstacksize(&attr, min_stack_size)) {
        SVT_ERROR("Failed to set thread stack size\n");
        pthread_attr_destroy(&attr);
        return NULL;
    }
    pthread_t *th = malloc(sizeof(*th));
    if (th == NULL) {
        SVT_ERROR("Failed to allocate thread handle\n");
        return NULL;
    }

    if (pthread_create(th, &attr, thread_function, thread_context)) {
        SVT_ERROR("Failed to create thread: %s\n", strerror(errno));
        free(th);
        return NULL;
    }

    pthread_attr_destroy(&attr);

    /* We can only use realtime priority if we are running as root, so
     * check if geteuid() == 0 (meaning either root or sudo).
     * If we don't do this check, we will eventually run into memory
     * issues if the encoder is uninitialized and re-initialized multiple
     * times in one executable due to a bug in glibc.
     * https://sourceware.org/bugzilla/show_bug.cgi?id=19511
     *
     * We still need to exclude the case of thread sanitizer because we
     * run the test as root inside the container and trying to change
     * the thread priority will __always__ fail the thread sanitizer.
     * https://github.com/google/sanitizers/issues/1088
     */
    if (!POINTER_TYPE_THREAD_SANITIZER_ENABLED && !geteuid()) {
        if (pthread_setschedparam(*th, SCHED_FIFO, &(struct sched_param){.sched_priority = 99}))
            SVT_WARN("Failed to set thread priority\n");
        // ignore if this failed
    }
    thread_handle = th;
#endif // _WIN32

    return thread_handle;
}

/****************************************
 * svt_jxs_destroy_thread
 ****************************************/
SvtJxsErrorType_t svt_jxs_destroy_thread(Handle_t thread_handle) {
    SvtJxsErrorType_t error_return;

#ifdef _WIN32
    WaitForSingleObject(thread_handle, INFINITE);
    error_return = CloseHandle(thread_handle) ? SvtJxsErrorNone : SvtJxsErrorDestroyThreadFailed;
#else
    error_return = pthread_join(*((pthread_t *)thread_handle), NULL) ? SvtJxsErrorDestroyThreadFailed : SvtJxsErrorNone;
    free(thread_handle);
#endif // _WIN32

    return error_return;
}

/***************************************
 * svt_jxs_create_semaphore
 ***************************************/
Handle_t svt_jxs_create_semaphore(uint32_t initial_count, uint32_t max_count) {
    Handle_t semaphore_handle;

#if defined(_WIN32)
    semaphore_handle = (Handle_t)CreateSemaphore(NULL,          // default security attributes
                                                 initial_count, // initial semaphore count
                                                 max_count,     // maximum semaphore count
                                                 NULL);         // semaphore is not named
#elif defined(__APPLE__)
    UNUSED(max_count);
    semaphore_handle = (Handle_t)dispatch_semaphore_create(initial_count);
#else
    UNUSED(max_count);

    semaphore_handle = (sem_t *)malloc(sizeof(sem_t));
    if (semaphore_handle != NULL)
        sem_init((sem_t *)semaphore_handle, // semaphore handle
                 0,                         // shared semaphore (not local)
                 initial_count);            // initial count
#endif

    return semaphore_handle;
}

/***************************************
 * svt_jxs_post_semaphore
 ***************************************/
SvtJxsErrorType_t svt_jxs_post_semaphore(Handle_t semaphore_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = !ReleaseSemaphore(semaphore_handle, // semaphore handle
                                     1,                // amount to increment the semaphore
                                     NULL)             // pointer to previous count (optional)
        ? SvtJxsErrorSemaphoreUnresponsive
        : SvtJxsErrorNone;
#elif defined(__APPLE__)
    dispatch_semaphore_signal((dispatch_semaphore_t)semaphore_handle);
    return_error = SvtJxsErrorNone;
#else
    return_error = sem_post((sem_t *)semaphore_handle) ? SvtJxsErrorSemaphoreUnresponsive : SvtJxsErrorNone;
#endif

    return return_error;
}

/***************************************
 * svt_jxs_block_on_semaphore
 ***************************************/
SvtJxsErrorType_t svt_jxs_block_on_semaphore(Handle_t semaphore_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = WaitForSingleObject((HANDLE)semaphore_handle, INFINITE) ? SvtJxsErrorSemaphoreUnresponsive : SvtJxsErrorNone;
#elif defined(__APPLE__)
    return_error = dispatch_semaphore_wait((dispatch_semaphore_t)semaphore_handle, DISPATCH_TIME_FOREVER)
        ? SvtJxsErrorSemaphoreUnresponsive
        : SvtJxsErrorNone;
#else
    int ret;
    do {
        ret = sem_wait((sem_t *)semaphore_handle);
    } while (ret == -1 && errno == EINTR);
    return_error = ret ? SvtJxsErrorSemaphoreUnresponsive : SvtJxsErrorNone;
#endif

    return return_error;
}

/***************************************
 * svt_jxs_destroy_semaphore
 ***************************************/
SvtJxsErrorType_t svt_jxs_destroy_semaphore(Handle_t semaphore_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = !CloseHandle((HANDLE)semaphore_handle) ? SvtJxsErrorDestroySemaphoreFailed : SvtJxsErrorNone;
#elif defined(__APPLE__)
    dispatch_release((dispatch_semaphore_t)semaphore_handle);
    return_error = SvtJxsErrorNone;
#else
    return_error = sem_destroy((sem_t *)semaphore_handle) ? SvtJxsErrorDestroySemaphoreFailed : SvtJxsErrorNone;
    free(semaphore_handle);
#endif

    return return_error;
}
/***************************************
 * svt_jxs_create_mutex
 ***************************************/
Handle_t svt_jxs_create_mutex(void) {
    Handle_t mutex_handle;

#ifdef _WIN32
    mutex_handle = (Handle_t)CreateMutex(NULL,  // default security attributes
                                         FALSE, // FALSE := not initially owned
                                         NULL); // mutex is not named

#else

    mutex_handle = (Handle_t)malloc(sizeof(pthread_mutex_t));

    if (mutex_handle != NULL) {
        pthread_mutex_init((pthread_mutex_t *)mutex_handle,
                           NULL); // default attributes
    }
#endif

    return mutex_handle;
}

/***************************************
 * svt_jxs_release_mutex
 ***************************************/
SvtJxsErrorType_t svt_jxs_release_mutex(Handle_t mutex_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = !ReleaseMutex((HANDLE)mutex_handle) ? SvtJxsErrorMutexUnresponsive : SvtJxsErrorNone;
#else
    return_error = pthread_mutex_unlock((pthread_mutex_t *)mutex_handle) ? SvtJxsErrorMutexUnresponsive : SvtJxsErrorNone;
#endif

    return return_error;
}

/***************************************
 * svt_jxs_block_on_mutex
 ***************************************/
SvtJxsErrorType_t svt_jxs_block_on_mutex(Handle_t mutex_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = WaitForSingleObject((HANDLE)mutex_handle, INFINITE) ? SvtJxsErrorMutexUnresponsive : SvtJxsErrorNone;
#else
    return_error = pthread_mutex_lock((pthread_mutex_t *)mutex_handle) ? SvtJxsErrorMutexUnresponsive : SvtJxsErrorNone;
#endif

    return return_error;
}

/***************************************
 * svt_jxs_destroy_mutex
 ***************************************/
SvtJxsErrorType_t svt_jxs_destroy_mutex(Handle_t mutex_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = CloseHandle((HANDLE)mutex_handle) ? SvtJxsErrorNone : SvtJxsErrorDestroyMutexFailed;
#else
    return_error = pthread_mutex_destroy((pthread_mutex_t *)mutex_handle) ? SvtJxsErrorDestroyMutexFailed : SvtJxsErrorNone;
    free(mutex_handle);
#endif

    return return_error;
}

/*
    create condition variable

    Condition variables are synchronization primitives that enable
    threads to wait until a particular condition occurs.
    Condition variables enable threads to atomically release
    a lock(mutex) and enter the sleeping state.
    it could be seen as a combined: wait and release mutex
*/
SvtJxsErrorType_t svt_jxs_create_cond_var(CondVar *cond_var) {
    SvtJxsErrorType_t return_error;
    cond_var->val = 0;
#ifdef _WIN32
    InitializeCriticalSection(&cond_var->cs);
    InitializeConditionVariable(&cond_var->cv);
    return_error = SvtJxsErrorNone;
#else
    return_error = pthread_mutex_init(&cond_var->m_mutex, NULL);
    if (!return_error) {
        return_error = pthread_cond_init(&cond_var->m_cond, NULL);
        if (return_error) {
            return_error |= pthread_mutex_destroy(&cond_var->m_mutex);
        }
    }
#endif
    return return_error;
}

/*
    free condition variable
*/
SvtJxsErrorType_t svt_jxs_free_cond_var(CondVar *cond_var) {
    SvtJxsErrorType_t return_error;
#ifdef _WIN32
    DeleteCriticalSection(&cond_var->cs);
    return_error = SvtJxsErrorNone;
#else
    return_error = pthread_mutex_destroy(&cond_var->m_mutex);
    return_error |= pthread_cond_destroy(&cond_var->m_cond);

#endif
    return return_error;
}

/*
    set a  condition variable to the new value
*/
SvtJxsErrorType_t svt_jxs_set_cond_var(CondVar *cond_var, int32_t new_value) {
    SvtJxsErrorType_t return_error;
#ifdef _WIN32
    EnterCriticalSection(&cond_var->cs);
    cond_var->val = new_value;
    WakeAllConditionVariable(&cond_var->cv);
    LeaveCriticalSection(&cond_var->cs);
    return_error = SvtJxsErrorNone;
#else
    return_error = pthread_mutex_lock(&cond_var->m_mutex);
    if (!return_error) {
        cond_var->val = new_value;
        return_error |= pthread_cond_broadcast(&cond_var->m_cond);
        return_error |= pthread_mutex_unlock(&cond_var->m_mutex);
    }
#endif
    return return_error;
}

/*
    add to condition variable to the value
*/
SvtJxsErrorType_t svt_jxs_add_cond_var(CondVar *cond_var, int32_t add_value) {
    SvtJxsErrorType_t return_error;
#ifdef _WIN32
    EnterCriticalSection(&cond_var->cs);
    cond_var->val += add_value;
    WakeAllConditionVariable(&cond_var->cv);
    LeaveCriticalSection(&cond_var->cs);
    return_error = SvtJxsErrorNone;
#else
    return_error = pthread_mutex_lock(&cond_var->m_mutex);
    if (!return_error) {
        cond_var->val += add_value;
        return_error |= pthread_cond_broadcast(&cond_var->m_cond);
        return_error |= pthread_mutex_unlock(&cond_var->m_mutex);
    }
#endif
    return return_error;
}

/*
    wait until the cond variable changes to a value
    different than input
*/
SvtJxsErrorType_t svt_jxs_wait_cond_var(CondVar *cond_var, int32_t input) {
    SvtJxsErrorType_t return_error = SvtJxsErrorNone;

#ifdef _WIN32
    EnterCriticalSection(&cond_var->cs);
    while (cond_var->val == input)
        SleepConditionVariableCS(&cond_var->cv, &cond_var->cs, INFINITE);
    LeaveCriticalSection(&cond_var->cs);
#else
    return_error = pthread_mutex_lock(&cond_var->m_mutex);
    if (!return_error) {
        while (cond_var->val == input) {
            return_error |= pthread_cond_wait(&cond_var->m_cond, &cond_var->m_mutex);
        }
        return_error |= pthread_mutex_unlock(&cond_var->m_mutex);
    }
#endif
    return return_error;
}
