/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "RateControl.h"
#include "EncDec.h"
#include "PrecinctEnc.h"
#include "BinarySearch.h"
#include "PictureControlSet.h"
#include "Codestream.h"
#include "PackPrecinct.h"
#include "SvtUtility.h"
#include "encoder_dsp_rtcd.h"

#define PRINT_RECALC_VPRED 0

static void rate_control_lut_fill(svt_jpeg_xs_encoder_common_t *enc_common,
#if LUT_SIGNIFICANE
                                  uint8_t coding_significance,
#endif
#if (LUT_SIGNIFICANE && SIGN_NOZERO_COEFF_LUT) || LUT_GC_SERIAL_SUMMATION
                                  SignHandlingStrategy coding_signs_handling,
#endif
                                  precinct_enc_t *precinct) {
    pi_t *pi = &enc_common->pi;

    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            uint32_t group_conding_width = precinct->p_info->b_info[c][b].gcli_width;
            uint32_t height_lines = precinct->p_info->b_info[c][b].height;
            for (uint32_t line = 0; line < height_lines; ++line) {
                rc_cache_band_line_t *cache_line = &precinct->bands[c][b].lines_common[line].rc_cache_line;
                memset(cache_line, 0, sizeof(*cache_line));
                uint8_t *gc_data = precinct->bands[c][b].lines_common[line].gcli_data_ptr;
                uint16_t *gc_lookup_table = cache_line->gc_lookup_table;
                for (uint32_t i = 0; i < group_conding_width; i++) {
                    assert(gc_data[i] <= TRUNCATION_MAX);
                    gc_lookup_table[gc_data[i]]++;
                    assert(gc_lookup_table[gc_data[i]] != 0);
                }
#if LUT_GC_SERIAL_SUMMATION
                if (coding_signs_handling == SIGN_HANDLING_STRATEGY_OFF) {
                    uint32_t *gc_lookup_table_size_data_no_sign_handling = cache_line->gc_lookup_table_size_data_no_sign_handling;
                    gc_lookup_table_size_data_no_sign_handling[15] = 0;
                    uint32_t sum_back = 0;
                    for (int32_t gtli_index = 14; gtli_index >= 0; --gtli_index) {
                        sum_back += gc_lookup_table[gtli_index + 1];
                        gc_lookup_table_size_data_no_sign_handling[gtli_index] =
                            gc_lookup_table_size_data_no_sign_handling[gtli_index + 1] + sum_back +
                            gc_lookup_table[gtli_index + 1];
#ifndef NDEBUG
                        uint32_t sum_data = 0;
                        for (int8_t i2 = gtli_index + 1; i2 < TRUNCATION_MAX + 1; ++i2) {
                            sum_data += (i2 - gtli_index + 1) * gc_lookup_table[i2];
                        }
                        if (sum_data != gc_lookup_table_size_data_no_sign_handling[gtli_index]) {
                            printf(
                                "LUT ERROR expected %i get %i", sum_data, gc_lookup_table_size_data_no_sign_handling[gtli_index]);
                            assert(0);
                        }
#endif /*NDEBUG*/
                    }
                }
                else {
                    uint32_t *gc_lookup_table_size_data_no_sign_handling = cache_line->gc_lookup_table_size_data_no_sign_handling;
                    uint32_t *gc_lookup_table_size_data_sign_handling = cache_line->gc_lookup_table_size_data_sign_handling;

                    gc_lookup_table_size_data_no_sign_handling[15] = 0;
                    gc_lookup_table_size_data_sign_handling[15] = 0;
                    uint32_t sum_back = 0;
                    for (int32_t gtli_index = 14; gtli_index >= 0; --gtli_index) {
                        sum_back += gc_lookup_table[gtli_index + 1];
                        gc_lookup_table_size_data_no_sign_handling[gtli_index] =
                            gc_lookup_table_size_data_no_sign_handling[gtli_index + 1] + sum_back +
                            gc_lookup_table[gtli_index + 1];
                        gc_lookup_table_size_data_sign_handling[gtli_index] =
                            gc_lookup_table_size_data_sign_handling[gtli_index + 1] + sum_back;
#ifndef NDEBUG
                        uint32_t sum_data = 0;
                        for (int8_t i2 = gtli_index + 1; i2 < TRUNCATION_MAX + 1; ++i2) {
                            sum_data += (i2 - gtli_index + 1) * gc_lookup_table[i2];
                        }
                        if (sum_data != gc_lookup_table_size_data_no_sign_handling[gtli_index]) {
                            printf(
                                "LUT ERROR expected %i get %i", sum_data, gc_lookup_table_size_data_no_sign_handling[gtli_index]);
                            assert(0);
                        }
                        sum_data = 0;
                        for (int8_t i2 = gtli_index + 1; i2 < TRUNCATION_MAX + 1; ++i2) {
                            sum_data += (i2 - gtli_index) * gc_lookup_table[i2];
                        }
                        if (sum_data != gc_lookup_table_size_data_sign_handling[gtli_index]) {
                            printf("LUT ERROR expected %i get %i", sum_data, gc_lookup_table_size_data_sign_handling[gtli_index]);
                            assert(0);
                        }
#endif /*NDEBUG*/
                    }
                }
                /*Summarize changes in table*/
                uint16_t summary = 0;
                for (uint32_t i = 0; i <= TRUNCATION_MAX; ++i) {
                    gc_lookup_table[i] += summary;
                    summary = gc_lookup_table[i];
                }
#endif /*LUT_GC_SERIAL_SUMMATION*/
#if LUT_SIGNIFICANE
                if (coding_significance) {
                    uint8_t *significance_data_max_ptr = precinct->bands[c][b].lines_common[line].significance_data_max_ptr;
                    const uint32_t full_group = group_conding_width / pi->significance_group_size;
                    uint16_t *significance_max_lookup_table = cache_line->significance_max_lookup_table;
                    for (uint32_t i = 0; i < full_group; i++) {
                        assert(significance_data_max_ptr[i] <= TRUNCATION_MAX);
                        significance_max_lookup_table[significance_data_max_ptr[i]]++;
                        assert(cache_line->significance_max_lookup_table[significance_data_max_ptr[i]] != 0);
                    }
#if LUT_SIGNIFICANE_SERIAL_SUMMATION
                    uint16_t summary = 0;
                    for (uint32_t i = 0; i <= TRUNCATION_MAX; ++i) {
                        significance_max_lookup_table[i] += summary;
                        summary = significance_max_lookup_table[i];
                    }
#endif
                }
#if SIGN_NOZERO_COEFF_LUT
                if (coding_signs_handling == SIGN_HANDLING_STRATEGY_FULL) {
                    if (enc_common->picture_header_dynamic.hdr_Qpih == QUANT_TYPE_DEADZONE) {
                        const uint32_t coeff_width = precinct->p_info->b_info[c][b].width;
                        uint16_t *coeff_16bit = precinct->bands[c][b].lines_common[line].coeff_data_ptr_16bit;
                        uint16_t *coeff_lookup_table = cache_line->coeff_lookup_table;
                        assert(coeff_width <= UINT16_MAX);
                        for (uint32_t i = 0; i < coeff_width; ++i) {
                            uint32_t log = svt_log2_32((coeff_16bit[i] & ~BITSTREAM_MASK_SIGN) << 1); //MSB
                            assert(log <= TRUNCATION_MAX);
                            coeff_lookup_table[log]++;
                        }
                    }
                }
#endif /*SIGN_NOZERO_COEFF_LUT*/
#endif /*LUT_SIGNIFICANE*/
                //TODO: Optimize lookup to array size but not see benefits
                //int remove = 0;
                //for (int i = 15; i > 0; --i) {
                //    if (gc_lookup_table[band->band_id][line][i] == 0) {
                //        remove++;
                //    } else {
                //        break;
                //    }
                //}
                //printf("Remove: %i\n", remove);
            }
        }
    }
}

