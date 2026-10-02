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
 * Timed wait before blocking (Linux only)
 *
 * A waiter on a Fifo_t or CondVar marked with timed_wait sleeps in short timed slices for up to
 * a time budget before falling back to the plain blocking wait. The thread is descheduled while
 * it waits, so it costs no CPU, but the pending timer tells the idle governor the core will be
 * needed again soon, so it picks a shallow C-state instead of a deep one (C6 exit latency is
 * ~220us on recent Xeons). This matters for the decoder, which hands work between threads many
 * times per frame and is otherwise bound by wake-up latency. Only the decoder marks its objects;
 * nothing the encoder or the frame pool waits on is marked.
 *
 * Threads created by the library (svt_jxs_create_thread) also get their timer slack lowered the
 * first time they do a timed wait, so the slices are not stretched by the default 50us slack.
 * Application threads (e.g. ffmpeg or gstreamer threads blocking in send_frame/get_frame) do the
 * timed wait too, but their timer slack is never changed.
 *
 * Only POSIX calls are used, so this builds on any Linux libc: semaphore slices use
 * sem_timedwait(), whose timeout is on CLOCK_REALTIME, while the budget itself is measured on
 * CLOCK_MONOTONIC. A wall-clock jump only affects the slice running at that moment: a forward
 * jump ends it early, a backward jump can stretch it by the size of the jump, past the budget.
 * That slice then acts like the blocking wait (sem_post() still wakes it), so only the C-state
 * benefit is lost, never correctness. The CondVar's condition uses CLOCK_MONOTONIC
 * (pthread_condattr_setclock), so its slices are not affected by wall-clock jumps at all.
 *
 * Tuning, read once per process (see svt_jxs_tw_parse_config()); values above 1 s are clamped,
 * values that do not fit in 64 bits are invalid:
 *   SVT_JXS_TW_US        budget in us, default 1000; 0, negative or invalid disables the timed wait
 *   SVT_JXS_TW_SLICE_US  slice length in us, default 50; 0, negative or invalid keeps the default
 *   SVT_JXS_TW_SLACK_NS  timer slack for library threads in ns, default 1000; 0 means 1, negative or
 *                        invalid keeps the default
 ****************************************/
#if defined(__linux__)
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

static SvtJxsTimedWaitConfig svt_jxs_tw_config;
static pthread_once_t svt_jxs_tw_once = PTHREAD_ONCE_INIT;
static __thread int svt_jxs_tw_lib_thread = 0; /* set on threads made by svt_jxs_create_thread */
static __thread int svt_jxs_tw_slack_set = 0;

#ifdef BUILD_TESTING
static __thread SvtJxsTimedWaitStats svt_jxs_tw_stats;
static uint32_t svt_jxs_tw_waits_entered; /* process-wide, read by other threads */
#define SVT_JXS_TW_COUNT(field) (svt_jxs_tw_stats.field++)
#define SVT_JXS_TW_ENTERED()    __atomic_fetch_add(&svt_jxs_tw_waits_entered, 1, __ATOMIC_SEQ_CST)
#else
#define SVT_JXS_TW_COUNT(field) ((void)0)
#define SVT_JXS_TW_ENTERED()    ((void)0)
#endif

static int64_t svt_jxs_tw_clock_ns(clockid_t clock) {
    struct timespec ts;
    clock_gettime(clock, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static int64_t svt_jxs_tw_now_ns(void) {
    return svt_jxs_tw_clock_ns(CLOCK_MONOTONIC);
}

static struct timespec svt_jxs_tw_ts(int64_t ns) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / 1000000000);
    ts.tv_nsec = (long)(ns % 1000000000);
    return ts;
}

/* Parses a whole decimal string (digits with an optional leading '-') into *value. Returns 0 on
 * success, -1 if the string is empty, has other characters or does not fit in a long long. long
 * long is used so that 32-bit and 64-bit targets accept the same range. */
static int svt_jxs_tw_parse_ll(const char *str, long long *value) {
    /* strtoll() also skips leading white space and accepts '+', reject those here. */
    if (!(str[0] >= '0' && str[0] <= '9') && str[0] != '-') {
        return -1;
    }
    char *end;
    errno                  = 0;
    const long long parsed = strtoll(str, &end, 10);
    if (end == str || *end != '\0' || errno == ERANGE) {
        return -1;
    }
    *value = parsed;
    return 0;
}

