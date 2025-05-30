/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Decoder.h"
#include "Codestream.h"
#include "ParseHeader.h"
#include "SvtUtility.h"
#include "Packing.h"
#include "Precinct.h"
#include "Mct.h"
#include "NltDec.h"

SvtJxsErrorType_t svt_jpeg_xs_decoder_probe(const uint8_t* bitstream_buf, size_t codestream_size,
                                            picture_header_const_t* picture_header_const,
                                            picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose) {
    if (bitstream_buf == NULL || codestream_size == 0) {
        return SvtJxsErrorDecoderInvalidPointer;
    }

    bitstream_reader_t bitstream;
    bitstream_reader_init(&bitstream, bitstream_buf, codestream_size);

    memset(picture_header_const, 0, sizeof(picture_header_const_t));
    if (picture_header_dynamic) {
        memset(picture_header_dynamic, 0, sizeof(picture_header_dynamic_t));
    }

    return get_header(&bitstream, picture_header_const, picture_header_dynamic, verbose);
}

void copy_weights_table(pi_t* pi, picture_header_const_t* picture_header_static) {
    uint32_t old_band_idx = 0;

    for (uint32_t new_band_idx = 0; new_band_idx < pi->bands_num_all; new_band_idx++) {
        const uint32_t b = pi->global_band_info[new_band_idx].band_id;
        const uint32_t c = pi->global_band_info[new_band_idx].comp_id;
        if (b == BAND_NOT_EXIST) {
            continue;
        }
        pi->components[c].bands[b].gain = picture_header_static->hdr_gain[old_band_idx];
        pi->components[c].bands[b].priority = picture_header_static->hdr_priority[old_band_idx];
        ;

        old_band_idx++;
    }
}

ColourFormat_t svt_jpeg_xs_get_format_from_params(uint32_t comps_num, uint32_t sx[MAX_COMPONENTS_NUM],
                                                  uint32_t sy[MAX_COMPONENTS_NUM]) {
    ColourFormat_t format = COLOUR_FORMAT_INVALID;
    if (comps_num == 3) {
        format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        if (sx[0] == 1 && sx[1] == 1 && sx[2] == 1 && sy[0] == 1 && sy[1] == 1 && sy[2] == 1) {
            format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        }
        else if (sx[0] == 1 && sx[1] == 2 && sx[2] == 2 && sy[0] == 1 && sy[1] == 1 && sy[2] == 1) {
            format = COLOUR_FORMAT_PLANAR_YUV422;
        }
        else if (sx[0] == 1 && sx[1] == 2 && sx[2] == 2 && sy[0] == 1 && sy[1] == 2 && sy[2] == 2) {
            format = COLOUR_FORMAT_PLANAR_YUV420;
        }
        else {
            format = COLOUR_FORMAT_INVALID;
        }
    }
    else if (comps_num == 4) {
        format = COLOUR_FORMAT_PLANAR_4_COMPONENTS;
    }
    else if (comps_num == 1) {
        format = COLOUR_FORMAT_GRAY;
    }
    else {
        format = COLOUR_FORMAT_INVALID;
    }
    return format;
}