static void rate_control_lut_summarize_non_significance_gc_and_data_code_sum(rc_cache_band_line_t *cache_line, int gtli,
                                                                             uint32_t *out_sum_data,
                                                                             uint32_t *sum_data_non_significance_code_gc,
                                                                             uint32_t *sum_signs_max,
                                                                             uint8_t coding_signs_handling) {
    uint16_t *table = cache_line->gc_lookup_table;
#if LUT_GC_SERIAL_SUMMATION
    if (coding_signs_handling) {
        uint32_t sum_non_significance_zero = table[gtli];
        uint32_t sum_data_no_signs = cache_line->gc_lookup_table_size_data_no_sign_handling[gtli];
        uint32_t sum_data_signs = cache_line->gc_lookup_table_size_data_sign_handling[gtli];
        *out_sum_data = sum_data_signs;
        *sum_data_non_significance_code_gc = sum_data_no_signs + sum_non_significance_zero;
        *sum_signs_max = sum_data_no_signs - sum_data_signs;
    }
    else {
        uint32_t sum_non_significance_zero = table[gtli];
        uint32_t sum_data_no_signs = cache_line->gc_lookup_table_size_data_no_sign_handling[gtli];
        *out_sum_data = sum_data_no_signs;
        *sum_data_non_significance_code_gc = sum_data_no_signs + sum_non_significance_zero;
        *sum_signs_max = 0;
    }

#else  /*LUT_GC_SERIAL_SUMMATION*/
    /* Pseudo code non significance gc:
     *  uint32_t sum = 0;
     *   for(all_gcli_values_in_band) {
     *       sum += MAX(0, gcli[*] - gtli) + 1;
     *   }
     *  return sum;
     *
     * Pseudo code data sum:
     *  uint32_t sum = 0;
     *   for (int i = 0; i < len; i++) {
     *    if(gclis[i] - gtli > 0) {
     *       sum += ((gclis[i] - gtli +1));
     *    }
     *  }
     *  return sum;
     *
    }*/

    if (coding_signs_handling) {
        uint32_t sum_data_no_signs = 0;
        uint32_t sum_data_signs = 0;
        uint32_t sum_non_significance_zero = 0;
        for (int8_t i = 0; i <= gtli; ++i) {
            sum_non_significance_zero += table[i];
        }
        for (int8_t i = gtli + 1; i < TRUNCATION_MAX + 1; ++i) {
            sum_data_no_signs += (i - gtli + 1) * table[i];
            sum_data_signs += (i - gtli) * table[i];
        }

        *out_sum_data = sum_data_signs;
        *sum_data_non_significance_code_gc = sum_data_no_signs + sum_non_significance_zero;
        *sum_signs_max = sum_data_no_signs - sum_data_signs;
    }
    else {
        uint32_t sum_data = 0;
        uint32_t sum_non_significance_zero = 0;
        for (int8_t i = 0; i <= gtli; ++i) {
            sum_non_significance_zero += table[i];
        }
        for (int8_t i = gtli + 1; i < TRUNCATION_MAX + 1; ++i) {
            sum_data += (i - gtli + 1) * table[i];
        }

        *out_sum_data = sum_data;
        *sum_data_non_significance_code_gc = sum_data + sum_non_significance_zero;
        *sum_signs_max = 0;
    }
#endif /*LUT_GC_SERIAL_SUMMATION*/
}

#if LUT_SIGNIFICANE
static uint32_t rate_control_lut_significance_elements_less_equal_gtli(rc_cache_band_line_t *cache_line, int gtli) {
    uint16_t *table = cache_line->significance_max_lookup_table;
    /* Pseudo code unsigned significances:
     * GCLI value 0 is compressed by VLC on 1 bit so always can remove
     * from pack_size_gcli number of bits equal size significance group.
     * uint32_t significance_flags_zeroed_sum = 0;
     * for (; sigf_group < full_group; sigf_group++) {
     *     if (sigFlagMaxGCli[sigf_group] <= band->gtli) {
     *         significance_flags_zeroed_sum += 1;
     *     }
     * }
     * return significance_flags_zeroed_sum;
    }*/
#if LUT_SIGNIFICANE_SERIAL_SUMMATION
    return table[gtli];
#else  /*LUT_SIGNIFICANE_SERIAL_SUMMATION*/
    uint32_t significance_flags_zeroed_sum = 0;
    for (int i = 0; i <= gtli; ++i) {
        significance_flags_zeroed_sum += table[i];
    }
    return significance_flags_zeroed_sum;
#endif /*LUT_SIGNIFICANE_SERIAL_SUMMATION*/
}

#endif

static uint32_t rate_control_get_headers_bytes(svt_jpeg_xs_encoder_common_t *enc_common, precinct_enc_t *precinct) {
    pi_t *pi = &enc_common->pi;
    uint32_t headers_bytes = BITS_TO_BYTE_WITH_ALIGN(PRECINCT_HEADER_SIZE_BYTES * 8 + pi->bands_num_exists * 2);

    uint32_t pack_header_bits;
    if (pi->use_short_header) {
        pack_header_bits = PACKET_HEADER_SHORT_SIZE_BYTES * 8;
    }
    else {
        pack_header_bits = PACKET_HEADER_LONG_SIZE_BYTES * 8;
    }
    assert(precinct->p_info->packets_exist_num >= 0); //Not implemented
    headers_bytes += BITS_TO_BYTE_WITH_ALIGN(pack_header_bits * precinct->p_info->packets_exist_num);
    return headers_bytes;
}

/*Return 0 if ok or 1 when empty*/
static int precinct_encoder_compute_truncation(pi_t *pi, precinct_enc_t *precinct, uint8_t quantization, uint8_t refinement) {
    //TODO: Unify with precinct_compute_truncation() from common
    int ret = 1;
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            struct band_data_enc *band = &precinct->bands[c][b];
            band->gtli = compute_truncation(
                pi->components[c].bands[b].gain, pi->components[c].bands[b].priority, quantization, refinement);
            if (band->gtli != TRUNCATION_MAX) {
                ret = 0;
            }
        }
    }
    return ret;
}

uint32_t rate_control_calc_vpred_cost_nosigf_c(uint32_t gcli_width, uint8_t *gcli_data_top_ptr, uint8_t *gcli_data_ptr,
                                               uint8_t *vpred_bits_pack, uint8_t gtli, uint8_t gtli_max) {
    uint32_t pack_size_gcli_no_sigf = 0;
    for (uint32_t i = 0; i < gcli_width; ++i) {
        uint8_t m_top = MAX(gcli_data_top_ptr[i], gtli_max);
        uint8_t gcli = MAX(gcli_data_ptr[i], gtli);
        int8_t delta_m = gcli - m_top; //Can be negative!!!
        uint8_t bits = vlc_encode_get_bits(delta_m, m_top, gtli);
        vpred_bits_pack[i] = bits;
        pack_size_gcli_no_sigf += bits;
    }
    return pack_size_gcli_no_sigf;
}

uint32_t rate_control_calc_vpred_cost_sigf_c(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                             uint8_t *gcli_data_top_ptr, uint8_t *gcli_data_ptr, uint8_t *vpred_bits_pack,
                                             uint8_t *vpred_significance, uint8_t gtli, uint8_t gtli_max) {
    uint32_t pack_size_gcli_sigf_reduction = 0;
    for (uint32_t s = 0; s < significance_width; ++s) {
        uint8_t sigf_group_flag = 1; //1 if possible group with significance else 0
        uint32_t index_end = MIN((s + 1) * SIGNIFICANCE_GROUP_SIZE, gcli_width);
        if (!hdr_Rm) {
            for (uint32_t i = (s)*SIGNIFICANCE_GROUP_SIZE; i < index_end; ++i) {
                int m_top = MAX(gcli_data_top_ptr[i], gtli_max);
                int gcli = MAX(gcli_data_ptr[i], gtli);
                if (m_top != gcli) {
                    sigf_group_flag = 0;
                    break;
                }
            }
        }
        else { /*if (hdr_Rm)*/
            for (uint32_t i = (s)*SIGNIFICANCE_GROUP_SIZE; i < index_end; ++i) {
                int gcli = gcli_data_ptr[i];
                if (gtli < gcli) {
                    sigf_group_flag = 0;
                    break;
                }
            }
        }

        vpred_significance[s] = sigf_group_flag;
        if (sigf_group_flag) {
            uint32_t sigf_group_reduction = 0;
            for (uint32_t i = (s)*SIGNIFICANCE_GROUP_SIZE; i < index_end; ++i) {
                int8_t bits = vpred_bits_pack[i]; // vlc_encode_get_bits(delta_m, m_top, gtli);
                sigf_group_reduction += bits + 1;
            }
            pack_size_gcli_sigf_reduction += sigf_group_reduction;
        }
    }
    return pack_size_gcli_sigf_reduction;
}

