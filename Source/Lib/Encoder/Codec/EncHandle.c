/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "EncHandle.h"
#include "DwtInput.h"
#include "DwtStageProcess.h"
#include "FinalStageProcess.h"
#include "InitStageProcess.h"
#include "PackOut.h"
#include "PackStageProcess.h"
#include "WeightTable.h"
#include "encoder_dsp_rtcd.h"
#include "SvtLog.h"
#include "Codestream.h"
#include "EncDec.h"

/**********************************
 * Encoder Library Handle Deconstructor
 **********************************/
static void svt_enc_handle_dctor(void_ptr p) {
    svt_jpeg_xs_encoder_api_prv_t* enc_api_prv = (svt_jpeg_xs_encoder_api_prv_t*)p;
    svt_jpeg_xs_encoder_common_t* enc_common = &enc_api_prv->enc_common;

    // Init Stage
    SVT_DESTROY_THREAD(enc_api_prv->init_stage_thread_handle);

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        // dwt Stage
        SVT_DESTROY_THREAD_ARRAY(enc_api_prv->dwt_stage_thread_handle_array, enc_api_prv->dwt_stage_threads_num);
    }
    // pack Stage
    SVT_DESTROY_THREAD_ARRAY(enc_api_prv->pack_stage_thread_handle_array, enc_api_prv->pack_stage_threads_num);
    // final Stage
    SVT_DESTROY_THREAD(enc_api_prv->final_stage_thread_handle);

    SVT_DELETE(enc_api_prv->picture_control_set_pool_ptr);
    SVT_DELETE(enc_api_prv->input_image_resource_ptr);
    SVT_DELETE(enc_api_prv->output_queue_resource_ptr);
    SVT_DELETE(enc_api_prv->pack_input_resource_ptr);
    SVT_DELETE(enc_api_prv->pack_output_resource_ptr);
    SVT_DELETE(enc_api_prv->init_stage_context_ptr);
    SVT_DELETE(enc_api_prv->final_stage_context_ptr);
    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        SVT_DELETE(enc_api_prv->dwt_input_resource_ptr);
        SVT_DELETE_PTR_ARRAY(enc_api_prv->dwt_stage_context_ptr_array, enc_api_prv->dwt_stage_threads_num);
    }
    if (enc_api_prv->enc_common.slice_sizes) {
        SVT_FREE(enc_api_prv->enc_common.slice_sizes);
    }
    SVT_DELETE_PTR_ARRAY(enc_api_prv->pack_stage_context_ptr_array, enc_api_prv->pack_stage_threads_num);
    SVT_FREE(enc_api_prv->sync_output_ringbuffer);
    svt_jxs_free_cond_var(&enc_api_prv->sync_output_ringbuffer_left);
}

/**********************************
 * Encoder Library Handle Constructor
 **********************************/
static SvtJxsErrorType_t svt_enc_handle_ctor(svt_jpeg_xs_encoder_api_prv_t* enc_api_prv) {
    enc_api_prv->dctor = svt_enc_handle_dctor;
    return SvtJxsErrorNone;
}

static void print_lib_params(svt_jpeg_xs_encoder_api_t* enc_api) {
    if (enc_api == NULL || enc_api->private_ptr == NULL) {
        return;
    }
    svt_jpeg_xs_encoder_api_prv_t* enc_api_prv = (svt_jpeg_xs_encoder_api_prv_t*)enc_api->private_ptr;
    svt_jpeg_xs_encoder_common_t* enc_common = &enc_api_prv->enc_common;

    const char* color_format_name = svt_jpeg_xs_get_format_name(enc_common->colour_format);

    SVT_LOG("------------------------------------------- ");
    SVT_LOG("\nSVT [config]: Resolution [width x height]         \t: %d x %d", enc_common->pi.width, enc_common->pi.height);
    SVT_LOG("\nSVT [config]: EncoderBitDepth / EncoderColorFormat\t: %d / %s", enc_common->bit_depth, color_format_name);
    SVT_LOG("\nSVT [config]: Slice height                        \t: %d", enc_common->pi.slice_height);

    const char* coding_signs_names[3] = {"Disabled", "Enabled Fast", "Enabled Full"};
    const char* coding_significance_names[2] = {"Disabled", "Enabled"};
    const char* coding_v_ped_names[3] = {"Disabled", "Predict from zero", "Predict full"};
    const char* quantization_names[2] = {"Deadzone", "Uniform"};
    const char* rc_names[RC_MODE_SIZE] = {
        "CBR per precinct", "CBR per precinct, move padding", "CBR per slice", "CBR per slice max RATE"};

    SVT_LOG("\nSVT [config]: Rate Control                        \t: %u:%s",
            enc_common->rate_control_mode,
            rc_names[enc_common->rate_control_mode]);
    SVT_LOG("\nSVT [config]: Coding Type: Significance           \t: %s",
            coding_significance_names[enc_common->coding_significance]);
    SVT_LOG("\nSVT [config]: Coding Type: Vertical Prediction    \t: %s",
            coding_v_ped_names[enc_common->coding_vertical_prediction_mode]);
    SVT_LOG("\nSVT [config]: Coding Type: Signs handling         \t: %s", coding_signs_names[enc_common->coding_signs_handling]);
    SVT_LOG("\nSVT [config]: Vertical / Horizontal               \t: %d / %d", enc_common->pi.decom_v, enc_common->pi.decom_h);
    SVT_LOG("\nSVT [config]: Quantization method                 \t: %s",
            quantization_names[enc_common->picture_header_dynamic.hdr_Qpih]);

    SVT_LOG("\nSVT [config]: BPP / Compression Ratio             \t: ");
    if (enc_api->bpp_denominator == 1) {
        SVT_LOG("%d", enc_api->bpp_numerator);
    }
    else {
        SVT_LOG("%.2f", (float)enc_api->bpp_numerator / enc_api->bpp_denominator);
    }
    SVT_LOG(" / %.2f", enc_common->compression_rate);

    if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
        SVT_LOG("\nSVT [config]: Profile Latency, Slice Threads      \t: %d", enc_api_prv->pack_stage_threads_num);
        assert(enc_api_prv->dwt_stage_threads_num == 0);
    }
    else {
        SVT_LOG("\nSVT [config]: Profile CPU, Slice Threads / DWT Threads \t: %d / %d",
                enc_api_prv->pack_stage_threads_num,
                enc_api_prv->dwt_stage_threads_num);
    }
    SVT_LOG("\n");

    fflush(stdout);
}