void svt_jxs_tw_parse_config(const char *budget_us, const char *slice_us, const char *slack_ns, SvtJxsTimedWaitConfig *config) {
    long long value;
    config->budget_ns = SVT_JXS_TW_DEFAULT_BUDGET_US * 1000;
    config->slice_ns  = SVT_JXS_TW_DEFAULT_SLICE_US * 1000;
    config->slack_ns  = SVT_JXS_TW_DEFAULT_SLACK_NS;
    if (budget_us) {
        /* Fail safe: anything that is not a positive number disables the timed wait. */
        if (svt_jxs_tw_parse_ll(budget_us, &value) || value <= 0) {
            config->budget_ns = 0;
        }
        else {
            config->budget_ns = (int64_t)(value < SVT_JXS_TW_MAX_US ? value : SVT_JXS_TW_MAX_US) * 1000;
        }
    }
    if (slice_us && !svt_jxs_tw_parse_ll(slice_us, &value) && value > 0) {
        config->slice_ns = (int64_t)(value < SVT_JXS_TW_MAX_US ? value : SVT_JXS_TW_MAX_US) * 1000;
    }
    if (slack_ns && !svt_jxs_tw_parse_ll(slack_ns, &value) && value >= 0) {
        /* 0 would reset the thread to its default slack (50us), so the minimum is 1ns. */
        const long long max_slack_ns = SVT_JXS_TW_MAX_US * 1000LL;
        config->slack_ns        = value == 0 ? 1UL : (unsigned long)(value < max_slack_ns ? value : max_slack_ns);
    }
}

static void svt_jxs_tw_read_env(void) {
    svt_jxs_tw_parse_config(
        getenv("SVT_JXS_TW_US"), getenv("SVT_JXS_TW_SLICE_US"), getenv("SVT_JXS_TW_SLACK_NS"), &svt_jxs_tw_config);
}

/* Returns the budget in ns, 0 when disabled. */
static int64_t svt_jxs_tw_budget(void) {
    pthread_once(&svt_jxs_tw_once, svt_jxs_tw_read_env);
    if (svt_jxs_tw_config.budget_ns > 0 && svt_jxs_tw_lib_thread && !svt_jxs_tw_slack_set) {
        prctl(PR_SET_TIMERSLACK, svt_jxs_tw_config.slack_ns);
        svt_jxs_tw_slack_set = 1;
    }
    return svt_jxs_tw_config.budget_ns;
}

#ifdef BUILD_TESTING
void svt_jxs_tw_set_config_for_testing(const SvtJxsTimedWaitConfig *config) {
    /* Read the environment first, so that a later first wait does not overwrite this config. */
    pthread_once(&svt_jxs_tw_once, svt_jxs_tw_read_env);
    svt_jxs_tw_config = *config;
}

void svt_jxs_tw_get_stats_for_testing(SvtJxsTimedWaitStats *stats) {
    *stats = svt_jxs_tw_stats;
}

void svt_jxs_tw_reset_stats_for_testing(void) {
    memset(&svt_jxs_tw_stats, 0, sizeof(svt_jxs_tw_stats));
    __atomic_store_n(&svt_jxs_tw_waits_entered, 0, __ATOMIC_SEQ_CST);
}

uint32_t svt_jxs_tw_get_waits_entered_for_testing(void) {
    return __atomic_load_n(&svt_jxs_tw_waits_entered, __ATOMIC_SEQ_CST);
}
#endif // BUILD_TESTING

/* Start routine for library threads: marks the thread as ours, then runs the real function. */
typedef struct SvtJxsThreadStart {
    void *(*thread_function)(void *);
    void *thread_context;
} SvtJxsThreadStart;

static void *svt_jxs_thread_start(void *arg) {
    SvtJxsThreadStart start = *(SvtJxsThreadStart *)arg;
    free(arg);
    svt_jxs_tw_lib_thread = 1;
    return start.thread_function(start.thread_context);
}
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
        pthread_attr_destroy(&attr);
        return NULL;
    }

#if defined(__linux__)
    SvtJxsThreadStart *start = malloc(sizeof(*start));
    if (start == NULL) {
        SVT_ERROR("Failed to allocate thread start context\n");
        free(th);
        pthread_attr_destroy(&attr);
        return NULL;
    }
    start->thread_function = thread_function;
    start->thread_context  = thread_context;
    const int create_error = pthread_create(th, &attr, svt_jxs_thread_start, start);
    if (create_error) {
        free(start);
    }
#else
    const int create_error = pthread_create(th, &attr, thread_function, thread_context);
