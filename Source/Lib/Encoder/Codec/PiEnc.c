/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "PiEnc.h"
#include "EncDec.h"
#include "SvtUtility.h"
#include "PrecinctEnc.h"

int pi_enc_weight_table_calc_max_quantization_refinement(pi_t* pi, pi_enc_t* pi_enc) {
    int ret = 0;

    /*From specification:
     * Refinement range [0, Nl -1] where Nl == pi->bands_num_all
     * Quantization range [0, 31]
     *From weight table it is possible to calculate max occurring refinement and quantization.
     */

    /*Find the last maximum refinement value that might make any difference
    in the truncation in quantization calculation.*/
    pi_enc->max_refinement = 0;
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
            if (pi->components[c].bands[b].priority > pi_enc->max_refinement) {
                pi_enc->max_refinement = pi->components[c].bands[b].priority;
            }
        }
    }
    /*When compared to priority, the refinement can be less or equal,
    but we extend it to all values that will be less than max.*/
    if (pi_enc->max_refinement < 255) {
        pi_enc->max_refinement += 1;
    }

    //Reduce maximum value to specification
    if (pi_enc->max_refinement > pi->bands_num_all - 1) {
        pi_enc->max_refinement = pi->bands_num_all - 1;
    }

    /*For bigger refinement it is possible to get bigger quantization with nonzero quantization result.*/
    precinct_t precinct_empty;
    pi_enc->max_quantization = 31;
    while (pi_enc->max_quantization > 0) {
        ret = precinct_compute_truncation(pi, &precinct_empty, pi_enc->max_quantization, pi_enc->max_refinement);
        if (ret == 0) {
            break;
        }
        pi_enc->max_quantization--;
    }

    return 0;
}

int pi_compute_encoder(pi_t* pi, pi_enc_t* pi_enc, uint8_t significance_flag, uint8_t vpred_flag, uint8_t verbose) {
    int ret = 0;

    ret = pi_enc_weight_table_calc_max_quantization_refinement(pi, pi_enc);
    if (verbose) {
        printf("Max Quantization %i, Max Refinement: %i\n", pi_enc->max_quantization, pi_enc->max_refinement);
    }
    if (ret) {
        return ret;
    }

    /*Calculate internal buffers for DWT in Encoder*/
    uint32_t offset;
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        offset = 0;
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            uint32_t w = pi->components[c].bands[b].width;
            uint32_t height_lines_num = pi->components[c].bands[b].height_lines_num;
            pi_enc->components[c].bands[b].coeff_buff_tmp_pos_offset_16bit = offset;
            offset += w * height_lines_num;
        }
        pi_enc->coeff_buff_tmp_size_precinct[c] = offset;
    }

    /*Calculate internal buffers for GC in Encoder*/
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        offset = 0;
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            uint32_t gcli_width = pi->p_info[PRECINCT_NORMAL].b_info[c][b].gcli_width;
            uint32_t height_lines_num = pi->components[c].bands[b].height_lines_num;
            pi_enc->components[c].bands[b].gc_buff_tmp_pos_offset = offset;
            offset += gcli_width * height_lines_num;
        }
        pi_enc->gc_buff_tmp_size_precinct[c] = offset;
    }

    //TODO: CACHE Reorganize all offsets to all data from one precinct with all bands will be keep in continuous memory.
    offset = 0;
    if (significance_flag) {
        for (uint32_t c = 0; c < pi->comps_num; ++c) {
            offset = 0;
            for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
                uint32_t significance_width = pi->p_info[PRECINCT_NORMAL].b_info[c][b].significance_width;
                uint32_t height_lines_num = pi->components[c].bands[b].height_lines_num;
                pi_enc->components[c].bands[b].gc_buff_tmp_significance_pos_offset = offset;
                offset += significance_width * height_lines_num;
            }
            pi_enc->gc_buff_tmp_significance_size_precinct[c] = offset;
        }
    }

    if (vpred_flag) {
        for (uint32_t c = 0; c < pi->comps_num; ++c) {
            offset = 0;
            for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
                uint32_t gcli_width = pi->p_info[PRECINCT_NORMAL].b_info[c][b].gcli_width;
                uint32_t height_lines_num = pi->components[c].bands[b].height_lines_num;
                pi_enc->components[c].bands[b].vped_bit_pack_tmp_buff_offset = offset;
                offset += (gcli_width * height_lines_num) * RC_BAND_CACHE_SIZE;
            }
            pi_enc->vped_bit_pack_size_precinct[c] = offset;
        }
        if (significance_flag) {
            for (uint32_t c = 0; c < pi->comps_num; ++c) {
                offset = 0;
                for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
                    uint32_t significance_width = pi->p_info[PRECINCT_NORMAL].b_info[c][b].significance_width;
                    uint32_t height_lines_num = pi->components[c].bands[b].height_lines_num;
                    pi_enc->components[c].bands[b].vped_significance_tmp_buff_offset = offset;
                    offset += (significance_width * height_lines_num) * RC_BAND_CACHE_SIZE;
                }
                pi_enc->vped_significance_size_precinct[c] = offset;
            }
        }
    }
    return ret;
}
