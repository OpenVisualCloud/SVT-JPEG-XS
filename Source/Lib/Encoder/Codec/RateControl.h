/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __RATE_CONTROL_H__
#define __RATE_CONTROL_H__
#include "RateControlCacheType.h"
#include "Pi.h"
#include "PrecinctEnc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PictureControlSet;

void rate_control_init_precinct(struct PictureControlSet *pcs_ptr, precinct_enc_t *precinct,
                                SignHandlingStrategy coding_signs_handling);
SvtJxsErrorType_t rate_control_precinct(struct PictureControlSet *pcs_ptr, precinct_enc_t *precinct, uint32_t budget_bytes,
                                        VerticalPredictionMode coding_vertical_prediction_mode,
                                        SignHandlingStrategy coding_signs_handling);
SvtJxsErrorType_t rate_control_slice_quantization_fast_no_vpred_no_sign_full(struct PictureControlSet *pcs_ptr,
                                                                             precinct_enc_t *precincts, uint32_t prec_num,
                                                                             uint32_t budget_slice_bytes,
                                                                             SignHandlingStrategy coding_signs_handling);

uint32_t rate_control_calc_vpred_cost_nosigf_c(uint32_t gcli_width, uint8_t *gcli_data_top_ptr, uint8_t *gcli_data_ptr,
                                               uint8_t *vpred_bits_pack, uint8_t gtli, uint8_t gtli_max);
void rate_control_calc_vpred_cost_sigf_nosigf_c(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                uint32_t significance_group_size, uint8_t *gcli_data_top_ptr,
                                                uint8_t *gcli_data_ptr, uint8_t *vpred_bits_pack, uint8_t *vpred_significance,
                                                uint8_t gtli, uint8_t gtli_max, uint32_t *pack_size_gcli_sigf_reduction,
                                                uint32_t *pack_size_gcli_no_sigf);

#ifdef __cplusplus
}
#endif
#endif /*__RATE_CONTROL_H__*/
