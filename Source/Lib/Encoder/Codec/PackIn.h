/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef PackIn_h
#define PackIn_h

#include "Definitions.h"
#include "Threads/SvtObject.h"
#include "Threads/SystemResourceManager.h"
#include "Pi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************
 * slice pack process input
 **************************************/
typedef struct PackInput {
    DctorCall dctor;
    ObjectWrapper_t* pcs_wrapper_ptr;
    uint32_t slice_idx;
    uint32_t slice_budget_bytes;
    uint32_t out_bytes_begin;
    uint32_t out_bytes_end;
    uint32_t tail_bytes_begin;
    uint8_t write_tail;

    /*Sync between pack tasks. Required for some CPU Profiles.*/
    volatile struct PackInput* sync_dwt_list_next; //One direction list to get next pack task in frame
    Handle_t sync_dwt_semaphore;
    volatile uint32_t sync_dwt_component_done_flag[MAX_COMPONENTS_NUM];
} PackInput_t;

/**************************************
 * Extern Function Declarations
 **************************************/
extern SvtJxsErrorType_t pack_input_creator(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr);

#ifdef __cplusplus
}
#endif

#endif // PackIn_h