SvtJxsErrorType_t svt_jpeg_xs_dec_init_common(svt_jpeg_xs_decoder_common_t* dec_common,
                                              svt_jpeg_xs_image_config_t* out_image_config, proxy_mode_t proxy_mode,
                                              uint32_t verbose) {
    SvtJxsErrorType_t ret = pi_compute(
        &dec_common->pi, //TODO: Update if required
        0 /*Init decoder*/,
        dec_common->picture_header_const.hdr_comps_num,
        dec_common->picture_header_const.hdr_coeff_group_size,
        dec_common->picture_header_const.hdr_significance_group_size,
        dec_common->picture_header_const.hdr_width,
        dec_common->picture_header_const.hdr_height,
        dec_common->picture_header_const.hdr_decom_h,
        dec_common->picture_header_const.hdr_decom_v,
        dec_common->picture_header_const.hdr_Sd,
        dec_common->picture_header_const.hdr_Sx,
        dec_common->picture_header_const.hdr_Sy,
        dec_common->picture_header_const.hdr_precinct_width,
        dec_common->picture_header_const.hdr_Hsl * (1 << dec_common->picture_header_const.hdr_decom_v));

    if (ret) {
        return ret;
    }

    //TODO: For multithreading separate PI and Gain Tables!!
    copy_weights_table(&dec_common->pi, &dec_common->picture_header_const); //TODO: Update in any frame

    /*TODO: Dump PI in Verbose mode
    pi_dump(&ctx->pi);
    */
    ret = pi_update_proxy_mode(&dec_common->pi, proxy_mode, verbose);
    if (ret) {
        return ret;
    }

    if (dec_common->picture_header_const.hdr_Cpih) {
        for (uint32_t c = 0; c < dec_common->pi.comps_num; c++) {
            SVT_MALLOC(dec_common->buffer_tmp_cpih[c], dec_common->pi.width * dec_common->pi.height * sizeof(int32_t));
        }
    }

    if (out_image_config) {
        pi_t* pi = &dec_common->pi;
        out_image_config->width = pi->width;
        out_image_config->height = pi->height;
        out_image_config->components_num = pi->comps_num;
        out_image_config->bit_depth = dec_common->picture_header_const.hdr_bit_depth[0];

        for (int32_t c = 0; c < out_image_config->components_num; c++) {
            out_image_config->components[c].width = pi->components[c].width;
            out_image_config->components[c].height = pi->components[c].height;
            uint32_t pixel_size = out_image_config->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
            out_image_config->components[c].byte_size = out_image_config->components[c].width *
                out_image_config->components[c].height * pixel_size;
        }

        out_image_config->format = svt_jpeg_xs_get_format_from_params(
            dec_common->pi.comps_num, dec_common->picture_header_const.hdr_Sx, dec_common->picture_header_const.hdr_Sy);
        if (out_image_config->format == COLOUR_FORMAT_INVALID) {
            return SvtJxsErrorDecoderInvalidBitstream;
        }
    }
    return SvtJxsErrorNone;
}

svt_jpeg_xs_decoder_instance_t* svt_jpeg_xs_dec_instance_alloc(svt_jpeg_xs_decoder_common_t* dec_common) {
    svt_jpeg_xs_decoder_instance_t* ctx;

    SVT_NO_THROW_CALLOC(ctx, 1, sizeof(svt_jpeg_xs_decoder_instance_t));
    if (!ctx) {
        return NULL;
    }

    ctx->dec_common = dec_common;
    pi_t* pi = &dec_common->pi;
    int ret = 0;

    ctx->precincts_line_coeff_size = 0;
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        ctx->precincts_line_coeff_comp_offset[c] = ctx->precincts_line_coeff_size;
        for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
            ctx->precincts_line_coeff_size += pi->components[c].bands[b].width * pi->components[c].bands[b].height_lines_num;
        }
    }

    uint32_t frame_coeff_size = ctx->precincts_line_coeff_size * pi->precincts_line_num;

    SVT_NO_THROW_MALLOC(ctx->coeff_buff_ptr_16bit, frame_coeff_size * sizeof(int16_t));
    if (!ctx->coeff_buff_ptr_16bit) {
        ret |= 1;
    }

    if (pi->decom_v == 0) {
        SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer, 1 * pi->width * sizeof(int32_t));
        SVT_NO_THROW_MALLOC(ctx->precinct_component_tmp_buffer, 1 * pi->width * sizeof(int32_t));
    }
    else if (pi->decom_v == 1) {
        SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer, 3 * pi->width * sizeof(int32_t));
        SVT_NO_THROW_MALLOC(ctx->precinct_component_tmp_buffer, 4 * pi->width * sizeof(int32_t));
    }
    else { // pi->.decom_v == 2
        uint32_t V1_len = (pi->width / 2) + (pi->width & 1);
        SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer, (7 * V1_len + 4 * pi->width) * sizeof(int32_t)); // ~7.5 * pi->width
        SVT_NO_THROW_MALLOC(ctx->precinct_component_tmp_buffer, 8 * pi->width * sizeof(int32_t));
    }

    if (!ctx->precinct_idwt_tmp_buffer || !ctx->precinct_component_tmp_buffer) {
        ret |= 1;
    }

    if (!ret) {
        SVT_NO_THROW_MALLOC(ctx->map_slices_decode_done, pi->slice_num * sizeof(CondVar));
        if (!ctx->map_slices_decode_done) {
            ret |= 1;
        }
        else {
            for (uint32_t slice_idx = 0; slice_idx < pi->slice_num; slice_idx++) {
                ret = svt_jxs_create_cond_var(&ctx->map_slices_decode_done[slice_idx]);
                if (ret) {
                    /*When any error, destroy all previous created Condition Variables*/
                    for (uint32_t i = 0; i < slice_idx; i++) {
                        svt_jxs_free_cond_var(&ctx->map_slices_decode_done[slice_idx]);
                    }
                    SVT_FREE(ctx->map_slices_decode_done);
                    break;
                }
            }
        }
    }

    if (dec_common->max_frame_bitstream_size) {
        SVT_NO_THROW_MALLOC(ctx->frame_bitstream_ptr, dec_common->max_frame_bitstream_size * sizeof(uint8_t));
        if (!ctx->frame_bitstream_ptr) {
            ret |= 1;
        }
    }

    if (ret) {
        svt_jpeg_xs_dec_instance_free(ctx);
        return NULL;
    }

    return ctx;
}

