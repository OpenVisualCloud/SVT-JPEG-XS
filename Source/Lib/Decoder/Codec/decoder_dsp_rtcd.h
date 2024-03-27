/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

// This file is generated. Do not edit.
#ifndef DECODER_DSP_RTCD_H_
#define DECODER_DSP_RTCD_H_

#include "common_dsp_rtcd.h"
#include "DwtDecoder.h"
#include "SvtType.h"
#include "SvtJpegxsDec.h"
#include "BitstreamReader.h"

/**************************************
 * Instruction Set Support
 **************************************/

#ifdef _WIN32
#include <intrin.h>
#endif

#undef RTCD_EXTERN
#ifdef DECODER_RTCD_C
#define RTCD_EXTERN // CHKN RTCD call in effect. declare the function pointers encHandle.
#else
#define RTCD_EXTERN extern // CHKN run time externing the function pointers.
#endif

#ifdef __cplusplus
extern "C" {
#endif

void setup_decoder_rtcd_internal(CPU_FLAGS flags);

RTCD_EXTERN void (*dequant)(uint16_t* buf, uint32_t buf_len, uint8_t* gclis, uint32_t group_size, uint8_t gtli,
                            QUANT_TYPE dq_type);
RTCD_EXTERN void (*linear_output_scaling_8bit)(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw,
                                               uint32_t depth, svt_jpeg_xs_image_buffer_t* out);
RTCD_EXTERN void (*linear_output_scaling_16bit)(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw,
                                                uint32_t depth, svt_jpeg_xs_image_buffer_t* out);
RTCD_EXTERN void (*inv_sign)(uint16_t* in_out, uint32_t width);
RTCD_EXTERN SvtJxsErrorType_t (*unpack_data)(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                             uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                             int32_t* precinct_bits_left);

RTCD_EXTERN void (*linear_output_scaling_8bit_line)(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w);
RTCD_EXTERN void (*idwt_horizontal_line_lf16_hf16)(const int16_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len,
                                                   uint8_t shift);
RTCD_EXTERN void (*idwt_horizontal_line_lf32_hf16)(const int32_t* in_lf, const int16_t* in_hf, int32_t* out, uint32_t len,
                                                   uint8_t shift);
RTCD_EXTERN void (*linear_output_scaling_16bit_line)(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w);

RTCD_EXTERN void (*idwt_vertical_line)(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                       uint32_t len, int32_t first_precinct, int32_t last_precinct, int32_t height);
RTCD_EXTERN void (*idwt_vertical_line_recalc)(const int32_t* in_lf, const int32_t* in_hf0, const int32_t* in_hf1, int32_t* out[4],
                                              uint32_t len, uint32_t precinct_line_idx);
#ifdef __cplusplus
} // extern "C"
#endif

#endif