#endif
    if (create_error) {
        /* pthread_create() returns the error code, it does not set errno. */
        SVT_ERROR("Failed to create thread: %s\n", strerror(create_error));
        free(th);
        pthread_attr_destroy(&attr);
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
 * svt_jxs_block_on_semaphore_timed_wait
 *   Same as svt_jxs_block_on_semaphore(), but does the timed wait first (see the top of this file).
 ***************************************/
SvtJxsErrorType_t svt_jxs_block_on_semaphore_timed_wait(Handle_t semaphore_handle) {
#if defined(__linux__)
    /* Fast path: the semaphore is already available, no clock reads needed. */
    if (sem_trywait((sem_t *)semaphore_handle) == 0) {
        return SvtJxsErrorNone;
    }
    const int64_t tw_budget = svt_jxs_tw_budget();
    if (tw_budget > 0) {
        const int64_t deadline = svt_jxs_tw_now_ns() + tw_budget;
        SVT_JXS_TW_ENTERED();
        for (;;) {
            const int64_t left = deadline - svt_jxs_tw_now_ns();
            if (left <= 0) {
                break;
            }
            /* The last slice is cut to what is left of the budget. */
            const int64_t slice = left < svt_jxs_tw_config.slice_ns ? left : svt_jxs_tw_config.slice_ns;
            const struct timespec ts = svt_jxs_tw_ts(svt_jxs_tw_clock_ns(CLOCK_REALTIME) + slice);
            if (sem_timedwait((sem_t *)semaphore_handle, &ts) == 0) {
#ifdef BUILD_TESTING
                /* sem_timedwait() takes a count that is already there even past its timeout, so a
                 * post can be seen in a slice after the budget ran out. Stats only. */
                if (svt_jxs_tw_now_ns() >= deadline) {
                    SVT_JXS_TW_COUNT(fallbacks);
                }
#endif
                return SvtJxsErrorNone;
            }
            if (errno == ETIMEDOUT) {
                SVT_JXS_TW_COUNT(slice_timeouts);
            }
            else if (errno != EINTR) {
                /* Unexpected error: let the blocking wait below handle the semaphore. */
                break;
            }
        }
        SVT_JXS_TW_COUNT(fallbacks);
    }
#endif
    return svt_jxs_block_on_semaphore(semaphore_handle);
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
    cond_var->val        = 0;
    cond_var->timed_wait = 0;
#ifdef _WIN32
    InitializeCriticalSection(&cond_var->cs);
    InitializeConditionVariable(&cond_var->cv);
    return_error = SvtJxsErrorNone;
#else
    return_error = pthread_mutex_init(&cond_var->m_mutex, NULL);
    if (!return_error) {
#if defined(__linux__)
        /* The timed wait's slices are measured on CLOCK_MONOTONIC (see the top of this file). */
        pthread_condattr_t attr;
        return_error = pthread_condattr_init(&attr);
        if (!return_error) {
            return_error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
            if (!return_error) {
                return_error = pthread_cond_init(&cond_var->m_cond, &attr);
            }
            pthread_condattr_destroy(&attr);
        }
#else
        return_error = pthread_cond_init(&cond_var->m_cond, NULL);
#endif
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
#if defined(__linux__)
        /* Clock reads only when the value has not changed yet. */
        const int64_t tw_budget = cond_var->timed_wait && cond_var->val == input ? svt_jxs_tw_budget() : 0;
        if (tw_budget > 0) {
            const int64_t deadline = svt_jxs_tw_now_ns() + tw_budget;
            SVT_JXS_TW_ENTERED();
            while (cond_var->val == input) {
                const int64_t now = svt_jxs_tw_now_ns();
                if (now >= deadline) {
                    break;
                }
                /* The last slice is cut to what is left of the budget. */
                const int64_t slice_end = now + svt_jxs_tw_config.slice_ns;
                const struct timespec ts = svt_jxs_tw_ts(slice_end < deadline ? slice_end : deadline);
                const int ret = pthread_cond_timedwait(&cond_var->m_cond, &cond_var->m_mutex, &ts);
                if (ret == ETIMEDOUT) {
                    SVT_JXS_TW_COUNT(slice_timeouts);
                }
                else if (ret != 0) {
                    /* Unexpected error: let the blocking wait below handle it. */
                    break;
                }
            }
#ifdef BUILD_TESTING
            /* A fallback means the budget ran out before the change was seen. A slice can see the
             * change after the deadline (e.g. it was descheduled while taking the mutex back), so
             * check the clock. Stats only, so release builds skip the clock read. */
            if (cond_var->val == input || svt_jxs_tw_now_ns() >= deadline) {
                SVT_JXS_TW_COUNT(fallbacks);
            }
#endif
        }
#endif
        while (cond_var->val == input) {
            return_error |= pthread_cond_wait(&cond_var->m_cond, &cond_var->m_mutex);
        }
        return_error |= pthread_mutex_unlock(&cond_var->m_mutex);
    }
#endif
    return return_error;
}
