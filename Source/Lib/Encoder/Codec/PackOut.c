/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include "PackOut.h"

SvtJxsErrorType_t pack_output_ctor(PackOutput *object_ptr, void_ptr object_init_data_ptr) {
    UNUSED(object_ptr);
    UNUSED(object_init_data_ptr);
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t pack_output_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr) {
    PackOutput *obj;

    *object_dbl_ptr = NULL;
    SVT_NEW(obj, pack_output_ctor, object_init_data_ptr);
    *object_dbl_ptr = obj;

    return SvtJxsErrorNone;
}
