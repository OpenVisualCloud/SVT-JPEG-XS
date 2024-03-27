/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PICTURE_CONTROL_SET_H_
#define _PICTURE_CONTROL_SET_H_

#include "Definitions.h"
#include "Encoder.h"
#include "SvtJpegxsEnc.h"
#include "Threads/SystemResourceManager.h"
#include "SvtType.h"
#include "SvtUtility.h"
#include "GcStageProcess.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PictureControlSet {
    /*!< Pointer to the dtor of the struct*/
    DctorCall dctor;
    svt_jpeg_xs_encoder_common_t *enc_common;
    svt_jpeg_xs_frame_t enc_input;
    int32_t frame_error;
    uint64_t frame_number;

    /*Buffers to keep all data in frame*/
    uint16_t *coeff_buff_ptr_16bit[MAX_COMPONENTS_NUM]; //Requires for Profile: CPU
    uint32_t slice_cnt;

    uint8_t *slice_ready_to_release_arr;
    uint32_t slice_released_idx;
    uint32_t bitstream_release_offset;
} PictureControlSet;

/**************************************
 * Extern Function Declarations
 **************************************/
extern SvtJxsErrorType_t picture_control_set_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr);

#ifdef __cplusplus
}
#endif
#endif /*_PICTURE_CONTROL_SET_H_*/
