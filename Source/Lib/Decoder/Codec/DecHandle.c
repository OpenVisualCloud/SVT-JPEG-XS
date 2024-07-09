/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "DecHandle.h"
#include "Decoder.h"
#include "Definitions.h"
#include "common_dsp_rtcd.h"
#include "decoder_dsp_rtcd.h"
#include "SvtJpegxsDec.h"
#include "ParseHeader.h"
#include "DecThreadInit.h"
#include "DecThreadSlice.h"
#include "DecThreadFinal.h"
#include "DecThreads.h"
#include "Threads/SvtThreads.h"
#include "SvtLog.h"
#include "EncDec.h"
#include "SvtJpegxsImageBufferTools.h"

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_get_single_frame_size(const uint8_t* bitstream_buf, size_t bitstream_buf_size,
                                                                       svt_jpeg_xs_image_config_t* out_image_config,
                                                                       uint32_t* frame_size, uint32_t fast_search) {
    return static_get_single_frame_size(bitstream_buf, bitstream_buf_size, out_image_config, frame_size, fast_search);
}

PREFIX_API void svt_jpeg_xs_decoder_close(svt_jpeg_xs_decoder_api_t* dec_api) {
    if (dec_api) {
        svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api->private_ptr;
        if (dec_api_prv) {
            svt_jxs_shutdown_process(dec_api_prv->input_buffer_resource_ptr);
            svt_jxs_shutdown_process(dec_api_prv->universal_buffer_resource_ptr);
            svt_jxs_shutdown_process(dec_api_prv->final_buffer_resource_ptr);
            svt_jxs_shutdown_process(dec_api_prv->output_buffer_resource_ptr);

            SVT_DESTROY_THREAD(dec_api_prv->input_stage_thread_handle);
            SVT_DESTROY_THREAD_ARRAY(dec_api_prv->universal_stage_thread_handle_array, dec_api_prv->universal_threads_num);
            SVT_DESTROY_THREAD(dec_api_prv->final_stage_thread_handle);

            SVT_DELETE(dec_api_prv->input_buffer_resource_ptr);
            SVT_DELETE(dec_api_prv->universal_buffer_resource_ptr);
            SVT_DELETE(dec_api_prv->final_buffer_resource_ptr);
            SVT_DELETE(dec_api_prv->output_buffer_resource_ptr);
            SVT_DELETE_PTR_ARRAY(dec_api_prv->universal_stage_context_ptr_array, dec_api_prv->universal_threads_num);

            SVT_DELETE(dec_api_prv->internal_pool_decoder_instance_resource_ptr);

            SVT_FREE(dec_api_prv->sync_output_ringbuffer);
            svt_jxs_free_cond_var(&dec_api_prv->sync_output_ringbuffer_left);

            for (uint32_t c = 0; c < MAX_COMPONENTS_NUM; c++) {
                SVT_FREE(dec_api_prv->dec_common.buffer_tmp_cpih[c]);
            }
            SVT_FREE(dec_api->private_ptr);
        }
        dec_api->private_ptr = NULL;
        svt_jxs_decrease_component_count();
    }
}

/**********************************
 * Decoder Handle Initialization
 **********************************/