#define WAVELET_IN_DEPTH_BW_DEFAULT      20 //TODO: Move this somewhere else
#define WAVELET_FRACTION_BITS_FQ_DEFAULT 8

static SvtJxsErrorType_t encoder_init_configuration(svt_jpeg_xs_encoder_common_t* enc_common,
                                                    svt_jpeg_xs_encoder_api_t* config_struct) {
    uint32_t sx[MAX_COMPONENTS_NUM];
    uint32_t sy[MAX_COMPONENTS_NUM];
    uint32_t num_comp = 0;

    /*Validation of encoder parameters:*/
    enc_common->bit_depth = config_struct->input_bit_depth;
    if (enc_common->bit_depth < 8 || enc_common->bit_depth > 14) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Incorrect bit_depth, expected: 8 to 14,  provided: %d\n", enc_common->bit_depth);
        }
        return SvtJxsErrorBadParameter;
    }
    enc_common->colour_format = config_struct->colour_format;
    SvtJxsErrorType_t return_error = format_get_sampling_factory(
        enc_common->colour_format, &num_comp, sx, sy, config_struct->verbose);
    if (return_error) {
        return return_error;
    }

    if (config_struct->ndecomp_v == 0 && config_struct->colour_format == COLOUR_FORMAT_PLANAR_YUV420) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Error: The input format YUV420 requires vertical decomposition level 1 or 2, provided %d\n",
                    config_struct->ndecomp_v);
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->slice_height == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: slice_height cannot be 0\n");
        }
        return SvtJxsErrorBadParameter;
    }
    if (config_struct->slice_height >= config_struct->source_height) {
        if (config_struct->verbose >= VERBOSE_SYSTEM_INFO) {
            fprintf(stderr, "Warning: slice_height is limited to source_height %d\n", config_struct->source_height);
        }
        config_struct->slice_height = config_struct->source_height;
    }
    else if (config_struct->slice_height % ((uint32_t)1 << config_struct->ndecomp_v)) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: slice_height have to be multiple of 2^(decomp_v)\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->quantization != QUANT_TYPE_DEADZONE && config_struct->quantization != QUANT_TYPE_UNIFORM) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unrecognized quantization method provided, expected 0 or 1\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->bpp_denominator == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "bpp_denominator cannot be 0!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->bpp_numerator == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "bpp_numerator cannot be 0!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    uint64_t bytes_per_frame = ((uint64_t)config_struct->source_width * config_struct->source_height *
                                    config_struct->bpp_numerator / config_struct->bpp_denominator +
                                7) /
        8;

    if (bytes_per_frame >= (((uint64_t)1) << 32)) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Impossible compression. Please use smaller bpp param!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (bytes_per_frame == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Impossible compression. Please use bigger bpp param!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    // Rate Control
    enc_common->rate_control_mode = config_struct->rate_control_mode;
    enc_common->picture_header_dynamic.hdr_Bw = WAVELET_IN_DEPTH_BW_DEFAULT;
    enc_common->picture_header_dynamic.hdr_Fq = WAVELET_FRACTION_BITS_FQ_DEFAULT;
    enc_common->coding_significance = config_struct->coding_significance;
    enc_common->cpu_profile = config_struct->cpu_profile;
    if (enc_common->cpu_profile != CPU_PROFILE_LOW_LATENCY && enc_common->cpu_profile != CPU_PROFILE_CPU) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unrecognized cpu_profile provided!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (enc_common->rate_control_mode >= RC_MODE_SIZE) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unrecognized rc mode provided!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->ndecomp_v == 0 && enc_common->cpu_profile != CPU_PROFILE_LOW_LATENCY) {
        //For test comment but when V is zero then not have sense to run LOW CPU
        enc_common->cpu_profile = CPU_PROFILE_LOW_LATENCY; //Force Low latency for V0
    }

    if (config_struct->coding_vertical_prediction_mode >= METHOD_PRED_SIZE) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            //Invalid VPrediction mode
            fprintf(stderr, "Error: Invalid Vertical Prediction Mode!\n");
        }
        return SvtJxsErrorBadParameter;
    }
    enc_common->coding_vertical_prediction_mode = config_struct->coding_vertical_prediction_mode;
    enc_common->picture_header_dynamic.hdr_Rm = 0;
    if (enc_common->coding_vertical_prediction_mode == METHOD_PRED_ZERO_COEFFICIENTS) {
        enc_common->picture_header_dynamic.hdr_Rm = 1;
    }

    enc_common->picture_header_dynamic.hdr_Qpih = config_struct->quantization;
    enc_common->pi.use_short_header = (((config_struct->source_width * num_comp) < 32768) && (config_struct->ndecomp_v < 3));

    enc_common->picture_header_dynamic.hdr_Rl = 1;

    if (config_struct->coding_signs_handling > SIGN_HANDLING_STRATEGY_FULL) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: Invalid coding_signs_handling mode!\n");
        }
        return SvtJxsErrorBadParameter;
    }
    if (config_struct->coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Warning: coding_signs_handling PACK mode in this RC mode do not have any quality benefits. Only slow down "
                    "performance!\n");
        }
    }
    enc_common->coding_signs_handling = config_struct->coding_signs_handling;
    /*Fs - Sign handling strategy
     *  0: Signs encoded jointly with the data
     *  1: Signs encoded separately
     *2-3: Reserved for ISO/IEC purposes*/
    enc_common->picture_header_dynamic.hdr_Fs = !!enc_common->coding_signs_handling;

    /*Bitstream minimum allocation size:*/
    enc_common->picture_header_dynamic.hdr_Lcod = (uint32_t)bytes_per_frame;

    //Set flags 0 or 1
    enc_common->coding_significance = !!enc_common->coding_significance;

    if (config_struct->source_width < 4) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: Minimum Width is 4!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if ((COLOUR_FORMAT_PLANAR_YUV422 == enc_common->colour_format || COLOUR_FORMAT_PLANAR_YUV420 == enc_common->colour_format) &&
        (config_struct->source_width % 2 != 0)) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: The input format requires a width divisible by 2!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (COLOUR_FORMAT_PLANAR_YUV420 == enc_common->colour_format) {
        if (config_struct->source_height % 2 != 0) {
            if (config_struct->verbose >= VERBOSE_ERRORS) {
                fprintf(stderr, "Error: The input format YUV420 requires a height divisible by 2!\n");
            }
            return SvtJxsErrorBadParameter;
        }
        if (config_struct->ndecomp_v == 0) {
            if (config_struct->verbose >= VERBOSE_ERRORS) {
                fprintf(stderr, "Error: The input format YUV420 requires not zeroed Vertical Decomposition!\n");
            }
            return SvtJxsErrorBadParameter;
        }
    }

    if (enc_common->colour_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB && enc_common->cpu_profile == CPU_PROFILE_CPU) {
        //TODO: Implement packed RGB/YUV444 in threading model CPU_PROFILE_CPU
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: The input format packed YUV/RGB works only in Low latency threading model!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->ndecomp_v > 2) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: Vertical Decomposition is too big (range 0-2)!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->ndecomp_h > 5) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: Horizontal Decomposition is too big (range 0-5)!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->ndecomp_h < config_struct->ndecomp_v) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: The horizontal decomposition must be greater or equal to the vertical decomposition!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->ndecomp_h == 0 && config_struct->ndecomp_v == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: Zero decomposition not supported now yet!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    /*Test exist zeroed bands*/
    uint32_t min_width_band = config_struct->source_width;
    uint32_t min_height_band = config_struct->source_height;
    uint32_t min_ndecomp_v = config_struct->ndecomp_v;
    if (COLOUR_FORMAT_PLANAR_YUV420 == enc_common->colour_format) {
        min_width_band >>= 1;
        min_height_band >>= 1;
        min_ndecomp_v = min_ndecomp_v - 1; //Zeroed ndecomp_v for YUV420 is checked earlier
        if (min_ndecomp_v > 0) {
        }
    }
    if (COLOUR_FORMAT_PLANAR_YUV422 == enc_common->colour_format) {
        min_width_band >>= 1;
    }

    for (uint32_t h = 0; h < config_struct->ndecomp_h; ++h) {
        if (min_width_band > 1) {
            min_width_band = min_width_band - (min_width_band / 2);
        }
        else {
            min_width_band = 0;
            break;
        }
    }

    for (uint32_t v = 0; v < min_ndecomp_v; ++v) {
        if (min_height_band > 1) {
            min_height_band = min_height_band - (min_height_band / 2);
        }
        else {
            min_height_band = 0;
            break;
        }
    }

    if (min_width_band == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "The width is too small for this format, use less Horizontal Decomposition!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (min_height_band == 0) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "The height is too small for this format, use less Vertical Decomposition!\n");
        }
        return SvtJxsErrorBadParameter;
    }

    enc_common->Cw = 0;
    pi_t* pi = &enc_common->pi;
    return_error = pi_compute(pi,
                              1 /*Init encoder*/,
                              num_comp,
                              GROUP_SIZE,
                              SIGNIFICANCE_GROUP_SIZE,
                              config_struct->source_width,
                              config_struct->source_height,
                              config_struct->ndecomp_h,
                              config_struct->ndecomp_v,
                              0,
                              sx,
                              sy,
                              enc_common->Cw,
                              config_struct->slice_height);

    if (return_error) {
        return return_error;
    }

    uint64_t values_sum = 0;
    for (uint8_t c = 0; c < pi->comps_num; ++c) {
        values_sum += (uint64_t)pi->components[c].width * pi->components[c].height;
    }
    uint64_t bits_raw = values_sum * enc_common->bit_depth;
    enc_common->compression_rate = (float)bits_raw / ((float)bytes_per_frame * 8);

    return_error = weight_table_calculate(pi, config_struct->verbose, enc_common->colour_format);
    if (return_error) {
        if (config_struct->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: Error calculate Weight Tables for this configuration\n");
        }
        return SvtJxsErrorBadParameter;
    }

    if (config_struct->print_bands_info && config_struct->verbose > VERBOSE_NONE) {
        pi_show_bands(pi, 0); //Print bands topology
        pi_show_bands(pi, 1); //Print bands priorities
    }

    pi_enc_t* pi_enc = &enc_common->pi_enc;
    return_error = pi_compute_encoder(pi,
                                      pi_enc,
                                      enc_common->coding_significance,
                                      enc_common->coding_vertical_prediction_mode != METHOD_PRED_DISABLE,
                                      (config_struct->verbose >= VERBOSE_SYSTEM_INFO));
    if (return_error) {
        return return_error;
    }

    SVT_DEBUG("%s, prec_num_in_slice %u, prec_num_in_frame %u\n", __func__, pi->precincts_per_slice, pi->precincts_line_num);
    return SvtJxsErrorNone;
}