void rate_control_calc_vpred_cost_sigf_nosigf_c(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                uint32_t significance_group_size, uint8_t *gcli_data_top_ptr,
                                                uint8_t *gcli_data_ptr, uint8_t *vpred_bits_pack, uint8_t *vpred_significance,
                                                uint8_t gtli, uint8_t gtli_max, uint32_t *pack_size_gcli_sigf_reduction,
                                                uint32_t *pack_size_gcli_no_sigf) {
    UNUSED(significance_group_size);
    assert(significance_group_size == SIGNIFICANCE_GROUP_SIZE);

    *pack_size_gcli_no_sigf = rate_control_calc_vpred_cost_nosigf(
        gcli_width, gcli_data_top_ptr, gcli_data_ptr, vpred_bits_pack, gtli, gtli_max);
    *pack_size_gcli_sigf_reduction = rate_control_calc_vpred_cost_sigf_c(significance_width,
                                                                         gcli_width,
                                                                         hdr_Rm,
                                                                         gcli_data_top_ptr,
                                                                         gcli_data_ptr,
                                                                         vpred_bits_pack,
                                                                         vpred_significance,
                                                                         gtli,
                                                                         gtli_max);
}

static uint32_t rate_control_sign_coding_get_sum_nonzero_coeff_c(rc_cache_band_line_t *cache_line, precinct_enc_t *precinct,
                                                                 svt_jpeg_xs_encoder_common_t *enc_common, uint32_t c, uint32_t b,
                                                                 uint32_t line_idx) {
#if !SIGN_NOZERO_COEFF_LUT
    UNUSED(cache_line);
#endif
    const uint32_t width = precinct->p_info->b_info[c][b].width;
    uint8_t gtli = precinct->bands[c][b].gtli;
    uint8_t *gclis = precinct->bands[c][b].lines_common[line_idx].gcli_data_ptr;
    uint16_t *coeff_16bit = precinct->bands[c][b].lines_common[line_idx].coeff_data_ptr_16bit;
    uint32_t sum_nonzero_coeff = 0;
    uint32_t group = 0;
    const uint32_t groups = DIV_ROUND_DOWN(width, GROUP_SIZE);
    const uint32_t leftover = width % GROUP_SIZE;

    if (enc_common->picture_header_dynamic.hdr_Qpih == QUANT_TYPE_DEADZONE) {
#if SIGN_NOZERO_COEFF_LUT
        rc_cache_band_line_t *cache_line = &precinct->bands[c][b].lines_common[line_idx].rc_cache_line;
        uint16_t *coeff_lookup_table_local = cache_line->coeff_lookup_table;
        for (uint8_t i = gtli + 1; i < TRUNCATION_MAX + 1; ++i) {
            sum_nonzero_coeff += coeff_lookup_table_local[i];
        }
#else /*SIGN_NOZERO_COEFF_LUT*/
        for (; group < groups; group++) {
            if (gclis[group] > gtli) {
                for (uint32_t i = 0; i < GROUP_SIZE; ++i) {
                    assert(coeff_16bit[i] != BITSTREAM_MASK_SIGN); //-0 never should happens
                    //Deadzone
                    if ((coeff_16bit[i] & ~BITSTREAM_MASK_SIGN) >> gtli) {
                        sum_nonzero_coeff++;
                    }
                }
            }
            coeff_16bit += GROUP_SIZE;
        }

        //leftover
        if (leftover) {
            if (gclis[group] > gtli) {
                // printf("[%i]", group);
                for (uint32_t i = 0; i < leftover; i++) {
                    assert(coeff_16bit[i] != BITSTREAM_MASK_SIGN); //-0 never should happens
                    if ((coeff_16bit[i] & ~BITSTREAM_MASK_SIGN) >> gtli) {
                        sum_nonzero_coeff++;
                    }
                }
            }
        }

#endif /*SIGN_NOZERO_COEFF_LUT*/
    }
    else {
        assert(enc_common->picture_header_dynamic.hdr_Qpih == QUANT_TYPE_UNIFORM);
        for (; group < groups; group++) {
            if (gclis[group] > gtli) {
                for (uint32_t i = 0; i < GROUP_SIZE; ++i) {
                    assert(coeff_16bit[i] != BITSTREAM_MASK_SIGN); //-0 nevert should happens
                    uint8_t gcli = gclis[group];
                    uint8_t scale_value = gcli - gtli + 1;
                    uint16_t d = coeff_16bit[i] & ~BITSTREAM_MASK_SIGN;
                    d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1);
                    if (d) {
                        sum_nonzero_coeff++;
                    }
                }
            }
            coeff_16bit += GROUP_SIZE;
        }

        //leftover
        if (leftover) {
            if (gclis[group] > gtli) {
                // printf("[%i]", group);
                for (uint32_t i = 0; i < leftover; i++) {
                    assert(coeff_16bit[i] != BITSTREAM_MASK_SIGN); //-0 never should happens
                    assert(enc_common->picture_header_dynamic.hdr_Qpih == QUANT_TYPE_UNIFORM);
                    uint8_t gcli = gclis[group];
                    uint8_t scale_value = gcli - gtli + 1;
                    uint16_t d = coeff_16bit[i] & ~BITSTREAM_MASK_SIGN;
                    d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1);
                    if (d) {
                        sum_nonzero_coeff++;
                    }
                }
            }
        }
    }

    return sum_nonzero_coeff;
}

