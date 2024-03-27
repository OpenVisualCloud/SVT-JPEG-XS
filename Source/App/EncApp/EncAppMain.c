/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

// main.cpp
//  -Constructs the following resources needed during the encoding process
//      -memory
//      -threads
//      -semaphores
//  -Configures the encoder
//  -Calls the encoder via the API
//  -Destructs the resources

/***************************************
 * Includes
 ***************************************/
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "EncAppConfig.h"
#include "SemaphoreApp.h"
#include "UtilityApp.h"

#ifndef TEST_STRIDE
#define TEST_STRIDE 0
#endif

#if TEST_STRIDE
uint32_t global_stride_add = 100;
#endif

static int32_t read_yuv_frame(FILE *in_file, svt_jpeg_xs_image_config_t *image_config, svt_jpeg_xs_image_buffer_t *yuv_buffer) {
    if (feof(in_file)) {
        return 1;
    }

#if TEST_STRIDE
    uint32_t pixel_size = image_config->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
#endif
    for (uint8_t c = 0; c < image_config->components_num; ++c) {
#if TEST_STRIDE
        uint32_t yuv_offset = 0;
        uint32_t width_stride = image_config->components[c].width;
        uint32_t width = width_stride - global_stride_add;
        uint32_t line_size_stride = width_stride * pixel_size;
        uint32_t line_size = width * pixel_size;
        uint32_t read_size = 0;
        for (uint32_t h = 0; h < image_config->components[c].height; ++h) {
            read_size += (uint32_t)fread(((uint8_t *)yuv_buffer->data_yuv[c]) + yuv_offset, 1, line_size, in_file);
            yuv_offset += line_size_stride;
        }
        if (image_config->components[c].height * line_size != read_size) {
            return 1;
        }
#else
        uint32_t read_size = (uint32_t)fread(yuv_buffer->data_yuv[c], 1, yuv_buffer->alloc_size[c], in_file);
        if (image_config->components[c].byte_size != read_size) {
            return 1;
        }
#endif
    }
    return 0;
}

static uint32_t get_single_frame_size(svt_jpeg_xs_image_config_t *image_config) {
    uint32_t size = 0;
    for (uint8_t c = 0; c < image_config->components_num; ++c) {
        size += image_config->components[c].byte_size;
    }
    return size;
}

static int32_t reset_yuv_file(FILE *in_file) {
    return fseek(in_file, 0, SEEK_SET);
}

static void show_encoding_progress(EncoderConfig_t *config, uint64_t encoded_frame_count) {
    switch (config->progress) {
    case 0:
        break;
    case 1:
        fprintf(stderr, "\b\b\b\b\b\b\b\b\b%9lu", (unsigned long)encoded_frame_count);
#ifdef FLAG_DEADLOCK_DETECT
        fprintf(stderr, "\n");
#endif
        break;
    default:
        break;
    }
    fflush(stderr);
}

