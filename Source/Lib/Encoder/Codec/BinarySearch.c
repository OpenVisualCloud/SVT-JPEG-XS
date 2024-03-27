/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "BinarySearch.h"
#include <assert.h>

void binary_search_init(BinarySearch_t *b, uint32_t begin_index, uint32_t end_index, uint8_t find_below_matching, uint32_t step) {
    assert(end_index >= begin_index);
    b->id_beg = begin_index;
    b->id_end = end_index;
    b->find_below_matching = find_below_matching;
    b->best_idx = -1;
    b->last_index = -1;
    if (step) {
        assert(begin_index <= step && step <= end_index);
        //Set initial step. Will be reduced when can not be continue.
        b->step = step;
    }
    else {
        //Binary Search
        b->step = (b->id_end - b->id_beg) / 2;
    }
}

BinarySearchResult binary_search_next_step(BinarySearch_t *b, BinarySearchStep result, uint32_t *out_next_test) {
    assert(b->id_beg <= b->id_end);

    if (BINARY_STEP_TOO_SMALL == result) {
        if (b->find_below_matching) {
            if (b->best_idx == -1 || b->best_idx < b->last_index) {
                b->best_idx = b->last_index;
            }
        }
        if (b->id_end >= b->last_index + 1) {
            b->id_beg = b->last_index + 1;
        }
        else {
            if (b->best_idx == -1) {
                return BINARY_RESULT_ERROR;
            }
            else {
                *out_next_test = b->best_idx;
                return BINARY_RESULT_FIND_CLOSE;
            }
        }
    }
    else if (BINARY_STEP_TOO_BIG == result || BINARY_STEP_OUT_OF_RANGE == result) {
        if (BINARY_STEP_TOO_BIG == result) {
            if (!b->find_below_matching) {
                if (b->best_idx == -1 || b->best_idx > b->last_index) {
                    b->best_idx = b->last_index;
                }
            }
        }
        //BINARY_STEP_TOO_BIG or BINARY_STEP_OUT_OF_RANGE
        if (b->id_beg <= b->last_index - 1) {
            b->id_end = b->last_index - 1;
        }
        else {
            if (b->best_idx == -1) {
                return BINARY_RESULT_ERROR;
            }
            else {
                *out_next_test = b->best_idx;
                return BINARY_RESULT_FIND_CLOSE;
            }
        }
    }

    if (b->step > b->id_end - b->id_beg) {
        b->step = (b->id_end - b->id_beg + 1) / 2;
    }
    b->last_index = b->step + b->id_beg;
    *out_next_test = b->last_index;
    return BINARY_RESULT_CONTINUE;
}
