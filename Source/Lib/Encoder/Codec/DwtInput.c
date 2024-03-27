/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>

#include "DwtInput.h"

SvtJxsErrorType_t dwt_input_ctor(DwtInput *object_ptr, void_ptr object_init_data_ptr) {
    (void)object_ptr;
    (void)object_init_data_ptr;

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t dwt_input_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr) {
    DwtInput *obj;

    *object_dbl_ptr = NULL;
    SVT_NEW(obj, dwt_input_ctor, object_init_data_ptr);
    *object_dbl_ptr = obj;

    return SvtJxsErrorNone;
}
