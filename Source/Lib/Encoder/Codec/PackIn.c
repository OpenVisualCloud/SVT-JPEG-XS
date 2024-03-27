/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include "PackIn.h"
#include "Encoder.h"
#include "Threads/SvtThreads.h"

void pack_input_dctor(void_ptr p) {
    PackInput_t* object_ptr = (PackInput_t*)p;
    if (object_ptr->sync_dwt_semaphore) {
        SVT_DESTROY_SEMAPHORE(object_ptr->sync_dwt_semaphore);
    }
}

SvtJxsErrorType_t pack_input_ctor(PackInput_t* object_ptr, void_ptr object_init_data_ptr) {
    (void)object_ptr;
    (void)object_init_data_ptr;
    object_ptr->dctor = pack_input_dctor;
    svt_jpeg_xs_encoder_common_t* enc_common = object_init_data_ptr;
    if (enc_common->cpu_profile == CPU_PROFILE_CPU) {
        SVT_CREATE_SEMAPHORE(object_ptr->sync_dwt_semaphore, 0, 1);
        if (object_ptr->sync_dwt_semaphore == NULL) {
            return SvtJxsErrorInsufficientResources;
        }
    }
    else {
        object_ptr->sync_dwt_semaphore = NULL;
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t pack_input_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    PackInput_t* obj;

    *object_dbl_ptr = NULL;
    SVT_NEW(obj, pack_input_ctor, object_init_data_ptr);
    *object_dbl_ptr = obj;

    return SvtJxsErrorNone;
}