void svt_jpeg_xs_dec_instance_free(svt_jpeg_xs_decoder_instance_t* ctx) {
    if (!ctx) {
        return;
    }
    SVT_FREE(ctx->coeff_buff_ptr_16bit);
    SVT_FREE(ctx->precinct_idwt_tmp_buffer);
    SVT_FREE(ctx->precinct_component_tmp_buffer);

    if (ctx->map_slices_decode_done) {
        for (uint32_t slice_idx = 0; slice_idx < ctx->dec_common->pi.slice_num; slice_idx++) {
            svt_jxs_free_cond_var(&ctx->map_slices_decode_done[slice_idx]);
        }
    }
    SVT_FREE(ctx->map_slices_decode_done);
    SVT_FREE(ctx->frame_bitstream_ptr);
    SVT_FREE(ctx);
}

svt_jpeg_xs_decoder_thread_context* svt_jpeg_xs_dec_thread_context_alloc(pi_t* pi) {
    svt_jpeg_xs_decoder_thread_context* ctx;

    SVT_NO_THROW_CALLOC(ctx, 1, sizeof(svt_jpeg_xs_decoder_thread_context));
    if (!ctx) {
        return NULL;
    }

    int ret = 0;

    //IDWT per precinct support
    if (pi->decom_v == 0) {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            ctx->precinct_components_tmp_buffer[c] = NULL;
            ctx->precinct_idwt_tmp_buffer[c] = NULL;
        }
        SVT_NO_THROW_MALLOC(ctx->precinct_components_tmp_buffer[0], pi->width * sizeof(int32_t));
        SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer[0], pi->width * sizeof(int32_t));

        if (!ctx->precinct_components_tmp_buffer[0] || !ctx->precinct_idwt_tmp_buffer[0]) {
            ret |= 1;
        }
    }
    else {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            if (pi->components[c].decom_v == 0) {
                SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer[c], pi->components[c].width * sizeof(int32_t));
                SVT_NO_THROW_MALLOC(ctx->precinct_components_tmp_buffer[c], pi->components[c].width * sizeof(int32_t));
            }
            else if (pi->components[c].decom_v == 1) {
                SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer[c], 3 * pi->components[c].width * sizeof(int32_t));
                SVT_NO_THROW_MALLOC(ctx->precinct_components_tmp_buffer[c], 4 * pi->components[c].width * sizeof(int32_t));
            }
            else { // pi->components[c].decom_v == 2
                uint32_t V1_len = (pi->components[c].width / 2) + (pi->components[c].width & 1);
                SVT_NO_THROW_MALLOC(ctx->precinct_idwt_tmp_buffer[c],
                                    (7 * V1_len + 3 * pi->components[c].width) * sizeof(int32_t)); // ~6.5 * component->width
                SVT_NO_THROW_MALLOC(ctx->precinct_components_tmp_buffer[c], 8 * pi->components[c].width * sizeof(int32_t));
            }
            if (!ctx->precinct_components_tmp_buffer[c] || !ctx->precinct_idwt_tmp_buffer[c]) {
                ret |= 1;
                break;
            }
        }
    }
    //END IDWT per precinct support

    if (pi->precincts_col_num >= MAX_PRECINCT_IN_LINE) {
        //maximum number of precincts per line exceeded
        ret |= 1;
    }
    if (!ret) {
        precinct_info_t* precinct_normal = &pi->p_info[PRECINCT_NORMAL];
        for (uint32_t s = 0; s < pi->precincts_col_num + 1; s++) {
            precinct_t* p;
            SVT_NO_THROW_CALLOC(p, 1, sizeof(precinct_t));
            if (!p) {
                ret |= 1;
                break;
            }
            ctx->precincts_top[s] = p;
            for (uint32_t c = 0; c < pi->comps_num; c++) {
                for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
                    uint32_t height_lines_num = pi->components[c].bands[b].height_lines_num;
                    assert(height_lines_num == precinct_normal->b_info[c][b].height);
                    uint32_t gcli_data_size = precinct_normal->b_info[c][b].gcli_width * height_lines_num;
                    SVT_NO_THROW_MALLOC(p->bands[c][b].gcli_data, sizeof(int8_t) * (gcli_data_size));
                    if (!p->bands[c][b].gcli_data) {
                        ret |= 1;
                        break;
                    }
                    uint32_t sig_data_size = precinct_normal->b_info[c][b].significance_width * height_lines_num;
                    SVT_NO_THROW_MALLOC(p->bands[c][b].significance_data, sizeof(uint8_t) * (sig_data_size));
                    if (!p->bands[c][b].significance_data) {
                        ret |= 1;
                        break;
                    }
                }
            }
        }
    }

    if (ret) {
        svt_jpeg_xs_dec_thread_context_free(ctx, pi);
        return NULL;
    }

    return ctx;
}

