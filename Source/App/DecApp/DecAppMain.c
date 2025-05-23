/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/***************************************
 * Includes
 ***************************************/
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>
#include "DecParamParser.h"
#include "SemaphoreApp.h"
#include "UtilityApp.h"

#ifndef TEST_STRIDE
#define TEST_STRIDE 0
#endif

#if TEST_STRIDE
uint32_t global_stride_add = 100;
#endif

int32_t write_frame(svt_jpeg_xs_image_config_t* image_config, svt_jpeg_xs_image_buffer_t* image_buffer, FILE* f_out) {
#if TEST_STRIDE
    uint32_t pixel_size = image_config->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
#endif

    for (int32_t c = 0; c < image_config->components_num; c++) {
#if TEST_STRIDE
        uint32_t yuv_offset = 0;
        uint32_t width_stride = image_config->components[c].width;
        uint32_t width = width_stride - global_stride_add;
        uint32_t line_size_stride = width_stride * pixel_size;
        uint32_t line_size = width * pixel_size;
        uint32_t write_size = 0;
        for (uint32_t h = 0; h < image_config->components[c].height; ++h) {
            write_size += (uint32_t)fwrite(((uint8_t*)image_buffer->data_yuv[c]) + yuv_offset, 1, line_size, f_out);
            yuv_offset += line_size_stride;
        }
        if (image_config->components[c].height * line_size != write_size) {
            fprintf(stderr, "error while writing to file!\n");
            return 1;
        }
#else
        uint32_t byte_size = image_config->components[c].byte_size;
        size_t ret = fwrite(image_buffer->data_yuv[c], 1, byte_size, f_out);
        if (ret != byte_size) {
            fprintf(stderr, "error while writing to file!\n");
            return -1;
        }
#endif
    }
    return 0;
}

SvtJxsErrorType_t read_data_from_file(FILE* f, uint8_t* buf, size_t size) {
    if (f == NULL) {
        return SvtJxsErrorUndefined;
    }
    size_t read_size = fread(buf, 1, size, f);
    if (read_size != size) {
        fprintf(stderr, "Error while reading file, read=%zu bytes, expected=%zu bytes\n", read_size, size);
        return SvtJxsErrorUndefined;
    }
    return SvtJxsErrorNone;
}

#define DEC_OK                 (0)
#define DEC_IGNORE_SOME_FRAMES (1)
#define DEC_INVALID_BITSTREAM  (2)
#define DEC_CAN_NOT_DECODE     (3)
#define DEC_INVALID_PARAMETER  (4)

static void* thread_send(void* arg) {
    DecoderConfig_t* config_dec = (DecoderConfig_t*)arg;

    double fps_interval_ms = 0;
    if (config_dec->limit_fps) {
        fps_interval_ms = 1000.0 / config_dec->limit_fps;
    }

    uint8_t* bitstream_ptr = config_dec->bitstream_buf_ref + config_dec->bitstream_offset;
    uint64_t bitstream_size = config_dec->bitstream_buf_size - config_dec->bitstream_offset;

    uint64_t thread_start_time[2];
    get_current_time(&thread_start_time[0], &thread_start_time[1]);

    uint8_t file_end = 0;
    uint64_t send_frames = 0;

    do {
        uint32_t frame_size = 0;
        SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_get_single_frame_size(bitstream_ptr, bitstream_size, NULL, &frame_size, 1, config_dec->decoder.proxy_mode);
        if (ret != SvtJxsErrorNone) {
            break;
        }
        if (frame_size > bitstream_size) {
            fprintf(stderr, "Last frame in file is invalid!!! \n");
            break;
        }

        svt_jpeg_xs_bitstream_buffer_t bitstream;
        bitstream.buffer = bitstream_ptr;
        bitstream.used_size = frame_size;
        bitstream.allocation_size = frame_size;

        if (frame_size == bitstream_size) {
            file_end = 1;
            bitstream_ptr = config_dec->bitstream_buf_ref + config_dec->bitstream_offset;
            bitstream_size = config_dec->bitstream_buf_size - config_dec->bitstream_offset;
        }
        else {
            bitstream_ptr += frame_size;
            bitstream_size -= frame_size;
        }

        svt_jpeg_xs_frame_t dec_input;
        dec_input.user_prv_ctx_ptr = NULL;
        ret = svt_jpeg_xs_frame_pool_get(config_dec->frame_pool, &dec_input, /*blocking*/ 1);
        if (ret != SvtJxsErrorNone) {
            break;
        }
        dec_input.bitstream = bitstream;

        //Optional block of code to measure latency
        {
            user_private_data_t* user_data = malloc(sizeof(user_private_data_t));
            if (!user_data) {
                fprintf(stderr, "Failed to allocate performance context!!! \n");
                break;
            }

            if (config_dec->limit_fps) {
                uint64_t current_time_s, current_time_ms;
                get_current_time(&current_time_s, &current_time_ms);
                const double elapsed_time_ms = compute_elapsed_time_in_ms(
                    thread_start_time[0], thread_start_time[1], current_time_s, current_time_ms);
                const double predicted_time_ms = send_frames * fps_interval_ms;
                const int32_t time_to_send_ms = (int32_t)(predicted_time_ms - elapsed_time_ms);

                if (time_to_send_ms > 0) {
                    sleep_in_ms(time_to_send_ms);
                }
            }

            user_data->frame_num = send_frames;
            get_current_time(&user_data->frame_start_time[0], &user_data->frame_start_time[1]);
            dec_input.user_prv_ctx_ptr = user_data;
        }

        ret = svt_jpeg_xs_decoder_send_frame(&config_dec->decoder, &dec_input, /*blocking*/ 1);
        if (ret != SvtJxsErrorNone) {
            break;
        }
        send_frames++;
    } while ((config_dec->frames_count == 0 && !file_end) || send_frames < config_dec->frames_count);

    svt_jpeg_xs_decoder_send_eoc(&config_dec->decoder);

    return NULL;
}

