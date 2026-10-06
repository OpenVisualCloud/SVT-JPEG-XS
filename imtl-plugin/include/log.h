/*
 * Copyright (C) 2021 Intel Corporation.
 *
 * This software and the related documents are Intel copyrighted materials,
 * and your use of them is governed by the express license under which they
 * were provided to you ("License").
 * Unless the License provides otherwise, you may not use, modify, copy,
 * publish, distribute, disclose or transmit this software or the related
 * documents without Intel's prior written permission.
 *
 * This software and the related documents are provided as is, with no
 * express or implied warranties, other than those that are expressly stated
 * in the License.
 *
 */

/* Header for log usage */

#ifndef _PL_LOG_HEAD_H_
#define _PL_LOG_HEAD_H_

#include <stdio.h>

/* log levels, set from the plugin config "log_level" */
enum pl_log_level {
  PL_LOG_ERROR = 0,
  PL_LOG_WARNING,
  PL_LOG_INFO,
  PL_LOG_DEBUG,
};

extern int pl_log_level;

#define pl_log(level, ...)                              \
  do {                                                  \
    if ((level) <= pl_log_level) printf(__VA_ARGS__);   \
  } while (0)

/* log define */
#define dbg(...) pl_log(PL_LOG_DEBUG, __VA_ARGS__)
#define info(...) pl_log(PL_LOG_INFO, __VA_ARGS__)
#define warn(...) pl_log(PL_LOG_WARNING, __VA_ARGS__)
#define err(...) pl_log(PL_LOG_ERROR, __VA_ARGS__)

#endif
