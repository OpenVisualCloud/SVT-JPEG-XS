/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _ENCODER_HANDLE_H_
#define _ENCODER_HANDLE_H_

#include "Definitions.h"
#include "Threads/SvtObject.h"
#include "Encoder.h"
#include "Threads/SystemResourceManager.h"
#include "Threads/SvtThreads.h"

typedef struct ThreadContext {
    DctorCall dctor;
    void_ptr priv;
} ThreadContext_t;

/**************************************
 * Component Private Data
 **************************************/
typedef struct svt_jpeg_xs_encoder_api_prv {
    DctorCall dctor;

    /*Pointer from init, return in callbacks.*/
    svt_jpeg_xs_encoder_api_t *callback_encoder_ctx;
    /* Callback: Call when available place in queue to send data.
    * Call when finish init encoder.
    * Call every time when the next element from the input queue is taken.
    * Only one callback will be triggered at a time. Always from that same thread.
    * No synchronization required.
    * encoder_handle - Pointer passed on initialization.*/
    void (*callback_send_data_available)(svt_jpeg_xs_encoder_api_t *encoder, void *context);
    void *callback_send_data_available_context;

    /* Callback: Call when new frame is ready to get.
    * Only one callback will be triggered at a time.
    * No synchronization required.
    * encoder_handle - Pointer passed on initialization.*/
    void (*callback_get_data_available)(svt_jpeg_xs_encoder_api_t *encoder, void *context);
    void *callback_get_data_available_context;

    // picture control set pool
    SystemResource_t *picture_control_set_pool_ptr;

    svt_jpeg_xs_encoder_common_t enc_common; /*Common encoder*/

    uint32_t dwt_stage_threads_num;
    uint32_t pack_stage_threads_num;

    ObjectWrapper_t **sync_output_ringbuffer; //Array of pointers to reorder output
    uint32_t sync_output_ringbuffer_size;
    CondVar sync_output_ringbuffer_left;

    // Thread Handles
    Handle_t init_stage_thread_handle;
    Handle_t *dwt_stage_thread_handle_array;
    Handle_t *pack_stage_thread_handle_array;
    Handle_t final_stage_thread_handle;

    // Contexts
    ThreadContext_t *init_stage_context_ptr;
    ThreadContext_t **dwt_stage_context_ptr_array;
    ThreadContext_t **pack_stage_context_ptr_array;
    ThreadContext_t *final_stage_context_ptr;

    // System Resource Managers
    SystemResource_t *input_image_resource_ptr;
    Fifo_t *input_image_producer_fifo_ptr;
    Fifo_t *input_image_consumer_fifo_ptr;

    SystemResource_t *dwt_input_resource_ptr;
    SystemResource_t *pack_input_resource_ptr;
    SystemResource_t *pack_output_resource_ptr;

    SystemResource_t *output_queue_resource_ptr;
    Fifo_t *output_queue_producer_fifo_ptr;
    Fifo_t *output_queue_consumer_fifo_ptr;

    uint64_t frame_number;
} svt_jpeg_xs_encoder_api_prv_t;

#endif /*_ENCODER_HANDLE_H_*/
