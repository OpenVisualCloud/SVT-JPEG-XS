/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include <string.h>

#include "EncHandle.h"
#include "PictureControlSet.h"
#include "PreRcStageProcess.h"
#include "Threads/SystemResourceManager.h"
#include "SvtLog.h"
#include "Threads/SvtObject.h"
#include "SvtUtility.h"
#include "common_dsp_rtcd.h"
#include "Codestream.h"
#include "BitstreamWriter.h"
#include "PackHeaders.h"
#include "Pi.h"
#include "Threads/SvtThreads.h"

uint32_t write_pic_level_header_nbytes(uint8_t* buffer_ptr, size_t buffer_size, svt_jpeg_xs_encoder_common_t* enc_common) {
    bitstream_writer_t bitstream;
    bitstream_writer_init(&bitstream, buffer_ptr, buffer_size);
    write_header(&bitstream, enc_common);

    return bitstream_writer_get_used_bytes(&bitstream);
}

PackInput_t* pre_rc_send_frame_to_pack_slices(PictureControlSet* pcs_ptr, Fifo_t* output_buffer_fifo_ptr, uint64_t frame_num,
                                              ObjectWrapper_t* pcs_wrapper_ptr) {
    UNUSED(frame_num); // Value only used when FLAG_DEADLOCK_DETECT is enabled
    svt_jpeg_xs_encoder_common_t* enc_common = pcs_ptr->enc_common;
    PackInput_t* first = NULL;
    ObjectWrapper_t* output_wrapper_ptr;
    ObjectWrapper_t* output_wrapper_ptr_next = NULL;

    /*Tested in svt_jpeg_xs_encoder_send_picture()*/
    assert(enc_common->frame_header_length_bytes < pcs_ptr->enc_input.bitstream.allocation_size);

    //Copy image header
    memcpy(pcs_ptr->enc_input.bitstream.buffer, enc_common->frame_header_buffer, enc_common->frame_header_length_bytes);
    pcs_ptr->enc_input.bitstream.used_size = enc_common->picture_header_dynamic.hdr_Lcod;

    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        //All tasks need pointer no next one in frame. Get first task.
        svt_get_empty_object(output_buffer_fifo_ptr, &output_wrapper_ptr_next);
    }

    uint32_t output_bytes_begin = enc_common->frame_header_length_bytes;
    for (uint32_t i = 0; i < enc_common->pi.slice_num; i++) {
        PackInput_t* pack_input;
        if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
            output_wrapper_ptr = output_wrapper_ptr_next;
            if (output_wrapper_ptr == NULL) {
                /*This path should never happen!*/
                fprintf(stderr, "Fatal Error: Pre RC Stage Process, Invalid next slice pointer!\n");
                return NULL;
            }
            pack_input = (PackInput_t*)output_wrapper_ptr->object_ptr;
            memset((void*)pack_input->sync_dwt_component_done_flag, 0, sizeof(pack_input->sync_dwt_component_done_flag));
            if (!first) {
                first = pack_input;
            }
            if (i + 1 < enc_common->pi.slice_num) {
                svt_get_empty_object(output_buffer_fifo_ptr, &output_wrapper_ptr_next);
                pack_input->sync_dwt_list_next = (PackInput_t*)output_wrapper_ptr_next->object_ptr;
            }
            else {
                output_wrapper_ptr_next = NULL;
                pack_input->sync_dwt_list_next = NULL;
            }
        }
        else {
            svt_get_empty_object(output_buffer_fifo_ptr, &output_wrapper_ptr);
            pack_input = (PackInput_t*)output_wrapper_ptr->object_ptr;
        }

        pack_input->slice_idx = i;
        pack_input->out_bytes_begin = output_bytes_begin;

        if (i != enc_common->pi.slice_num - 1) {
            pack_input->slice_budget_bytes = enc_common->slice_sizes[i] - SLICE_HEADER_SIZE_BYTES;
            pack_input->out_bytes_end = pack_input->out_bytes_begin + enc_common->slice_sizes[i];
            pack_input->write_tail = 0;
        }
        else {
            pack_input->slice_budget_bytes = enc_common->slice_sizes[i] - SLICE_HEADER_SIZE_BYTES - CODESTREAM_SIZE_BYTES;
            pack_input->out_bytes_end = pack_input->out_bytes_begin + enc_common->slice_sizes[i] - CODESTREAM_SIZE_BYTES;
            //Last slice, End of Bitstream
            pack_input->tail_bytes_begin = pack_input->out_bytes_end;
            pack_input->write_tail = 1;
        }

        output_bytes_begin = pack_input->out_bytes_end;
        pack_input->pcs_wrapper_ptr = pcs_wrapper_ptr;
#ifdef FLAG_DEADLOCK_DETECT
        printf("04[%s:%i] frame: %03li slice: %03d\n", __func__, __LINE__, (size_t)frame_num, pack_input->slice_idx);
#endif
        //Send direct to PACK
        svt_post_full_object(output_wrapper_ptr);
    }
    return first;
}