static void rate_control_calculate_band_best_method(pi_t *pi, precinct_enc_t *precinct, svt_jpeg_xs_encoder_common_t *enc_common,
                                                    VerticalPredictionMode coding_vertical_prediction_mode,
                                                    SignHandlingStrategy coding_signs_handling) {
    //Precalculate All Methods
#if PRINT_RECALC_VPRED
    uint8_t break_line = 0;
#endif

    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            struct band_data_enc *band = &(precinct->bands[c][b]);
            const uint32_t height_lines = precinct->p_info->b_info[c][b].height;
            /*height_lines can be 0 for last precinct*/
            if ((height_lines > 0) && (band->cache_actual->gtli_rc_last_calculated != band->gtli)) {
                /*Always need recalculate next precinct when actual GCLI changed.*/
                precinct->need_recalculate_next_precinct = 1;

                /*Check all cached values to find calculation for GTLI. If not find then override older calculation.*/
                uint8_t need_recalculate = 1;
                for (uint32_t i = 1; i < RC_BAND_CACHE_SIZE; ++i) {
                    uint32_t cache_index_test = (band->cache_index + i) % RC_BAND_CACHE_SIZE;
                    if (band->cache_buffers[cache_index_test].gtli_rc_last_calculated == band->gtli) {
                        band->cache_index = cache_index_test;
                        band->cache_actual = &band->cache_buffers[band->cache_index];
                        need_recalculate = 0;
                        break;
                    }
                }
                if (!need_recalculate) {
                    continue;
                }
                /*Override older calculation*/
                band->cache_index = (band->cache_index + 1) % RC_BAND_CACHE_SIZE;
                band->cache_actual = &band->cache_buffers[band->cache_index];
                assert(band->cache_actual->gtli_rc_last_calculated != band->gtli);
                struct band_data_enc_cache *band_cache = band->cache_actual;
                const uint32_t gcli_width = precinct->p_info->b_info[c][b].gcli_width;
                const uint32_t significance_width = precinct->p_info->b_info[c][b].significance_width;
                band_cache->pack_method = METHOD_INVALID;
                uint32_t sum_data_non_significance_code_gc[MAX_BAND_LINES];

                for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                    rc_cache_band_line_t *cache_line = &band->lines_common[line_idx].rc_cache_line;
                    //Precalculate Data Size
                    uint32_t sum_data;
                    uint32_t sum_signs_max;
                    rate_control_lut_summarize_non_significance_gc_and_data_code_sum(cache_line,
                                                                                     band->gtli,
                                                                                     &sum_data,
                                                                                     &sum_data_non_significance_code_gc[line_idx],
                                                                                     &sum_signs_max,
                                                                                     !!coding_signs_handling);

                    band_cache->lines[line_idx].pack_size_data_bits = sum_data * pi->coeff_group_size;

                    if (coding_signs_handling == SIGN_HANDLING_STRATEGY_OFF) {
                        band_cache->lines[line_idx].packet_size_signs_handling_bits = 0;
                    }
                    else if (coding_signs_handling == SIGN_HANDLING_STRATEGY_FAST) {
                        band_cache->lines[line_idx].packet_size_signs_handling_bits = sum_signs_max * pi->coeff_group_size;
                    }
                    else {
                        assert(coding_signs_handling == SIGN_HANDLING_STRATEGY_FULL);
                        band_cache->lines[line_idx].packet_size_signs_handling_bits =
                            rate_control_sign_coding_get_sum_nonzero_coeff_c(cache_line, precinct, enc_common, c, b, line_idx);
                    }
                }

                /*Budget Coding NO SIGNIFICANCE:*/
                uint32_t best_budget = 0;
                for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                    /*Set for METHOD_ZERO_SIGNIFICANCE_DISABLE*/
                    band_cache->lines[line_idx].pack_size_significance_bits = 0;
                    band_cache->lines[line_idx].pack_size_gcli_bits = sum_data_non_significance_code_gc[line_idx];
                    best_budget += sum_data_non_significance_code_gc[line_idx];
                }
                band_cache->pack_method = METHOD_ZERO_SIGNIFICANCE_DISABLE;

                if (enc_common->coding_significance) {
                    uint32_t budget = significance_width * height_lines; /*Budget is sum of significance + gcli size*/
                    uint32_t pack_size_gcli[MAX_BAND_LINES];

                    for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                        rc_cache_band_line_t *cache_line = &band->lines_common[line_idx].rc_cache_line;
                        //Remove from values zeroed sig flags
                        uint8_t *sigFlagMaxGCLI = band->lines_common[line_idx].significance_data_max_ptr;
                        const uint32_t full_group = gcli_width / pi->significance_group_size;
                        /*GCLI value 0 is compressed by VLC to 1 bit,
                         *so we can remove number of bits equal to significance group size from pack_size_gcli*/
#if LUT_SIGNIFICANE
                        uint32_t significance_flags_zeroed_sum = rate_control_lut_significance_elements_less_equal_gtli(
                            cache_line, band->gtli);
                        significance_flags_zeroed_sum *= pi->significance_group_size;
                        if (full_group < significance_width) {
                            if (sigFlagMaxGCLI[full_group] <= band->gtli) {
                                significance_flags_zeroed_sum += gcli_width - full_group * pi->significance_group_size;
                            }
                        }
#else  /*LUT_SIGNIFICANE*/
                        uint32_t significance_flags_zeroed_sum = 0;
                        uint32_t sigf_group = 0;
                        for (; sigf_group < full_group; sigf_group++) {
                            if (sigFlagMaxGCLI[sigf_group] <= band->gtli) {
                                significance_flags_zeroed_sum += pi->significance_group_size;
                            }
                        }
                        if (sigf_group < significance_width) {
                            if (sigFlagMaxGCLI[sigf_group] <= band->gtli) {
                                significance_flags_zeroed_sum += gcli_width - sigf_group * pi->significance_group_size;
                            }
                        }
#endif /*SIGNIFICANE_LUT*/
                        /*Use GCLI from disable signification, and remove signification zero elements*/
                        pack_size_gcli[line_idx] = band_cache->lines[line_idx].pack_size_gcli_bits -
                            significance_flags_zeroed_sum;
                        budget += pack_size_gcli[line_idx];
                    }

                    if (best_budget > budget) {
                        best_budget = budget;
                        band_cache->pack_method = METHOD_ZERO_SIGNIFICANCE_ENABLE;
                        for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                            /*Set for METHOD_ZERO_SIGNIFICANCE_ENABLE*/
                            band_cache->lines[line_idx].pack_size_significance_bits = significance_width;
                            band_cache->lines[line_idx].pack_size_gcli_bits = pack_size_gcli[line_idx];
                        }
                    }
                }

                /*Vertical Prediction*/
                if (precinct->precinct_top && (coding_vertical_prediction_mode != METHOD_PRED_DISABLE)) {
                    const uint32_t height_lines_top = precinct->precinct_top->p_info->b_info[c][b].height;

#if PRINT_RECALC_VPRED
                    printf("%02i%02i%02i ", c, b, band->gtli);
                    break_line = 1;
#endif

                    uint32_t pack_size_gcli_normal[MAX_BAND_LINES];
                    uint32_t pack_size_gcli_significance[MAX_BAND_LINES];
                    uint32_t budget_vpred_no_sigf = 0;
                    uint32_t budget_vpred_signf = 0;

                    for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                        int gtli_top;
                        int gtli = band->gtli;
                        uint8_t *gcli_data_top_ptr;
                        if (line_idx == 0) {
                            gcli_data_top_ptr =
                                precinct->precinct_top->bands[c][b].lines_common[height_lines_top - 1].gcli_data_ptr;
                            gtli_top = precinct->precinct_top->bands[c][b].gtli;
                        }
                        else {
                            gcli_data_top_ptr = band->lines_common[line_idx - 1].gcli_data_ptr;
                            gtli_top = gtli;
                        }

                        uint8_t *gcli_data_ptr = band->lines_common[line_idx].gcli_data_ptr;
                        int gtli_max = MAX(gtli, gtli_top);

                        uint32_t pack_size_gcli_no_sigf = 0;
                        if (enc_common->coding_significance) {
                            uint32_t pack_size_gcli_sigf_reduction = 0;
                            rate_control_calc_vpred_cost_sigf_nosigf(significance_width,
                                                                     gcli_width,
                                                                     enc_common->picture_header_dynamic.hdr_Rm,
                                                                     pi->significance_group_size,
                                                                     gcli_data_top_ptr,
                                                                     gcli_data_ptr,
                                                                     band_cache->lines[line_idx].vpred_bits_pack,
                                                                     band_cache->lines[line_idx].vpred_significance,
                                                                     gtli,
                                                                     gtli_max,
                                                                     &pack_size_gcli_sigf_reduction,
                                                                     &pack_size_gcli_no_sigf);
                            pack_size_gcli_no_sigf += gcli_width; //Add a number of zeros to the end of the VLC encoding
                            pack_size_gcli_significance[line_idx] = pack_size_gcli_no_sigf - pack_size_gcli_sigf_reduction;
                            budget_vpred_signf += pack_size_gcli_significance[line_idx];
                            budget_vpred_signf += significance_width;
                        }
                        else {
                            pack_size_gcli_no_sigf = rate_control_calc_vpred_cost_nosigf(
                                gcli_width,
                                gcli_data_top_ptr,
                                gcli_data_ptr,
                                band_cache->lines[line_idx].vpred_bits_pack,
                                gtli,
                                gtli_max);

                            pack_size_gcli_no_sigf += gcli_width; //Add a number of zeros to the end of the VLC encoding
                        }

                        /*Update No Significance*/
                        pack_size_gcli_normal[line_idx] = pack_size_gcli_no_sigf;
                        budget_vpred_no_sigf += pack_size_gcli_no_sigf;
                    }

                    if (best_budget > budget_vpred_no_sigf) {
                        best_budget = budget_vpred_no_sigf;
                        band_cache->pack_method = METHOD_VPRED_SIGNIFICANCE_DISABLE;
                    }

                    if (enc_common->coding_significance && (best_budget > budget_vpred_signf)) {
                        best_budget = budget_vpred_signf;
                        band_cache->pack_method = METHOD_VPRED_SIGNIFICANCE_ENABLE;
                    }

                    if (band_cache->pack_method == METHOD_VPRED_SIGNIFICANCE_DISABLE) {
                        for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                            band_cache->lines[line_idx].pack_size_significance_bits = 0;
                            band_cache->lines[line_idx].pack_size_gcli_bits = pack_size_gcli_normal[line_idx];
                        }
                    }
                    else if (band_cache->pack_method == METHOD_VPRED_SIGNIFICANCE_ENABLE) {
                        for (uint32_t line_idx = 0; line_idx < height_lines; ++line_idx) {
                            band_cache->lines[line_idx].pack_size_significance_bits = significance_width;
                            band_cache->lines[line_idx].pack_size_gcli_bits = pack_size_gcli_significance[line_idx];
                        }
                    }
                } /*Vertical Prediction End*/

                assert(band_cache->pack_method != METHOD_INVALID);
                band_cache->gtli_rc_last_calculated = band->gtli; //Set cache
            }
        }
    }