/**********************************
 * Encoder Handle Initialization
 **********************************/
SvtJxsErrorType_t encoder_allocate_handle(svt_jpeg_xs_encoder_api_t* enc_api) {
    svt_jpeg_xs_encoder_api_prv_t* enc_api_prv;
    if (enc_api->verbose >= VERBOSE_SYSTEM_INFO) {
        SVT_LOG("-------------------------------------------\n");
        SVT_LOG("SVT [version]:\tSVT-JPEGXS Encoder Lib v%i.%i\n", SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR);

#if defined(_MSC_VER) && (_MSC_VER >= 1930)
        SVT_LOG("SVT [build]  :\tVisual Studio 2022");
#elif defined(_MSC_VER) && (_MSC_VER >= 1920)
        SVT_LOG("SVT [build]  :\tVisual Studio 2019");
#elif defined(_MSC_VER) && (_MSC_VER >= 1910)
        SVT_LOG("SVT [build]  :\tVisual Studio 2017");
#elif defined(_MSC_VER) && (_MSC_VER >= 1900)
        SVT_LOG("SVT [build]  :\tVisual Studio 2015");
#elif defined(_MSC_VER)
        SVT_LOG("SVT [build]  :\tVisual Studio (old)");
#elif defined(__GNUC__)
        SVT_LOG("SVT [build]  :\tGCC %s\t", __VERSION__);
#else
        SVT_LOG("SVT [build]  :\tunknown compiler");
#endif
        SVT_LOG(" %zu bit\n", sizeof(void*) * 8);
        SVT_LOG("LIB Build date: %s %s\n", __DATE__, __TIME__);
        SVT_LOG("-------------------------------------------\n");
    }

    SVT_NEW(enc_api_prv, svt_enc_handle_ctor);
    enc_api->private_ptr = enc_api_prv;
    return SvtJxsErrorNone;
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_load_default_parameters(uint64_t version_api_major, uint64_t version_api_minor,
                                                                         svt_jpeg_xs_encoder_api_t* enc_api) {
    if ((version_api_major > SVT_JPEGXS_API_VER_MAJOR) ||
        (version_api_major == SVT_JPEGXS_API_VER_MAJOR && version_api_minor > SVT_JPEGXS_API_VER_MINOR)) {
        return SvtJxsErrorInvalidApiVersion;
    }

    if (!enc_api) {
        fprintf(stderr, "Error: svt_jpeg_xs_encoder_api_t parameter cannot be NULL!\n");
        return SvtJxsErrorBadParameter;
    }

    memset(enc_api, 0, sizeof(svt_jpeg_xs_encoder_api_t));

    enc_api->source_width = 0;
    enc_api->source_height = 0;
    enc_api->input_bit_depth = 0;
    enc_api->colour_format = COLOUR_FORMAT_INVALID;
    enc_api->bpp_numerator = 0;
    enc_api->bpp_denominator = 1;
    enc_api->ndecomp_v = 2;
    enc_api->ndecomp_h = 5;
    enc_api->quantization = 0;
    enc_api->slice_height = 16;
    enc_api->use_cpu_flags = CPU_FLAGS_ALL;
    enc_api->threads_num = 0;
    enc_api->cpu_profile = 0;
    enc_api->print_bands_info = 0;
    enc_api->verbose = VERBOSE_SYSTEM_INFO;
    enc_api->rate_control_mode = RC_CBR_PER_PRECINCT_MOVE_PADDING;
    enc_api->coding_signs_handling = 0;
    enc_api->coding_significance = 1;
    enc_api->coding_vertical_prediction_mode = 0;
    enc_api->callback_send_data_available = NULL;
    enc_api->callback_send_data_available_context = NULL;
    enc_api->callback_get_data_available = NULL;
    enc_api->callback_get_data_available_context = NULL;

    enc_api->slice_packetization_mode = 0;
    enc_api->private_ptr = NULL;

    return SvtJxsErrorNone;
}

/**********************************
 * Initialize Encoder Library
 **********************************/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_get_image_config(uint64_t version_api_major, uint64_t version_api_minor,
                                                                  svt_jpeg_xs_encoder_api_t* enc_api,
                                                                  svt_jpeg_xs_image_config_t* out_image_config,
                                                                  uint32_t* out_bytes_per_frame) {
    SvtJxsErrorType_t return_error = SvtJxsErrorNone;
    if ((version_api_major > SVT_JPEGXS_API_VER_MAJOR) ||
        (version_api_major == SVT_JPEGXS_API_VER_MAJOR && version_api_minor > SVT_JPEGXS_API_VER_MINOR)) {
        return SvtJxsErrorInvalidApiVersion;
    }
    if (enc_api == NULL || out_image_config == NULL) {
        return SvtJxsErrorBadParameter;
    }

    out_image_config->width = enc_api->source_width;
    out_image_config->height = enc_api->source_height;
    out_image_config->bit_depth = enc_api->input_bit_depth;
    out_image_config->format = enc_api->colour_format;
    if (out_image_config->bit_depth < 8 || out_image_config->bit_depth > 14) {
        if (enc_api->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Incorrect bit_depth, expected: 8 to 14,  provided: %d\n", out_image_config->bit_depth);
        }
        return SvtJxsErrorBadParameter;
    }

    uint32_t sx[MAX_COMPONENTS_NUM];
    uint32_t sy[MAX_COMPONENTS_NUM];
    uint32_t components_num = 0;
    return_error = format_get_sampling_factory(out_image_config->format, &components_num, sx, sy, enc_api->verbose);
    if (return_error) {
        return return_error;
    }
    if (components_num > MAX_COMPONENTS_NUM) {
        return SvtJxsErrorBadParameter;
    }

    uint32_t pixel_size = out_image_config->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    if (out_image_config->format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
        out_image_config->components_num = 1;
        //Single plane of size w*h*3
        out_image_config->components[0].byte_size = enc_api->source_width * enc_api->source_height * 3 * pixel_size;
        out_image_config->components[0].width = enc_api->source_width;
        out_image_config->components[0].height = enc_api->source_height;
    }
    else {
        out_image_config->components_num = components_num;
        for (int32_t c = 0; c < out_image_config->components_num; c++) {
            out_image_config->components[c].width = out_image_config->width >> (sx[c] - 1);
            out_image_config->components[c].height = out_image_config->height >> (sy[c] - 1);
        }
        for (int32_t c = 0; c < out_image_config->components_num; c++) {
            out_image_config->components[c].byte_size = out_image_config->components[c].width *
                out_image_config->components[c].height * pixel_size;
        }
    }

    if (out_bytes_per_frame) {
        if (enc_api->bpp_denominator == 0) {
            if (enc_api->verbose >= VERBOSE_ERRORS) {
                fprintf(stderr, "bpp_denominator cannot be 0!\n");
            }
            return SvtJxsErrorBadParameter;
        }

        uint64_t bytes_per_frame =
            ((uint64_t)enc_api->source_width * enc_api->source_height * enc_api->bpp_numerator / enc_api->bpp_denominator + 7) /
            8;

        *out_bytes_per_frame = (uint32_t)bytes_per_frame;
    }

    return SvtJxsErrorNone;
}

/**********************************
 * Initialize Encoder Library
 **********************************/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_init(uint64_t version_api_major, uint64_t version_api_minor,
                                                      svt_jpeg_xs_encoder_api_t* enc_api) {
    SvtJxsErrorType_t return_error = SvtJxsErrorNone;
    if ((version_api_major > SVT_JPEGXS_API_VER_MAJOR) ||
        (version_api_major == SVT_JPEGXS_API_VER_MAJOR && version_api_minor > SVT_JPEGXS_API_VER_MINOR)) {
        return SvtJxsErrorInvalidApiVersion;
    }
    if (enc_api == NULL) {
        return SvtJxsErrorBadParameter;
    }
    enc_api->private_ptr = NULL;
    svt_log_init();
    // Init Component OS objects (threads, semaphores, etc.)
    // also links the various Component control functions
    return_error = encoder_allocate_handle(enc_api);
    if (return_error != SvtJxsErrorNone) {
        svt_jpeg_xs_encoder_close(enc_api);
        return return_error;
    }
    svt_jpeg_xs_encoder_api_prv_t* enc_api_prv = (svt_jpeg_xs_encoder_api_prv_t*)enc_api->private_ptr;
    if (enc_api_prv == NULL) {
        return SvtJxsErrorUndefined;
    }
    svt_jpeg_xs_encoder_common_t* enc_common = &enc_api_prv->enc_common;
    svt_jxs_increase_component_count();

    enc_api_prv->callback_encoder_ctx = enc_api;
    enc_api_prv->callback_send_data_available = enc_api->callback_send_data_available;
    enc_api_prv->callback_send_data_available_context = enc_api->callback_send_data_available_context;
    enc_api_prv->callback_get_data_available = enc_api->callback_get_data_available;
    enc_api_prv->callback_get_data_available_context = enc_api->callback_get_data_available_context;

    if (enc_api->slice_packetization_mode > 1) {
        if (enc_api->verbose >= VERBOSE_ERRORS) {
            SVT_LOG("Unrecognized slice packetization mode\n");
        }
        return SvtJxsErrorBadParameter;
    }
    enc_common->slice_packetization_mode = enc_api->slice_packetization_mode;

    const CPU_FLAGS cpu_flags = get_cpu_flags();
    enc_api->use_cpu_flags &= cpu_flags;
    if (enc_api->verbose >= VERBOSE_SYSTEM_INFO) {
        SVT_LOG("[asm level on system : up to %s]\n", get_asm_level_name_str(cpu_flags));
        SVT_LOG("[asm level selected : up to %s]\n", get_asm_level_name_str(enc_api->use_cpu_flags));
    }
    setup_common_rtcd_internal(enc_api->use_cpu_flags);
    setup_encoder_rtcd_internal(enc_api->use_cpu_flags);

    return_error = encoder_init_configuration(&enc_api_prv->enc_common, enc_api);
    if (return_error != SvtJxsErrorNone) {
        svt_jpeg_xs_encoder_close(enc_api);
        return return_error;
    }

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        if (enc_api->threads_num > 12) {
            enc_api_prv->dwt_stage_threads_num = 3;
        }
        else if (enc_api->threads_num > 8) {
            enc_api_prv->dwt_stage_threads_num = 2;
        }
        else {
            enc_api_prv->dwt_stage_threads_num = 1;
        }

        int32_t threads_left = (int32_t)enc_api->threads_num - 2 - (int32_t)enc_api_prv->dwt_stage_threads_num;
        enc_api_prv->pack_stage_threads_num = MAX(1, threads_left);
    }
    else if (enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
        enc_api_prv->dwt_stage_threads_num = 0;
        enc_api_prv->pack_stage_threads_num = MAX(1, (int32_t)enc_api->threads_num - 2);
    }
    else {
        assert(0);
    }

    uint32_t pack_input_fifo_count = 2 * enc_api_prv->pack_stage_threads_num;
    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        /*Set minimum 2 frames to schedule.
         *If size of queue is smaller than number of slices then deadlock.*/
        pack_input_fifo_count = MAX(pack_input_fifo_count, 2 * enc_common->pi.slice_num);
        pack_input_fifo_count = MAX(pack_input_fifo_count,
                                    (enc_api_prv->dwt_stage_threads_num / enc_common->pi.comps_num) * enc_common->pi.slice_num);
    }

    const uint32_t init_stage_process_threads_num = 1;
    uint32_t dwt_input_fifo_count = enc_api_prv->dwt_stage_threads_num * 3; /* Using only for CPU_PROFILE_CPU! */
    uint32_t input_buffer_fifo_count = 10;
    uint32_t picture_control_set_pool_count = input_buffer_fifo_count; //TODO: 3 will be enough for latency mode???
    enc_api_prv->sync_output_ringbuffer_size = picture_control_set_pool_count + 10;
    uint32_t output_buffer_fifo_count = enc_api_prv->sync_output_ringbuffer_size + 8;
    uint32_t pack_output_fifo_count = pack_input_fifo_count;

    enc_api_prv->sync_output_ringbuffer = NULL;
    SVT_CALLOC_ARRAY(enc_api_prv->sync_output_ringbuffer, enc_api_prv->sync_output_ringbuffer_size);
    return_error = svt_jxs_create_cond_var(&enc_api_prv->sync_output_ringbuffer_left);
    if (return_error) {
        svt_jpeg_xs_encoder_close(enc_api);
        return return_error;
    }
    svt_jxs_set_cond_var(&enc_api_prv->sync_output_ringbuffer_left, enc_api_prv->sync_output_ringbuffer_size);

    if (enc_api->verbose >= VERBOSE_SYSTEM_INFO) {
        SVT_LOG(
            "Number of logical cores available: %u\nNumber of PPCS %u\n", enc_api->threads_num, picture_control_set_pool_count);
        if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
            SVT_LOG("dwt input count %d\n", dwt_input_fifo_count);
        }
        SVT_LOG("slice pack input count %d\n", pack_input_fifo_count);
        SVT_LOG("slice pack output count %d\n", pack_output_fifo_count);

        print_lib_params(enc_api);
    }

    /*Prepare header for all frames*/
    enc_common->frame_header_length_bytes = write_pic_level_header_nbytes(
        enc_common->frame_header_buffer, sizeof(enc_common->frame_header_buffer), enc_common);
    assert(enc_common->frame_header_length_bytes <= sizeof(enc_common->frame_header_buffer));
    if (enc_common->frame_header_length_bytes > sizeof(enc_common->frame_header_buffer)) {
        assert(0);
        //Should not happens
        svt_jpeg_xs_encoder_close(enc_api);
        return SvtJxsErrorInsufficientResources;
    }

    uint32_t headers_len_per_frame = enc_common->frame_header_length_bytes + CODESTREAM_SIZE_BYTES +
        SLICE_HEADER_SIZE_BYTES * enc_common->pi.slice_num;
    if (headers_len_per_frame >= enc_common->picture_header_dynamic.hdr_Lcod) {
        if (enc_api->verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Impossible compression. Please use bigger bpp param!\n");
        }
        svt_jpeg_xs_encoder_close(enc_api);
        return SvtJxsErrorBadParameter;
    }

    SVT_MALLOC(enc_common->slice_sizes, enc_common->pi.slice_num * sizeof(uint32_t));
    /*Calculate size of each slice*/
    {
        uint32_t picture_headlen_bytes = enc_common->frame_header_length_bytes;
        uint32_t heders_and_tags_bytes = picture_headlen_bytes + CODESTREAM_SIZE_BYTES +
            SLICE_HEADER_SIZE_BYTES * enc_common->pi.slice_num;
        assert(enc_common->picture_header_dynamic.hdr_Lcod >= heders_and_tags_bytes);
        uint32_t size_all_precincts_bytes = enc_common->picture_header_dynamic.hdr_Lcod - heders_and_tags_bytes;

        uint32_t precincst_last_slice = enc_common->pi.precincts_per_slice -
            (enc_common->pi.slice_num * enc_common->pi.precincts_per_slice - enc_common->pi.precincts_line_num);
        /* Last slice can have less precinct so need to balance size between slices.
     * First slices get more one byte per slice*/
        uint32_t min_size_per_precinct_bytes = size_all_precincts_bytes / enc_common->pi.precincts_line_num;
        uint32_t min_size_per_slice_bytes = min_size_per_precinct_bytes * enc_common->pi.precincts_per_slice;
        uint32_t last_size_per_slice_bytes = min_size_per_precinct_bytes * precincst_last_slice;
        assert(size_all_precincts_bytes >= last_size_per_slice_bytes + min_size_per_slice_bytes * (enc_common->pi.slice_num - 1));
        uint32_t size_left_bytes = size_all_precincts_bytes - last_size_per_slice_bytes -
            min_size_per_slice_bytes * (enc_common->pi.slice_num - 1);

        /*Add to slices proportionally to precincts in slice*/
        if (enc_common->pi.slice_num > 1) {
            assert(((uint64_t)size_left_bytes * (enc_common->pi.slice_num - 1) * enc_common->pi.precincts_per_slice) <
                   UINT32_MAX);
            min_size_per_slice_bytes += (size_left_bytes * (enc_common->pi.slice_num - 1) * enc_common->pi.precincts_per_slice /
                                         enc_common->pi.precincts_line_num) /
                (enc_common->pi.slice_num - 1);
        }
        assert(((uint64_t)size_left_bytes * precincst_last_slice) < UINT32_MAX);
        last_size_per_slice_bytes += size_left_bytes * precincst_last_slice / enc_common->pi.precincts_line_num;
        size_left_bytes = size_all_precincts_bytes - last_size_per_slice_bytes -
            min_size_per_slice_bytes * (enc_common->pi.slice_num - 1);
        assert(size_left_bytes <= enc_common->pi.slice_num);

        uint32_t bytes_left = enc_common->picture_header_dynamic.hdr_Lcod - picture_headlen_bytes;
        for (uint32_t i = 0; i < enc_common->pi.slice_num; i++) {
            if (i != enc_common->pi.slice_num - 1) {
                uint32_t size_per_slice_bytes = min_size_per_slice_bytes;
                if (i < size_left_bytes) {
                    size_per_slice_bytes++;
                }
                size_per_slice_bytes += SLICE_HEADER_SIZE_BYTES;
                enc_common->slice_sizes[i] = size_per_slice_bytes;
                bytes_left -= size_per_slice_bytes;
            }
            else {
                enc_common->slice_sizes[i] = bytes_left;
            }
        }
    }

    uint32_t process_index;
    enc_api_prv->frame_number = 0;

    SVT_NEW(enc_api_prv->picture_control_set_pool_ptr,
            svt_jxs_system_resource_ctor,
            picture_control_set_pool_count,
            1,
            0,
            picture_control_set_creator,
            enc_common,
            NULL);

    /************************************
   * System Resource Managers & Fifos
   ************************************/

    // EncoderInputImageItem Input
    SVT_NEW(enc_api_prv->input_image_resource_ptr,
            svt_jxs_system_resource_ctor,
            input_buffer_fifo_count,
            1,
            1,
            input_item_creator,
            enc_common,
            input_item_destroyer);

    enc_api_prv->input_image_producer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(enc_api_prv->input_image_resource_ptr, 0);
    enc_api_prv->input_image_consumer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(enc_api_prv->input_image_resource_ptr, 0);

    SVT_NEW(enc_api_prv->output_queue_resource_ptr,
            svt_jxs_system_resource_ctor,
            output_buffer_fifo_count,
            1,
            1,
            output_item_creator,
            enc_common,
            output_item_destroyer);

    enc_api_prv->output_queue_producer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(
        enc_api_prv->output_queue_resource_ptr,
                                                                                        0);
    enc_api_prv->output_queue_consumer_fifo_ptr = svt_jxs_system_resource_get_consumer_fifo(
        enc_api_prv->output_queue_resource_ptr,
                                                                                        0);

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        // Wavelet Vertical Transform Input
        DwtInputInitData dwt_input_init_data;

        SVT_NEW(enc_api_prv->dwt_input_resource_ptr,
                svt_jxs_system_resource_ctor,
                dwt_input_fifo_count,
                init_stage_process_threads_num,
                enc_api_prv->dwt_stage_threads_num,
                dwt_input_creator,
                &dwt_input_init_data,
                NULL);
    }

    //INIT -> PACK
    SVT_NEW(enc_api_prv->pack_input_resource_ptr,
            svt_jxs_system_resource_ctor,
            pack_input_fifo_count,
            init_stage_process_threads_num,
            enc_api_prv->pack_stage_threads_num,
            pack_input_creator,
            enc_common,
            NULL);

    // slice pack output
    // PACK -> FINISH THREAD
    PackOutputInitData pack_output_init_data;
    SVT_NEW(enc_api_prv->pack_output_resource_ptr,
            svt_jxs_system_resource_ctor,
            pack_output_fifo_count,
            enc_api_prv->pack_stage_threads_num,
            1,
            pack_output_creator,
            &pack_output_init_data,
            NULL);

    /************************************
   * Contexts
   ************************************/
    // Init Stage Context
    SVT_NEW(enc_api_prv->init_stage_context_ptr, init_stage_context_ctor, enc_api_prv);

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        // Dwt Ver Stage Context
        SVT_ALLOC_PTR_ARRAY(enc_api_prv->dwt_stage_context_ptr_array, enc_api_prv->dwt_stage_threads_num);
        for (process_index = 0; process_index < enc_api_prv->dwt_stage_threads_num; ++process_index) {
            SVT_NEW(enc_api_prv->dwt_stage_context_ptr_array[process_index], dwt_stage_context_ctor, enc_api_prv, process_index);
        }
    }

    // Pack Stage Context
    SVT_ALLOC_PTR_ARRAY(enc_api_prv->pack_stage_context_ptr_array, enc_api_prv->pack_stage_threads_num);
    for (process_index = 0; process_index < enc_api_prv->pack_stage_threads_num; ++process_index) {
        SVT_NEW(enc_api_prv->pack_stage_context_ptr_array[process_index], pack_stage_context_ctor, enc_api_prv, process_index);
    }

    // Final Stage Context
    SVT_NEW(enc_api_prv->final_stage_context_ptr, final_stage_context_ctor, enc_api_prv);

    // Init Stage Kernel
    SVT_CREATE_THREAD(enc_api_prv->init_stage_thread_handle, init_stage_kernel, enc_api_prv->init_stage_context_ptr);

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        // Dwt Ver Stage Kernel
        SVT_CREATE_THREAD_ARRAY(enc_api_prv->dwt_stage_thread_handle_array,
                                enc_api_prv->dwt_stage_threads_num,
                                dwt_stage_kernel,
                                enc_api_prv->dwt_stage_context_ptr_array);
    }

    // Pack Stage Kernel
    SVT_CREATE_THREAD_ARRAY(enc_api_prv->pack_stage_thread_handle_array,
                            enc_api_prv->pack_stage_threads_num,
                            pack_stage_kernel,
                            enc_api_prv->pack_stage_context_ptr_array);
    // Final Stage Kernel
    SVT_CREATE_THREAD(enc_api_prv->final_stage_thread_handle, final_stage_kernel, enc_api_prv->final_stage_context_ptr);

    svt_jxs_print_memory_usage();
    return return_error;
}