SvtJxsErrorType_t decoder_allocate_handle(svt_jpeg_xs_decoder_api_t* dec_api) {
    if (dec_api->verbose >= VERBOSE_SYSTEM_INFO) {
        fprintf(stderr, "-------------------------------------------\n");
        fprintf(stderr, "SVT [version]:\tSVT-JPEGXS Decoder Lib v%i.%i\n", SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR);

#if defined(_MSC_VER) && (_MSC_VER >= 1930)
        fprintf(stderr, "SVT [build]  :\tVisual Studio 2022");
#elif defined(_MSC_VER) && (_MSC_VER >= 1920)
        fprintf(stderr, "SVT [build]  :\tVisual Studio 2019");
#elif defined(_MSC_VER) && (_MSC_VER >= 1910)
        fprintf(stderr, "SVT [build]  :\tVisual Studio 2017");
#elif defined(_MSC_VER) && (_MSC_VER >= 1900)
        fprintf(stderr, "SVT [build]  :\tVisual Studio 2015");
#elif defined(_MSC_VER)
        fprintf(stderr, "SVT [build]  :\tVisual Studio (old)");
#elif defined(__GNUC__)
        fprintf(stderr, "SVT [build]  :\tGCC %s\t", __VERSION__);
#else
        fprintf(stderr, "SVT [build]  :\tunknown compiler");
#endif
        fprintf(stderr, " %zu bit\n", sizeof(void*) * 8);
        fprintf(stderr, "LIB Build date: %s %s\n", __DATE__, __TIME__);
        fprintf(stderr, "-------------------------------------------\n");
    }

    SVT_CALLOC(dec_api->private_ptr, 1, sizeof(svt_jpeg_xs_decoder_api_prv_t));

    return SvtJxsErrorNone;
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_init(uint64_t version_api_major, uint64_t version_api_minor,
                                                      svt_jpeg_xs_decoder_api_t* dec_api, const uint8_t* bitstream_buf,
                                                      size_t codestream_size, svt_jpeg_xs_image_config_t* out_image_config) {
    if ((version_api_major > SVT_JPEGXS_API_VER_MAJOR) ||
        (version_api_major == SVT_JPEGXS_API_VER_MAJOR && version_api_minor > SVT_JPEGXS_API_VER_MINOR)) {
        return SvtJxsErrorInvalidApiVersion;
    }

    if (dec_api == NULL || bitstream_buf == NULL || codestream_size == 0) {
        return SvtJxsErrorDecoderInvalidPointer;
    }

    SvtJxsErrorType_t ret = decoder_allocate_handle(dec_api);
    if (ret) {
        svt_jpeg_xs_decoder_close(dec_api);
        return ret;
    }

    svt_jxs_increase_component_count();
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api->private_ptr;
    dec_api_prv->callback_decoder_ctx = dec_api;
    dec_api_prv->callback_send_data_available = dec_api->callback_send_data_available;
    dec_api_prv->callback_send_data_available_context = dec_api->callback_send_data_available_context;
    dec_api_prv->callback_get_data_available = dec_api->callback_get_data_available;
    dec_api_prv->callback_get_data_available_context = dec_api->callback_get_data_available_context;
    dec_api_prv->verbose = dec_api->verbose;

    dec_api_prv->packetization_mode = dec_api->packetization_mode;
    if (dec_api_prv->packetization_mode != 0 && dec_api_prv->packetization_mode != 1) {
        if (dec_api->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unrecognized packetization mode\n");
        }
        return SvtJxsErrorBadParameter;
    }

    const CPU_FLAGS cpu_flags = get_cpu_flags();
    dec_api->use_cpu_flags &= cpu_flags;
    if (dec_api_prv->verbose >= VERBOSE_SYSTEM_INFO) {
        fprintf(stderr, "[asm level on system : up to %s]\n", get_asm_level_name_str(cpu_flags));
        fprintf(stderr, "[asm level selected : up to %s]\n", get_asm_level_name_str(dec_api->use_cpu_flags));
    }

    setup_common_rtcd_internal(dec_api->use_cpu_flags);
    setup_decoder_rtcd_internal(dec_api->use_cpu_flags);

    //Init queue
    if (dec_api_prv->verbose >= VERBOSE_SYSTEM_INFO) {
        fprintf(stderr, "[Threads            : %i]\n", dec_api->threads_num);
    }

    //Zeroed handle memory
    dec_api_prv->input_buffer_resource_ptr = NULL;
    dec_api_prv->input_producer_fifo_ptr = NULL;
    dec_api_prv->input_consumer_fifo_ptr = NULL;
    dec_api_prv->input_stage_thread_handle = NULL;
    dec_api_prv->universal_buffer_resource_ptr = NULL;
    dec_api_prv->universal_producer_fifo_ptr = NULL;
    dec_api_prv->universal_threads_num = 0;
    dec_api_prv->universal_stage_thread_handle_array = NULL;
    dec_api_prv->universal_stage_context_ptr_array = NULL;
    dec_api_prv->final_buffer_resource_ptr = NULL;
    dec_api_prv->final_consumer_fifo_ptr = NULL;
    dec_api_prv->final_stage_thread_handle = NULL;
    dec_api_prv->sync_output_ringbuffer = NULL;
    dec_api_prv->sync_output_ringbuffer_size = 0;
    dec_api_prv->output_buffer_resource_ptr = NULL;
    dec_api_prv->output_producer_fifo_ptr = NULL;
    dec_api_prv->output_consumer_fifo_ptr = NULL;
    dec_api_prv->internal_pool_decoder_instance_resource_ptr = NULL;
    dec_api_prv->internal_pool_decoder_instance_fifo_ptr = NULL;

    memset(&dec_api_prv->slice_scheduler_ctx, 0, sizeof(svt_jpeg_xs_slice_scheduler_ctx_t));

    //Create common decoder
    picture_header_dynamic_t header_dynamic;
    ret = svt_jpeg_xs_decoder_probe(
        bitstream_buf, codestream_size, &dec_api_prv->dec_common.picture_header_const, &header_dynamic, dec_api_prv->verbose);
    if (ret) {
        svt_jpeg_xs_decoder_close(dec_api);
        return ret;
    }
    if (dec_api_prv->packetization_mode == 1 && header_dynamic.hdr_Lcod == 0) {
        if (dec_api->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Packetization mode(multiple packets per frame) not supported with variable bitrate coding\n");
        }
        svt_jpeg_xs_decoder_close(dec_api);
        return SvtJxsErrorBadParameter;
    }

    dec_api_prv->dec_common.max_frame_bitstream_size = 0;
    if (dec_api_prv->packetization_mode) {
        dec_api_prv->dec_common.max_frame_bitstream_size = header_dynamic.hdr_Lcod;
    }

    ret = svt_jpeg_xs_dec_init_common(&dec_api_prv->dec_common, out_image_config);
    if (ret) {
        svt_jpeg_xs_decoder_close(dec_api);
        return ret;
    }

    if (dec_api_prv->verbose >= VERBOSE_SYSTEM_INFO) {
        ColourFormat_t format = svt_jpeg_xs_get_format_from_params(dec_api_prv->dec_common.pi.comps_num,
                                                                   dec_api_prv->dec_common.picture_header_const.hdr_Sx,
                                                                   dec_api_prv->dec_common.picture_header_const.hdr_Sy);
        const char* color_format_name = svt_jpeg_xs_get_format_name(format);
        fprintf(stderr, "-------------------------------------------\n");
        fprintf(stderr,
                "SVT [config]: Resolution [width x height]         \t: %d x %d\n",
                dec_api_prv->dec_common.picture_header_const.hdr_width,
                dec_api_prv->dec_common.picture_header_const.hdr_height);
        fprintf(stderr,
                "SVT [config]: DecoderBitDepth / DecoderColorFormat\t: %d / %s\n",
                dec_api_prv->dec_common.picture_header_const.hdr_bit_depth[0],
                color_format_name);
    }

    if (dec_api->threads_num <= 2) {
        dec_api_prv->universal_threads_num = 1;
    }
    else {
        dec_api_prv->universal_threads_num = dec_api->threads_num - 2;
    }

    /*Number of threads: adding 10 contexts to faster schedule tasks when 10 outputs are waiting on write or on synchronize output*/
    uint32_t output_bitsteram_queue_count = 2 * dec_api_prv->universal_threads_num + 10;
    uint32_t input_bitstream_queue_count = 2 * dec_api_prv->universal_threads_num + 10;
    uint32_t pool_decoders_instances_count =
        3; //Allocate decoder instances, One instance to prepare init, one to calculate and one to finish
    dec_api_prv->sync_output_ringbuffer_size = dec_api_prv->universal_threads_num +
        20; //Should be more that pool_decoders_instances_count to not reduce performance

    if (dec_api_prv->verbose >= VERBOSE_SYSTEM_INFO_ALL) {
        fprintf(stderr, "-------------------------------------------\n");
        fprintf(stderr, "[%s] universal_threads_num: %i\n", __FUNCTION__, (int)dec_api_prv->universal_threads_num);
        fprintf(stderr, "[%s] TaskInputBitstreamQueueCount: %i\n", __FUNCTION__, (int)input_bitstream_queue_count);
        fprintf(stderr, "[%s] OutoutBitstreamQueueCount: %i\n", __FUNCTION__, (int)output_bitsteram_queue_count);
        fprintf(stderr, "[%s] sync_output_ringbuffer_size: %i\n", __FUNCTION__, (int)dec_api_prv->sync_output_ringbuffer_size);
        fprintf(stderr, "[%s] PoolDecContextsNum: %i\n", __FUNCTION__, (int)pool_decoders_instances_count);
    }

    if (!dec_api_prv->packetization_mode) {
        //Allocate Input Queue:
        SVT_NEW(dec_api_prv->input_buffer_resource_ptr,
                svt_jxs_system_resource_ctor,
                input_bitstream_queue_count,
                1,
                1,
                input_bitstream_creator,
                NULL,
                input_bitstream_destroyer);
        dec_api_prv->input_producer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(dec_api_prv->input_buffer_resource_ptr,
                                                                                         0);
        dec_api_prv->input_consumer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(dec_api_prv->input_buffer_resource_ptr,
                                                                                         0);
    }

    SVT_NEW(dec_api_prv->universal_buffer_resource_ptr,
            svt_jxs_system_resource_ctor,
            dec_api_prv->universal_threads_num,
            1,
            dec_api_prv->universal_threads_num,
            universal_frame_task_creator,
            NULL,
            universal_frame_task_creator_destroy);
    dec_api_prv->universal_producer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(
        dec_api_prv->universal_buffer_resource_ptr, 0);

    SVT_NEW(dec_api_prv->final_buffer_resource_ptr,
            svt_jxs_system_resource_ctor,
            output_bitsteram_queue_count,
            dec_api_prv->universal_threads_num,
            1,
            final_sync_creator,
            NULL,
            final_sync_destroyer);
    dec_api_prv->final_consumer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(dec_api_prv->final_buffer_resource_ptr, 0);

    //Call after alloc Universal output queue
    SVT_ALLOC_PTR_ARRAY(dec_api_prv->universal_stage_context_ptr_array, dec_api_prv->universal_threads_num);
    for (uint32_t process_index = 0; process_index < dec_api_prv->universal_threads_num; ++process_index) {
        SVT_NEW(dec_api_prv->universal_stage_context_ptr_array[process_index],
                universal_thread_context_ctor,
                dec_api_prv,
                process_index);
    }

    SVT_MALLOC(dec_api_prv->sync_output_ringbuffer, dec_api_prv->sync_output_ringbuffer_size * sizeof(OutItem));
    ret = svt_jxs_create_cond_var(&dec_api_prv->sync_output_ringbuffer_left);
    if (ret) {
        svt_jpeg_xs_decoder_close(dec_api);
        return ret;
    }
    svt_jxs_set_cond_var(&dec_api_prv->sync_output_ringbuffer_left, dec_api_prv->sync_output_ringbuffer_size);

    SVT_NEW(dec_api_prv->output_buffer_resource_ptr,
            svt_jxs_system_resource_ctor,
            output_bitsteram_queue_count,
            1,
            1,
            output_frame_creator,
            NULL,
            output_frame_destroyer);
    dec_api_prv->output_producer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(dec_api_prv->output_buffer_resource_ptr, 0);
    dec_api_prv->output_consumer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(dec_api_prv->output_buffer_resource_ptr, 0);

    SVT_NEW(dec_api_prv->internal_pool_decoder_instance_resource_ptr,
            svt_jxs_system_resource_ctor,
            pool_decoders_instances_count,
            1,
            0,
            pool_decoder_instance_create_ctor,
            dec_api_prv,
            pool_decoder_instance_destroy_ctor);
    dec_api_prv->internal_pool_decoder_instance_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(
        dec_api_prv->internal_pool_decoder_instance_resource_ptr, 0);

    if (!dec_api_prv->packetization_mode) {
        /*Start threads*/
        SVT_CREATE_THREAD(dec_api_prv->input_stage_thread_handle, thread_init_stage_kernel, dec_api_prv);
    }

    SVT_CREATE_THREAD_ARRAY(dec_api_prv->universal_stage_thread_handle_array,
                            dec_api_prv->universal_threads_num,
                            thread_universal_stage_kernel,
                            dec_api_prv->universal_stage_context_ptr_array);

    SVT_CREATE_THREAD(dec_api_prv->final_stage_thread_handle, thread_final_stage_kernel, dec_api_prv);

    if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
        fprintf(stderr, "[%s] End\n", __FUNCTION__);
    }

    if (dec_api_prv->verbose >= VERBOSE_ERRORS) {
        fprintf(stderr, "-------------------------------------------\n");
        svt_log_init();
        svt_jxs_print_memory_usage();
    }

    return SvtJxsErrorNone;
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_frame(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_input,
                                                            uint8_t blocking_flag) {
    if (dec_api == NULL || dec_api->private_ptr == NULL || dec_input == NULL) {
        return SvtJxsErrorDecoderInvalidPointer;
    }

    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api->private_ptr;

    if (dec_api_prv->packetization_mode) {
        if (dec_api->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "\nDecoder initialized for packet-based input, but svt_jpeg_xs_decoder_send_frame() is called\n");
            fprintf(stderr, "Please use svt_jpeg_xs_decoder_send_packet() instead\n");
        }
        return SvtJxsErrorUndefined;
    }

    pi_t* pi = &dec_api_prv->dec_common.pi;
    uint8_t input_bit_depth = dec_api_prv->dec_common.picture_header_const.hdr_bit_depth[0];
    uint32_t pixel_size = input_bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    for (uint8_t c = 0; c < pi->comps_num; ++c) {
        uint32_t min_size;
        // The last row might be shorter than the stride, e.g. in case the application is decoding
        // an interlaced image (represented by two codestreams, one for each field) into an output
        // image where the fields in memory are interleaved row by row. When feeding each field
        // codestream individually to the decoder, the stride will be double the usual rowstride
        // so that the decoder skips a row in the output image and only splats the field pixels
        // into every second row. The last row of the second field which is the last row of the
        // output image would only have a single row of data left in it then though (at most half
        // the specified rowstride in that case).
        min_size = dec_input->image.stride[c] * pixel_size * (pi->components[c].height - 1);
        min_size += pi->components[c].width * pixel_size;
        if (dec_input->image.alloc_size[c] < min_size) {
            return SvtJxsErrorBadParameter;
        }
    }

    ObjectWrapper_t* input_wrapper_ptr;
    if (blocking_flag) {
        svt_jxs_get_empty_object(dec_api_prv->input_producer_fifo_ptr, &input_wrapper_ptr);
    }
    else {
        svt_jxs_get_empty_object_non_blocking(dec_api_prv->input_producer_fifo_ptr, &input_wrapper_ptr);
    }

    if (dec_api->verbose >= VERBOSE_INFO_MULTITHREADING) {
        fprintf(stderr, "\n[%s] Send frame to Lib, Item: %p\n", __FUNCTION__, input_wrapper_ptr);
    }

    if (input_wrapper_ptr != NULL) {
        TaskInputBitstream* buffer_input = (TaskInputBitstream*)input_wrapper_ptr->object_ptr;
        buffer_input->dec_input = *dec_input; /*Copy output buffer structure.*/
        buffer_input->flags = 0;
        svt_jxs_post_full_object(input_wrapper_ptr);
        return SvtJxsErrorNone;
    }
    return SvtJxsErrorNoErrorEmptyQueue; //Queue is full, please try again later
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_packet(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_input,
                                                             uint32_t* bytes_used) {
    if (dec_api == NULL || dec_api->private_ptr == NULL || dec_input == NULL || bytes_used == NULL) {
        return SvtJxsErrorDecoderInvalidPointer;
    }
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api->private_ptr;
    return internal_svt_jpeg_xs_decoder_send_packet(dec_api_prv, dec_input, bytes_used);
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_get_frame(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_output,
                                                           uint8_t blocking_flag) {
    if (dec_api == NULL || dec_api->private_ptr == NULL || dec_output == NULL) {
        return SvtJxsErrorDecoderInvalidPointer;
    }

    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api->private_ptr;
    ObjectWrapper_t* wrapper_ptr = NULL;
    if (blocking_flag) {
        svt_jxs_get_full_object(dec_api_prv->output_consumer_fifo_ptr, &wrapper_ptr);
    }
    else {
        svt_jxs_get_full_object_non_blocking(dec_api_prv->output_consumer_fifo_ptr, &wrapper_ptr);
    }

    if (wrapper_ptr) {
        TaskOutFrame* input_buffer_ptr = (TaskOutFrame*)wrapper_ptr->object_ptr;

        *dec_output = input_buffer_ptr->dec_input; //Copy structure
        SvtJxsErrorType_t frame_error = input_buffer_ptr->frame_error;

        //Release buffer back:
        svt_jxs_release_object(wrapper_ptr);
        return frame_error;
    }
    else {
        dec_output->user_prv_ctx_ptr = NULL;
    }
    return SvtJxsErrorNoErrorEmptyQueue;
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_eoc(svt_jpeg_xs_decoder_api_t* dec_api) {
    if (dec_api == NULL || dec_api->private_ptr == NULL) {
        return SvtJxsErrorDecoderInvalidPointer;
    }

    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)dec_api->private_ptr;

    if (dec_api_prv->packetization_mode) {
        ObjectWrapper_t* wrapper_ptr_decoder_ctx = NULL;

        SvtJxsErrorType_t ret = svt_jxs_get_empty_object(dec_api_prv->internal_pool_decoder_instance_fifo_ptr,
                                                         &wrapper_ptr_decoder_ctx);

        if (ret != SvtJxsErrorNone || wrapper_ptr_decoder_ctx == NULL) {
            return ret;
        }

        svt_jpeg_xs_decoder_instance_t* dec_ctx = wrapper_ptr_decoder_ctx->object_ptr;

        dec_ctx->frame_num = dec_api_prv->slice_scheduler_ctx.frame_num;
        dec_ctx->sync_output_frame_idx = dec_api_prv->slice_scheduler_ctx.sync_output_frame_idx;

        dec_api_prv->slice_scheduler_ctx.sync_output_frame_idx = (dec_api_prv->slice_scheduler_ctx.sync_output_frame_idx + 1) %
            dec_api_prv->sync_output_ringbuffer_size;
        dec_api_prv->slice_scheduler_ctx.frame_num++;

        ObjectWrapper_t* universal_wrapper_ptr = NULL;
        ret = svt_jxs_get_empty_object(dec_api_prv->universal_producer_fifo_ptr, &universal_wrapper_ptr);
        if (ret != SvtJxsErrorNone || universal_wrapper_ptr == NULL) {
            return ret;
        }

        TaskCalculateFrame* buffer_output = (TaskCalculateFrame*)universal_wrapper_ptr->object_ptr;
        buffer_output->wrapper_ptr_decoder_ctx = wrapper_ptr_decoder_ctx;
        //buffer_output->image_buffer = input_buffer_ptr->dec_input.image;
        memset(&buffer_output->image_buffer, 0, sizeof(buffer_output->image_buffer));
        //dec_ctx->dec_input = buffer_output->image_buffer;

        /*Use wrapper output only to propagate error to Thread Final*/
        buffer_output->frame_error = SvtJxsDecoderEndOfCodestream;
        buffer_output->slice_id = 0;
        dec_ctx->sync_num_slices_to_receive = 1;

        svt_jxs_post_full_object(universal_wrapper_ptr);
    }
    else {
        ObjectWrapper_t* input_wrapper_ptr = NULL;
        SvtJxsErrorType_t ret = svt_jxs_get_empty_object(dec_api_prv->input_producer_fifo_ptr, &input_wrapper_ptr);
        if (ret != SvtJxsErrorNone || input_wrapper_ptr == NULL) {
            return ret;
        }

        if (dec_api->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "\n[%s] Send EOC to Lib, Item: %p\n", __FUNCTION__, input_wrapper_ptr);
        }

        if (input_wrapper_ptr != NULL) {
            TaskInputBitstream* buffer_input = (TaskInputBitstream*)input_wrapper_ptr->object_ptr;
            memset(&buffer_input->dec_input, 0, sizeof(buffer_input->dec_input));
            buffer_input->flags = SvtJxsDecoderEndOfCodestream;
            svt_jxs_post_full_object(input_wrapper_ptr);
            return SvtJxsErrorNone;
        }
    }
    return SvtJxsErrorUndefined;
}
