/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "SemaphoreApp.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <pthread.h>
#endif // _WIN32

void* semaphore_create(uint32_t initial_count, uint32_t max_count) {
    void* semaphore_handle;

#if defined(_WIN32)
    semaphore_handle = (void*)CreateSemaphore(NULL,          // default security attributes
                                              initial_count, // initial semaphore count
                                              max_count,     // maximum semaphore count
                                              NULL);         // semaphore is not named
#elif defined(__APPLE__)
    (void)(max_count);
    semaphore_handle = (Handle_t)dispatch_semaphore_create(initial_count);
#else
    (void)(max_count);

    semaphore_handle = (sem_t*)malloc(sizeof(sem_t));
    if (semaphore_handle != NULL)
        sem_init((sem_t*)semaphore_handle, // semaphore handle
                 0,                        // shared semaphore (not local)
                 initial_count);           // initial count
#endif
    return semaphore_handle;
}

SvtJxsErrorType_t semaphore_destroy(void* semaphore_handle) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = !CloseHandle((HANDLE)semaphore_handle) ? SvtJxsErrorDestroySemaphoreFailed : SvtJxsErrorNone;
#elif defined(__APPLE__)
    dispatch_release((dispatch_semaphore_t)semaphore_handle);
    return_error = SvtJxsErrorNone;
#else
    return_error = sem_destroy((sem_t*)semaphore_handle) ? SvtJxsErrorDestroySemaphoreFailed : SvtJxsErrorNone;
    free(semaphore_handle);
#endif

    return return_error;
}

SvtJxsErrorType_t semaphore_post(void* semaphore_handle) {
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
    return_error = sem_post((sem_t*)semaphore_handle) ? SvtJxsErrorSemaphoreUnresponsive : SvtJxsErrorNone;
#endif

    return return_error;
}

SvtJxsErrorType_t semaphore_block(void* semaphore_handle, int32_t timout_ms) {
    SvtJxsErrorType_t return_error;

#ifdef _WIN32
    return_error = WaitForSingleObject((HANDLE)semaphore_handle, (timout_ms >= 0) ? (uint32_t)timout_ms : INFINITE)
        ? SvtJxsErrorSemaphoreUnresponsive
        : SvtJxsErrorNone;
#elif defined(__APPLE__)
#ERROR Implement timeout for dispatch_semaphore_wait() !
    return_error = dispatch_semaphore_wait((dispatch_semaphore_t)semaphore_handle, DISPATCH_TIME_FOREVER)
        ? SvtJxsErrorSemaphoreUnresponsive
        : SvtJxsErrorNone;
#else
    int ret;
    if (timout_ms < 0) {
        do {
            ret = sem_wait((sem_t*)semaphore_handle);
        } while (ret == -1 && errno == EINTR);
    }
    else {
        struct timespec ts;
        ret = clock_gettime(CLOCK_REALTIME, &ts);
        if (ret != -1) {
            ts.tv_nsec += timout_ms * 1000000L;
            while (ts.tv_nsec > 1000000000L) {
                ts.tv_nsec -= 1000000000L;
                ts.tv_sec++;
            }
            do {
                ret = sem_timedwait((sem_t*)semaphore_handle, &ts);
            } while (ret == -1 && errno == EINTR);
        }
    }
    return_error = ret ? SvtJxsErrorSemaphoreUnresponsive : SvtJxsErrorNone;
#endif

    return return_error;
}

/****************************************
 * svt_create_thread
 ****************************************/
void* app_create_thread(void* (*thread_function)(void*), void* thread_context) {
    void* thread_handle = NULL;

#ifdef _WIN32
    thread_handle = (void*)CreateThread(NULL,                                    // default security attributes
                                        0,                                       // default stack size
                                        (LPTHREAD_START_ROUTINE)thread_function, // function to be tied to the new thread
                                        thread_context,                          // context to be tied to the new thread
                                        0,                                       // thread active when created
                                        NULL);                                   // new thread ID
#else
    pthread_t* th = malloc(sizeof(*th));
    if (th == NULL) {
        return NULL;
    }
    if (pthread_create(th, NULL, thread_function, thread_context)) {
        free(th);
        return NULL;
    }

    if (pthread_setschedparam(*th, SCHED_FIFO, &(struct sched_param){.sched_priority = 99})) {
        printf("Failed to set thread priority\n");
    }

    thread_handle = th;
#endif // _WIN32

    return thread_handle;
}

/****************************************
 * svt_destroy_thread
 ****************************************/
SvtJxsErrorType_t app_destroy_thread(void* thread_handle) {
    SvtJxsErrorType_t error_return;

#ifdef _WIN32
    WaitForSingleObject(thread_handle, INFINITE);
    error_return = CloseHandle(thread_handle) ? SvtJxsErrorNone : SvtJxsErrorDestroyThreadFailed;
#else
    error_return = pthread_join(*((pthread_t*)thread_handle), NULL) ? SvtJxsErrorDestroyThreadFailed : SvtJxsErrorNone;
    free(thread_handle);
#endif // _WIN32

    return error_return;
}
