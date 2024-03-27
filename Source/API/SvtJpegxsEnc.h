/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_JPEG_XS_API_ENC_H_
#define _SVT_JPEG_XS_API_ENC_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "SvtJpegxs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct svt_jpeg_xs_encoder_api {
    // Input Info
    uint32_t source_width;        /* Mandatory, The width of input source in units of picture luma pixels.*/
    uint32_t source_height;       /* Mandatory, The height of input source in units of picture luma pixels. */
    uint8_t input_bit_depth;      /* Mandatory, Specifies the bit depth of input video. * 8 = 8 bit. * 10 = 10 bit. */
    ColourFormat_t colour_format; /* Mandatory, Specifies the chroma sub-sampling format of input video. */
    uint32_t bpp_numerator;       /* Mandatory, Specifies the bits per pixel. BPP value is fraction: */
    uint32_t bpp_denominator;     /* Optional, default 1, BPP = bpp_numerator / bpp_denominator; */
    uint32_t ndecomp_v;           /* Optional, default 2, Specifies decomp_v depth. */
    uint32_t ndecomp_h;           /* Optional, default 5, Specifies decomp_v depth.*/
    uint32_t quantization;        /* Optional, default 0, Specifies quantization method: 0 = deadzone, 1 = uniform.*/
    uint32_t slice_height;        /* Optional, default 16, The height of each slice in units of picture luma pixels.*/
    CPU_FLAGS
    use_cpu_flags; /* Optional, default CPU_FLAGS_ALL, CPU FLAGS to limit assembly instruction set used by encoder. Max: CPU_FLAGS_ALL */
    uint32_t threads_num;      /* Optional, default 0, The number of logical processor which encoder threads run on. */
    uint8_t cpu_profile;       /* Optional, default 0, CPU profile optimizations 0 = Low latency, 1 = Low CPU usage */
    uint32_t print_bands_info; /* Optional, default 0, Print Band info in initialization. */

    uint32_t coding_signs_handling; /* Optional, default 0, Enable Signs handling strategy full: 2, fast: 1, disable: 0*/
    uint32_t coding_significance;   /* Optional, default 1, Enable Significance coding method flag. */
    /* Rate Control mode
     * CBR budget per precinct = 0,
     * CBR budget per precinct = 1,
     * CBR budget per slice = 2,
     * CBR budget per slice with max rate size = 3
     * Optional, default 0, */
    uint32_t rate_control_mode;

    /* Vertical Prediction Coding method:
    * 0 = Disable Vertical Prediction Coding method
    * 1 = Enable Vertical Prediction Coding method, runs indicate zero prediction residuals
    * 2 = Enable Vertical Prediction Coding method, runs indicate zero coefficients
    * 3 = FROCE Vertical Prediction with indicate zero prediction residuals in any possible Precinct
    * 4 = FROCE  Vertical Prediction with indicate zero coefficients in any possible Precinct
    * Optional, default 0 */
    uint32_t coding_vertical_prediction_mode;

    uint32_t verbose; //VerboseMessages

    /* Callback: Call when available place in queue to send data.
     * Call when finish init encoder.
     * Call every time when the next element from the input queue is taken.
     * Only one callback will be triggered at a time. Always from that same thread.
     * No synchronization required.
     * encoder_handle - Pointer passed on initialization,
     * Optional, default NULL */
    void (*callback_send_data_available)(struct svt_jpeg_xs_encoder_api* encoder, void* context);
    void* callback_send_data_available_context;

    /* Callback: Call when new frame is ready to get.
     * Only one callback will be triggered at a time.
     * No synchronization required.
     * encoder_handle - Pointer passed on initialization,
     * Optional, default NULL */
    void (*callback_get_data_available)(struct svt_jpeg_xs_encoder_api* encoder, void* context);
    void* callback_get_data_available_context;

    /* Packetization mode:
    * Please refer to https://datatracker.ietf.org/doc/html/rfc9134 for details
    * 0 = Codestream packetization mode -> packetization unit SHALL be the entire JPEG XS picture segment
    * 1 = Slice packetization mode -> packetization unit SHALL be the slice, The first packetization unit SHALL
    *     be made of the JPEG XS header segment... This first unit is then followed by successive units, each
    *     containing one and only one slice. The packetization unit containing the last slice of a JPEG XS codestream
    *     SHALL also contain the EOC marker immediately following this last slice
    * Optional, default 0  */
    uint8_t slice_packetization_mode;

    void* private_ptr; /*Private encoder pointer, do not touch!!! */
} svt_jpeg_xs_encoder_api_t;