#define PACKET_SIZE_BYTES 10000

static void* thread_send_packet(void* arg) {
    DecoderConfig_t* config_dec = (DecoderConfig_t*)arg;

    double fps_interval_ms = 0;
    if (config_dec->limit_fps) {
        fps_interval_ms = 1000.0 / config_dec->limit_fps;
    }

    uint8_t* bitstream_ptr = config_dec->bitstream_buf_ref + config_dec->bitstream_offset;
    uint64_t bitstream_size = config_dec->bitstream_buf_size - config_dec->bitstream_offset;

    uint64_t thread_start_time[2];
    get_current_time(&thread_start_time[0], &thread_start_time[1]);

    uint8_t file_end = 0;
    uint64_t send_frames = 0;

    do {
        svt_jpeg_xs_frame_t dec_input;
        dec_input.user_prv_ctx_ptr = NULL;
        SvtJxsErrorType_t ret = svt_jpeg_xs_frame_pool_get(config_dec->frame_pool, &dec_input, /*blocking*/ 1);
        if (ret != SvtJxsErrorNone) {
            break;
        }

        //Optional block of code to measure latency
        {
            user_private_data_t* user_data = malloc(sizeof(user_private_data_t));
            if (!user_data) {
                fprintf(stderr, "Failed to allocate performance context!!! \n");
                break;
            }

            if (config_dec->limit_fps) {
                uint64_t current_time_s, current_time_ms;
                get_current_time(&current_time_s, &current_time_ms);
                const double elapsed_time_ms = compute_elapsed_time_in_ms(
                    thread_start_time[0], thread_start_time[1], current_time_s, current_time_ms);
                const double predicted_time_ms = send_frames * fps_interval_ms;
                const int32_t time_to_send_ms = (int32_t)(predicted_time_ms - elapsed_time_ms);

                if (time_to_send_ms > 0) {
                    sleep_in_ms(time_to_send_ms);
                }
            }

            user_data->frame_num = send_frames;
            get_current_time(&user_data->frame_start_time[0], &user_data->frame_start_time[1]);
            dec_input.user_prv_ctx_ptr = user_data;
        }

        do {
            svt_jpeg_xs_bitstream_buffer_t bitstream;
            memset(&bitstream, 0, sizeof(svt_jpeg_xs_bitstream_buffer_t));
            bitstream.buffer = bitstream_ptr;
            bitstream.used_size = PACKET_SIZE_BYTES > bitstream_size ? bitstream_size : PACKET_SIZE_BYTES;
            bitstream.allocation_size = bitstream.used_size;

            dec_input.bitstream = bitstream;

            uint32_t bytes_used = 0;
            ret = svt_jpeg_xs_decoder_send_packet(&config_dec->decoder, &dec_input, &bytes_used);

            if (bytes_used == bitstream_size) {
                file_end = 1;
                bitstream_ptr = config_dec->bitstream_buf_ref + config_dec->bitstream_offset;
                bitstream_size = config_dec->bitstream_buf_size - config_dec->bitstream_offset;
            }
            else {
                bitstream_ptr += bytes_used;
                bitstream_size -= bytes_used;
            }
        } while (ret == SvtJxsErrorDecoderBitstreamTooShort);

        send_frames++;
    } while ((config_dec->frames_count == 0 && !file_end) || send_frames < config_dec->frames_count);

    svt_jpeg_xs_decoder_send_eoc(&config_dec->decoder);

    return NULL;
}

