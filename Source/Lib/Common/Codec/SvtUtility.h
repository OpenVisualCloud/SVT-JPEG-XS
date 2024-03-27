/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_UTILITY_H_
#define _SVT_UTILITY_H_

#include "Definitions.h"
#include "common_dsp_rtcd.h"
#ifdef _WIN32
#include <sys/timeb.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
#include <time.h>

void assert_err(uint32_t condition, char* err_msg);
////**************************************************
//// MACROS
////**************************************************
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define DIV_ROUND_UP(val, round)   (((val) + (round)-1) / (round))
#define DIV_ROUND_DOWN(val, round) ((val) / (round))

static INLINE void get_current_time(uint64_t* const seconds, uint64_t* const mseconds) {
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

static INLINE double compute_elapsed_time_in_ms(const uint64_t start_seconds, const uint64_t start_mseconds,
                                                const uint64_t finish_seconds, const uint64_t finish_mseconds) {
    const int64_t s_diff = (int64_t)finish_seconds - (int64_t)start_seconds,
                  m_diff = (int64_t)finish_mseconds - (int64_t)start_mseconds;
    return (double)s_diff * 1000.0 + (double)m_diff;
}

#ifdef __cplusplus
}
#endif

#endif /*_SVT_UTILITY_H_*/