#if PRINT_RECALC_VPRED
    if (break_line) {
        printf("\n");
    }
#endif
}

static void rate_control_reset_cache(pi_t *pi, precinct_enc_t *precinct) {
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            struct band_data_enc *band = &(precinct->bands[c][b]);
            for (uint32_t i = 0; i < RC_BAND_CACHE_SIZE; ++i) {
                band->cache_buffers[i].gtli_rc_last_calculated = UINT8_MAX; //Set invalid value
            }
            band->cache_index = 0;
            band->cache_actual = &band->cache_buffers[band->cache_index]; //Always set some pointer even on invalid cache
        }
    }
}

static uint32_t precinct_get_budget_bytes(svt_jpeg_xs_encoder_common_t *enc_common, precinct_enc_t *precinct,
                                          VerticalPredictionMode coding_vertical_prediction_mode,
                                          SignHandlingStrategy coding_signs_handling) {
    pi_t *pi = &enc_common->pi;
    /*For current implementation support only more complex implementation
      that 2 packages with that same band can have different ram mode flag.*/
    assert(enc_common->picture_header_dynamic.hdr_Rl != 0);
    rate_control_calculate_band_best_method(pi, precinct, enc_common, coding_vertical_prediction_mode, coding_signs_handling);
    precinct_info_t *p_info = precinct->p_info;

#ifndef NDEBUG
    //Check Precalculate All Methods
    for (uint32_t packet_idx = 0; packet_idx < pi->packets_num; packet_idx++) {
        for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop; band_idx++) {
            const uint32_t b = pi->global_band_info[band_idx].band_id;
            const uint32_t c = pi->global_band_info[band_idx].comp_id;
            struct band_data_enc *band = &(precinct->bands[c][b]);
            const uint32_t height_lines = p_info->b_info[c][b].height;
            //Check correct Cache info
            assert((height_lines == 0) || (band->cache_actual->gtli_rc_last_calculated == band->gtli));
            assert(band->cache_actual->pack_method != METHOD_INVALID);
        }
    }
#endif

    uint32_t precinct_size_bytes = 0;
    //Find best methods for bands: Normal packing or RAW
    for (uint32_t packet_idx = 0; packet_idx < pi->packets_num; packet_idx++) {
        /*Data Pack size:*/
        uint32_t packet_size_data_bits = 0;
        uint32_t packet_size_signs_handling_bits = 0;
        /*GCLI Pack Size:*/
        uint32_t packet_size_gcli_bits = 0;
        uint32_t packet_size_significance_bits = 0;

        for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop; band_idx++) {
            const uint32_t b = pi->global_band_info[band_idx].band_id;
            const uint32_t c = pi->global_band_info[band_idx].comp_id;
            const uint32_t line_idx = pi->packets[packet_idx].line_idx;
            uint32_t height_lines = p_info->b_info[c][b].height;
            if (line_idx < height_lines) {
                struct band_data_enc *band = &(precinct->bands[c][b]);
                struct band_data_enc_cache *band_cache = band->cache_actual;
                packet_size_gcli_bits += band_cache->lines[line_idx].pack_size_gcli_bits;
                packet_size_significance_bits += band_cache->lines[line_idx].pack_size_significance_bits;
                packet_size_data_bits += band_cache->lines[line_idx].pack_size_data_bits;
                packet_size_signs_handling_bits += band_cache->lines[line_idx].packet_size_signs_handling_bits;
            }
        }

        /*Data Pack size:*/
        uint32_t packet_size_data_bytes = BITS_TO_BYTE_WITH_ALIGN(packet_size_data_bits);
        uint32_t packet_size_signs_handling_bytes = BITS_TO_BYTE_WITH_ALIGN(packet_size_signs_handling_bits);
        precinct->packet_size_data_bytes[packet_idx] = packet_size_data_bytes;
        precinct->packet_size_signs_handling_bytes[packet_idx] = packet_size_signs_handling_bytes;
        precinct_size_bytes += packet_size_data_bytes + packet_size_signs_handling_bytes;

        /*GCLI Pack Size:*/
        uint32_t packet_size_significance_bytes = BITS_TO_BYTE_WITH_ALIGN(packet_size_significance_bits);
        uint32_t packet_size_gcli_bytes = BITS_TO_BYTE_WITH_ALIGN(packet_size_gcli_bits);
        if (packet_size_significance_bytes + packet_size_gcli_bytes > p_info->packet_size_gcli_raw_bytes[packet_idx]) {
            /*Set Pack RAW*/
            precinct->packet_methods_raw[packet_idx] = 1;
            precinct->packet_size_significance_bytes[packet_idx] = 0;
            precinct->packet_size_gcli_bytes[packet_idx] = p_info->packet_size_gcli_raw_bytes[packet_idx];
            precinct_size_bytes += p_info->packet_size_gcli_raw_bytes[packet_idx];
        }
        else {
            /*Set Pack Normal*/
            precinct->packet_methods_raw[packet_idx] = 0;
            precinct->packet_size_significance_bytes[packet_idx] = packet_size_significance_bytes;
            precinct->packet_size_gcli_bytes[packet_idx] = packet_size_gcli_bytes;
            precinct_size_bytes += packet_size_significance_bytes + packet_size_gcli_bytes;
        }
    }

    return precinct_size_bytes;
}