/***************************************
 * Decoder App Main
 ***************************************/
int32_t main(int32_t argc, char* argv[]) {
    if (get_help(argc, argv)) {
        return 0;
    }

    // GLOBAL VARIABLES
    SvtJxsErrorType_t return_error = SvtJxsErrorNone; // Error Handling
    void* thread_send_handle = NULL;

    // Initialize config
    DecoderConfig_t config_dec;
    memset(&config_dec, 0, sizeof(DecoderConfig_t));
    config_dec.decoder.threads_num = 0;
    config_dec.decoder.verbose = VERBOSE_SYSTEM_INFO;
    config_dec.decoder.use_cpu_flags = CPU_FLAGS_ALL;
    config_dec.decoder.packetization_mode = 0;
    config_dec.decoder.proxy_mode = 0;

    PerformanceContext_t performance_context;
    performance_context.total_latency_ms = .0;
    performance_context.max_latency_ms = .0;
    performance_context.min_latency_ms = 1e30;
    get_current_time(&performance_context.lib_start_time[0], &performance_context.lib_start_time[1]);

    if (read_command_line(argc, argv, &config_dec) != 0) {
        fprintf(stderr, "Error in configuration, could not begin decoding! ... \n");
        fprintf(stderr, "Run %s --help for a list of options\n", argv[0]);
        return_error = DEC_INVALID_PARAMETER;
        goto fail;
    }

    if (!config_dec.in_filename[0]) {
        fprintf(stderr, "Input file not specified. \n");
        show_help();
        return_error = DEC_INVALID_PARAMETER;
        goto fail;
    }

    FOPEN(config_dec.in_file, config_dec.in_filename, "rb");
    if (!config_dec.in_file) {
        fprintf(stderr, "Invalid input file \n");
        return_error = DEC_INVALID_PARAMETER;
        goto fail;
    }

    if (config_dec.out_filename[0]) {
        FOPEN(config_dec.out_file, config_dec.out_filename, "wb");
        if (!config_dec.out_file) {
            fprintf(stderr, "Invalid output file \n");
            return_error = DEC_INVALID_PARAMETER;
            goto fail;
        }
    }

    if (config_dec.limit_fps > 240) {
        fprintf(stderr, "Error The fps limit is 240 fps \n");
        goto fail;
    }

    config_dec.bitstream_buf_size = get_file_size(config_dec.in_file);
    if (config_dec.bitstream_buf_size <= 0) {
        fprintf(stderr, "Unable to open file %s\n", config_dec.in_filename);
        return_error = DEC_INVALID_BITSTREAM;
        goto fail;
    }

    config_dec.bitstream_buf_ref = (uint8_t*)malloc(config_dec.bitstream_buf_size);
    if (!config_dec.bitstream_buf_ref) {
        fprintf(stderr, "Unable allocate memory for read file\n");
        return_error = DEC_CAN_NOT_DECODE;
        goto fail;
    }

    SvtJxsErrorType_t ret = read_data_from_file(config_dec.in_file, config_dec.bitstream_buf_ref, config_dec.bitstream_buf_size);
    if (ret) {
        fprintf(stderr, "Unable to read file %s\n", config_dec.in_filename);
        return_error = DEC_INVALID_BITSTREAM;
        goto fail;
    }

    if (config_dec.autodetect_bitstream_header) {
        int find = 0;
        for (size_t i = 0; i < config_dec.bitstream_buf_size - 4; ++i) {
            if ((config_dec.bitstream_buf_ref[i] == 0xff) && (config_dec.bitstream_buf_ref[i + 1] == 0x10) &&
                (config_dec.bitstream_buf_ref[i + 2] == 0xff) && (config_dec.bitstream_buf_ref[i + 3] == 0x50)) {
                config_dec.bitstream_offset = i;
                find = 1;
                fprintf(stderr, "Detect Bitstream header offset: %zu\n", i);
                break;
            }
        }
        if (!find) {
            fprintf(stderr, "Detect Bitstream header FAILED!\n");
            return_error = DEC_CAN_NOT_DECODE;
            goto fail;
        }
    }

    /*First frame consistency test.*/
    uint32_t frame_size = 0;
    ret = svt_jpeg_xs_decoder_get_single_frame_size(config_dec.bitstream_buf_ref + config_dec.bitstream_offset,
                                                    config_dec.bitstream_buf_size - config_dec.bitstream_offset,
                                                    &config_dec.image_config,
                                                    &frame_size,
                                                    1,
                                                    config_dec.decoder.proxy_mode);
    if (ret != SvtJxsErrorNone) {
        fprintf(stderr, "Unable to get first frame size, input codestream (%s)\n", config_dec.in_filename);
        return_error = DEC_INVALID_BITSTREAM;
        if (config_dec.force_decode) {
            fprintf(stderr, "Force decode first frame from invalid codestream (%s)!!!\n", config_dec.in_filename);
        }
        else {
            goto fail;
        }
    }

#if TEST_STRIDE
    uint32_t pixel_size = config_dec.image_config.bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    for (uint8_t c = 0; c < config_dec.image_config.components_num; ++c) {
        config_dec.image_config.components[c].width += global_stride_add;
        config_dec.image_config.components[c].byte_size += global_stride_add * config_dec.image_config.components[c].height *
            pixel_size;
    }
#endif

    config_dec.frame_pool = svt_jpeg_xs_frame_pool_alloc(&config_dec.image_config, 0, 5); //Allocate 5 output YUV buffers
    if (!config_dec.frame_pool) {
        fprintf(stderr, "Invalid YUV buffers allocation!\n");
        return_error = DEC_INVALID_BITSTREAM;
        goto fail;
    }

    svt_jpeg_xs_image_config_t image_config_init = {0};
    ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                   SVT_JPEGXS_API_VER_MINOR,
                                   &config_dec.decoder,
                                   config_dec.bitstream_buf_ref + config_dec.bitstream_offset,
                                   config_dec.bitstream_buf_size - config_dec.bitstream_offset,
                                   &image_config_init);
    if (ret == SvtJxsErrorBadParameter && config_dec.decoder.packetization_mode == 1) {
        fprintf(stderr, "Decoder init failed, trying with packetization_mode=0 \n");
        config_dec.decoder.packetization_mode = 0;
        ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                       SVT_JPEGXS_API_VER_MINOR,
                                       &config_dec.decoder,
                                       config_dec.bitstream_buf_ref + config_dec.bitstream_offset,
                                       config_dec.bitstream_buf_size - config_dec.bitstream_offset,
                                       &image_config_init);
    }
    if (ret) {
        fprintf(stderr, "Unable to parse input codestream (%s)\n", config_dec.in_filename);
        return_error = DEC_INVALID_BITSTREAM;
        goto fail;
    }

