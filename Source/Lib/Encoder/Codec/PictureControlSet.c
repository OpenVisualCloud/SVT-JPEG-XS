/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include "PictureControlSet.h"

void picture_control_set_dctor(void_ptr p) {
    PictureControlSet* obj = (PictureControlSet*)p;
    svt_jpeg_xs_encoder_common_t* enc_common = obj->enc_common;
    pi_t* pi = &enc_common->pi;
    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        for (uint32_t c = 0; c < pi->comps_num; ++c) {
            if (pi->components[c].decom_v == 1 || pi->components[c].decom_v == 2) {
                SVT_FREE_ALIGNED_ARRAY(obj->coeff_buff_ptr_16bit[c]);
            }
        }
    }

    if (enc_common->slice_packetization_mode) {
        SVT_FREE(obj->slice_ready_to_release_arr);
    }
}

SvtJxsErrorType_t picture_control_set_ctor(PictureControlSet* obj, void_ptr object_init_data_ptr) {
    SvtJxsErrorType_t return_error = SvtJxsErrorNone;
    svt_jpeg_xs_encoder_common_t* enc_common = (svt_jpeg_xs_encoder_common_t*)object_init_data_ptr;
    pi_t* pi = &enc_common->pi;
    pi_enc_t* pi_enc = &enc_common->pi_enc;

    obj->dctor = picture_control_set_dctor;
    obj->enc_common = enc_common;

    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        obj->coeff_buff_ptr_16bit[c] = NULL;
        if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
            if (pi->components[c].decom_v == 1 || pi->components[c].decom_v == 2) {
                SVT_MALLOC_ALIGNED_ARRAY(obj->coeff_buff_ptr_16bit[c],
                                         (size_t)pi_enc->coeff_buff_tmp_size_precinct[c] * pi->precincts_line_num);
            }
        }
    }

    if (enc_common->slice_packetization_mode) {
        SVT_MALLOC(obj->slice_ready_to_release_arr, pi->slice_num);
    }

    return return_error;
}

SvtJxsErrorType_t picture_control_set_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    PictureControlSet* obj;

    *object_dbl_ptr = NULL;
    SVT_NEW(obj, picture_control_set_ctor, object_init_data_ptr);
    *object_dbl_ptr = obj;

    return SvtJxsErrorNone;
}
