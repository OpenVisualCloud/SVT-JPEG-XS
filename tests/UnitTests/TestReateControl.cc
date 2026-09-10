/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "gtest/gtest.h"
#include <RateControl.h>
#include <BinarySearch.h>
#include "random.h"
#include "encoder_dsp_rtcd.h"
#include "EncDec.h" /* TRUNCATION_MAX */

#ifdef ARCH_X86_64
#include "RateControl_avx2.h"
#include "Enc_avx512.h"
#endif /* ARCH_X86_64 */

#ifdef ARCH_AARCH64
#include "RateControl_neon.h"
#endif /* ARCH_AARCH64 */

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

/* The histogram of GCLI values over one band line.
 *
 * The three implementations fill all sixteen bins themselves - nobody clears
 * the table for them - so the test hands them a table pre-filled with a value
 * no count can be: a bin left untouched shows up as a mismatch rather than as a
 * lucky zero. Width zero is part of the contract as well: an empty line has to
 * leave an all-zero histogram, not the previous one. */
typedef void (*gc_histogram_16_fn)(const uint8_t* data, uint32_t width, uint16_t* hist);

static void gc_histogram_16_test(gc_histogram_16_fn hist_fn) {
    /* the widths around the vector step, plus a few long lines */
    const uint32_t widths[] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 480, 1920, 3840};
    const uint32_t widths_num = sizeof(widths) / sizeof(widths[0]);
    const uint32_t max_width = 3840;

    svt_jxs_test_tool::SVTRandom* rnd = new svt_jxs_test_tool::SVTRandom(32, false);
    uint8_t* data = (uint8_t*)malloc(max_width);
    ASSERT_TRUE(data != NULL);

    for (uint32_t width_idx = 0; width_idx < widths_num; width_idx++) {
        const uint32_t width = widths[width_idx];
        for (uint32_t test_num = 0; test_num < 20; test_num++) {
            uint16_t ref[TRUNCATION_MAX + 1];
            uint16_t cmp[TRUNCATION_MAX + 1];
            memset(ref, 0, sizeof(ref));
            for (uint32_t i = 0; i <= TRUNCATION_MAX; i++) {
                cmp[i] = 0xDEAD;
            }

            for (uint32_t i = 0; i < width; i++) {
                /* the first round is the extreme case of a single bin: GCLI is
                 * spatially correlated and whole lines of one value do occur */
                data[i] = (uint8_t)(test_num == 0 ? TRUNCATION_MAX : rnd->Rand8() % (TRUNCATION_MAX + 1));
                ref[data[i]]++;
            }

            hist_fn(data, width, cmp);
            ASSERT_EQ(memcmp(ref, cmp, sizeof(ref)), 0) << "width " << width << " round " << test_num;
        }
    }

    free(data);
    delete rnd;
}

TEST(gc_histogram_16, C) {
    gc_histogram_16_test(gc_histogram_16_c);
}

#ifdef ARCH_AARCH64
TEST(gc_histogram_16, NEON) {
    gc_histogram_16_test(gc_histogram_16_neon);
}
#endif /* ARCH_AARCH64 */

#ifdef ARCH_X86_64
TEST(gc_histogram_16, AVX2) {
    if (!(get_cpu_flags() & CPU_FLAGS_AVX2)) {
        GTEST_SKIP();
    }
    gc_histogram_16_test(gc_histogram_16_avx2);
}
#endif /* ARCH_X86_64 */

#ifdef ARCH_X86_64
TEST(gc_histogram_16, AVX512) {
    const CPU_FLAGS required = CPU_FLAGS_AVX512F | CPU_FLAGS_BMI2 | CPU_FLAGS_POPCNT;
    if ((get_cpu_flags() & required) != required) {
        GTEST_SKIP();
    }
    gc_histogram_16_test(gc_histogram_16_avx512);
}
#endif /* ARCH_X86_64 */