PREFIX_API void svt_jpeg_xs_encoder_close(svt_jpeg_xs_encoder_api_t* enc_api) {
    if (enc_api && enc_api->private_ptr) {
        svt_jpeg_xs_encoder_api_prv_t* enc_api_prv = (svt_jpeg_xs_encoder_api_prv_t*)enc_api->private_ptr;
        svt_jxs_shutdown_process(enc_api_prv->input_image_resource_ptr);
        svt_jxs_shutdown_process(enc_api_prv->dwt_input_resource_ptr);
        svt_jxs_shutdown_process(enc_api_prv->pack_input_resource_ptr);
        svt_jxs_shutdown_process(enc_api_prv->pack_output_resource_ptr);
        svt_jxs_shutdown_process(enc_api_prv->output_queue_resource_ptr);
        SVT_DELETE(enc_api_prv);
        enc_api->private_ptr = NULL;
        svt_jxs_decrease_component_count();
    }
}

/**********************************
 * Empty This Buffer
 **********************************/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_send_picture(svt_jpeg_xs_encoder_api_t* enc_api, svt_jpeg_xs_frame_t* enc_input,
                                                              uint8_t blocking_flag) {
    if (enc_api == NULL || enc_api->private_ptr == NULL || enc_input == NULL) {
        return SvtJxsErrorBadParameter;
    }

    svt_jpeg_xs_encoder_api_prv_t* enc_api_prv = (svt_jpeg_xs_encoder_api_prv_t*)enc_api->private_ptr;

    if (enc_input->bitstream.allocation_size < enc_api_prv->enc_common.picture_header_dynamic.hdr_Lcod) {
        return SvtJxsErrorBadParameter;
    }

    pi_t* pi = &enc_api_prv->enc_common.pi;
    uint8_t input_bit_depth = enc_api_prv->enc_common.bit_depth;
    uint32_t pixel_size = input_bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    for (uint8_t c = 0; c < pi->comps_num; ++c) {
        uint32_t min_size;
        // The last row might be shorter than the stride, e.g. in case the application is encoding
        // an interlaced image with fields interleaved row by row, but feeding each field individually
        // to the encoder. In that case the stride would be double the usual stride so the encoder only
        // encodes every second row, but the last row of the second field would only have a single row
        // of data left in it (half the stride in that case).
        min_size = enc_input->image.stride[c] * pixel_size * (pi->components[c].height - 1);
        min_size += pi->components[c].width * pixel_size;
        if (enc_input->image.alloc_size[c] < min_size) {
            return SvtJxsErrorBadParameter;
        }
    }

    ObjectWrapper_t* wrapper_ptr = NULL;
#ifdef FLAG_DEADLOCK_DETECT
    printf("00[%s:%i] Send frame to encoder: %03li\n", __func__, __LINE__, (size_t)enc_api_prv->frame_number);
#endif

    SvtJxsErrorType_t ret = SvtJxsErrorNone;
    if (blocking_flag) {
        ret = svt_jxs_get_empty_object(enc_api_prv->input_image_producer_fifo_ptr, &wrapper_ptr);
    }
    else {
        ret = svt_jxs_get_empty_object_non_blocking(enc_api_prv->input_image_producer_fifo_ptr, &wrapper_ptr);
    }

    if (wrapper_ptr && (ret == SvtJxsErrorNone)) {
        EncoderInputItem* input_item = (EncoderInputItem*)wrapper_ptr->object_ptr;
        input_item->enc_input = *enc_input; //Copy input structure
        input_item->frame_number = enc_api_prv->frame_number;
        enc_api_prv->frame_number++;
        svt_jxs_post_full_object(wrapper_ptr);
        SVT_DEBUG("%s\n", __func__);
#ifdef FLAG_DEADLOCK_DETECT
        printf("00[%s:%i] Send frame to encoder: %03li\n", __func__, __LINE__, (size_t)input_item->frame_number);
#endif
        return SvtJxsErrorNone;
    }
    return SvtJxsErrorNoErrorEmptyQueue;
}

