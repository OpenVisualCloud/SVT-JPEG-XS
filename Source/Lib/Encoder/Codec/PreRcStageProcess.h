/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _PRE_RC_STAGE_H_
#define _PRE_RC_STAGE_H_

#include "Definitions.h"
#include "Encoder.h"
#include "Threads/SystemResourceManager.h"
#include "Threads/SvtThreads.h"
#include "PictureControlSet.h"
#include "BitstreamWriter.h"
#include "PackIn.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t write_pic_level_header_nbytes(uint8_t* buffer_ptr, size_t buffer_size, svt_jpeg_xs_encoder_common_t* enc_common);

PackInput_t* pre_rc_send_frame_to_pack_slices(PictureControlSet* pcs_ptr, Fifo_t* output_buffer_fifo_ptr, uint64_t frame_num,
                                              ObjectWrapper_t* pcs_wrapper_ptr);

#ifdef __cplusplus
}
#endif
#endif /*_PRE_RC_STAGE_H_*/