void svt_jpeg_xs_dec_thread_context_free(svt_jpeg_xs_decoder_thread_context* ctx, pi_t* pi) {
    if (!ctx) {
        return;
    }

    //IDWT per precinct support
    if (pi->decom_v == 0) {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            SVT_FREE(ctx->precinct_components_tmp_buffer[c]);
        }
        SVT_FREE(ctx->precinct_idwt_tmp_buffer[0]);
    }
    else {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            SVT_FREE(ctx->precinct_components_tmp_buffer[c]);
            SVT_FREE(ctx->precinct_idwt_tmp_buffer[c]);
        }
    }
    //END IDWT per precinct support

    for (uint32_t s = 0; s < MIN(pi->precincts_col_num + 1, MAX_PRECINCT_IN_LINE); s++) {
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
                if (ctx->precincts_top[s]) {
                    if (ctx->precincts_top[s]->bands[c][b].gcli_data) {
                        SVT_FREE(ctx->precincts_top[s]->bands[c][b].gcli_data);
                    }
                    if (ctx->precincts_top[s]->bands[c][b].significance_data) {
                        SVT_FREE(ctx->precincts_top[s]->bands[c][b].significance_data);
                    }
                }
            }
        }
        SVT_FREE(ctx->precincts_top[s]);
    }

    SVT_FREE(ctx);
}

//Parse header and return size header, or return ERROR
SvtJxsErrorType_t svt_jpeg_xs_decode_header(svt_jpeg_xs_decoder_instance_t* ctx, const uint8_t* bitstream_buf,
                                            size_t bitstream_buf_size, uint32_t* out_header_size, uint32_t verbose) {
    bitstream_reader_t bitstream;
    bitstream_reader_init(&bitstream, bitstream_buf, bitstream_buf_size);

    picture_header_const_t picture_header_const_tmp = {0};
    SvtJxsErrorType_t ret = get_header(&bitstream, &picture_header_const_tmp, &ctx->picture_header_dynamic, verbose);
    if (ret) {
        return ret;
    }

    if (memcmp(&picture_header_const_tmp, &ctx->dec_common->picture_header_const, sizeof(picture_header_const_t))) {
        return SvtJxsErrorDecoderConfigChange;
    }
    //TODO: For multithreading separate PI and Gain Tables!!
    //copy_weights_table(pi, &ctx->picture_header_dynamic);

    *out_header_size = bitstream_reader_get_used_bytes(&bitstream);

    return SvtJxsErrorNone;
}

void decoder_get_precinct_bands_pointers(const pi_t* pi, svt_jpeg_xs_decoder_instance_t* ctx,
                                         int16_t* precicnt_bands_ptr[MAX_BANDS_PER_COMPONENT_NUM], uint32_t comp,
                                         uint32_t precinct_line_idx) {
    int16_t* bands_offset = ctx->coeff_buff_ptr_16bit + precinct_line_idx * ctx->precincts_line_coeff_size +
        ctx->precincts_line_coeff_comp_offset[comp];
    for (uint32_t b = 0; b < pi->components[comp].bands_num; b++) {
        precicnt_bands_ptr[b] = bands_offset;
        bands_offset += pi->components[comp].bands[b].width * pi->components[comp].bands[b].height_lines_num;
    }
}

