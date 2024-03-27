/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include <RateControl.h>
#include <BinarySearch.h>

TEST(RateControl, EqualSimple) {
    BinarySearch_t search;
    int test_array[36] = {1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
                          37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];

    for (uint32_t target_index = 0; target_index < test_array_size; ++target_index) {
        for (uint32_t min_index = 0; min_index < test_array_size - 2; ++min_index) {
            binary_search_init(&search, min_index, test_array_size - 1, 0, 0);
            memset(test_array_flags, 0, test_array_size);
            uint32_t index = 999;
            int32_t find = test_array[target_index];
            BinarySearchResult result;
            BinarySearchStep next_step = BINARY_STEP_BEGIN;
            //Main loop:
            //int counter = 0;
            //printf("Test Index: ");
            while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
                //counter++;
                //printf("%i ", index);
                if (index < test_array_size) {
                    //Test double check
                    ASSERT_EQ(test_array_flags[index], 0);
                    test_array_flags[index] = 1;
                }
                if (index >= test_array_size) {
                    next_step = BINARY_STEP_OUT_OF_RANGE;
                }
                else if (test_array[index] < find) {
                    next_step = BINARY_STEP_TOO_SMALL;
                }
                else {
                    //(test_array[index] < find)
                    next_step = BINARY_STEP_TOO_BIG;
                }
            }
            //printf("Counter: %i\n", counter);
            if (min_index >= target_index) {
                ASSERT_EQ(index, min_index);
            }
            else {
                ASSERT_EQ(index, target_index);
            }
            ASSERT_EQ(result, BINARY_STEP_TOO_SMALL);
        }
    }
}

TEST(RateControl, NotEqualBiggerSimple) {
    BinarySearch_t search;
    int test_array[36] = {1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
                          37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];

    for (uint32_t target_index = 0; target_index < test_array_size; ++target_index) {
        binary_search_init(&search, 0, test_array_size - 1, 0, 0);
        memset(test_array_flags, 0, test_array_size);
        uint32_t index = 999;
        int32_t find = test_array[target_index] - 1;
        BinarySearchResult result;
        BinarySearchStep next_step = BINARY_STEP_BEGIN;
        //Main loop:
        //int counter = 0;
        //printf("Test Index: ");
        while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
            //counter++;
            //printf("%i ", index);
            if (index < test_array_size) {
                //Test double check
                ASSERT_EQ(test_array_flags[index], 0);
                test_array_flags[index] = 1;
            }

            if (index >= test_array_size) {
                next_step = BINARY_STEP_OUT_OF_RANGE;
            }
            else if (test_array[index] < find) {
                next_step = BINARY_STEP_TOO_SMALL;
            }
            else {
                //(test_array[index] < find)
                next_step = BINARY_STEP_TOO_BIG;
            }
        }
        //printf("Counter: %i\n", counter);

        ASSERT_EQ(test_array[index] - 1, find);
        ASSERT_EQ(result, BINARY_RESULT_FIND_CLOSE);
    }
}

TEST(RateControl, NotEqualBiggerInvalidSimple) {
    BinarySearch_t search;
    int test_array[36] = {1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
                          37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];

    binary_search_init(&search, 0, test_array_size - 1, 0, 0);
    memset(test_array_flags, 0, test_array_size);
    uint32_t index = 999;
    int32_t find = test_array[test_array_size - 1] + 1;
    BinarySearchResult result;
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    //Main loop:
    //int counter = 0;
    //printf("Test Index: ");
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
        //counter++;
        //printf("%i ", index);
        if (index < test_array_size) {
            //Test double check
            ASSERT_EQ(test_array_flags[index], 0);
            test_array_flags[index] = 1;
        }
        if (index >= test_array_size) {
            next_step = BINARY_STEP_OUT_OF_RANGE;
        }
        else if (test_array[index] < find) {
            next_step = BINARY_STEP_TOO_SMALL;
        }
        else {
            //(test_array[index] < find)
            next_step = BINARY_STEP_TOO_BIG;
        }
    }
    //printf("Counter: %i\n", counter);
    ASSERT_EQ(result, BINARY_RESULT_ERROR);
}

TEST(RateControl, NotEqualSmallerSimple) {
    BinarySearch_t search;
    int test_array[36] = {1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
                          37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];

    for (uint32_t target_index = 0; target_index < test_array_size; ++target_index) {
        binary_search_init(&search, 0, test_array_size - 1, 1, 0);
        memset(test_array_flags, 0, test_array_size);
        uint32_t index = 999;
        int32_t find = test_array[target_index] + 1;
        BinarySearchResult result;
        BinarySearchStep next_step = BINARY_STEP_BEGIN;
        //Main loop:
        //int counter = 0;
        //printf("Test Index: ");
        while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
            //counter++;
            //printf("%i ", index);
            if (index < test_array_size) {
                //Test double check
                ASSERT_EQ(test_array_flags[index], 0);
                test_array_flags[index] = 1;
            }
            if (index >= test_array_size) {
                next_step = BINARY_STEP_OUT_OF_RANGE;
            }
            else if (test_array[index] < find) {
                next_step = BINARY_STEP_TOO_SMALL;
            }
            else {
                //(test_array[index] < find)
                next_step = BINARY_STEP_TOO_BIG;
            }
        }
        //printf("Counter: %i\n", counter);
        ASSERT_EQ(test_array[index] + 1, find);
        ASSERT_EQ(result, BINARY_RESULT_FIND_CLOSE);
    }
}

