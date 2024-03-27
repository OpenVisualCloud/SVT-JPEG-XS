/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __APP_SEMAPHORE_H__
#define __APP_SEMAPHORE_H__
#include "SvtJpegxs.h"

void* semaphore_create(uint32_t initial_count, uint32_t max_count);
SvtJxsErrorType_t semaphore_destroy(void* semaphore_handle);

SvtJxsErrorType_t semaphore_post(void* semaphore_handle);
SvtJxsErrorType_t semaphore_block(void* semaphore_handle, int32_t timout_ms);

void* app_create_thread(void* (*thread_function)(void*), void* thread_context);
SvtJxsErrorType_t app_destroy_thread(void* thread_handle);

#endif
