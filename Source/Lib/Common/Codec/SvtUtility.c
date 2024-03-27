/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/time.h>
#endif

#include "SvtLog.h"
#include "SvtUtility.h"

/* assert a certain condition and report err if condition not met */
void assert_err(uint32_t condition, char* err_msg) {
    if (!condition) {
        SVT_ERROR("\n %s \n", err_msg);
    }
    assert(condition);
}

/*****************************************
 * Long Log 2
 *  This is a quick adaptation of a Number
 *  Leading Zeros (NLZ) algorithm to get
 *  the log2f of a 64-bit number
 *****************************************/
uint64_t log2f_64_c(uint64_t x) {
    int64_t n = 64, c = 32;

    do {
        uint64_t y = x >> c;
        if (y > 0) {
            n -= c;
            x = y;
        }
        c >>= 1;
    } while (c > 0);

    return 64 - n;
}

/*****************************************
  * Long Log 2
  *  This is a quick adaptation of a Number
  *  Leading Zeros (NLZ) algorithm to get
  *  the log2f of a 32-bit number
  *****************************************/
uint32_t log2_32_c(uint32_t x) {
    uint32_t log = 0;
    int32_t i;
    for (i = 4; i >= 0; --i) {
        const uint32_t shift = (1 << i);
        const uint32_t n = x >> shift;
        if (n != 0) {
            x = n;
            log += shift;
        }
    }
    return log;
}
