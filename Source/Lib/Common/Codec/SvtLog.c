/*
 * Copyright(c) 2019 Intel Corporation
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at https://www.aomedia.org/license/software-license. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * https://www.aomedia.org/license/patent-license.
 */
#include "SvtLog.h"
// for getenv and fopen on windows
#define _CRT_SECURE_NO_WARNINGS
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *log_level_str(SvtLogLevel level) {
    switch (level) {
    case SVT_LOG_FATAL:
        return "fatal";
    case SVT_LOG_ERROR:
        return "error";
    case SVT_LOG_WARN:
        return "warn";
    case SVT_LOG_INFO:
        return "info";
    case SVT_LOG_DEBUG:
        return "debug";
    default:
        return "unknown";
    }
}

typedef struct DefaultLogCtx {
    SvtLogLevel level;
    FILE *file;
} DefaultLogCtx;

/* Handles both the default logger and a registered custom logger.
 * If fn == default_logger, ctx is a DefaultLogCtx*, otherwise ctx is the user-provided context. */
typedef struct SvtLogger {
    SvtJxsLogCallback fn;
    void *ctx;
} SvtLogger;

static void default_logger(void *context, SvtLogLevel level, const char *tag, const char *fmt, va_list args) {
    DefaultLogCtx *logger = (DefaultLogCtx *)context;
    if (level > logger->level)
        return;
    if (tag)
        fprintf(logger->file, "%s[%s]: ", tag, log_level_str(level));
    vfprintf(logger->file, fmt, args);
    fflush(logger->file);
}

static SvtLogger *g_logger;

static void logger_cleanup(void) {
    if (g_logger->fn == default_logger) {
        DefaultLogCtx *ctx = (DefaultLogCtx *)g_logger->ctx;
        if (ctx->file && ctx->file != stderr)
            fclose(ctx->file);
        free(ctx);
    }
    free(g_logger);
    g_logger = NULL;
}

// Staged by svt_jpeg_xs_set_log_callback() before first use; read exactly once by logger_create().
static SvtLogger g_custom_logger_input;

#ifdef BUILD_TESTING
// atexit() only needs to be registered once ever: logger_create() can run more than once per
// process when svt_jxs_log_reset_for_testing() rearms the once-guard, and re-registering
// logger_cleanup() each time would leave stale atexit entries that read g_logger after an
// earlier entry already freed and NULLed it.
static int g_logger_atexit_registered;
#endif // BUILD_TESTING

static void logger_create(void) {
    g_logger = (SvtLogger *)calloc(1, sizeof(*g_logger));
    if (!g_logger)
        return;

    SvtLogger custom = g_custom_logger_input;
    if (custom.fn) {
        // A custom logger was registered before the logger was first used.
        *g_logger = custom;
#ifdef BUILD_TESTING
        if (!g_logger_atexit_registered) {
            atexit(logger_cleanup);
            g_logger_atexit_registered = 1;
        }
#else
        atexit(logger_cleanup);
#endif // BUILD_TESTING
        return;
    }

    DefaultLogCtx *ctx = (DefaultLogCtx *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(g_logger);
        g_logger = NULL;
        return;
    }

    const char *log = getenv("SVT_LOG");
    ctx->level = SVT_LOG_INFO;
    if (log) {
        const int new_level = atoi(log);
        if (new_level >= SVT_LOG_ALL && new_level <= SVT_LOG_DEBUG)
            ctx->level = (SvtLogLevel)new_level;
    }

    const char *file = getenv("SVT_LOG_FILE");
    if (file)
        ctx->file = fopen(file, "w+");
    if (!ctx->file)
        ctx->file = stderr;

    g_logger->fn = default_logger;
    g_logger->ctx = ctx;
#ifdef BUILD_TESTING
    if (!g_logger_atexit_registered) {
        atexit(logger_cleanup);
        g_logger_atexit_registered = 1;
    }
#else
    atexit(logger_cleanup);
#endif // BUILD_TESTING
}

#ifdef _WIN32
#include <windows.h>

static INIT_ONCE g_logger_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK logger_create_wrapper(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContext) {
    (void)InitOnce;
    (void)Parameter;
    (void)lpContext;
    logger_create();
    return TRUE;
}

static SvtLogger *get_logger(void) {
    InitOnceExecuteOnce(&g_logger_once, logger_create_wrapper, NULL, NULL);
    return g_logger;
}
#else
#include <pthread.h>

static pthread_once_t g_logger_once = PTHREAD_ONCE_INIT;

static SvtLogger *get_logger(void) {
    pthread_once(&g_logger_once, logger_create);
    return g_logger;
}
#endif // _WIN32

void svt_jxs_log_init() {
    get_logger();
}

#ifdef BUILD_TESTING
// Rearms the once-guard and drops any built logger/staged callback, so the next svt_jxs_log()
// call in this process re-runs logger_create() as if none had happened yet. Meant to be called
// right after fork(), in a child that wants a virgin logger, before staging a callback via
// svt_jpeg_xs_set_log_callback() - it does not undo effects already observed by other
// threads/processes sharing this one.
void svt_jxs_log_reset_for_testing(void) {
    if (g_logger)
        logger_cleanup();
    memset(&g_custom_logger_input, 0, sizeof(g_custom_logger_input));
    g_logger_atexit_registered = 0;
#ifdef _WIN32
    g_logger_once = (INIT_ONCE)INIT_ONCE_STATIC_INIT;
#else
    /* Assigned from a copy rather than directly: on glibc PTHREAD_ONCE_INIT is a
     * plain 0, but on other libcs (macOS among them) it is a brace initializer,
     * which is only valid where an initializer is expected. */
    static const pthread_once_t once_init = PTHREAD_ONCE_INIT;
    g_logger_once = once_init;
#endif // _WIN32
}
#endif // BUILD_TESTING

PREFIX_API void svt_jpeg_xs_set_log_callback(SvtJxsLogCallback callback, void *context) {
    // Only take the custom context if a custom callback is also provided, so the default
    // logger's own DefaultLogCtx* is never mistaken for a user context (or vice versa).
    // Has effect only if called before the logger is first used (see logger_create()).
    if (callback) {
        g_custom_logger_input.fn = callback;
        g_custom_logger_input.ctx = context;
    }
}

void svt_jxs_log(SvtLogLevel level, const char *tag, const char *format, ...) {
    SvtLogger *logger = get_logger();
    if (!logger)
        return;

    va_list args;
    va_start(args, format);
    logger->fn(logger->ctx, level, tag, format, args);
    va_end(args);
}
