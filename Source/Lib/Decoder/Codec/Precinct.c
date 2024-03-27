/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Precinct.h"
#include "Codestream.h"

void inv_sign_c(uint16_t* in_out, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) {
        const uint16_t val = in_out[i];
        in_out[i] = ((val & BITSTREAM_MASK_SIGN) ? -((val & ~BITSTREAM_MASK_SIGN)) : val);
    }
}

void inv_precinct_calculate_data(precinct_t* precinct, const pi_t* const pi, int dq_type) {
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
            precinct_band_t* b_data = &precinct->bands[c][b];
            precinct_band_info_t* b_info = &precinct->p_info->b_info[c][b];
            for (uint32_t height = 0; height < b_info->height; height++) {
                uint16_t* coeff_data = (b_data->coeff_data + height * pi->components[c].bands[b].width);
                uint8_t* gcli_data = b_data->gcli_data + height * b_info->gcli_width;
                dequant(coeff_data, b_info->width, gcli_data, pi->coeff_group_size, b_data->gtli, dq_type);
                inv_sign(coeff_data, b_info->width);
            }
        }
    }
}