#ifndef NDEBUG
/* Find the minimum quantization to total budget that will be less than or equal to the available buffer.*/
static SvtJxsErrorType_t rate_control_find_best_quantization(svt_jpeg_xs_encoder_common_t *enc_common, uint32_t budget_bytes,
                                                             precinct_enc_t *precinct, uint8_t *out_quantization,
                                                             VerticalPredictionMode coding_vertical_prediction_mode,
                                                             SignHandlingStrategy coding_signs_handling) {
    uint8_t quantization = 0;
    uint8_t max_quantization = enc_common->pi_enc.max_quantization;
    int empty = 0;
    SvtJxsErrorType_t ret = SvtJxsErrorEncodeFrameError;
    while (quantization <= max_quantization && !empty) {
        empty = precinct_encoder_compute_truncation(&enc_common->pi, precinct, quantization, 0);
        if (empty) {
            break;
        }
        uint32_t total_budget_bytes = precinct_get_budget_bytes(
            enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
        if (total_budget_bytes <= budget_bytes) {
            ret = SvtJxsErrorNone;
            *out_quantization = quantization;
            break;
        }
        ++quantization;
    }

    return ret;
}

/* Find maximum refinement for total budget that will be smaller or equal that available buffer.
 * Take care of recalculate precinct with final refinement for value.*/
static SvtJxsErrorType_t rate_control_find_best_refinement(svt_jpeg_xs_encoder_common_t *enc_common, uint32_t budget_bytes,
                                                           precinct_enc_t *precinct, uint8_t quantization,
                                                           uint8_t *out_refinement, uint32_t *data_budget_bytes,
                                                           VerticalPredictionMode coding_vertical_prediction_mode,
                                                           SignHandlingStrategy coding_signs_handling) {
    SvtJxsErrorType_t ret = SvtJxsErrorEncodeFrameError;
    uint8_t max_refinement = enc_common->pi_enc.max_refinement;
    int empty = 0;
    int8_t refinement_last_correct = -1;
    int8_t refinement = 0;

    while (refinement <= max_refinement && !empty) {
        empty = precinct_encoder_compute_truncation(&enc_common->pi, precinct, quantization, refinement);
        if (empty) {
            break;
        }
        uint32_t total_budget_bytes = precinct_get_budget_bytes(
            enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
        if (total_budget_bytes > budget_bytes) {
            break;
        }

        refinement_last_correct = refinement;
        /* Can not stop on because bigger refinement can have that same budget but better quality
            if (total_budget == budget) {
            break;
        }*/
        ++refinement;
    }

    //Recalculate last correct
    if (refinement_last_correct >= 0) {
        *out_refinement = refinement_last_correct;
        refinement = refinement_last_correct;
        empty = precinct_encoder_compute_truncation(&enc_common->pi, precinct, quantization, refinement);
        *data_budget_bytes = precinct_get_budget_bytes(
            enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
        ret = SvtJxsErrorNone;
    }
    assert(*data_budget_bytes <= budget_bytes);

    return ret;
}

static SvtJxsErrorType_t rate_control_find_best_quantization_refinement(svt_jpeg_xs_encoder_common_t *enc_common,
                                                                        uint32_t budget_bytes, precinct_enc_t *precinct,
                                                                        uint8_t *out_quantization, uint8_t *out_refinement,
                                                                        uint32_t *out_data_bytes,
                                                                        VerticalPredictionMode coding_vertical_prediction_mode,
                                                                        SignHandlingStrategy coding_signs_handling) {
    SvtJxsErrorType_t ret;
    ret = rate_control_find_best_quantization(
        enc_common, budget_bytes, precinct, out_quantization, coding_vertical_prediction_mode, coding_signs_handling);
    if (ret) {
        fprintf(stderr, "[%s[%d]] RC precinct error not found quantization\n", __FUNCTION__, __LINE__);
        return ret;
    }
#if PRINT_RECALC_VPRED
    printf("Ref: %i:\n", precinct->prec_idx);
#endif
    ret = rate_control_find_best_refinement(enc_common,
                                            budget_bytes,
                                            precinct,
                                            *out_quantization,
                                            out_refinement,
                                            out_data_bytes,
                                            coding_vertical_prediction_mode,
                                            coding_signs_handling);
    if (ret) {
        fprintf(stderr, "[%s[%d]] RC precinct error not found refinement\n", __FUNCTION__, __LINE__);
    }
    return ret;
}
#endif /*NDEBUG*/

/* Find minimum quantization to total budget will be smaller or equal that available buffer.*/
static SvtJxsErrorType_t rate_control_find_best_quantization_binary_search(svt_jpeg_xs_encoder_common_t *enc_common,
                                                                           uint32_t budget_bytes, precinct_enc_t *precinct,
                                                                           uint8_t *out_quantization,
                                                                           VerticalPredictionMode coding_vertical_prediction_mode,
                                                                           SignHandlingStrategy coding_signs_handling) {
    const uint8_t max_quantization = enc_common->pi_enc.max_quantization;
    SvtJxsErrorType_t ret = SvtJxsErrorEncodeFrameError;
    pi_t *pi = &enc_common->pi; /* Picture Information */

    uint32_t initial_step = 6; //TODO: Initial value should be set empirical in future
    if (initial_step > max_quantization) {
        initial_step = 0;
    }
    int find_below_or_equal = 0; //Find min correct value, but not less that first correct budget
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    BinarySearchResult result;
    BinarySearch_t search;
    binary_search_init(&search, 0, max_quantization, find_below_or_equal, initial_step);
    uint32_t quantization_simple;

    SignHandlingStrategy coding_signs_handling_bin_search = coding_signs_handling;
    if (coding_signs_handling_bin_search == SIGN_HANDLING_STRATEGY_FULL) {
        coding_signs_handling_bin_search = SIGN_HANDLING_STRATEGY_FAST;
    }

    //MAIN LOOP: Search simple with disable coding_vertical_prediction_mode and coding_signs_handling
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &quantization_simple))) {
        //Test quantization value:
        int empty = precinct_encoder_compute_truncation(pi, precinct, quantization_simple, 0);
        if (empty) {
            //Required when add too big size
            next_step = BINARY_STEP_OUT_OF_RANGE;
            continue;
        }
        uint32_t total_budget_bytes = precinct_get_budget_bytes(
            enc_common, precinct, METHOD_PRED_DISABLE, coding_signs_handling_bin_search);

        if (total_budget_bytes > budget_bytes) {
            //To small value of quantization
            next_step = BINARY_STEP_TOO_SMALL;
        }
        else {
            //To big value of quantization, but acceptable if not exist better value
            next_step = BINARY_STEP_TOO_BIG;
        }
    }

    if (result == BINARY_RESULT_FIND_CLOSE) {
        if (coding_vertical_prediction_mode == METHOD_PRED_DISABLE && coding_signs_handling == coding_signs_handling_bin_search) {
            ret = SvtJxsErrorNone;
            *out_quantization = quantization_simple;
        }
        else {
            assert((coding_signs_handling == coding_signs_handling_bin_search &&
                    coding_vertical_prediction_mode != METHOD_PRED_DISABLE) ||
                   (coding_signs_handling == SIGN_HANDLING_STRATEGY_FULL));
            /*Search with full modes
            * For Vertical Prediction and Sign coding which are computationally complex,
            * results will be only better, with small exceptions. Search better result with possible
            * small number of steps computing sign and vpred, and keep in cache results reusable in refinement search.
            */
            //Reset cache when use different features
            rate_control_reset_cache(pi, precinct);

            /* Start from decrement one step quantization,
             * Try to keep in cache best solution and one better results to reuse in refinement
             * Then increase quantization steps to find solution in budget.
             */
            int32_t quantization_best = -1; //Negative invalid. Some value mean that one hight is invalid

            if (quantization_simple > 0) {
                int32_t quantization_test = quantization_simple -
                    1; //Less quantization value is better solution required more budget
                while (quantization_test >= 0) {
                    int empty = precinct_encoder_compute_truncation(pi, precinct, quantization_test, 0);
                    uint8_t correct_solution = 0;
                    if (!empty) {
                        uint32_t total_budget_bytes = precinct_get_budget_bytes(
                            enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
                        if (total_budget_bytes <= budget_bytes) {
                            correct_solution = 1;
                        }
                    }
                    if (correct_solution) {
                        quantization_best = quantization_test;
                    }
                    else {
                        break;
                    }
                    quantization_test--;
                }
            }

            if (quantization_best < 0) {
                //Not find best and one step upper so goo for lower direction
                uint8_t quantization_test = quantization_simple;
                while (quantization_test <= max_quantization) {
                    int empty = precinct_encoder_compute_truncation(pi, precinct, quantization_test, 0);
                    uint8_t correct_solution = 0;
                    if (!empty) {
                        uint32_t total_budget_bytes = precinct_get_budget_bytes(
                            enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
                        if (total_budget_bytes <= budget_bytes) {
                            correct_solution = 1;
                        }
                    }

                    if (correct_solution) {
                        quantization_best = quantization_test;
                        break;
                    }
                    quantization_test++;
                }
            }

            if (quantization_best > -1) {
#if PRINT_RECALC_VPRED
                printf("QUANT DIFF full modes: %i\n", quantization_simple - quantization_best);
#endif
                *out_quantization = quantization_best;
                ret = SvtJxsErrorNone;
            }
        }
    }

    return ret;
}

/* Find maximum refinement for total budget that will be smaller or equal that available buffer.
 * Take care of recalculate precinct with final refinement for value.*/
static SvtJxsErrorType_t rate_control_find_best_refinement_binary_search(svt_jpeg_xs_encoder_common_t *enc_common,
                                                                         uint32_t budget_bytes, precinct_enc_t *precinct,
                                                                         uint8_t quantization, uint8_t *out_refinement,
                                                                         uint32_t *data_budget_bytes,
                                                                         VerticalPredictionMode coding_vertical_prediction_mode,
                                                                         SignHandlingStrategy coding_signs_handling) {
    const uint8_t max_refinement = enc_common->pi_enc.max_refinement;
    SvtJxsErrorType_t ret = SvtJxsErrorEncodeFrameError;

    uint32_t initial_step = 6; //TODO: Initial value should be set empirical in future
    if (initial_step >= max_refinement) {
        initial_step = 0;
    }
    int find_below_or_equal = 1; //Find max correct value, but not more that last correct budget
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    BinarySearchResult result;
    BinarySearch_t search;
    binary_search_init(&search, 0, max_refinement, find_below_or_equal, initial_step);
    uint32_t refinement;
    int32_t refinement_last_tested = -1;
    uint32_t total_budget_last_bytes = 0;

    //MAIN LOOP:
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &refinement))) {
        refinement_last_tested = -1;
        //Test refinement value:
        int empty = precinct_encoder_compute_truncation(&enc_common->pi, precinct, quantization, refinement);
        if (empty) {
            //Required when add too big size
            next_step = BINARY_STEP_OUT_OF_RANGE;
            continue;
        }
        uint32_t total_budget_bytes = precinct_get_budget_bytes(
            enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
        refinement_last_tested = refinement;
        total_budget_last_bytes = total_budget_bytes;

        //Because look for maximum pass not stop on first equal budget
        if (total_budget_bytes <= budget_bytes) {
            //To small value of refinement but acceptable if not exist better value
            next_step = BINARY_STEP_TOO_SMALL;
        }
        else {
            //To big value of refinement
            next_step = BINARY_STEP_TOO_BIG;
        }
    }

    if (result == BINARY_RESULT_FIND_CLOSE) {
        ret = SvtJxsErrorNone;
        *out_refinement = refinement;
        if ((int32_t)refinement == refinement_last_tested) {
            *data_budget_bytes = total_budget_last_bytes;
        }
        else {
            //Need to recalculate budget and structures
            precinct_encoder_compute_truncation(&enc_common->pi, precinct, quantization, refinement);
            *data_budget_bytes = precinct_get_budget_bytes(
                enc_common, precinct, coding_vertical_prediction_mode, coding_signs_handling);
        }
    }

    assert(*data_budget_bytes <= budget_bytes);
    return ret;
}

