/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DECODER_HANDLE_H_
#define _DECODER_HANDLE_H_

#include "Definitions.h"
#include "Pi.h"
#include "Decoder.h"
#include "DecThreads.h"
#include "Threads/SystemResourceManager.h"
#include "SvtJpegxsImageBufferTools.h"
#include "SvtJpegxsDec.h"

typedef struct {
    int32_t in_use;
    int32_t ready_to_send;
    uint32_t received_slices;
    ObjectWrapper_t* wrapper_ptr_decoder_ctx;
    svt_jpeg_xs_frame_t dec_input;
    uint64_t frame_num;
    uint32_t sync_output_frame_idx;
    uint32_t frame_error_slice;
    SvtJxsErrorType_t frame_error; //Positive read size of frame in bitstream, otherwise error code.
    uint32_t slice_next_to_recalc; //TODO: Recalculate any received Pair of slices, not from top to bottom.
} OutItem;

typedef struct svt_jpeg_xs_slice_scheduler_ctx {
    ObjectWrapper_t* wrapper_ptr_decoder_ctx;
    uint32_t sync_output_frame_idx;
    uint64_t frame_num;
    uint32_t slices_sent;

    uint32_t header_size;
    uint32_t bytes_filled;
    uint32_t bytes_processed;
} svt_jpeg_xs_slice_scheduler_ctx_t;

typedef struct svt_jpeg_xs_decoder_api_prv {
    svt_jpeg_xs_decoder_api_t* callback_decoder_ctx;
    /* Callback: Call when available place in queue to send data.
     * Call when finish init decoder.
     * Call every time when the next element from the input queue is taken.
     * Only one callback will be triggered at a time. Always from that same thread.
     * No synchronization required.*/
    void (*callback_send_data_available)(svt_jpeg_xs_decoder_api_t* decoder, void* context);
    void* callback_send_data_available_context;
    /* Callback: Call when new frame is ready to get.
     * Only one callback will be triggered at a time.
     * No synchronization required.*/
    void (*callback_get_data_available)(svt_jpeg_xs_decoder_api_t* decoder, void* context);
    void* callback_get_data_available_context;

    uint32_t verbose;
    uint8_t packetization_mode;
    proxy_mode_t proxy_mode;

    svt_jpeg_xs_decoder_common_t dec_common; /*Common decoder*/

    /* Thread model resources:
     *                            | <---------------------------\            -Output to final or schedule next type of work
     *........................... |-> [Universal Task Threads] -^-> |
     *                            | <---------------------------\            -Output to final or schedule next type of work
     * [AppDec] -> [Init Thread] -|-> [Universal Task Threads] -^-> |-> [Final Thread] -> [AppDec]
     *                            | <---------------------------\            -Output to final or schedule next type of work
     *........................... |-> [Universal Task Threads] -^-> |
     *           ^- input_buffer_resource_ptr
     *                              ^- universal_buffer_resource_ptr
     *                                                                ^- final_buffer_resource_ptr
     *                                                                                   ^- output_buffer_resource_ptr
     */

    /*
     * Input buffers between App and thread_init_stage_kernel()
     * type: TaskInputBitstream
     */
    SystemResource_t* input_buffer_resource_ptr;
    Fifo_t* input_producer_fifo_ptr;
    Fifo_t* input_consumer_fifo_ptr;

    /*
     * Thread thread_init_stage_kernel()
     */
    Handle_t input_stage_thread_handle;

    /*
     * Buffers between thread_init_stage_kernel() and thread_universal_stage_kernel()
     * type: TaskCalculateFrame
     * consumers keep individual in contexts of thread_universal_stage_kernel()
     */
    SystemResource_t* universal_buffer_resource_ptr;
    Fifo_t* universal_producer_fifo_ptr;

    /*
     * Threads and Contexts of threads thread_universal_stage_kernel()
     * type of context: UniversalThreadContext
     */
    uint32_t universal_threads_num;                      //Number of threads
    Handle_t* universal_stage_thread_handle_array;       //Threads
    ThreadContext_t** universal_stage_context_ptr_array; //Contexts for threads UniversalThreadContext

    /*
     * Buffers between thread_universal_stage_kernel() and thread_final_stage_kernel()
     * type: TaskFinalSync
     * producers keep individual in contexts of thread_universal_stage_kernel()
     */
    SystemResource_t* final_buffer_resource_ptr;
    Fifo_t* final_consumer_fifo_ptr;

    /*
     * Thread thread_final_stage_kernel()
     */
    Handle_t final_stage_thread_handle;
    OutItem* sync_output_ringbuffer;
    uint32_t sync_output_ringbuffer_size;
    CondVar sync_output_ringbuffer_left;

    /*
     * Buffers between thread_final_stage_kernel() and App
     * type: TaskOutFrame
     */
    SystemResource_t* output_buffer_resource_ptr;
    Fifo_t* output_producer_fifo_ptr;
    Fifo_t* output_consumer_fifo_ptr;

    /*
     * Pool of Decoder instances contexts to separate frames
     * type: svt_jpeg_xs_decoder_instance_t*
     */
    SystemResource_t* internal_pool_decoder_instance_resource_ptr;
    Fifo_t* internal_pool_decoder_instance_fifo_ptr;

    /*
    * Context required only when packetization_mode is enabled,
    * Holds the required data to properly schedule subsequent slices for processing
    */
    svt_jpeg_xs_slice_scheduler_ctx_t slice_scheduler_ctx;
} svt_jpeg_xs_decoder_api_prv_t;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /*_DECODER_HANDLE_H_*/
