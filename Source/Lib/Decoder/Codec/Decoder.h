/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _JPEGXS_DECODER_H_
#define _JPEGXS_DECODER_H_
#include "Pi.h"
#include "SvtJpegxsDec.h"
#include "Definitions.h"
#include "Threads/SvtThreads.h"

#define MAX_PRECINCT_IN_LINE (130)

enum UniversalThreadStatus { SYNC_INIT = 0, SYNC_OK = 1, SYNC_ERROR = 2 };

typedef struct svt_jpeg_xs_decoder_common {
    pi_t pi; /* Picture Information */
    picture_header_const_t picture_header_const;
    // Temporary buffer when picture_header_dynamic->hdr_Cpih is enabled
    // TODO: Can be removed/optimized in future
    int32_t* buffer_tmp_cpih[MAX_COMPONENTS_NUM];

    // max_frame_bitstream_size is used only when packetization_mode is enabled
    uint32_t max_frame_bitstream_size;
} svt_jpeg_xs_decoder_common_t;

typedef struct svt_jpeg_xs_decoder_thread_context {
    precinct_t* precincts_top[MAX_PRECINCT_IN_LINE];
    int32_t* precinct_components_tmp_buffer[MAX_COMPONENTS_NUM];
    int32_t* precinct_idwt_tmp_buffer[MAX_COMPONENTS_NUM];
} svt_jpeg_xs_decoder_thread_context;

/*TODO Decoder instance Per frame, rename to decoder per frame.*/
typedef struct svt_jpeg_xs_decoder_instance {
    svt_jpeg_xs_decoder_common_t* dec_common; /* Pointer to global decoder common*/
    picture_header_dynamic_t picture_header_dynamic;

    /*
Band coefficients location in memory:
            _______________________
           |  comp-0, band0        | <- 0 * precincts_line_coeff_size + precincts_line_coeff_comp_offset[0]
           |  comp-0, band1        |
           |  comp-0, band2        |
           |  comp-0, band3        |
           |  comp-0, band4        |
           |  comp-0, band5        |
precinct 0 |  comp-0, band6        |
           |  comp-0, band7-line0  |
           |  comp-0, band7-line1  |
           |  comp-0, band8-line0  |
           |  comp-0, band8-line1  |
           |  comp-0, band9-line0  |
           |  comp-0, band9-line1  |
************************************
           |  comp-n, band0        | <- 0 * precincts_line_coeff_size + precincts_line_coeff_comp_offset[n]
           |  comp-n, band1        |
           |  comp-n, band2        |
           |  comp-n, band3        |
           |  comp-n, band4        |
           |  comp-n, band5        |
precinct 0 |  comp-n, band6        |
           |  comp-n, band7-line0  |
           |  comp-n, band7-line1  |
           |  comp-n, band8-line0  |
           |  comp-n, band8-line1  |
           |  comp-n, band9-line0  |
           |  comp-n, band9-line1  |
************************************
************************************
           |  comp-n, band0        |<- X * precincts_line_coeff_size + precincts_line_coeff_comp_offset[n]
           |  comp-n, band1        |
           |  comp-n, band2        |
           |  comp-n, band3        |
           |  comp-n, band4        |
           |  comp-n, band5        |
precinct X |  comp-n, band6        |
           |  comp-n, band7-line0  |
           |  comp-n, band7-line1  |
           |  comp-n, band8-line0  |
           |  comp-n, band8-line1  |
           |  comp-n, band9-line0  |
           |  comp-n, band9-line1  |
            -----------------------
*/
    int16_t* coeff_buff_ptr_16bit;
    uint32_t precincts_line_coeff_size;
    uint32_t precincts_line_coeff_comp_offset[MAX_COMPONENTS_NUM];

    uint32_t sync_output_frame_idx;
    uint32_t sync_num_slices_to_receive;
    uint64_t frame_num;
    svt_jpeg_xs_frame_t dec_input;

    uint8_t sync_slices_idwt;        /*Calculation slice before IDWT wait to finish decode next slice.*/
    CondVar* map_slices_decode_done; /*When sync_slices_idwt use as array of Condition Variable, else use as array of "val"*/

    //TODO: Used only by Final Thread. Can be moved to Final Thread context, or to Slice thread context in future solution.
    int32_t* precinct_idwt_tmp_buffer;
    int32_t* precinct_component_tmp_buffer;

    // Buffer allocated only when packetization_mode is enabled
    uint8_t* frame_bitstream_ptr;
} svt_jpeg_xs_decoder_instance_t;

#ifdef __cplusplus
extern "C" {
#endif

SvtJxsErrorType_t svt_jpeg_xs_decoder_probe(const uint8_t* bitstream_buf, size_t codestream_size,
                                            picture_header_const_t* picture_header_const,
                                            picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose);
ColourFormat_t svt_jpeg_xs_get_format_from_params(uint32_t comps_num, uint32_t sx[MAX_COMPONENTS_NUM],
                                                  uint32_t sy[MAX_COMPONENTS_NUM]);

SvtJxsErrorType_t svt_jpeg_xs_dec_init_common(svt_jpeg_xs_decoder_common_t* dec_common,
                                              svt_jpeg_xs_image_config_t* out_image_config);

svt_jpeg_xs_decoder_instance_t* svt_jpeg_xs_dec_instance_alloc(svt_jpeg_xs_decoder_common_t* dec_common);
void svt_jpeg_xs_dec_instance_free(svt_jpeg_xs_decoder_instance_t* ctx);
svt_jpeg_xs_decoder_thread_context* svt_jpeg_xs_dec_thread_context_alloc(pi_t* pi);
void svt_jpeg_xs_dec_thread_context_free(svt_jpeg_xs_decoder_thread_context* ctx, pi_t* pi);

SvtJxsErrorType_t svt_jpeg_xs_decode_header(svt_jpeg_xs_decoder_instance_t* ctx, const uint8_t* bitstream_buf,
                                            size_t bitstream_buf_size, uint32_t* out_header_size, uint32_t verbose);
SvtJxsErrorType_t svt_jpeg_xs_decode_slice(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_decoder_thread_context* thread_ctx,
                                           const uint8_t* bitstream_buf, size_t bitstream_buf_size, uint32_t slice,
                                           uint32_t* out_slice_size, svt_jpeg_xs_image_buffer_t* out, uint32_t verbose);

SvtJxsErrorType_t svt_jpeg_xs_decode_final(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_image_buffer_t* out);

SvtJxsErrorType_t svt_jpeg_xs_decode_final_slice_overlap(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_image_buffer_t* out,
                                                         uint32_t slice_idx);

#ifdef __cplusplus
}
#endif

#endif /*_JPEGXS_DECODER_H_*/
