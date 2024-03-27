/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __BINARY_SEARCH_H__
#define __BINARY_SEARCH_H__
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BinarySearch {
    int32_t id_beg;
    int32_t id_end;
    int32_t step;

    int32_t best_idx;
    int32_t last_index;

    /*Politics to find below (1) or above (0) matching value*/
    uint8_t find_below_matching;
} BinarySearch_t;

typedef enum BinarySearchStep {
    BINARY_STEP_BEGIN = 0,
    BINARY_STEP_TOO_SMALL,
    BINARY_STEP_TOO_BIG,
    BINARY_STEP_OUT_OF_RANGE
} BinarySearchStep;

typedef enum BinarySearchResult {
    BINARY_RESULT_CONTINUE = 0,
    BINARY_RESULT_FIND_CLOSE,
    BINARY_RESULT_ERROR //NOT AVAILABLE RESULT
} BinarySearchResult;

/* 
 * Initial binary search. Work only when relation in table is increasing
 * begin_index - minimal search value
 * end_index - maximum search value
 * find_below_matching - when not finish by correct value return at the end closer value below expectation, otherwise above
 * step - initial step of algorithm will be incremented with this step until find above value, or out of range.
 *        If step is zero then binary search from middle of range.
 */
void binary_search_init(BinarySearch_t *b, uint32_t begin_index, uint32_t end_index, uint8_t find_below_matching, uint32_t step);

BinarySearchResult binary_search_next_step(BinarySearch_t *b, BinarySearchStep result, uint32_t *out_next_test);

/*void binary_search_sample() {
    int test_array[36] = {1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,
                          39,41,43,45,47,49,51,53,55,57,59,61,63,65,67,69,71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];
    int value_to_find = 7;

    BinarySearch_t search;
    uint32_t index;
    BinarySearchResult result;
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    int find_below_or_equal = 1;
    binary_search_init(&search, test_array_size, find_below_or_equal, 0);

    //SAMPLE MAIN LOOP:
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
        if (index >= test_array_size) {
            //Required when add too big size
            next_step = BINARY_STEP_OUT_OF_RANGE;
        } else if (test_array[index] < value_to_find) {
            next_step = BINARY_STEP_TOO_SMALL;
        } else {
            //(test_array[index] < value_to_find)
            next_step = BINARY_STEP_TOO_BIG;
        }
    }

    if (result == BINARY_RESULT_FIND_CLOSE) {
        if (find_below_or_equal) {
            printf("Found max below target value index %i\n", index);
        } else {
            printf("Found min above target value index %i\n", index);
        }
    } else  if (result == BINARY_RESULT_ERROR) {
        printf("Result not found\n");
    }
} */

#ifdef __cplusplus
}
#endif
#endif /*__BINARY_SEARCH_H__*/
