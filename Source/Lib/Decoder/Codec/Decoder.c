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
#include "SvtLog.h"

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
        if (sx[0] == 1 && sx[1] == 1 && sx[2] == 1 && sx[3] == 1 && sy[0] == 1 && sy[1] == 1 && sy[2] == 1 && sy[3] == 1) {
            format = COLOUR_FORMAT_PLANAR_4_COMPONENTS; //4:4:4:4 - RGBA/GBRA/YUVA444, all components full resolution
        }
        else if (sx[0] == 1 && sx[1] == 2 && sx[2] == 2 && sx[3] == 1 && sy[0] == 1 && sy[1] == 1 && sy[2] == 1 && sy[3] == 1) {
            format =
                COLOUR_FORMAT_PLANAR_YUV422_ALPHA; //4:2:2:4 - Y/Cb/Cr at 4:2:2 (comp 1,2 half horizontal res), alpha full res
        }
        else {
            format = COLOUR_FORMAT_INVALID; //Unrecognized 4-component sampling shape
        }
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

    dec_common->rct_per_precinct = 0;
    if (dec_common->picture_header_const.hdr_Cpih == 1) {
        // Per-line RCT needs components 0-2 to be wavelet transformed and to share the frame geometry,
        // so that every precinct produces the same output lines for all three of them.
        const pi_t* pi = &dec_common->pi;
        dec_common->rct_per_precinct = (pi->comps_num >= 3) && (pi->comps_num - pi->Sd >= 3);
        for (uint32_t c = 0; c < 3 && dec_common->rct_per_precinct; c++) {
            const pi_component_t* comp = &pi->components[c];
            if (comp->width != pi->width || comp->height != pi->height || comp->decom_h != pi->components[0].decom_h ||
                comp->decom_v != pi->components[0].decom_v || comp->precinct_height != pi->components[0].precinct_height) {
                dec_common->rct_per_precinct = 0;
            }
        }
    }

    if (dec_common->picture_header_const.hdr_Cpih && !dec_common->rct_per_precinct) {
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

    // Zero-initialize: IDWT may read padding/boundary elements before they are written
    SVT_NO_THROW_CALLOC(ctx->coeff_buff_ptr_16bit, 1, frame_coeff_size * sizeof(int16_t));
    if (!ctx->coeff_buff_ptr_16bit) {
        ret |= 1;
    }

    // Zero-initialize: IDWT temp buffers may be partially read at slice boundaries before full write
    // The per-precinct RCT path transforms the components precinct by precinct, so each needs its own buffers.
    const uint32_t buffers_num = dec_common->rct_per_precinct ? pi->comps_num : 1;
    for (uint32_t c = 0; c < buffers_num; c++) {
        if (pi->decom_v == 0) {
            SVT_NO_THROW_CALLOC(ctx->precinct_idwt_tmp_buffer[c], 1, 1 * pi->width * sizeof(int32_t));
            SVT_NO_THROW_CALLOC(ctx->precinct_component_tmp_buffer[c], 1, 1 * pi->width * sizeof(int32_t));
        }
        else if (pi->decom_v == 1) {
            SVT_NO_THROW_CALLOC(ctx->precinct_idwt_tmp_buffer[c], 1, 3 * pi->width * sizeof(int32_t));
            SVT_NO_THROW_CALLOC(ctx->precinct_component_tmp_buffer[c], 1, 4 * pi->width * sizeof(int32_t));
        }
        else { // pi->.decom_v == 2
            uint32_t V1_len = (pi->width / 2) + (pi->width & 1);
            SVT_NO_THROW_CALLOC(
                ctx->precinct_idwt_tmp_buffer[c], 1, (7 * V1_len + 4 * pi->width) * sizeof(int32_t)); // ~7.5 * pi->width
            SVT_NO_THROW_CALLOC(ctx->precinct_component_tmp_buffer[c], 1, 8 * pi->width * sizeof(int32_t));
        }

        if (!ctx->precinct_idwt_tmp_buffer[c] || !ctx->precinct_component_tmp_buffer[c]) {
            ret |= 1;
        }
    }
    if (dec_common->rct_per_precinct) {
        SVT_NO_THROW_MALLOC(ctx->rct_tmp_buffer, 3 * pi->width * sizeof(int32_t));
        if (!ctx->rct_tmp_buffer) {
            ret |= 1;
        }
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
                ctx->map_slices_decode_done[slice_idx].timed_wait = 1;
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
    for (uint32_t c = 0; c < MAX_COMPONENTS_NUM; c++) {
        SVT_FREE(ctx->precinct_idwt_tmp_buffer[c]);
        SVT_FREE(ctx->precinct_component_tmp_buffer[c]);
    }
    SVT_FREE(ctx->rct_tmp_buffer);

    if (ctx->map_slices_decode_done) {
        for (uint32_t slice_idx = 0; slice_idx < ctx->dec_common->pi.slice_num; slice_idx++) {
            svt_jxs_free_cond_var(&ctx->map_slices_decode_done[slice_idx]);
        }
    }
    SVT_FREE(ctx->map_slices_decode_done);
    SVT_FREE(ctx->frame_bitstream_ptr);
    SVT_FREE(ctx);
}

svt_jpeg_xs_decoder_thread_context* svt_jpeg_xs_dec_thread_context_alloc(svt_jpeg_xs_decoder_common_t* dec_common) {
    svt_jpeg_xs_decoder_thread_context* ctx;
    pi_t* pi = &dec_common->pi;

    SVT_NO_THROW_CALLOC(ctx, 1, sizeof(svt_jpeg_xs_decoder_thread_context));
    if (!ctx) {
        return NULL;
    }

    int ret = 0;

    //IDWT per precinct support
    // Zero-initialize: IDWT temp buffers may be partially read at slice boundaries before full write
    // Separate buffers per component also with decom_v == 0: the per-precinct RCT keeps the IDWT
    // output lines of components 0-2 alive at the same time.
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        if (pi->components[c].decom_v == 0) {
            SVT_NO_THROW_CALLOC(ctx->precinct_idwt_tmp_buffer[c], 1, pi->components[c].width * sizeof(int32_t));
            SVT_NO_THROW_CALLOC(ctx->precinct_components_tmp_buffer[c], 1, pi->components[c].width * sizeof(int32_t));
        }
        else if (pi->components[c].decom_v == 1) {
            SVT_NO_THROW_CALLOC(ctx->precinct_idwt_tmp_buffer[c], 1, 3 * pi->components[c].width * sizeof(int32_t));
            SVT_NO_THROW_CALLOC(ctx->precinct_components_tmp_buffer[c], 1, 4 * pi->components[c].width * sizeof(int32_t));
        }
        else { // pi->components[c].decom_v == 2
            uint32_t V1_len = (pi->components[c].width / 2) + (pi->components[c].width & 1);
            SVT_NO_THROW_CALLOC(ctx->precinct_idwt_tmp_buffer[c],
                                1,
                                (7 * V1_len + 3 * pi->components[c].width) * sizeof(int32_t)); // ~6.5 * component->width
            SVT_NO_THROW_CALLOC(ctx->precinct_components_tmp_buffer[c], 1, 8 * pi->components[c].width * sizeof(int32_t));
        }
        if (!ctx->precinct_components_tmp_buffer[c] || !ctx->precinct_idwt_tmp_buffer[c]) {
            ret |= 1;
            break;
        }
    }
    if (!ret && dec_common->rct_per_precinct) {
        SVT_NO_THROW_MALLOC(ctx->rct_tmp_buffer, 3 * pi->width * sizeof(int32_t));
        if (!ctx->rct_tmp_buffer) {
            ret |= 1;
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
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        SVT_FREE(ctx->precinct_components_tmp_buffer[c]);
        SVT_FREE(ctx->precinct_idwt_tmp_buffer[c]);
    }
    SVT_FREE(ctx->rct_tmp_buffer);
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

/* Inverse wavelet transform of one precinct of component c. The finished lines are
 * out_lines->buffer_out[line_start..line_stop], the first one at component line
 * precinct_line_idx * precinct_height + out_lines->offset. */
static void transform_precinct_idwt(const pi_t* pi, svt_jpeg_xs_decoder_instance_t* ctx, uint32_t c, uint32_t precinct_line_idx,
                                    int32_t* precinct_components_tmp_buffer, int32_t* precinct_idwt_tmp_buffer,
                                    transform_lines_t* out_lines_ptr, uint8_t shift) {
    int16_t* buff_in[MAX_BANDS_PER_COMPONENT_NUM] = {0};
    int16_t* buff_in_prev[MAX_BANDS_PER_COMPONENT_NUM] = {0};
    uint32_t width = pi->components[c].width;

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
    *out_lines_ptr = out_lines;
}

/* Inverse NLT of one finished line of component c into line component_line_idx of the output picture.
 * All components share the bit depth of component 0, so it decides between the 8-bit and 16-bit writer. */
static void output_line(svt_jpeg_xs_decoder_instance_t* ctx, uint32_t c, int32_t* in, uint32_t component_line_idx, uint32_t width,
                        svt_jpeg_xs_image_buffer_t* out) {
    void* out_buf = out->data_yuv[c];
    uint32_t out_stride = out->stride[c];
    uint8_t bit_depth = ctx->dec_common->picture_header_const.hdr_bit_depth[0];
    if (bit_depth == 8) {
        uint8_t* out_buf_8 = ((uint8_t*)out_buf) + component_line_idx * out_stride;
        nlt_inverse_transform_line_8bit(in, bit_depth, &ctx->picture_header_dynamic, out_buf_8, width);
    }
    else {
        uint16_t* out_buf_16 = ((uint16_t*)out_buf) + component_line_idx * out_stride;
        nlt_inverse_transform_line_16bit(
            in, bit_depth, &ctx->picture_header_dynamic, out_buf_16, width, ctx->dec_common->output_bit_depth_msb_aligned);
    }
}

void transform_precinct(const pi_t* pi, svt_jpeg_xs_decoder_instance_t* ctx, uint32_t c, uint32_t precinct_line_idx,
                        int32_t* precinct_components_tmp_buffer, int32_t* precinct_idwt_tmp_buffer,
                        svt_jpeg_xs_image_buffer_t* out, uint8_t shift) {
    transform_lines_t out_lines;
    transform_precinct_idwt(
        pi, ctx, c, precinct_line_idx, precinct_components_tmp_buffer, precinct_idwt_tmp_buffer, &out_lines, shift);

    int32_t component_line_idx = precinct_line_idx * pi->components[c].precinct_height + out_lines.offset;
    for (uint32_t line = out_lines.line_start; line <= out_lines.line_stop; line++) {
        output_line(ctx, c, out_lines.buffer_out[line], component_line_idx, pi->components[c].width, out);
        component_line_idx++;
    }
}

/* Transform one precinct line of all components into the output picture.
 * With rct_per_precinct, components 0-2 are inverse wavelet transformed first and the
 * inverse RCT is applied to each finished line. The RCT runs on a copy because the
 * IDWT line buffers are still read by the vertical lifting of the next precinct. */
static void transform_precinct_all(const pi_t* pi, svt_jpeg_xs_decoder_instance_t* ctx, uint32_t precinct_line_idx,
                                   int32_t* precinct_components_tmp_buffer[MAX_COMPONENTS_NUM],
                                   int32_t* precinct_idwt_tmp_buffer[MAX_COMPONENTS_NUM], int32_t* rct_tmp_buffer,
                                   svt_jpeg_xs_image_buffer_t* out, uint8_t shift) {
    uint32_t c = 0;
    if (ctx->dec_common->rct_per_precinct) {
        transform_lines_t out_lines[3];
        for (c = 0; c < 3; c++) {
            transform_precinct_idwt(pi,
                                    ctx,
                                    c,
                                    precinct_line_idx,
                                    precinct_components_tmp_buffer[c],
                                    precinct_idwt_tmp_buffer[c],
                                    &out_lines[c],
                                    shift);
            //Same geometry (checked in svt_jpeg_xs_dec_init_common()) gives the same finished lines for all three
            assert(out_lines[c].line_start == out_lines[0].line_start && out_lines[c].line_stop == out_lines[0].line_stop &&
                   out_lines[c].offset == out_lines[0].offset);
        }

        const uint32_t width = pi->width;
        int32_t* rct_comps[MAX_COMPONENTS_NUM] = {rct_tmp_buffer, rct_tmp_buffer + width, rct_tmp_buffer + 2 * width};
        int32_t component_line_idx = precinct_line_idx * pi->components[0].precinct_height + out_lines[0].offset;
        for (uint32_t line = out_lines[0].line_start; line <= out_lines[0].line_stop; line++) {
            for (c = 0; c < 3; c++) {
                memcpy(rct_comps[c], out_lines[c].buffer_out[line], width * sizeof(int32_t));
            }
            inverse_rct(rct_comps, width, 1);
            for (c = 0; c < 3; c++) {
                output_line(ctx, c, rct_comps[c], component_line_idx, width, out);
            }
            component_line_idx++;
        }
    }
    for (; c < pi->comps_num; c++) {
        transform_precinct(
            pi, ctx, c, precinct_line_idx, precinct_components_tmp_buffer[c], precinct_idwt_tmp_buffer[c], out, shift);
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
            SVT_ERROR("Error: (slice index) corruption detected  index=%d , expected=%d\n", slice_idx, slice);
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

        //when 2nd line is unpacked set flag to true (a 1-line last slice signals after its only line)
        if (ctx->sync_slices_idwt && line == MIN(1, lines_per_slice - 1)) {
            svt_jxs_set_cond_var(&ctx->map_slices_decode_done[slice], SYNC_OK);
        }

        //The whole-frame colour transform path does the IDWT in svt_jpeg_xs_decode_final(), the slice only unpacks
        if (ctx->dec_common->picture_header_const.hdr_Cpih && !ctx->dec_common->rct_per_precinct) {
            continue;
        }
        /*****V0 Hx IDWT per precinct implementation**********/
        if (pi->decom_v == 0) {
            transform_precinct_all(pi,
                                   ctx,
                                   precinct_line_idx,
                                   thread_ctx->precinct_components_tmp_buffer,
                                   thread_ctx->precinct_idwt_tmp_buffer,
                                   thread_ctx->rct_tmp_buffer,
                                   out,
                                   picture_header_dynamic->hdr_Fq);
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
                transform_precinct_all(pi,
                                       ctx,
                                       precinct_line_idx,
                                       thread_ctx->precinct_components_tmp_buffer,
                                       thread_ctx->precinct_idwt_tmp_buffer,
                                       thread_ctx->rct_tmp_buffer,
                                       out,
                                       picture_header_dynamic->hdr_Fq);
            }
        }
    }

    *out_slice_size = bitstream_reader_get_used_bytes(&bitstream);

    if (ctx->sync_slices_idwt && !is_last_slice && (lines_per_slice > 2)) {
        svt_jxs_wait_cond_var(&ctx->map_slices_decode_done[slice + 1], SYNC_INIT);
        if (ctx->map_slices_decode_done[slice + 1].val == SYNC_ERROR) {
            return SvtJxsErrorDecoderInternal;
        }

        //The next slice can be the last one with fewer than 2 precinct lines; it then has only 1 to compute here
        const uint32_t next_slice_lines = ((slice + 1) == (pi->slice_num - 1)) ? lines_per_slice_last : pi->precincts_per_slice;
        for (uint32_t line = 0; line < MIN(2, next_slice_lines); line++) {
            uint32_t precinct_line_idx = (slice + 1) * pi->precincts_per_slice + line;
            transform_precinct_all(pi,
                                   ctx,
                                   precinct_line_idx,
                                   thread_ctx->precinct_components_tmp_buffer,
                                   thread_ctx->precinct_idwt_tmp_buffer,
                                   thread_ctx->rct_tmp_buffer,
                                   out,
                                   picture_header_dynamic->hdr_Fq);
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
    // The first 2 precinct lines of the slice (fewer if the slice is shorter) need the end of the previous
    // slice in the vertical IDWT, so the slice thread leaves them to be calculated here. Never go past the
    // end of the slice: the next slice may still be decoding its coefficients.
    uint32_t precinct_line_idx = slice_idx * pi->precincts_per_slice;
    uint32_t precincts_in_slice = (slice_idx == (pi->slice_num - 1)) ? precincts_per_slice_last : pi->precincts_per_slice;
    uint32_t precincts_to_calculate = MIN(2, precincts_in_slice);

    if (ctx->dec_common->rct_per_precinct) {
        // Each component has its own buffers, so all components can be transformed precinct by precinct
        for (uint32_t c = 0; c < pi->comps_num; c++) {
            transform_precinct_initialize(pi,
                                          ctx,
                                          c,
                                          precinct_line_idx,
                                          ctx->precinct_component_tmp_buffer[c],
                                          ctx->precinct_idwt_tmp_buffer[c],
                                          ctx->picture_header_dynamic.hdr_Fq);
        }
        for (uint32_t precinct = 0; precinct < precincts_to_calculate; precinct++) {
            transform_precinct_all(pi,
                                   ctx,
                                   (precinct_line_idx + precinct),
                                   ctx->precinct_component_tmp_buffer,
                                   ctx->precinct_idwt_tmp_buffer,
                                   ctx->rct_tmp_buffer,
                                   out,
                                   ctx->picture_header_dynamic.hdr_Fq);
        }
        return SvtJxsErrorNone;
    }

    //for each component
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        transform_precinct_initialize(pi,
                                      ctx,
                                      c,
                                      precinct_line_idx,
                                      ctx->precinct_component_tmp_buffer[0],
                                      ctx->precinct_idwt_tmp_buffer[0],
                                      ctx->picture_header_dynamic.hdr_Fq);

        for (uint32_t precinct = 0; precinct < precincts_to_calculate; precinct++) {
            transform_precinct(pi,
                               ctx,
                               c,
                               (precinct_line_idx + precinct),
                               ctx->precinct_component_tmp_buffer[0],
                               ctx->precinct_idwt_tmp_buffer[0],
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
                                   ctx->precinct_component_tmp_buffer[0],
                                   ctx->precinct_idwt_tmp_buffer[0],
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
                                             ctx->precinct_idwt_tmp_buffer[0],
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

                /* UBSan fix: use LSHIFT32 to avoid UB when left-shifting negative wavelet coefficients. */
                for (uint32_t idx = 0; idx < component_precinct_height * width; idx++) {
                    buff_out[idx] = LSHIFT32(buff_in[0][idx], ctx->picture_header_dynamic.hdr_Fq);
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