/**********************************
 * svt_jpeg_xs_encoder_get_packet sends out packet
 **********************************/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_get_packet(svt_jpeg_xs_encoder_api_t* enc_api, svt_jpeg_xs_frame_t* enc_output,
                                                            uint8_t blocking_flag) {
    SvtJxsErrorType_t return_error = SvtJxsErrorNone;
    if (enc_api == NULL || enc_api->private_ptr == NULL || enc_output == NULL) {
        return SvtJxsErrorBadParameter;
    }
    svt_jpeg_xs_encoder_api_prv_t* enc_api_prv = (svt_jpeg_xs_encoder_api_prv_t*)enc_api->private_ptr;
    ObjectWrapper_t* wrapper_ptr = NULL;

    if (blocking_flag) {
        svt_jxs_get_full_object(enc_api_prv->output_queue_consumer_fifo_ptr, &wrapper_ptr);
    }
    else {
        svt_jxs_get_full_object_non_blocking(enc_api_prv->output_queue_consumer_fifo_ptr, &wrapper_ptr);
    }

    if (wrapper_ptr) {
        EncoderOutputItem* output_item = (EncoderOutputItem*)wrapper_ptr->object_ptr;
        // return the output stream buffer
        *enc_output = output_item->enc_input; //Copy structure
        int32_t error = output_item->frame_error;
        if (error) {
            return_error = SvtJxsErrorEncodeFrameError;
        }
#ifdef FLAG_DEADLOCK_DETECT
        printf("09[%s:%i] Receive encoded frame  %03li\n", __func__, __LINE__, (size_t)output_item->frame_number);
#endif
        svt_jxs_release_object(wrapper_ptr);
    }
    else {
        enc_output->user_prv_ctx_ptr = NULL;
        return_error = SvtJxsErrorNoErrorEmptyQueue;
    }

    // SVT_DEBUG("%s\n", __func__);
    return return_error;
}