/* Find minimum quantization to total budget will be smaller or equal that available buffer.*/
static SvtJxsErrorType_t rate_control_find_best_quantization_fast_no_vpred_no_sign_full_binary_search(
    svt_jpeg_xs_encoder_common_t *enc_common, uint32_t budget_slice_bytes, precinct_enc_t *precincts, uint32_t prec_num,
    uint8_t *out_quantization, SignHandlingStrategy coding_signs_handling) {
    const uint8_t max_quantization = enc_common->pi_enc.max_quantization;
    SvtJxsErrorType_t ret = SvtJxsErrorEncodeFrameError;
    pi_t *pi = &enc_common->pi; /* Picture Information */

    uint32_t initial_step = 6; //TODO: Initial value should be set empirical in future
    if (initial_step > max_quantization) {
        initial_step = 0;
    }
    int find_below_or_equal = 0; //Find min correct value, but not less that first correct budget
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    BinarySearchResult result;
    BinarySearch_t search;
    binary_search_init(&search, 0, max_quantization, find_below_or_equal, initial_step);
    uint32_t quantization_simple;

    SignHandlingStrategy coding_signs_handling_bin_search = coding_signs_handling;
    if (coding_signs_handling_bin_search == SIGN_HANDLING_STRATEGY_FULL) {
        coding_signs_handling_bin_search = SIGN_HANDLING_STRATEGY_FAST;
    }

    //MAIN LOOP: Search simple with disable coding_vertical_prediction_mode and coding_signs_handling
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &quantization_simple))) {
        //Test quantization value:
        int empty = 0;
        uint32_t total_budget_bytes = 0;
        for (uint32_t i = 0; i < prec_num; ++i) {
            empty += precinct_encoder_compute_truncation(pi, &precincts[i], quantization_simple, 0);
            if (empty) {
                break;
            }
            total_budget_bytes += precinct_get_budget_bytes(
                enc_common, &precincts[i], METHOD_PRED_DISABLE, coding_signs_handling_bin_search);
        }
        if (empty) {
            //Required when add too big size
            next_step = BINARY_STEP_OUT_OF_RANGE;
            continue;
        }

        if (total_budget_bytes > budget_slice_bytes) {
            //To small value of quantization
            next_step = BINARY_STEP_TOO_SMALL;
        }
        else {
            //To big value of quantization, but acceptable if not exist better value
            next_step = BINARY_STEP_TOO_BIG;
        }
    }

    if (result == BINARY_RESULT_FIND_CLOSE) {
        ret = SvtJxsErrorNone;
        *out_quantization = quantization_simple;
    }
    return ret;
}

/* Find maximum refinement for total budget that will be smaller or equal that available buffer.
 * Take care of recalculate precinct with final refinement for value.*/
static SvtJxsErrorType_t rate_control_find_best_refinement_fast_no_vpred_no_sign_full_binary_search(
    svt_jpeg_xs_encoder_common_t *enc_common, uint32_t budget_slice_bytes, precinct_enc_t *precincts, uint32_t prec_num,
    uint8_t quantization, uint8_t *out_refinement, uint32_t *data_budget_bytes, SignHandlingStrategy coding_signs_handling) {
    const uint8_t max_refinement = enc_common->pi_enc.max_refinement;
    SvtJxsErrorType_t ret = SvtJxsErrorEncodeFrameError;

    uint32_t initial_step = 6; //TODO: Initial value should be set empirical in future
    if (initial_step >= max_refinement) {
        initial_step = 0;
    }
    int find_below_or_equal = 1; //Find max correct value, but not more that last correct budget
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    BinarySearchResult result;
    BinarySearch_t search;
    binary_search_init(&search, 0, max_refinement, find_below_or_equal, initial_step);
    uint32_t refinement;
    uint32_t refinement_last_good = 0;
    uint32_t total_budget_last_good_bytes = 0;

    //MAIN LOOP:
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &refinement))) {
        int empty = 0;
        uint32_t total_budget_bytes = 0;
        //Test refinement value:
        for (uint32_t i = 0; i < prec_num; ++i) {
            empty += precinct_encoder_compute_truncation(&enc_common->pi, &precincts[i], quantization, refinement);
            if (empty) {
                break;
            }
            total_budget_bytes += precinct_get_budget_bytes(
                enc_common, &precincts[i], METHOD_PRED_DISABLE, coding_signs_handling);
        }
        if (empty) {
            //Required when add too big size
            next_step = BINARY_STEP_OUT_OF_RANGE;
            continue;
        }

        //Because look for maximum pass not stop on first equal budget
        if (total_budget_bytes <= budget_slice_bytes) {
            //To small value of refinement but acceptable if not exist better value
            next_step = BINARY_STEP_TOO_SMALL;
            if (refinement_last_good < refinement) {
                refinement_last_good = refinement;
                total_budget_last_good_bytes = total_budget_bytes;
            }
        }
        else {
            //To big value of refinement
            next_step = BINARY_STEP_TOO_BIG;
        }
    }

    if (result == BINARY_RESULT_FIND_CLOSE) {
        ret = SvtJxsErrorNone;
        *out_refinement = refinement;
        assert(refinement_last_good == refinement);
        *data_budget_bytes = total_budget_last_good_bytes;
    }

    assert(*data_budget_bytes <= budget_slice_bytes);
    return ret;
}

static SvtJxsErrorType_t rate_control_find_best_quantization_refinement_binary_search(
    svt_jpeg_xs_encoder_common_t *enc_common, uint32_t budget_bytes, precinct_enc_t *precinct, uint8_t *out_quantization,
    uint8_t *out_refinement, uint32_t *out_data_bytes, VerticalPredictionMode coding_vertical_prediction_mode,
    SignHandlingStrategy coding_signs_handling) {
    SvtJxsErrorType_t ret = rate_control_find_best_quantization_binary_search(
        enc_common, budget_bytes, precinct, out_quantization, coding_vertical_prediction_mode, coding_signs_handling);
    if (ret) {
#ifndef NDEBUG
        fprintf(stderr, "[%s[%d]] RC precinct error not found quantization\n", __FUNCTION__, __LINE__);
#endif
        return ret;
    }
#if PRINT_RECALC_VPRED
    printf("Ref: %i:\n", precinct->prec_idx);
#endif
    ret = rate_control_find_best_refinement_binary_search(enc_common,
                                                          budget_bytes,
                                                          precinct,
                                                          *out_quantization,
                                                          out_refinement,
                                                          out_data_bytes,
                                                          coding_vertical_prediction_mode,
                                                          coding_signs_handling);
#ifndef NDEBUG
    if (ret) {
        fprintf(stderr, "[%s[%d]] RC precinct error not found refinement\n", __FUNCTION__, __LINE__);
    }
#endif
    return ret;
}

