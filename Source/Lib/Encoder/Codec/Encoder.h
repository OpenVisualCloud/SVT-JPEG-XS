/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _JPEGXS_ENCODER_H_
#define _JPEGXS_ENCODER_H_

#include "PiEnc.h"
#include "PrecinctEnc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CpuProfile for Threads:
    * CPU_PROFILE_LOW_LATENCY:
    *                                 -> [pack_stage_kernel():DWT v0 v1 v2, RC, Pack per slice]
    * [AppEnc] -> [init_stage_kernel] -> [pack_stage_kernel():DWT v0 v1 v2, RC, Pack per slice] -> [final_stage_kernel(): Reorder frames] -> [AppEnc]
    *                                 -> [pack_stage_kernel():DWT v0 v1 v2, RC, Pack per slice]
    *
    * CPU_PROFILE_CPU:
    * Where ^ is a sync semaphore.
    *                                 -> [dwt_stage_kernel():DWT v0, v1, v2, per component]
    * [AppEnc] -> [init_stage_kernel] -> [dwt_stage_kernel():DWT v0, v1, v2, per component]
    *                                 -> [dwt_stage_kernel():DWT v0, v1, v2, per component]
    *                                    -> [pack_stage_kernel():DWT   ^v0 ^v1 ^v2, RC, Pack per slice]
    *                                      -> [pack_stage_kernel():DWT  ^v0 ^v1 ^v2, RC, Pack per slice]   -> [final_stage_kernel(): Reorder frames] -> [AppEnc]
    *                                        -> [pack_stage_kernel():DWT ^v0 ^v1 ^v2, RC, Pack per slice]
    */
typedef enum CpuProfile {
    CPU_PROFILE_LOW_LATENCY = 0,
    CPU_PROFILE_CPU = 1,
} CpuProfile;

typedef enum RateControlType {
    /*Budget per precinct.*/
    RC_CBR_PER_PRECINCT = 0,
    /*Budget per precinct. Move padding to next Precinct.
     *Support with Lazy Sign handling: SIGN_HANDLING_STRATEGY_FAST
    */
    RC_CBR_PER_PRECINCT_MOVE_PADDING = 1,
    /*Budget per slice. Common Quantization and Refinement.
     *Move padding to next Precinct. when use Vpred or SIGN_HANDLING_STRATEGY_FAST
     *Support with Lazy Sign handling: SIGN_HANDLING_STRATEGY_FAST
    */
    RC_CBR_PER_SLICE_COMMON_QUANT = 2,
    /*Budget per slice. Common Quantization and Refinement.
     * Max RATE between Max and Min budget per precinct can not be bigger than TUNING_RC_CBR_PER_SLICE_MAX_PRECINCT_BUDGET_RATE
     *Move padding to next Precinct. when use Vpred or SIGN_HANDLING_STRATEGY_FAST
     *Support with Lazy Sign handling: SIGN_HANDLING_STRATEGY_FAST
    */
    RC_CBR_PER_SLICE_COMMON_QUANT_MAX_RATE = 3, //TODO: Need tuning TUNING_RC_CBR_PER_SLICE_MAX_PRECINCT_BUDGET_RATE
    RC_MODE_SIZE
} RateControlType;

/************************************
    * Sequence Control Set
    ************************************/
typedef struct svt_jpeg_xs_encoder_common {
    CpuProfile cpu_profile;

    uint16_t Cw; //Precinct width
    ColourFormat_t colour_format;
    uint8_t bit_depth; // Pixel Bit Depth
    float compression_rate;

    pi_t pi; /* Picture Information */
    picture_header_dynamic_t picture_header_dynamic;
    pi_enc_t pi_enc; /* Picture Information for encoder, allocate buffers pointers etc.*/

    RateControlType rate_control_mode;
    uint32_t coding_significance;
    VerticalPredictionMode coding_vertical_prediction_mode;
    SignHandlingStrategy coding_signs_handling;

    uint32_t frame_header_length_bytes;

    /*
    short name | long name                           | order in stream | mandatory/optional | max byte size (assuming 4 components)
        SOC    | start of codestream                 | 1st             | mandatory          |   2
        CAP    | capabilities                        | 2nd             | mandatory          |   6
        PIH    | picture header                      | 3rd             | mandatory          |  28
        WGT    | weights table                       | 4th             | mandatory          |  84
        CDT    | component table                     | 5th or later    | mandatory          |  12
        NLT    | nonlinearity                        | 5th or later    | optional           |  14
        CWD    | component-dependent decomposition   | 5th or later    | optional           |   5
        CTS    | colour transformation specification | 5th or later    | optional           |   6
        CRG    | component registration marker       | 5th or later    | optional           |  20
        COM    | extension marker                    | 5th or later    | optional           |  XX (variable size, can be gigabytes, not used in encoder)
                                                                                        sum | 177
        To keep it safe lets set size to 256 bytes
    */
    uint8_t frame_header_buffer[256];

    /*
    * Array that keep size of each coded slice
    */
    uint32_t *slice_sizes;
    uint8_t slice_packetization_mode;
} svt_jpeg_xs_encoder_common_t;

#ifdef __cplusplus
}
#endif
#endif /*_JPEGXS_ENCODER_H_*/