void transform_precinct_initialize(const pi_t* pi, svt_jpeg_xs_decoder_instance_t* ctx, uint32_t c, uint32_t precinct_line_idx,
                                   int32_t* precinct_components_tmp_buffer, int32_t* precinct_idwt_tmp_buffer, uint8_t shift) {
    int16_t* buff_in_prev_2[MAX_BANDS_PER_COMPONENT_NUM] = {0};
    int16_t* buff_in_prev_1[MAX_BANDS_PER_COMPONENT_NUM] = {0};
    uint32_t width = pi->components[c].width;
    transform_lines_t out_lines;
    memset(&out_lines, 0, sizeof(transform_lines_t));

    if (pi->components[c].decom_v == 0) {
        out_lines.buffer_out[0] = precinct_components_tmp_buffer;
    }
    else if (pi->components[c].decom_v == 1) {
        out_lines.buffer_out[0] = precinct_components_tmp_buffer + ((2 * precinct_line_idx + 0) % 4) * width;
    }
    else { // pi->components[c].decom_v == 2
        out_lines.buffer_out[0] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 0) % 8) * width;
    }

    if (precinct_line_idx > 1) {
        decoder_get_precinct_bands_pointers(pi, ctx, buff_in_prev_2, c, (precinct_line_idx - 2));
    }
    if (precinct_line_idx > 0) {
        decoder_get_precinct_bands_pointers(pi, ctx, buff_in_prev_1, c, (precinct_line_idx - 1));
    }

    new_transform_component_line_recalc(&pi->components[c],
                                        buff_in_prev_2,
                                        buff_in_prev_1,
                                        out_lines.buffer_out,
                                        precinct_line_idx,
                                        precinct_idwt_tmp_buffer,
                                        shift);
}

void transform_precinct(const pi_t* pi, svt_jpeg_xs_decoder_instance_t* ctx, uint32_t c, uint32_t precinct_line_idx,
                        int32_t* precinct_components_tmp_buffer, int32_t* precinct_idwt_tmp_buffer,
                        svt_jpeg_xs_image_buffer_t* out, uint8_t shift) {
    int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM] = {0};
    int16_t* buff_in_prev[MAX_BANDS_PER_COMPONENT_NUM] = {0};
    uint32_t width = pi->components[c].width;
    int32_t component_line_idx = precinct_line_idx * pi->components[c].precinct_height;

    transform_lines_t out_lines;
    memset(&out_lines, 0, sizeof(transform_lines_t));

    if (pi->components[c].decom_v == 0) {
        out_lines.buffer_out[0] = precinct_components_tmp_buffer;
    }
    else if (pi->components[c].decom_v == 1) {
        out_lines.buffer_out[0] = precinct_components_tmp_buffer + ((2 * precinct_line_idx + 0) % 4) * width;
        out_lines.buffer_out[1] = precinct_components_tmp_buffer + ((2 * precinct_line_idx + 1) % 4) * width;
        out_lines.buffer_out[2] = precinct_components_tmp_buffer + ((2 * precinct_line_idx + 2) % 4) * width;
        out_lines.buffer_out[3] = precinct_components_tmp_buffer + ((2 * precinct_line_idx + 3) % 4) * width;
    }
    else { // pi->components[c].decom_v == 2
        out_lines.buffer_out[0] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 0) % 8) * width;
        out_lines.buffer_out[1] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 1) % 8) * width;
        out_lines.buffer_out[2] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 2) % 8) * width;
        out_lines.buffer_out[3] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 3) % 8) * width;
        out_lines.buffer_out[4] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 4) % 8) * width;
        out_lines.buffer_out[5] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 5) % 8) * width;
        out_lines.buffer_out[6] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 6) % 8) * width;
        out_lines.buffer_out[7] = precinct_components_tmp_buffer + ((4 * precinct_line_idx + 7) % 8) * width;
    }

    decoder_get_precinct_bands_pointers(pi, ctx, buff_in, c, precinct_line_idx);
    if (precinct_line_idx > 0) {
        decoder_get_precinct_bands_pointers(pi, ctx, buff_in_prev, c, precinct_line_idx - 1);
    }

    new_transform_component_line(&pi->components[c],
                                 buff_in,
                                 buff_in_prev,
                                 &out_lines,
                                 precinct_line_idx,
                                 precinct_idwt_tmp_buffer,
                                 pi->precincts_line_num,
                                 shift);

    component_line_idx += out_lines.offset;

    for (uint32_t line = out_lines.line_start; line <= out_lines.line_stop; line++) {
        int32_t* in = out_lines.buffer_out[line];
        void* out_buf = out->data_yuv[c];
        uint32_t out_stride = out->stride[c];
        uint8_t bit_depth = ctx->dec_common->picture_header_const.hdr_bit_depth[0];
        if (bit_depth == 8) {
            uint8_t* out_buf_8 = ((uint8_t*)out_buf) + component_line_idx * out_stride;
            nlt_inverse_transform_line_8bit(in, bit_depth, &ctx->picture_header_dynamic, out_buf_8, width);
        }
        else {
            uint16_t* out_buf_16 = ((uint16_t*)out_buf) + component_line_idx * out_stride;
            nlt_inverse_transform_line_16bit(in, bit_depth, &ctx->picture_header_dynamic, out_buf_16, width);
        }
        component_line_idx++;
    }
}

