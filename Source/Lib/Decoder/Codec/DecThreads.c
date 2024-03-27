/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "DecThreads.h"
#include "DecHandle.h"

SvtJxsErrorType_t output_frame_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_init_data_ptr);
    TaskOutFrame* input_buffer;

    *object_dbl_ptr = NULL;
    SVT_CALLOC(input_buffer, 1, sizeof(TaskOutFrame));
    *object_dbl_ptr = (void_ptr)input_buffer;

    return SvtJxsErrorNone;
}

void output_frame_destroyer(void_ptr p) {
    TaskOutFrame* obj = (TaskOutFrame*)p;
    SVT_FREE(obj);
}

SvtJxsErrorType_t pool_decoder_instance_create_ctor(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    svt_jpeg_xs_decoder_api_prv_t* dec_api_prv = (svt_jpeg_xs_decoder_api_prv_t*)object_init_data_ptr;
    svt_jpeg_xs_decoder_instance_t* ctx = svt_jpeg_xs_dec_instance_alloc(&dec_api_prv->dec_common);
    if (ctx == NULL) {
        return SvtJxsErrorDecoderInternal;
    }
    *object_dbl_ptr = ctx;
    return SvtJxsErrorNone;
}

void pool_decoder_instance_destroy_ctor(void_ptr p) {
    svt_jpeg_xs_decoder_instance_t* ctx = (svt_jpeg_xs_decoder_instance_t*)p;
    svt_jpeg_xs_dec_instance_free(ctx);
}