/*Init precalculate precinct for Rate Control*/
void rate_control_init_precinct(struct PictureControlSet *pcs_ptr, precinct_enc_t *precinct,
                                SignHandlingStrategy coding_signs_handling) {
    UNUSED(coding_signs_handling);
    svt_jpeg_xs_encoder_common_t *enc_common = pcs_ptr->enc_common;
    /*Precalculate LUT*/
    rate_control_lut_fill(enc_common,
#if LUT_SIGNIFICANE
                          enc_common->coding_significance,
#endif
#if (LUT_SIGNIFICANE && SIGN_NOZERO_COEFF_LUT) || LUT_GC_SERIAL_SUMMATION
                          coding_signs_handling,
#endif
                          precinct);
}

/*Find best Quantization and Refinement for Precinct. Need before call rate_control_init_precinct().*/
SvtJxsErrorType_t rate_control_precinct(struct PictureControlSet *pcs_ptr, precinct_enc_t *precinct, uint32_t budget_bytes,
                                        VerticalPredictionMode coding_vertical_prediction_mode,
                                        SignHandlingStrategy coding_signs_handling) {
    svt_jpeg_xs_encoder_common_t *enc_common = pcs_ptr->enc_common;

    if (budget_bytes > PRECINCT_MAX_BYTES_SIZE) {
#ifndef NDEBUG
        fprintf(stderr, "[%s[%d]] RC precinct error Precinct size is Too BIG, use smaller bpp value!!\n", __FUNCTION__, __LINE__);
#endif
        return SvtJxsErrorEncodeFrameError;
    }
    /*Calculate Precinct header size and all Pack headers size*/
    uint32_t headers_bytes = rate_control_get_headers_bytes(enc_common, precinct);
    if (budget_bytes <= headers_bytes) {
#ifndef NDEBUG
        fprintf(stderr, "Impossible compression. Please use bigger bpp param!\n");
#endif
        return SvtJxsErrorUndefined;
    }
    uint32_t budget_to_data_bytes = (budget_bytes - headers_bytes);

    if (coding_vertical_prediction_mode && precinct->precinct_top && precinct->precinct_top->need_recalculate_next_precinct) {
        /*Required if GTLI in any band in previous precinct changed, no need recalculate in first call*/
        rate_control_reset_cache(&enc_common->pi, precinct);
        precinct->precinct_top->need_recalculate_next_precinct = 0;
    }

#if PRINT_RECALC_VPRED
    printf("PrecIndex: %i (Calculate: comp band GTLI):\n", precinct->prec_idx);
#endif

    uint8_t quantization = 0;
    uint8_t refinement = 0;
    uint32_t data_bytes = 0;
    SvtJxsErrorType_t ret = rate_control_find_best_quantization_refinement_binary_search(enc_common,
                                                                                         budget_to_data_bytes,
                                                                                         precinct,
                                                                                         &quantization,
                                                                                         &refinement,
                                                                                         &data_bytes,
                                                                                         coding_vertical_prediction_mode,
                                                                                         coding_signs_handling);
#ifndef NDEBUG
    uint8_t quantization_test = 0;
    uint8_t refinement_test = 0;
    uint32_t data_bytes_test = 0;
    rate_control_reset_cache(&enc_common->pi, precinct); //Only to confirm that result will be the same
    SvtJxsErrorType_t ret_test = rate_control_find_best_quantization_refinement(enc_common,
                                                                                budget_to_data_bytes,
                                                                                precinct,
                                                                                &quantization_test,
                                                                                &refinement_test,
                                                                                &data_bytes_test,
                                                                                coding_vertical_prediction_mode,
                                                                                coding_signs_handling);
    assert(ret_test == ret);
    assert(ret || (quantization == quantization_test));
    assert(ret || (refinement == refinement_test));
    assert(ret || (data_bytes == data_bytes_test));
#endif /*NDEBUG*/
    if (ret) {
        return ret;
    }

#if PRINT_RECALC_VPRED
    printf("\n\n");
#endif

    precinct->pack_quantization = quantization;
    precinct->pack_refinement = refinement;

    precinct->pack_padding_bytes = budget_to_data_bytes - data_bytes;
    precinct->pack_total_bytes = budget_bytes;
    return SvtJxsErrorNone;
}

/*Find fast Quantization for Slice. Need before call rate_control_init_precinct().*/
SvtJxsErrorType_t rate_control_slice_quantization_fast_no_vpred_no_sign_full(struct PictureControlSet *pcs_ptr,
                                                                             precinct_enc_t *precincts, uint32_t prec_num,
                                                                             uint32_t budget_slice_bytes,
                                                                             SignHandlingStrategy coding_signs_handling) {
    svt_jpeg_xs_encoder_common_t *enc_common = pcs_ptr->enc_common;
    if (coding_signs_handling == SIGN_HANDLING_STRATEGY_FULL) {
        coding_signs_handling = SIGN_HANDLING_STRATEGY_FAST;
    }

    if (DIV_ROUND_UP(budget_slice_bytes, prec_num) > PRECINCT_MAX_BYTES_SIZE) {
#ifndef NDEBUG
        fprintf(stderr, "[%s[%d]] RC precinct error Precinct size is Too BIG, use smaller bpp value!!\n", __FUNCTION__, __LINE__);
#endif
        return SvtJxsErrorEncodeFrameError;
    }
    /*Calculate Precinct header size and all Pack headers size*/
    uint32_t headers_bytes = 0;
    for (uint32_t i = 0; i < prec_num; ++i) {
        precincts[i].pack_total_bytes = rate_control_get_headers_bytes(enc_common, &precincts[i]);
        headers_bytes += precincts[i].pack_total_bytes;
    }
    if (budget_slice_bytes <= headers_bytes) {
#ifndef NDEBUG
        fprintf(stderr, "Impossible compression. Please use bigger bpp param!\n");
#endif
        return SvtJxsErrorUndefined;
    }
    uint32_t budget_slice_to_data_bytes = (budget_slice_bytes - headers_bytes);

    /*VPREDICTION disabled so not need to reset cache.*/
#if PRINT_RECALC_VPRED
    printf("Precincts (Calculate: comp band GTLI):\n");
#endif

    uint8_t quantization = 0;
    uint8_t refinement = 0;
    uint32_t data_bytes = 0;
    SvtJxsErrorType_t ret = rate_control_find_best_quantization_fast_no_vpred_no_sign_full_binary_search(
        enc_common, budget_slice_to_data_bytes, precincts, prec_num, &quantization, coding_signs_handling);
    if (ret) {
#ifndef NDEBUG
        fprintf(stderr, "[%s[%d]] RC precinct error not found quantization\n", __FUNCTION__, __LINE__);
#endif
        return ret;
    }
    ret = rate_control_find_best_refinement_fast_no_vpred_no_sign_full_binary_search(enc_common,
                                                                                     budget_slice_to_data_bytes,
                                                                                     precincts,
                                                                                     prec_num,
                                                                                     quantization,
                                                                                     &refinement,
                                                                                     &data_bytes,
                                                                                     coding_signs_handling);
    if (ret) {
#ifndef NDEBUG
        fprintf(stderr, "[%s[%d]] RC precinct error not found refinement\n", __FUNCTION__, __LINE__);
#endif
        return ret;
    }

#if PRINT_RECALC_VPRED
    printf("\n\n");
#endif
    uint32_t padding_last = budget_slice_to_data_bytes;
    for (uint32_t i = 0; i < prec_num; ++i) {
        precincts[i].pack_quantization = quantization;
        precincts[i].pack_refinement = refinement;
        int empty = precinct_encoder_compute_truncation(&enc_common->pi, &precincts[i], quantization, refinement);
        if (empty) {
#ifndef NDEBUG
            fprintf(stderr, "[%s[%d]] RC Invalid recalculate truncation!\n", __FUNCTION__, __LINE__);
#endif
            return SvtJxsErrorEncodeFrameError;
        }
        //TODO: Double recalculation but more values in cache
        uint32_t total_budget_bytes = precinct_get_budget_bytes(
            enc_common, &precincts[i], METHOD_PRED_DISABLE, coding_signs_handling);
        assert(total_budget_bytes <= padding_last);
        padding_last -= total_budget_bytes;

        if (i + 1 >= prec_num) {
            precincts[i].pack_padding_bytes = padding_last;
        }
        else {
            precincts[i].pack_padding_bytes = 0;
        }
        precincts[i].pack_total_bytes += total_budget_bytes + precincts[i].pack_padding_bytes;
    }
    return SvtJxsErrorNone;
}
