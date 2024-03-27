/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "PrecinctEnc.h"
#include "PictureControlSet.h"
#include "PackIn.h"

void precinct_enc_init(struct PictureControlSet* pcs_ptr, pi_t* pi, uint32_t prec_idx, precinc_info_enum type,
                       precinct_enc_t* precinct_top, precinct_enc_t* out_precinct) {
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    pi_enc_t* pi_enc = &enc_common->pi_enc;
    precinct_info_t* p_info = &pi->p_info[type];
    out_precinct->precinct_top = precinct_top;
    out_precinct->p_info = p_info;
    out_precinct->prec_idx = prec_idx;

    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            struct band_data_enc* band = &out_precinct->bands[c][b];
#ifndef NDEBUG
            memset(band, 0xcd, sizeof(*band));
#endif
            band->gtli = UINT8_MAX; //Result for quantization
            uint32_t band_height_lines_num = out_precinct->p_info->b_info[c][b].height;
            /*To calculate offset in buffer can not get band height from last precinct.*/

            if (band_height_lines_num > 0) {
                uint32_t coeff_width = out_precinct->p_info->b_info[c][b].width;
                uint32_t gcli_width = out_precinct->p_info->b_info[c][b].gcli_width;
                uint32_t significance_width = out_precinct->p_info->b_info[c][b].significance_width;

                uint8_t* gc_tmp_buffer_level = (uint8_t*)(out_precinct->gc_buff_ptr[c]);
                uint32_t gc_rec_offset = pi_enc->components[c].bands[b].gc_buff_tmp_pos_offset;

                for (uint32_t line_idx = 0; line_idx < band_height_lines_num; ++line_idx) {
                    band->lines_common[line_idx].gcli_data_ptr = gc_tmp_buffer_level + gc_rec_offset + (line_idx)*gcli_width;

                    if (pi->components[c].decom_v == 0 || enc_common->cpu_profile == CPU_PROFILE_LOW_LATENCY) {
                        uint16_t* coeff_data_ptr_16bit = out_precinct->coeff_buff_ptr_16bit[c] +
                            pi_enc->components[c].bands[b].coeff_buff_tmp_pos_offset_16bit;
                        //Fix to H per slice, line_idx always 0
                        band->lines_common[line_idx].coeff_data_ptr_16bit = coeff_data_ptr_16bit + (line_idx)*coeff_width;
                    }
                    else {
                        //Fix to H per slice, line_idx always 0
                        uint16_t* buff_comp_precinct = pcs_ptr->coeff_buff_ptr_16bit[c] +
                            prec_idx * pi_enc->coeff_buff_tmp_size_precinct[c];
                        band->lines_common[line_idx].coeff_data_ptr_16bit = buff_comp_precinct +
                            pi_enc->components[c].bands[b].coeff_buff_tmp_pos_offset_16bit + (line_idx)*coeff_width;
                    }
                }
                for (uint32_t line_idx = band_height_lines_num; line_idx < MAX_BAND_LINES; ++line_idx) {
                    band->lines_common[line_idx].coeff_data_ptr_16bit = NULL;
                    band->lines_common[line_idx].gcli_data_ptr = NULL;
                }

                if (enc_common->coding_significance) {
                    int8_t* gc_sigflag_tmp_buffer_level = (int8_t*)(out_precinct->gc_significance_buff_ptr[c]);
                    uint32_t gc_rec_sig_offset = pi_enc->components[c].bands[b].gc_buff_tmp_significance_pos_offset;
                    for (uint32_t line_idx = 0; line_idx < band_height_lines_num; ++line_idx) {
                        band->lines_common[line_idx].significance_data_max_ptr = (uint8_t*)gc_sigflag_tmp_buffer_level +
                            +gc_rec_sig_offset + (line_idx)*significance_width;
                    }
                }
                for (uint32_t line_idx = band_height_lines_num; line_idx < MAX_BAND_LINES; ++line_idx) {
                    band->lines_common[line_idx].significance_data_max_ptr = NULL;
                }

                if (enc_common->coding_vertical_prediction_mode != METHOD_PRED_DISABLE) {
                    int8_t* vped_bit_pack_buff_ptr = (int8_t*)(out_precinct->vped_bit_pack_buff_ptr[c]);
                    uint32_t vpred_rec_bits_offset = pi_enc->components[c].bands[b].vped_bit_pack_tmp_buff_offset;
                    for (uint32_t i = 0; i < RC_BAND_CACHE_SIZE; ++i) {
                        for (uint32_t line_idx = 0; line_idx < band_height_lines_num; ++line_idx) {
                            band->cache_buffers[i].lines[line_idx].vpred_bits_pack = (uint8_t*)vped_bit_pack_buff_ptr +
                                vpred_rec_bits_offset + (line_idx)*gcli_width;
                        }
                        vpred_rec_bits_offset += gcli_width * band_height_lines_num;
                    }

                    if (enc_common->coding_significance) {
                        int8_t* vped_significance_ptr = (int8_t*)(out_precinct->vped_significance_ptr[c]);
                        uint32_t vpred_rec_sig_offset = pi_enc->components[c].bands[b].vped_significance_tmp_buff_offset;
                        for (uint32_t i = 0; i < RC_BAND_CACHE_SIZE; ++i) {
                            for (uint32_t line_tmp = 0; line_tmp < band_height_lines_num; ++line_tmp) {
                                band->cache_buffers[i].lines[line_tmp].vpred_significance = (uint8_t*)vped_significance_ptr +
                                    vpred_rec_sig_offset + (line_tmp)*significance_width;
                            }
                            vpred_rec_sig_offset += band_height_lines_num * significance_width;
                        }
                    }
                    else {
                        for (uint32_t line_tmp = 0; line_tmp < band_height_lines_num; ++line_tmp) {
                            for (uint32_t i = 0; i < RC_BAND_CACHE_SIZE; ++i) {
                                band->cache_buffers[i].lines[line_tmp].vpred_significance = NULL;
                            }
                        }
                    }
                }
                for (uint32_t line_idx = band_height_lines_num; line_idx < MAX_BAND_LINES; ++line_idx) {
                    for (uint32_t i = 0; i < RC_BAND_CACHE_SIZE; ++i) {
                        band->cache_buffers[i].lines[line_idx].vpred_bits_pack = NULL;
                        band->cache_buffers[i].lines[line_idx].vpred_significance = NULL;
                    }
                }
            }

            for (uint32_t i = 0; i < RC_BAND_CACHE_SIZE; ++i) {
                band->cache_buffers[i].gtli_rc_last_calculated = UINT8_MAX; //Set invalid value
            }
            band->cache_index = 0;
            band->cache_actual = &band->cache_buffers[band->cache_index]; //Always set some pointer even on invalid cache
        }
    }
    out_precinct->need_recalculate_next_precinct = 0;
    out_precinct->pack_signs_handling_fast_retrieve_bytes = 0;
    out_precinct->pack_signs_handling_cut_precing = 0;
}
