/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "PictureControlSet.h"
#include "QuantStageProcess.h"
#include "common_dsp_rtcd.h"
#include "encoder_dsp_rtcd.h"
#include "Quant.h"
#include "PrecinctEnc.h"

void precinct_quantization(PictureControlSet *pcs_ptr, pi_t *pi, precinct_enc_t *precinct) {
    svt_jpeg_xs_encoder_common_t *enc_common = pcs_ptr->enc_common;
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            struct band_data_enc *band = &precinct->bands[c][b];
            uint32_t height_lines = precinct->p_info->b_info[c][b].height;

            /*Not call Quanization when nothing will be quantized If gtli == 0,
              * because input and output is on that same buffer.*/
            if (band->gtli) {
                for (uint32_t line = 0; line < height_lines; ++line) {
                    quantization(band->lines_common[line].coeff_data_ptr_16bit,
                                 precinct->p_info->b_info[c][b].width,
                                 band->lines_common[line].gcli_data_ptr,
                                 pi->coeff_group_size,
                                 band->gtli,
                                 enc_common->picture_header_dynamic.hdr_Qpih);
                }
            }
        }
    }
}
