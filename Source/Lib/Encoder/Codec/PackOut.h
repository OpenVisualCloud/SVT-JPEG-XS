/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef PackOut_h
#define PackOut_h

#include "Definitions.h"
#include "Threads/SvtObject.h"
#include "Threads/SystemResourceManager.h"
#ifdef __cplusplus
extern "C" {
#endif
/**************************************
 * slice pack process output
 **************************************/
typedef struct PackOutput {
    DctorCall dctor;
    ObjectWrapper_t *pcs_wrapper_ptr;
    uint32_t slice_idx;
    SvtJxsErrorType_t slice_error;
} PackOutput;

typedef struct PackOutputInitData {
    int32_t junk;
} PackOutputInitData;

/**************************************
 * Extern Function Declarations
 **************************************/
extern SvtJxsErrorType_t pack_output_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr);

#ifdef __cplusplus
}
#endif
#endif // PackOut_h