#if !TEST_STRIDE
    /*Test if functions svt_jpeg_xs_decoder_get_single_frame_size and svt_jpeg_xs_decoder_init
     *return the same image config
     */
    if (memcmp(&image_config_init, &config_dec.image_config, sizeof(config_dec.image_config))) {
        fprintf(stderr,
                "Decoder svt_jpeg_xs_decoder_init() return different image_config than "
                "svt_jpeg_xs_decoder_get_single_frame_size()!!\n");
        return_error = DEC_INVALID_BITSTREAM;
        goto fail;
    }
#endif

    if (config_dec.decoder.verbose >= VERBOSE_WARNINGS) {
        fprintf(stderr, "Start Main loop frames_count %i\n", config_dec.frames_count);
    }

    get_current_time(&performance_context.processing_start_time[0], &performance_context.processing_start_time[1]);

    if (config_dec.decoder.packetization_mode) {
        thread_send_handle = app_create_thread(thread_send_packet, &config_dec);
    }
    else {
        thread_send_handle = app_create_thread(thread_send, &config_dec);
    }
    if (thread_send_handle == NULL) {
        goto fail;
    }

    uint64_t frames_received = 0;
    do {
        svt_jpeg_xs_frame_t dec_output;
        SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_get_frame(&config_dec.decoder, &dec_output, 1 /*blocking*/);
        if (ret == SvtJxsDecoderEndOfCodestream) {
            break;
        }
        //Optional block of code to measure latency
        if (dec_output.user_prv_ctx_ptr) {
            user_private_data_t* prv_data_ = (user_private_data_t*)dec_output.user_prv_ctx_ptr;
            assert(prv_data_->frame_num == frames_received);
            uint64_t frame_end_time[2];
            get_current_time(&frame_end_time[0], &frame_end_time[1]);

            double frame_encode_time_ms = compute_elapsed_time_in_ms(
                prv_data_->frame_start_time[0], prv_data_->frame_start_time[1], frame_end_time[0], frame_end_time[1]);

            if (frame_encode_time_ms > performance_context.max_latency_ms) {
                performance_context.max_latency_ms = frame_encode_time_ms;
            }
            if (frame_encode_time_ms < performance_context.min_latency_ms) {
                performance_context.min_latency_ms = frame_encode_time_ms;
            }
            performance_context.total_latency_ms += frame_encode_time_ms;
            free(prv_data_);
        }

        frames_received++;
        if (ret == SvtJxsErrorNone) {
            if (config_dec.out_file != NULL) {
                write_frame(&config_dec.image_config, &dec_output.image, config_dec.out_file);
            }
            if (config_dec.decoder.verbose >= VERBOSE_INFO_MULTITHREADING) {
                fprintf(stderr, "Release frame received_frame %lu\n", (unsigned long)frames_received);
            }
            fprintf(stderr, "\b\b\b\b\b\b\b\b\b%9lu", (unsigned long)frames_received);
            fflush(stderr);
        }
        else { //(ret < 0)
            if (ret == SvtJxsErrorDecoderConfigChange) {
                fprintf(stderr,
                        "Decoder error frame %lu: SvtJxsErrorDecoderConfigChange Try continue next frames...\n",
                        (unsigned long)frames_received);
            }
            else {
                fprintf(stderr,
                        "Decoder error frame %lu: Error: %i Try continue next frames...\n",
                        (unsigned long)frames_received,
                        ret);
            }
            return_error = DEC_IGNORE_SOME_FRAMES;
        }
        svt_jpeg_xs_frame_pool_release(config_dec.frame_pool, &dec_output);

    } while (1);

    uint64_t decode_end_time[2]; // [sec, micro_sec] first frame sent
    get_current_time(&decode_end_time[0], &decode_end_time[1]);
    performance_context.total_process_time = compute_elapsed_time_in_ms(performance_context.processing_start_time[0],
                                                                        performance_context.processing_start_time[1],
                                                                        decode_end_time[0],
                                                                        decode_end_time[1]);
    performance_context.total_execution_time = compute_elapsed_time_in_ms(
        performance_context.lib_start_time[0], performance_context.lib_start_time[1], decode_end_time[0], decode_end_time[1]);

    fprintf(stderr,
            "\nFinish %lu frames, average %.2f[fps]\n",
            (unsigned long)frames_received,
            (double)frames_received * 1000 / performance_context.total_process_time);

    fprintf(stderr,
            "Latency: min %.2f[ms], max %.2f[ms], average %.2f[ms]\n",
            performance_context.min_latency_ms,
            performance_context.max_latency_ms,
            performance_context.total_latency_ms / frames_received);

    fprintf(stderr,
            "Time: total decoding %.2f[s], total execution %.2f[s]\n",
            performance_context.total_process_time / 1000,
            performance_context.total_execution_time / 1000);

    fflush(stderr);

fail:
    if (thread_send_handle) {
        app_destroy_thread(thread_send_handle);
    }
    svt_jpeg_xs_frame_pool_free(config_dec.frame_pool);
    svt_jpeg_xs_decoder_close(&config_dec.decoder);

    if (config_dec.in_file) {
        fclose(config_dec.in_file);
        config_dec.in_file = NULL;
    }

    if (config_dec.out_file) {
        fclose(config_dec.out_file);
        config_dec.out_file = NULL;
    }

    if (config_dec.bitstream_buf_ref) {
        free(config_dec.bitstream_buf_ref);
    }

    return return_error;
}