//Return size of used bitstream or return ERROR
SvtJxsErrorType_t svt_jpeg_xs_decode_slice(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_decoder_thread_context* thread_ctx,
                                           const uint8_t* bitstream_buf, size_t bitstream_buf_size, uint32_t slice,
                                           uint32_t* out_slice_size, svt_jpeg_xs_image_buffer_t* out, uint32_t verbose) {
    SvtJxsErrorType_t ret = SvtJxsErrorNone;

    bitstream_reader_t bitstream;
    bitstream_reader_init(&bitstream, bitstream_buf, bitstream_buf_size);

    pi_t* pi = &ctx->dec_common->pi;
    picture_header_dynamic_t* picture_header_dynamic = &ctx->picture_header_dynamic;

    uint32_t lines_per_slice_last = pi->precincts_line_num - (pi->slice_num - 1) * pi->precincts_per_slice;
    uint16_t slice_idx = 0;

    ret = get_slice_header(&bitstream, &slice_idx);
    if (ret) {
        return ret;
    }
    if (slice_idx != slice) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Error: (slice index) corruption detected  index=%d , expected=%d\n", slice_idx, slice);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    const uint32_t is_last_slice = (slice == (pi->slice_num - 1));
    const uint32_t lines_per_slice = is_last_slice ? lines_per_slice_last : pi->precincts_per_slice;

    for (uint32_t line = 0; line < lines_per_slice; line++) {
        const uint32_t precinct_line_idx = slice * pi->precincts_per_slice + line;
        for (uint32_t column = 0; column < pi->precincts_col_num; column++) {
            precinct_t* precinct = thread_ctx->precincts_top[pi->precincts_col_num];
            precinct_t* precincts_top = NULL;
            if (line != 0) {
                precincts_top = thread_ctx->precincts_top[column];
            }

            const uint32_t is_last_line = (line == (lines_per_slice - 1));
            const uint32_t is_last_column = (column == (pi->precincts_col_num - 1));

            if (is_last_slice && is_last_line && is_last_column) {
                precinct->p_info = &pi->p_info[PRECINCT_LAST];
            }
            else if (is_last_slice && is_last_line) {
                precinct->p_info = &pi->p_info[PRECINCT_LAST_NORMAL];
            }
            else if (is_last_column) {
                precinct->p_info = &pi->p_info[PRECINCT_NORMAL_LAST];
            }
            else {
                precinct->p_info = &pi->p_info[PRECINCT_NORMAL];
            }
            for (uint32_t c = 0; c < pi->comps_num; c++) {
                int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM];
                decoder_get_precinct_bands_pointers(pi, ctx, buff_in, c, precinct_line_idx);

                for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
                    uint32_t x_pos = column * pi->p_info[PRECINCT_NORMAL].b_info[c][b].width;
                    precinct->bands[c][b].coeff_data = (uint16_t*)buff_in[b] + x_pos;
                }
            }

            ret = unpack_precinct(&bitstream, precinct, precincts_top, pi, picture_header_dynamic, verbose);
            if (ret) {
                return ret;
            }

            inv_precinct_calculate_data(precinct, pi, picture_header_dynamic->hdr_Qpih);

            //Swap pointers in precincts_top
            thread_ctx->precincts_top[pi->precincts_col_num] = thread_ctx->precincts_top[column];
            thread_ctx->precincts_top[column] = precinct;
        }

        //when 2nd line is unpacked set flag to true
        if (ctx->sync_slices_idwt && line == 1) {
            svt_jxs_set_cond_var(&ctx->map_slices_decode_done[slice], SYNC_OK);
        }

        if (ctx->dec_common->picture_header_const.hdr_Cpih) {
            continue;
        }
        /*****V0 Hx IDWT per precinct implementation**********/
        if (pi->decom_v == 0) {
            for (uint32_t c = 0; c < pi->comps_num; c++) {
                transform_precinct(pi,
                                   ctx,
                                   c,
                                   precinct_line_idx,
                                   thread_ctx->precinct_components_tmp_buffer[0],
                                   thread_ctx->precinct_idwt_tmp_buffer[0],
                                   out,
                                   picture_header_dynamic->hdr_Fq);
            }
            continue;
        }
        /*****End V0 Hx IDWT per precinct implementation**********/

        if (lines_per_slice > 2) {
            //slice index 0 does not need skip
            if ((line < 1) && slice) {
                continue;
            }
            //slice index 0 does not need recalculation
            if ((line == 1) && slice) {
                uint32_t precint_idx_to_init = precinct_line_idx + 1;
                for (uint32_t c = 0; c < pi->comps_num; c++) {
                    transform_precinct_initialize(pi,
                                                  ctx,
                                                  c,
                                                  precint_idx_to_init,
                                                  thread_ctx->precinct_components_tmp_buffer[c],
                                                  thread_ctx->precinct_idwt_tmp_buffer[c],
                                                  picture_header_dynamic->hdr_Fq);
                }
            }
            else { // (line > 1)
                for (uint32_t c = 0; c < pi->comps_num; c++) {
                    transform_precinct(pi,
                                       ctx,
                                       c,
                                       precinct_line_idx,
                                       thread_ctx->precinct_components_tmp_buffer[c],
                                       thread_ctx->precinct_idwt_tmp_buffer[c],
                                       out,
                                       picture_header_dynamic->hdr_Fq);
                }
            }
        }
    }

    *out_slice_size = bitstream_reader_get_used_bytes(&bitstream);

    if (ctx->sync_slices_idwt && !is_last_slice && (lines_per_slice > 2)) {
        svt_jxs_wait_cond_var(&ctx->map_slices_decode_done[slice + 1], SYNC_INIT);
        if (ctx->map_slices_decode_done[slice + 1].val == SYNC_ERROR) {
            return SvtJxsErrorDecoderInternal;
        }

        for (uint32_t line = 0; line < 2; line++) {
            uint32_t precinct_line_idx = (slice + 1) * pi->precincts_per_slice + line;
            for (uint32_t c = 0; c < pi->comps_num; c++) {
                transform_precinct(pi,
                                   ctx,
                                   c,
                                   precinct_line_idx,
                                   thread_ctx->precinct_components_tmp_buffer[c],
                                   thread_ctx->precinct_idwt_tmp_buffer[c],
                                   out,
                                   picture_header_dynamic->hdr_Fq);
            }
        }
    }

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t svt_jpeg_xs_decode_final_slice_overlap(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_image_buffer_t* out,
                                                         uint32_t slice_idx) {
    pi_t* pi = &ctx->dec_common->pi;

    //First slice does not require recalculation
    // 0 vertical decomposition case is done in slice-thread
    if ((slice_idx == 0 && pi->precincts_per_slice > 2) || (pi->decom_v == 0)) {
        return SvtJxsErrorNone;
    }

    uint32_t precincts_per_slice_last = pi->precincts_line_num - (pi->slice_num - 1) * pi->precincts_per_slice;
    // Number of "precincts" lines in one slice
    //for each component
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        uint32_t precinct_line_idx = slice_idx * pi->precincts_per_slice;
        transform_precinct_initialize(pi,
                                      ctx,
                                      c,
                                      precinct_line_idx,
                                      ctx->precinct_component_tmp_buffer,
                                      ctx->precinct_idwt_tmp_buffer,
                                      ctx->picture_header_dynamic.hdr_Fq);

        uint32_t precincts_to_calculate = 2;
        if (slice_idx == (pi->slice_num - 1)) {
            precincts_to_calculate = MIN(precincts_to_calculate, precincts_per_slice_last);
        }
        for (uint32_t precinct = 0; precinct < precincts_to_calculate; precinct++) {
            transform_precinct(pi,
                               ctx,
                               c,
                               (precinct_line_idx + precinct),
                               ctx->precinct_component_tmp_buffer,
                               ctx->precinct_idwt_tmp_buffer,
                               out,
                               ctx->picture_header_dynamic.hdr_Fq);
        }
    }

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t svt_jpeg_xs_decode_final(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_image_buffer_t* out) {
    pi_t* pi = &ctx->dec_common->pi;
    picture_header_dynamic_t* picture_header_dynamic = &ctx->picture_header_dynamic;

    if (ctx->dec_common->picture_header_const.hdr_Cpih == 0) {
        for (uint32_t comp_id = 0; comp_id < pi->comps_num; ++comp_id) {
            //for each precinct in component
            for (uint32_t precinct_idx = 0; precinct_idx < pi->precincts_line_num; precinct_idx++) {
                transform_precinct(pi,
                                   ctx,
                                   comp_id,
                                   precinct_idx,
                                   ctx->precinct_component_tmp_buffer,
                                   ctx->precinct_idwt_tmp_buffer,
                                   out,
                                   ctx->picture_header_dynamic.hdr_Fq);
            }
        }
    }
    else {
        //for each component
        for (uint32_t comp_id = 0; comp_id < (pi->comps_num - pi->Sd); ++comp_id) {
            //for each precinct in component
            for (uint32_t precinct_idx = 0; precinct_idx < pi->precincts_line_num; precinct_idx++) {
                int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                int16_t* buff_in_prev[MAX_BANDS_PER_COMPONENT_NUM] = {0};

                decoder_get_precinct_bands_pointers(pi, ctx, buff_in, comp_id, precinct_idx);
                if (precinct_idx > 0) {
                    decoder_get_precinct_bands_pointers(pi, ctx, buff_in_prev, comp_id, precinct_idx - 1);
                }

                uint32_t width = pi->components[comp_id].width;
                int32_t* buff_out = ctx->dec_common->buffer_tmp_cpih[comp_id] +
                    precinct_idx * pi->components[comp_id].precinct_height * width;
                transform_lines_t out_lines;
                memset(&out_lines, 0, sizeof(transform_lines_t));

                if (pi->components[comp_id].decom_v == 0) {
                    out_lines.buffer_out[0] = buff_out;
                }
                else if (pi->components[comp_id].decom_v == 1) {
                    out_lines.buffer_out[0] = buff_out - 2 * width;
                    out_lines.buffer_out[1] = buff_out - 1 * width;
                    out_lines.buffer_out[2] = buff_out + 0 * width;
                    out_lines.buffer_out[3] = buff_out + 1 * width;
                }
                else { //(pi->components[comp_id].decom_v == 2)
                    out_lines.buffer_out[0] = buff_out - 4 * width;
                    out_lines.buffer_out[1] = buff_out - 3 * width;
                    out_lines.buffer_out[2] = buff_out - 2 * width;
                    out_lines.buffer_out[3] = buff_out - 1 * width;
                    out_lines.buffer_out[4] = buff_out + 0 * width;
                    out_lines.buffer_out[5] = buff_out + 1 * width;
                    out_lines.buffer_out[6] = buff_out + 2 * width;
                    out_lines.buffer_out[7] = buff_out + 3 * width;
                }
                new_transform_component_line(&pi->components[comp_id],
                                             buff_in,
                                             buff_in_prev,
                                             &out_lines,
                                             precinct_idx,
                                             ctx->precinct_idwt_tmp_buffer,
                                             pi->precincts_line_num,
                                             ctx->picture_header_dynamic.hdr_Fq);
            }
        }
        for (uint32_t comp_id = (pi->comps_num - pi->Sd); comp_id < pi->comps_num; comp_id++) {
            for (uint32_t precinct_idx = 0; precinct_idx < pi->precincts_line_num; precinct_idx++) {
                int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM] = {0};
                decoder_get_precinct_bands_pointers(pi, ctx, buff_in, comp_id, precinct_idx);
                uint32_t width = pi->components[comp_id].width;
                uint32_t component_precinct_height = pi->components[comp_id].precinct_height;
                int32_t* buff_out = ctx->dec_common->buffer_tmp_cpih[comp_id] + precinct_idx * component_precinct_height * width;

                if (pi->decom_v && precinct_idx == (pi->precincts_line_num - 1)) {
                    component_precinct_height = pi->components[comp_id].height % pi->components[comp_id].precinct_height;
                }

                for (uint32_t idx = 0; idx < component_precinct_height * width; idx++) {
                    buff_out[idx] = (int32_t)(buff_in[0][idx]) << ctx->picture_header_dynamic.hdr_Fq;
                }
            }
        }

        mct_inverse_transform(
            ctx->dec_common->buffer_tmp_cpih, pi, picture_header_dynamic, ctx->dec_common->picture_header_const.hdr_Cpih);
        nlt_inverse_transform(
            ctx->dec_common->buffer_tmp_cpih, pi, &ctx->dec_common->picture_header_const, picture_header_dynamic, out);
    }
    return SvtJxsErrorNone;
}