/* STEP 0 (Optional): Set default encoder parameters.
 * Parameter:
 * @ version_api_major - Use version of API Major number (SVT_JPEGXS_API_VER_MAJOR)
   @ version_api_minor - Use version of API Minor number (SVT_JPEGXS_API_VER_MINOR)
 * @ *enc_api          - Encoder handle with configuration to set*/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_load_default_parameters(uint64_t version_api_major, uint64_t version_api_minor,
                                                                         svt_jpeg_xs_encoder_api_t* enc_api);

/* STEP x: Get image config info from encoder parameters. No need to init encoder.
 * Parameter:
 * @ version_api_major - Use version of API Major number (SVT_JPEGXS_API_VER_MAJOR)
   @ version_api_minor - Use version of API Minor number (SVT_JPEGXS_API_VER_MINOR)
 * @ *enc_api          - Encoder handle with configuration*/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_get_image_config(uint64_t version_api_major, uint64_t version_api_minor,
                                                                  svt_jpeg_xs_encoder_api_t* enc_api,
                                                                  svt_jpeg_xs_image_config_t* out_image_config,
                                                                  uint32_t* out_bytes_per_frame);

/* STEP 1: Initialize encoder Handle and allocates memory to necessary buffers.
 * Parameter:
 * @ version_api_major - Use version of API Major number (SVT_JPEGXS_API_VER_MAJOR)
   @ version_api_minor - Use version of API Minor number (SVT_JPEGXS_API_VER_MINOR)
 * @ *enc_api          - Encoder handle with configuration*/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_init(uint64_t version_api_major, uint64_t version_api_minor,
                                                      svt_jpeg_xs_encoder_api_t* enc_api);

/* STEP 4: */
PREFIX_API void svt_jpeg_xs_encoder_close(svt_jpeg_xs_encoder_api_t* enc_api);

/* STEP 2: Send the picture.
 *
 * Parameter:
 * @ *enc_api            Encoder handler.
 * @ *enc_input          Structure with frame info: pointers on yuv and bitstream, frame context. Structure will be copy internal.
 * @ blocking_flag       If set to 1, then function is blocked until frame is sent to encoder, otherwise "frame not ready try again later" error code can be returned*/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_send_picture(svt_jpeg_xs_encoder_api_t* enc_api, svt_jpeg_xs_frame_t* enc_input,
                                                              uint8_t blocking_flag);

/* STEP 3: Receive packet.
 * Parameter:
 * @ *enc_api            Encoder handler.
 * @ *enc_output         Fill output structure with encoder results
 * @ blocking_flag       If set to 1, then function is blocked until frame is received from encoder, otherwise "frame not ready try again later" error code can be returned,
 * this call becomes locking one this signal is 1. Non-locking call, returns
 * SvtJxsErrorMax for an encode error, SvtJxsErrorNoErrorEmptyQueue when the library does
 * not have any available packets, and enc_output.user_prv_ctx_ptr set to NULL*/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_encoder_get_packet(svt_jpeg_xs_encoder_api_t* enc_api, svt_jpeg_xs_frame_t* enc_output,
                                                            uint8_t blocking_flag);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /*_SVT_JPEG_XS_API_ENC_H_*/
