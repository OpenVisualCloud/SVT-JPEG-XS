/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "DecThreads.h"
#include "DecThreadInit.h"
#include "DecThreadSlice.h"
#include "SvtUtility.h"
#include "Codestream.h"
#include "SvtJpegxsImageBufferTools.h"

SvtJxsErrorType_t input_bitstream_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_init_data_ptr);
    TaskInputBitstream* input_buffer;

    *object_dbl_ptr = NULL;
    SVT_CALLOC(input_buffer, 1, sizeof(TaskInputBitstream));
    *object_dbl_ptr = (void_ptr)input_buffer;

    return SvtJxsErrorNone;
}

void input_bitstream_destroyer(void_ptr p) {
    TaskInputBitstream* obj = (TaskInputBitstream*)p;
    SVT_FREE(obj);
}

static uint16_t get_16_bits(const uint8_t* buf) {
    uint16_t ret_val = (uint16_t)buf[0] << 8;
    return ret_val | buf[1];
}

//TODO: handle frame_size and error code
static int32_t get_slice_size(pi_t* pi, const uint8_t* bitstream_buf, size_t bitstream_buf_size, uint32_t slice,
                              uint32_t* out_slice_size) {
    uint32_t offset_bytes = 0;
    uint32_t header_size = 0;
    uint16_t marker = 0;
    uint32_t precinct_headers_begin = 0;
    uint8_t first_marker = 1;

    uint32_t lines_per_slice_last = pi->precincts_line_num - (pi->slice_num - 1) * pi->precincts_per_slice;
    const uint32_t is_last_slice = (slice == (pi->slice_num - 1));
    uint32_t expect_precincts = pi->precincts_per_slice * pi->precincts_col_num;
    if (is_last_slice) {
        expect_precincts = lines_per_slice_last * pi->precincts_col_num;
    }

    if (bitstream_buf == NULL || bitstream_buf_size == 0) {
        //end of stream
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    do {
        if (offset_bytes + 2 > bitstream_buf_size) {
            // return frame not full
            return SvtJxsErrorDecoderBitstreamTooShort;
        }
        else {
            marker = get_16_bits(bitstream_buf + offset_bytes);
        }
        switch (marker) {
        case CODESTREAM_PIH:
        case CODESTREAM_SOC:
        case CODESTREAM_CWD:
        case CODESTREAM_CDT:
        case CODESTREAM_CAP:
        case CODESTREAM_WGT:
        case CODESTREAM_NLT:
        case CODESTREAM_CTS:
        case CODESTREAM_CRG:
        case CODESTREAM_COM:
        case CODESTREAM_EOC:
            return SvtJxsErrorDecoderInvalidBitstream;
            break;
        case CODESTREAM_SLH:
            if (first_marker == 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            first_marker = 0;
            if (offset_bytes + 6 > bitstream_buf_size) {
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            header_size = get_16_bits(bitstream_buf + offset_bytes + 2);
            uint32_t slice_index = get_16_bits(bitstream_buf + offset_bytes + 4);
            offset_bytes += 2 + header_size;
            if (slice_index != slice) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            precinct_headers_begin = 1;
            break;
        default:
            if (precinct_headers_begin == 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            if ((marker >> 12) != 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }

            if (offset_bytes + 3 <= bitstream_buf_size) {
                uint32_t Lprc_8lsb = bitstream_buf[offset_bytes + 2];
                int32_t precinct_size = marker << 8 | Lprc_8lsb;
                if (precinct_size > PRECINCT_MAX_BYTES_SIZE) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                offset_bytes += precinct_size + 5; //Lprc + Q[p] + R[p]
                /*Calculate Bit-plane count coding mode with alignment*/
                int spb_bits = pi->bands_num_exists * 2;
                int spb_bytes = spb_bits / 8;
                if (spb_bits & 7) {
                    spb_bytes++;
                }
                offset_bytes += spb_bytes;
                if (offset_bytes > bitstream_buf_size) {
                    return SvtJxsErrorDecoderBitstreamTooShort;
                }

                expect_precincts--;
                if (expect_precincts == 0) {
                    *out_slice_size = offset_bytes;
                    return 0;
                }
            }
            else {
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
        }
    } while (1);
    return SvtJxsErrorDecoderInvalidBitstream;
}

static void send_slices_tasks(svt_jpeg_xs_decoder_api_prv_t* dec_api_prv, TaskInputBitstream* input_buffer_ptr,
                              ObjectWrapper_t* wrapper_ptr_decoder_ctx, svt_jpeg_xs_image_buffer_t* image_buffer,
                              uint32_t header_size) {
    uint32_t offset = header_size;
    svt_jpeg_xs_decoder_instance_t* dec_ctx = wrapper_ptr_decoder_ctx->object_ptr;
    pi_t* pi = &dec_ctx->dec_common->pi;

    dec_ctx->sync_num_slices_to_receive = pi->slice_num;
    dec_ctx->sync_slices_idwt = (pi->decom_v != 0) && (dec_api_prv->universal_threads_num > 1) && (pi->precincts_per_slice > 2) &&
        (dec_ctx->dec_common->picture_header_const.hdr_Cpih == 0);

    for (uint32_t slice_idx = 0; slice_idx < pi->slice_num; slice_idx++) {
        svt_jxs_set_cond_var(&dec_ctx->map_slices_decode_done[slice_idx], SYNC_INIT);
    }

    for (uint32_t slice = 0; slice < pi->slice_num; slice++) {
        /*Get Wrapper output*/
        ObjectWrapper_t* universal_wrapper_ptr = NULL;
        SvtJxsErrorType_t err = svt_jxs_get_empty_object(dec_api_prv->universal_producer_fifo_ptr, &universal_wrapper_ptr);
        if (err != SvtJxsErrorNone || universal_wrapper_ptr == NULL) {
            return;
        }
        TaskCalculateFrame* buffer_output = (TaskCalculateFrame*)universal_wrapper_ptr->object_ptr;
        buffer_output->wrapper_ptr_decoder_ctx = wrapper_ptr_decoder_ctx;
        buffer_output->image_buffer = *image_buffer;
        buffer_output->bitstream_buf = input_buffer_ptr->dec_input.bitstream.buffer + offset;
        buffer_output->bitstream_buf_size = input_buffer_ptr->dec_input.bitstream.used_size - offset;
        buffer_output->slice_id = slice;
        uint32_t frame_bitstream_size = header_size;

        uint32_t out_slice_size;
        int32_t ret = get_slice_size(
            &dec_ctx->dec_common->pi, buffer_output->bitstream_buf, buffer_output->bitstream_buf_size, slice, &out_slice_size);
        if (!ret) {
            offset += out_slice_size;
            if (slice + 1 == pi->slice_num) {
                if (offset + 1 < input_buffer_ptr->dec_input.bitstream.used_size) {
                    uint16_t val = get_16_bits(input_buffer_ptr->dec_input.bitstream.buffer + offset);
                    if (val != CODESTREAM_EOC) {
                        ret = SvtJxsErrorDecoderInvalidBitstream;
                    }
                }
                else {
                    ret = SvtJxsErrorDecoderBitstreamTooShort;
                }
                if (!ret) {
                    frame_bitstream_size = offset + 2;

                    if (dec_ctx->picture_header_dynamic.hdr_Lcod != 0 &&
                        dec_ctx->picture_header_dynamic.hdr_Lcod != frame_bitstream_size) {
                        if (dec_api_prv->verbose >= VERBOSE_ERRORS) {
                            fprintf(stderr,
                                    "Warning: Frame decoded but may be broken! Decoded different stream size than expected from "
                                    "header get=%u, expected=%u\n",
                                    frame_bitstream_size,
                                    dec_ctx->picture_header_dynamic.hdr_Lcod);
                        }
                    }
                }
            }
        }

        buffer_output->frame_error = ret;
        if (ret) {
            dec_ctx->sync_num_slices_to_receive = slice + 1;
        }
        svt_jxs_post_full_object(universal_wrapper_ptr);
        if (ret) {
            break;
        }
    }
}

void* thread_init_stage_kernel(void* input_ptr) {
    ThreadContext_t* thread_ctx = (ThreadContext_t*)input_ptr;
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)thread_ctx;
    CondVar* sync_output_ringbuffer_left = &dec_api_prv->sync_output_ringbuffer_left;

    uint64_t frame_num = 0;
    uint32_t sync_output_frame_idx = 0;

    /*Callback available space in Input Queue*/
    svt_jpeg_xs_decoder_api_t* callback_decoder_ctx = dec_api_prv->callback_decoder_ctx;
    void (*callback_send)(svt_jpeg_xs_decoder_api_t*, void*) = dec_api_prv->callback_send_data_available;
    void* callback_send_context = dec_api_prv->callback_send_data_available_context;

    //Initial available input buffer
    if (callback_send) {
        callback_send(callback_decoder_ctx, callback_send_context);
    }

    for (;;) {
        ObjectWrapper_t* input_wrapper_ptr;

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "[%s] Before SVT_GET_FULL_OBJECT\n", __FUNCTION__);
        }

        SVT_GET_FULL_OBJECT(dec_api_prv->input_consumer_fifo_ptr, &input_wrapper_ptr);
        TaskInputBitstream* input_buffer_ptr = (TaskInputBitstream*)input_wrapper_ptr->object_ptr;

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr,
                    "\n[%s] Send frame to Lib, Item: %p frame %lu\n",
                    __FUNCTION__,
                    input_wrapper_ptr,
                    (unsigned long)frame_num);
        }

        if (dec_api_prv->verbose >= VERBOSE_WARNINGS && (input_buffer_ptr->flags != SvtJxsDecoderEndOfCodestream)) {
            uint32_t read_size = 0;
            SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
                input_buffer_ptr->dec_input.bitstream.buffer,
                input_buffer_ptr->dec_input.bitstream.used_size,
                NULL,
                &read_size,
                0,
                dec_api_prv->proxy_mode);
            if ((ret != SvtJxsErrorNone) && input_buffer_ptr->dec_input.bitstream.used_size != read_size) {
                //TODO: Hard to test this case and in future this problem should be removed
                fprintf(stderr,
                        "[%s] %p, %i Invalid frame, HOW TO HANDLE ERROR????\n",
                        __FUNCTION__,
                        input_buffer_ptr->dec_input.bitstream.buffer,
                        (int32_t)input_buffer_ptr->dec_input.bitstream.used_size);
            }
        }

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "[%s] Before svt_jxs_get_empty_object(dec_api_prv->universal_producer_fifo_ptr\n", __FUNCTION__);
        }

        ObjectWrapper_t* wrapper_ptr_decoder_ctx = NULL;

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "[%s] Before svt_jxs_get_empty_object(dec_api_prv->internal_pool_frame_context_fifo_ptr\n", __FUNCTION__);
        }
        SvtJxsErrorType_t err = svt_jxs_get_empty_object(dec_api_prv->internal_pool_decoder_instance_fifo_ptr,
                                                         &wrapper_ptr_decoder_ctx);
        if (err != SvtJxsErrorNone || wrapper_ptr_decoder_ctx == NULL) {
            continue;
        }

        if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
            fprintf(stderr, "[%s] Send frame  %i from Init thread\n", __FUNCTION__, (int)frame_num);
        }

        svt_jpeg_xs_decoder_instance_t* dec_ctx = wrapper_ptr_decoder_ctx->object_ptr;
        dec_ctx->frame_num = frame_num++;
        dec_ctx->sync_output_frame_idx = sync_output_frame_idx;
        //Protect to overflow integer (frame_num) will not break output sync buffer.
        sync_output_frame_idx = (sync_output_frame_idx + 1) % dec_api_prv->sync_output_ringbuffer_size;
        dec_ctx->dec_input = input_buffer_ptr->dec_input;
        dec_ctx->dec_input.bitstream.ready_to_release = 0;
        dec_ctx->dec_input.image.ready_to_release = 0;

        svt_jxs_wait_cond_var(sync_output_ringbuffer_left, 0); //Wait until will be free place in ring buffer
        svt_jxs_add_cond_var(sync_output_ringbuffer_left, -1); //Decrement number of elements to use.

        uint32_t header_size = 0;
        SvtJxsErrorType_t ret = SvtJxsErrorNone;
        if (input_buffer_ptr->flags == SvtJxsDecoderEndOfCodestream) {
            ret = SvtJxsDecoderEndOfCodestream;
        }
        else {
            ret = svt_jpeg_xs_decode_header(dec_ctx,
                                            input_buffer_ptr->dec_input.bitstream.buffer,
                                            input_buffer_ptr->dec_input.bitstream.used_size,
                                            &header_size,
                                            dec_api_prv->verbose);
        }
        if (ret) {
            /*Error path when invalid parse header, send error to slice Thread to forward error information to final Thread.*/
            if (dec_api_prv->verbose >= VERBOSE_ERRORS) {
                if (input_buffer_ptr->flags == SvtJxsDecoderEndOfCodestream) {
                    if (dec_api_prv->verbose >= VERBOSE_INFO_MULTITHREADING) {
                        fprintf(stderr, "[%s] Send EOC frame: %i\n", __FUNCTION__, (int)dec_ctx->frame_num);
                    }
                }
                else {
                    fprintf(stderr, "[%s] Invalid Header frame: %i\n", __FUNCTION__, (int)dec_ctx->frame_num);
                }
            }
            ObjectWrapper_t* universal_wrapper_ptr = NULL;
            err = svt_jxs_get_empty_object(dec_api_prv->universal_producer_fifo_ptr, &universal_wrapper_ptr);
            if (err != SvtJxsErrorNone || universal_wrapper_ptr == NULL) {
                continue;
            }

            TaskCalculateFrame* buffer_output = (TaskCalculateFrame*)universal_wrapper_ptr->object_ptr;
            buffer_output->wrapper_ptr_decoder_ctx = wrapper_ptr_decoder_ctx;
            buffer_output->image_buffer = input_buffer_ptr->dec_input.image;
            /*Use wrapper output only to propagate error to Thread Final*/
            buffer_output->frame_error = ret;
            buffer_output->slice_id = 0;
            dec_ctx->sync_num_slices_to_receive = 1;
            svt_jxs_post_full_object(universal_wrapper_ptr);
        }
        else {
            send_slices_tasks(
                dec_api_prv, input_buffer_ptr, wrapper_ptr_decoder_ctx, &input_buffer_ptr->dec_input.image, header_size);
        }
        svt_jxs_release_object(input_wrapper_ptr);
        /*Callback available space in Input Queue*/
        if (callback_send) {
            callback_send(callback_decoder_ctx, callback_send_context);
        }
    }

    return NULL;
}

