/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __APP_UTILITY_H__
#define __APP_UTILITY_H__
#include <time.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#ifdef _WIN32
#define FOPEN(f, s, m) fopen_s(&f, s, m)
#else
#define FOPEN(f, s, m) f = fopen(s, m)
#endif

typedef struct PerformanceContext {
    /****************************************
   * Computational Performance Data
   ****************************************/
    uint64_t lib_start_time[2];        // [sec, micro_sec] including init time
    uint64_t processing_start_time[2]; // [sec, micro_sec] first frame sent

    double total_execution_time; // includes init
    double total_process_time;   // not including init

    double total_latency_ms;
    double max_latency_ms;
    double min_latency_ms;

    //encoder only
    double packet_total_latency_ms;
    double packet_max_latency_ms;
    double packet_min_latency_ms;

} PerformanceContext_t;

typedef struct user_private_data {
    uint64_t frame_num;
    uint64_t frame_start_time[2]; // [sec, micro_sec]
} user_private_data_t;

static __inline void get_current_time(uint64_t* const seconds, uint64_t* const mseconds) {
#ifdef _WIN32
    struct _timeb curr_time;
    _ftime_s(&curr_time);
    *seconds = (uint64_t)curr_time.time;
    *mseconds = (uint64_t)curr_time.millitm;
#elif defined(CLOCK_MONOTONIC) && !defined(OLD_MACOS)
    struct timespec curr_time;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    *seconds = (uint64_t)curr_time.tv_sec;
    *mseconds = (uint64_t)curr_time.tv_nsec / 1000000UL;
#else
    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);
    *seconds = (uint64_t)curr_time.tv_sec;
    *mseconds = (uint64_t)curr_time.tv_usec / 1000UL;
#endif
}

static __inline double compute_elapsed_time_in_ms(const uint64_t start_seconds, const uint64_t start_mseconds,
                                                  const uint64_t finish_seconds, const uint64_t finish_mseconds) {
    const int64_t s_diff = (int64_t)finish_seconds - (int64_t)start_seconds;
    const int64_t m_diff = (int64_t)finish_mseconds - (int64_t)start_mseconds;
    return (double)s_diff * 1000.0 + (double)m_diff;
}

void sleep_in_ms(const unsigned milliseconds) {
    if (!milliseconds)
        return;
#ifdef _WIN32
    Sleep(milliseconds);
#else
    nanosleep(&(struct timespec){milliseconds / 1000, (milliseconds % 1000) * 1000000}, NULL);
#endif
}

size_t get_file_size(FILE* f) {
    if (f == NULL) {
        return 0;
    }
#ifdef _WIN32
    int64_t prev = _ftelli64(f);
    if (prev < 0) {
        return 0;
    }
    if (_fseeki64(f, (long)0, (long)SEEK_END) != 0) {
        (void)_fseeki64(f, prev, (long)SEEK_SET);
        return 0;
    }
    else {
        int64_t size = _ftelli64(f);
        (void)_fseeki64(f, prev, (long)SEEK_SET);
        if (size < 0) {
            return 0;
        }
        return size;
    }
#else
    long prev = ftell(f);
    if (prev < 0) {
        return 0;
    }
    if (fseek(f, (long)0, (long)SEEK_END) != 0) {
        (void)fseek(f, prev, (long)SEEK_SET);
        return 0;
    }
    else {
        long size = ftell(f);
        (void)fseek(f, prev, (long)SEEK_SET);
        if (size < 0) {
            return 0;
        }
        return (size_t)size;
    }
#endif
}

#endif