static void *thread_send(void *arg) {
    EncoderConfig_t *config_enc = (EncoderConfig_t *)arg;

    double fps_interval_ms = 0;
    if (config_enc->limit_fps) {
        fps_interval_ms = 1000.0 / config_enc->limit_fps;
    }

    uint64_t thread_start_time[2];
    get_current_time(&thread_start_time[0], &thread_start_time[1]);

    uint64_t send_frames = 0;

    do {
        svt_jpeg_xs_frame_t enc_input;
        enc_input.user_prv_ctx_ptr = NULL;
        SvtJxsErrorType_t ret = svt_jpeg_xs_frame_pool_get(config_enc->frame_pool, &enc_input, /*blocking*/ 1);
        if (ret != SvtJxsErrorNone) {
            break;
        }

        if (read_yuv_frame(config_enc->in_file, &config_enc->image_config, &enc_input.image)) {
            //End of file reached or read less than expected, Repeat read file from begin

            int32_t read_ret = reset_yuv_file(config_enc->in_file);
            if (read_ret != 0) {
                fprintf(stderr, "Input invalid read file from begin! Error!!! \n");
                break;
            }
            read_ret = read_yuv_frame(config_enc->in_file, &config_enc->image_config, &enc_input.image);
            if (read_ret) {
                //file does not hold enough data for frame
                break;
            }
        }

        //Optional block of code to measure latency
        {
            user_private_data_t *user_data = malloc(sizeof(user_private_data_t));
            if (!user_data) {
                fprintf(stderr, "Failed to allocate performance context!!! \n");
            }
            else {
                if (config_enc->limit_fps) {
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
                enc_input.user_prv_ctx_ptr = user_data;
            }
        }

        ret = svt_jpeg_xs_encoder_send_picture(&config_enc->encoder, &enc_input, /*blocking*/ 1);
        if (ret != SvtJxsErrorNone) {
            break;
        }

        send_frames++;
    } while (send_frames < config_enc->frames_count);

    return NULL;
}

/***************************************
 * Encoder App Main
 ***************************************/
#define ENC_IGNORE_SOME_FRAMES (11)

int32_t main(int32_t argc, char *argv[]) {
    if (get_help(argc, argv)) {
        return 0;
    }

    // GLOBAL VARIABLES
    SvtJxsErrorType_t return_error = SvtJxsErrorNone; // Error Handling
    void *thread_send_handle = NULL;

    EncoderConfig_t config_enc;
    memset(&config_enc, 0, sizeof(EncoderConfig_t));
    config_enc.progress = 1;
    config_enc.frames_count = 0;

    PerformanceContext_t performance_context;
    performance_context.total_latency_ms = .0;
    performance_context.max_latency_ms = .0;
    performance_context.min_latency_ms = 1e30;
    performance_context.packet_total_latency_ms = .0;
    performance_context.packet_max_latency_ms = .0;
    performance_context.packet_min_latency_ms = 1e30;
    get_current_time(&performance_context.lib_start_time[0], &performance_context.lib_start_time[1]);

    ///********************** APPLICATION INIT  ******************///
    return_error = svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &config_enc.encoder);
    if (return_error != SvtJxsErrorNone) {
        fprintf(stderr, "Error while loading default parameters! ... \n");
        goto fail;
    }

    // Parse input
    return_error = read_command_line(argc, argv, &config_enc);
    if (return_error != SvtJxsErrorNone) {
        goto fail;
    }

    if (!config_enc.in_filename[0]) {
        fprintf(stderr, "Error: Not set Input source File\n");
        return_error = SvtJxsErrorBadParameter;
        goto fail;
    }

    FOPEN(config_enc.in_file, config_enc.in_filename, "rb");
    if (!config_enc.in_file) {
        fprintf(stderr, "Invalid input file: '%s'\n", config_enc.in_filename);
        return_error = SvtJxsErrorBadParameter;
        goto fail;
    }

    if (config_enc.out_filename[0]) {
        FOPEN(config_enc.out_file, config_enc.out_filename, "wb");
        if (!config_enc.out_file) {
            fprintf(stderr, "Invalid output file: '%s'\n", config_enc.out_filename);
            return_error = SvtJxsErrorBadParameter;
            goto fail;
        }
    }

    return_error = verify_settings(&config_enc);
    if (return_error != SvtJxsErrorNone) {
        fprintf(stderr, "Error in configuration, could not begin encoding! ... \n");
        fprintf(stderr, "Run %s --help for a list of options\n", argv[0]);
        // show_help();
        goto fail;
    }

    uint32_t bitstream_size = 0;
    return_error = svt_jpeg_xs_encoder_get_image_config(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &config_enc.encoder, &config_enc.image_config, &bitstream_size);
    if (return_error != SvtJxsErrorNone) {
        fprintf(stderr, "Error get image config! ... \n");
        goto fail;
    }

#if TEST_STRIDE
    uint32_t pixel_size = image_config.bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    for (uint8_t c = 0; c < image_config.components_num; ++c) {
        image_config.components[c].width += global_stride_add;
        image_config.components[c].byte_size += global_stride_add * image_config.components[c].height * pixel_size;
    }
#endif

    config_enc.frame_pool = svt_jpeg_xs_frame_pool_alloc(
        &config_enc.image_config, bitstream_size, 5); //Allocate 5 output YUV buffers
    if (!config_enc.frame_pool) {
        fprintf(stderr, "Invalid YUV and bitstream buffer pool allocation!\n");
        return_error = SvtJxsErrorInsufficientResources;
        goto fail;
    }

    //Initialize encoder and allocates memory to necessary buffers.
    return_error = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &config_enc.encoder);
    if (return_error != SvtJxsErrorNone) {
        fprintf(stderr, "Error init encoder! ... \n");
        goto fail;
    }

    //Calculate number of frames in file if user does not specify any
    if (config_enc.frames_count == 0) {
        int64_t file_size = get_file_size(config_enc.in_file);
        uint32_t single_frame_size = get_single_frame_size(&config_enc.image_config);
        config_enc.frames_count = (uint32_t)(file_size / single_frame_size);
    }

    get_current_time(&performance_context.processing_start_time[0], &performance_context.processing_start_time[1]);

    thread_send_handle = app_create_thread(thread_send, &config_enc);
    if (thread_send_handle == NULL) {
        goto fail;
    }

    uint64_t frames_received = 0;
    uint32_t packets_received = 0;
    double packet_total_time = 0;
    do {
        svt_jpeg_xs_frame_t enc_output;
        SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_get_packet(&config_enc.encoder, &enc_output, /*blocking*/ 1);
        packets_received++;
        //Optional block of code to measure latency
        if (enc_output.user_prv_ctx_ptr) {
            user_private_data_t *prv_data_ = (user_private_data_t *)enc_output.user_prv_ctx_ptr;
            assert(prv_data_->frame_num == frames_received);
            uint64_t frame_end_time[2];
            get_current_time(&frame_end_time[0], &frame_end_time[1]);

            double packet_encode_time_ms = compute_elapsed_time_in_ms(
                prv_data_->frame_start_time[0], prv_data_->frame_start_time[1], frame_end_time[0], frame_end_time[1]);

            packet_total_time += packet_encode_time_ms;

            if (enc_output.bitstream.last_packet_in_frame) {
                packet_total_time /= packets_received;
                performance_context.packet_total_latency_ms += packet_total_time;
                if (packet_total_time > performance_context.packet_max_latency_ms) {
                    performance_context.packet_max_latency_ms = packet_total_time;
                }
                if (packet_total_time < performance_context.packet_min_latency_ms) {
                    performance_context.packet_min_latency_ms = packet_total_time;
                }
                packet_total_time = 0;

                if (packet_encode_time_ms > performance_context.max_latency_ms) {
                    performance_context.max_latency_ms = packet_encode_time_ms;
                }
                if (packet_encode_time_ms < performance_context.min_latency_ms) {
                    performance_context.min_latency_ms = packet_encode_time_ms;
                }
                performance_context.total_latency_ms += packet_encode_time_ms;
                free(prv_data_);
            }
        }

        if (enc_output.bitstream.last_packet_in_frame) {
            frames_received++;
            packets_received = 0;
        }

        if (ret == SvtJxsErrorMax) {
            fprintf(stderr, "---------Error output queue!!!---------\n");
            return_error = ret;
            goto fail;
        }
        if (ret == SvtJxsErrorNone) {
            if (config_enc.out_file) {
                fwrite(enc_output.bitstream.buffer, 1, enc_output.bitstream.used_size, config_enc.out_file);
            }
        }
        else {
            fprintf(stderr, "---------Error encode frame %lu ---------\n", (unsigned long)frames_received);
            return_error = ENC_IGNORE_SOME_FRAMES;
        }

        svt_jpeg_xs_frame_pool_release(config_enc.frame_pool, &enc_output);
        // Dump progress
        show_encoding_progress(&config_enc, frames_received);

    } while (frames_received < config_enc.frames_count);

    uint64_t encode_end_time[2]; // [sec, micro_sec] first frame sent
    get_current_time(&encode_end_time[0], &encode_end_time[1]);
    performance_context.total_process_time = compute_elapsed_time_in_ms(performance_context.processing_start_time[0],
                                                                        performance_context.processing_start_time[1],
                                                                        encode_end_time[0],
                                                                        encode_end_time[1]);

    performance_context.total_execution_time = compute_elapsed_time_in_ms(
        performance_context.lib_start_time[0], performance_context.lib_start_time[1], encode_end_time[0], encode_end_time[1]);
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
            "Packet Latency: min %.2f[ms], max %.2f[ms], average %.2f[ms]\n",
            performance_context.packet_min_latency_ms,
            performance_context.packet_max_latency_ms,
            performance_context.packet_total_latency_ms / frames_received);

    fprintf(stderr,
            "Time: total encoding %.2f[s], total execution %.2f[s]\n",
            performance_context.total_process_time / 1000,
            performance_context.total_execution_time / 1000);

    fflush(stderr);

fail:
    if (thread_send_handle) {
        app_destroy_thread(thread_send_handle);
    }
    svt_jpeg_xs_frame_pool_free(config_enc.frame_pool);
    svt_jpeg_xs_encoder_close(&config_enc.encoder);

    // Close any files that are open
    if (config_enc.in_file) {
        fclose(config_enc.in_file);
        config_enc.in_file = NULL;
    }

    if (config_enc.out_file) {
        fclose(config_enc.out_file);
        config_enc.out_file = NULL;
    }

    return return_error;
}