TEST(RateControl, NotEqualSmallerInvalidSimple) {
    BinarySearch_t search;
    int test_array[36] = {1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
                          37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];

    binary_search_init(&search, 0, test_array_size - 1, 1, 0);
    memset(test_array_flags, 0, test_array_size);
    uint32_t index = 999;
    int32_t find = test_array[0] - 1;
    BinarySearchResult result;
    BinarySearchStep next_step = BINARY_STEP_BEGIN;
    //Main loop:
    //int counter = 0;
    //printf("Test Index: ");
    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
        //counter++;
        //printf("%i ", index);
        if (index < test_array_size) {
            //Test double check
            ASSERT_EQ(test_array_flags[index], 0);
            test_array_flags[index] = 1;
        }
        if (index >= test_array_size) {
            next_step = BINARY_STEP_OUT_OF_RANGE;
        }
        else if (test_array[index] < find) {
            next_step = BINARY_STEP_TOO_SMALL;
        }
        else {
            //(test_array[index] < find)
            next_step = BINARY_STEP_TOO_BIG;
        }
    }
    //printf("Counter: %i\n", counter);
    ASSERT_EQ(result, BINARY_RESULT_ERROR);
}

static void Test_next_step_full(uint32_t find_below_matching, int32_t target_diff) {
    BinarySearch_t search;
    int test_array[36] = {1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35,
                          37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71};
    uint32_t test_array_size = 36;
    char test_array_flags[36];

    //Test check all combinations and strategy to confirm that any result can be achieved.
    do {
        for (int init_size_correct = 0; init_size_correct < 2; ++init_size_correct) {
            for (uint32_t check_steps = 0; check_steps < test_array_size + 2; ++check_steps) {
                int max_count = 0;
                if (init_size_correct == 0) {
                    uint32_t tmp;
                    if (check_steps == 0) {
                        tmp = test_array_size;
                    }
                    else if (check_steps < test_array_size) {
                        max_count = test_array_size / check_steps;
                        tmp = check_steps;
                    }
                    else {
                        max_count = check_steps / test_array_size;
                        tmp = check_steps;
                    }
                    //Log2
                    while (tmp) {
                        ++max_count;
                        tmp /= 2;
                    }
                    ++max_count;
                }
                for (uint32_t target_index = 0; target_index < test_array_size; ++target_index) {
                    uint32_t end_index = init_size_correct * 999999 + test_array_size - 1;
                    binary_search_init(&search,
                                       0,
                                       end_index,
                                       find_below_matching,
                                       check_steps <= end_index ? check_steps : 0 /*0 binary search*/);
                    memset(test_array_flags, 0, test_array_size);
                    uint32_t index = 999;
                    int32_t find = test_array[target_index] + target_diff;
                    BinarySearchResult result;
                    BinarySearchStep next_step = BINARY_STEP_BEGIN;
                    //Main loop:
                    int counter = 0;
                    //printf("Test Index: ");
                    while (BINARY_RESULT_CONTINUE == (result = binary_search_next_step(&search, next_step, &index))) {
                        counter++;
                        //printf("%i ", index);
                        if (index < test_array_size) {
                            //Test double check
                            ASSERT_EQ(test_array_flags[index], 0);
                            test_array_flags[index] = 1;
                        }
                        if (index >= test_array_size) {
                            next_step = BINARY_STEP_OUT_OF_RANGE;
                        }
                        else if (test_array[index] == find) {
                            if (find_below_matching) {
                                next_step = BINARY_STEP_TOO_SMALL;
                            }
                            else {
                                next_step = BINARY_STEP_TOO_BIG;
                            }
                        }
                        else if (test_array[index] < find) {
                            next_step = BINARY_STEP_TOO_SMALL;
                        }
                        else {
                            //(test_array[index] < find)
                            next_step = BINARY_STEP_TOO_BIG;
                        }
                    }
                    //printf("Counter: %i\n", counter);
                    if (max_count) {
                        ASSERT_LE(counter, max_count);
                    }
                    if (target_diff == 0) {
                        ASSERT_EQ(index, target_index);
                        ASSERT_EQ(result, BINARY_STEP_TOO_SMALL);
                    }
                    else {
                        if (target_index > 0 && target_index < test_array_size - 1 && test_array_size > 2) {
                            ASSERT_EQ(result, BINARY_RESULT_FIND_CLOSE);
                            if (target_diff > 0 && find_below_matching == 0) {
                                ASSERT_EQ(index - 1, target_index);
                            }
                            else if (target_diff < 0 && find_below_matching != 0) {
                                ASSERT_EQ(index + 1, target_index);
                            }
                        }
                    }
                }
            }
        }
    } while (--test_array_size);
}

TEST(RateControl, EqualFull) {
    Test_next_step_full(0, 0);
    Test_next_step_full(1, 0);
}

TEST(RateControl, NotEqualSmallerFull) {
    Test_next_step_full(1, 1);
    Test_next_step_full(1, -1);
}

TEST(RateControl, NotEqualBiggerFull) {
    Test_next_step_full(0, 1);
    Test_next_step_full(0, -1);
}
