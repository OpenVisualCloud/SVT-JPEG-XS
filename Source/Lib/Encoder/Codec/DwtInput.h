/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _DWT_INPUT_H_
#define _DWT_INPUT_H_

#include "Definitions.h"
#include "Threads/SvtObject.h"
#include "Threads/SystemResourceManager.h"
#include "PreRcStageProcess.h"
#ifdef __cplusplus
extern "C" {
#endif
/**************************************
 * Process Results
 **************************************/
typedef struct DwtInput {
    DctorCall dctor;
    ObjectWrapper_t *pcs_wrapper_ptr;
    uint32_t component_id;
    uint64_t frame_num;
    PackInput_t *list_slices; /*List of slices to sync in profile: CPU.*/
} DwtInput;

typedef struct DwtInputInitData {
    int32_t junk;
} DwtInputInitData;

/**************************************
 * Extern Function Declarations
 **************************************/
extern SvtJxsErrorType_t dwt_input_creator(void_ptr *object_dbl_ptr, void_ptr object_init_data_ptr);

#ifdef __cplusplus
}
#endif
#endif /*_DWT_INPUT_H_*/