SvtJxsErrorType_t internal_svt_jpeg_xs_decoder_send_packet(svt_jpeg_xs_decoder_api_prv_t* dec_api_prv,
                                                           svt_jpeg_xs_frame_t* dec_input, uint32_t* bytes_used) {
    svt_jpeg_xs_slice_scheduler_ctx_t* slice_scheduler_ctx = &dec_api_prv->slice_scheduler_ctx;
    ObjectWrapper_t* wrapper_ptr_decoder_ctx = slice_scheduler_ctx->wrapper_ptr_decoder_ctx;

    //Get and initialize new frame context
    if (wrapper_ptr_decoder_ctx == NULL) {
        SvtJxsErrorType_t ret = svt_jxs_get_empty_object(dec_api_prv->internal_pool_decoder_instance_fifo_ptr,
                                                         &wrapper_ptr_decoder_ctx);
        if (ret != SvtJxsErrorNone || wrapper_ptr_decoder_ctx == NULL) {
            return ret;
        }
        slice_scheduler_ctx->wrapper_ptr_decoder_ctx = wrapper_ptr_decoder_ctx;

        svt_jpeg_xs_decoder_instance_t* dec_ctx = wrapper_ptr_decoder_ctx->object_ptr;

        dec_ctx->frame_num = slice_scheduler_ctx->frame_num;
        dec_ctx->sync_output_frame_idx = slice_scheduler_ctx->sync_output_frame_idx;
        dec_input->image.ready_to_release = 0;
        dec_ctx->dec_input.image = dec_input->image;

        memset(&dec_ctx->dec_input.bitstream, 0, sizeof(svt_jpeg_xs_bitstream_buffer_t));
        dec_ctx->dec_input.user_prv_ctx_ptr = dec_input->user_prv_ctx_ptr;

        //Protect to overflow integer (frame_num) will not break output sync buffer.
        slice_scheduler_ctx->sync_output_frame_idx = (slice_scheduler_ctx->sync_output_frame_idx + 1) %
            dec_api_prv->sync_output_ringbuffer_size;
        slice_scheduler_ctx->frame_num++;
        slice_scheduler_ctx->slices_sent = 0;
        slice_scheduler_ctx->header_size = 0;
        slice_scheduler_ctx->bytes_filled = 0;
        slice_scheduler_ctx->bytes_processed = 0;

        CondVar* sync_output_ringbuffer_left = &dec_api_prv->sync_output_ringbuffer_left;
        svt_jxs_wait_cond_var(sync_output_ringbuffer_left, 0); //Wait until there is a free place in the ring buffer.
        svt_jxs_add_cond_var(sync_output_ringbuffer_left, -1); //Decrement the number of elements to use.

        for (uint32_t slice_idx = 0; slice_idx < dec_ctx->dec_common->pi.slice_num; slice_idx++) {
            svt_jxs_set_cond_var(&dec_ctx->map_slices_decode_done[slice_idx], SYNC_INIT);
        }

        dec_ctx->sync_num_slices_to_receive = dec_ctx->dec_common->pi.slice_num;
        dec_ctx->sync_slices_idwt = (dec_ctx->dec_common->pi.decom_v != 0) && (dec_api_prv->universal_threads_num > 1) &&
            (dec_ctx->dec_common->pi.precincts_per_slice > 2) && (dec_ctx->dec_common->picture_header_const.hdr_Cpih == 0);
    }

    svt_jpeg_xs_decoder_instance_t* dec_ctx = wrapper_ptr_decoder_ctx->object_ptr;

    *bytes_used = MIN(dec_ctx->dec_common->max_frame_bitstream_size - slice_scheduler_ctx->bytes_filled,
                      dec_input->bitstream.used_size);

    assert((*bytes_used + slice_scheduler_ctx->bytes_filled) <= dec_ctx->dec_common->max_frame_bitstream_size);

    //Copy provided chunk of bitstream into internal frame-context
    memcpy(dec_ctx->frame_bitstream_ptr + slice_scheduler_ctx->bytes_filled, dec_input->bitstream.buffer, *bytes_used);
    slice_scheduler_ctx->bytes_filled += *bytes_used;
    dec_input->bitstream.ready_to_release = 1;
    dec_input->image.ready_to_release = 0;

    SvtJxsErrorType_t ret = SvtJxsErrorNone;

    //Process bitstream Header
    if (slice_scheduler_ctx->header_size == 0) {
        ret = svt_jpeg_xs_decode_header(dec_ctx,
                                        dec_ctx->frame_bitstream_ptr,
                                        slice_scheduler_ctx->bytes_filled,
                                        &slice_scheduler_ctx->header_size,
                                        dec_api_prv->verbose);
        slice_scheduler_ctx->bytes_processed += slice_scheduler_ctx->header_size;
    }

    //Process and schedule bitstream into slice-threads
    do {
        uint32_t slice_size = 0;
        if (ret == SvtJxsErrorNone) {
            ret = get_slice_size(&dec_ctx->dec_common->pi,
                                 dec_ctx->frame_bitstream_ptr + slice_scheduler_ctx->bytes_processed,
                                 slice_scheduler_ctx->bytes_filled - slice_scheduler_ctx->bytes_processed,
                                 slice_scheduler_ctx->slices_sent,
                                 &slice_size);
        }
        if (ret == SvtJxsErrorDecoderBitstreamTooShort) {
            //Not enough data to process slice,
            return SvtJxsErrorDecoderBitstreamTooShort;
        }

        ObjectWrapper_t* universal_wrapper_ptr = NULL;
        SvtJxsErrorType_t err = svt_jxs_get_empty_object(dec_api_prv->universal_producer_fifo_ptr, &universal_wrapper_ptr);
        if (err != SvtJxsErrorNone || universal_wrapper_ptr == NULL) {
            return err;
        }
        TaskCalculateFrame* buffer_output = (TaskCalculateFrame*)universal_wrapper_ptr->object_ptr;
        buffer_output->wrapper_ptr_decoder_ctx = wrapper_ptr_decoder_ctx;
        buffer_output->image_buffer = dec_ctx->dec_input.image;

        buffer_output->bitstream_buf = dec_ctx->frame_bitstream_ptr + slice_scheduler_ctx->bytes_processed;
        buffer_output->bitstream_buf_size = slice_size;
        buffer_output->slice_id = slice_scheduler_ctx->slices_sent;
        buffer_output->frame_error = ret;

        if (ret) {
            dec_ctx->sync_num_slices_to_receive = slice_scheduler_ctx->slices_sent + 1;
        }

        svt_jxs_post_full_object(universal_wrapper_ptr);

        slice_scheduler_ctx->slices_sent++;
        slice_scheduler_ctx->bytes_processed += slice_size;

        //If we get an error while processing slice or we processes all slices then we need to get new frame-context
        if (ret || slice_scheduler_ctx->slices_sent == dec_ctx->dec_common->pi.slice_num) {
            slice_scheduler_ctx->wrapper_ptr_decoder_ctx = NULL;
            break;
        }
    } while (1);

    return SvtJxsErrorNone;
}
