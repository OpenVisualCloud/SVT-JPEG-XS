/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "WeightTable.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "SvtJpegxs.h"

/*Band not exist, local define*/
#define NE BAND_NOT_EXIST

// clang-format off
/*Default weight tables sample from specification*/
/*YUV444*/
static const uint8_t weight_table_444_0_5_gain[18]     = {3, 2, 2, 2, 1, 1, 2, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0}; //From specification
static const uint8_t weight_table_444_1_5_gain[24]     = {4, 2, 2, 3, 2, 2, 2, 1, 1, 2, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0}; //From specification
static const uint8_t weight_table_444_2_5_gain[30]     = {4, 3, 3, 3, 2, 2, 3, 2, 2, 2, 1, 1, 2, 1, 1, 2, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0}; //From specification
static const uint8_t weight_table_444_0_5_priority[18] = { 6,  8,  7,  1,  4,  5, 12, 14, 15,  0,  2,  3, 9,  11, 10, 13, 16, 17}; //From specification
static const uint8_t weight_table_444_1_5_priority[24] = {21,  1,  0, 15, 19, 18,  5,  9,  8, 14, 17, 16, 2,   4,  3,  7, 13, 11,  6, 12, 10, 20, 23, 22}; //From specification
static const uint8_t weight_table_444_2_5_priority[30] = {12, 15, 14,  3, 11, 10, 24, 26, 27,  0,  4,  5, 18, 21, 20, 19, 23, 22, 13, 16, 17,  2,  9, 6,  1,  7,  8, 25, 28, 29}; //From specification

/*YUV422*/
/*Commented tables are generated automatically*/
//     const uint8_t weight_table_422_0_1_gain[6]      =                                                       {0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_0_2_gain[9]      =                                              {0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_0_3_gain[12]     =                                     {1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_0_4_gain[15]     =                            {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t weight_table_422_0_5_gain[18]     =                   {2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}; //From specification
//     const uint8_t weight_table_422_1_1_gain[12]     =                                     {1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_1_2_gain[15]     =                                     {1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_1_3_gain[18]     =                            {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_1_4_gain[21]     =                   {2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t weight_table_422_1_5_gain[24]     =          {2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; //From specification
//     const uint8_t weight_table_422_2_2_gain[21]     =                            {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_2_3_gain[24]     =                   {2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//     const uint8_t weight_table_422_2_4_gain[27]     =          {2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t weight_table_422_2_5_gain[30]     = {3, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; //From specification
//     const uint8_t weight_table_422_0_1_priority[6]  = { 0,  1,  2,  3,  4,  5};
//     const uint8_t weight_table_422_0_2_priority[9]  = { 0,  1,  2,  3,  4,  5,  6,  7, 8};
//     const uint8_t weight_table_422_0_3_priority[12] = { 0,  1,  2,  4,  8,  5,  3,  6,  7,  9, 10, 11};
//     const uint8_t weight_table_422_0_4_priority[15] = { 0,  1,  2,  9, 10, 11,  4,  8,  5,  3,  6,  7, 12, 13, 14};
static const uint8_t weight_table_422_0_5_priority[18] = { 8,  7,  6,  5,  3,  4, 13, 12, 14,  1,  0,  2,  9, 11, 10, 16, 15, 17}; //From specification
//     const uint8_t weight_table_422_1_1_priority[12] = { 0,  1,  2,  9, 10, 11,  4,  8,  5,  3,  6,  7};
//     const uint8_t weight_table_422_1_2_priority[15] = { 0,  1,  2,  9, 10, 11,  4,  8,  5,  3,  6,  7, 12, 13, 14};
//     const uint8_t weight_table_422_1_3_priority[18] = { 0,  1,  2,  9, 10, 11, 12, 13, 14,  4,  8,  5,  3,  6,  7, 15, 16, 17};
//     const uint8_t weight_table_422_1_4_priority[21] = { 9, 10, 11,  0,  1,  2, 12, 13, 14, 17, 15, 16,  4,  8,  5,  3,  6,  7, 18, 19, 20};
static const uint8_t weight_table_422_1_5_priority[24] = { 0,  1,  2, 18, 19, 20,  6,  7,  8, 15, 16, 17,  3,  4,  5, 10, 12, 14,  9, 11, 13, 21, 22, 23}; //From specification
//     const uint8_t weight_table_422_2_2_priority[21] = { 0,  1,  2,  9, 10, 11, 12, 13, 14, 17, 15, 16,  4,  8,  5,  3,  6,  7, 18, 19, 20};
//     const uint8_t weight_table_422_2_3_priority[24] = { 9, 10, 11,  0,  1,  2, 12, 13, 14, 18, 19, 20, 17, 15, 16,  4,  8,  5,  3,  6,  7, 21, 22, 23};
//     const uint8_t weight_table_422_2_4_priority[27] = { 9, 11, 10, 12, 13, 14,  0,  1,  2, 19, 20, 18, 23, 22, 21, 17, 15, 16,  4,  8,  5,  3,  6,  7, 24, 25, 26};
static const uint8_t weight_table_422_2_5_priority[30] = {14, 13, 12,  9, 11, 10, 25, 24, 26,  0,  1,  2, 19, 20, 18, 23, 22, 21, 17, 15, 16,  4,  8,  5,  3,  6,  7, 28, 27, 29}; //From specification

/*YUV420*/
static const uint8_t weight_table_420_1_1_gain[12]     =                          {1, 1, 1, 1, 0, 0,          1,NE,NE, 0,NE,NE}; //Added manually
static const uint8_t weight_table_420_1_2_gain[15]   =                            {1, 1, 1, 1, 0, 0, 1, 0, 0, 1,NE,NE, 0,NE,NE}; //Added manually
static const uint8_t weight_table_420_1_3_gain[18]   =                   {2, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1,NE,NE, 0,NE,NE}; //Added manually
static const uint8_t weight_table_420_1_4_gain[21]   =          {2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1,NE,NE, 0,NE,NE}; //Added manually
static const uint8_t weight_table_420_1_5_gain[24]   = {3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1,NE,NE, 0,NE,NE}; //From specification
static const uint8_t weight_table_420_2_2_gain[21]   =                            {2, 1, 1, 1, 1, 1, 1, NE, NE, 0, NE, NE, 1, 1, 1, 1, 1, 1, 0, 0, 0}; //Added manually
static const uint8_t weight_table_420_2_3_gain[24]   =                   {2, 2, 2, 2, 1, 1, 1, 1, 1, 1, NE, NE, 0, NE, NE, 1, 1, 1, 1, 1, 1, 0, 0, 0}; //Added manually
static const uint8_t weight_table_420_2_4_gain[27]   =          {3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, NE, NE, 0, NE, NE, 1, 1, 1, 1, 1, 1, 0, 0, 0}; //Added manually
static const uint8_t weight_table_420_2_5_gain[30]   = {3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, NE, NE, 0, NE, NE, 1, 1, 1, 1, 1, 1, 0, 0, 0}; //From specification
static const uint8_t weight_table_420_1_1_priority[12] =                                      {2,  5,  4,  6,  1,  0,              7, NE, NE,  3, NE, NE}; //Added manually
static const uint8_t weight_table_420_1_2_priority[15] =                                      {4,  7,  6,  8,  1,  0, 10,  3,  2,  9, NE, NE,  5, NE, NE}; //Added manually
static const uint8_t weight_table_420_1_3_priority[18] =                         {11,  3,  2,  6,  9,  8, 10,  1,  0, 13,  5,  4, 12, NE, NE,  7, NE, NE}; //Added manually
static const uint8_t weight_table_420_1_4_priority[21] =                      {7, 12, 11, 14,  3,  2,  6, 10,  9, 13,  1,  0, 16,  5,  4, 15, NE, NE,  8, NE, NE}; //Added manually
static const uint8_t weight_table_420_1_5_priority[24] = { 9, 16, 15,  7, 13, 12, 17,  3,  2,  6, 11, 10, 14,  1,  0, 19,  5,  4, 18, NE, NE,  8, NE, NE}; //From specification
static const uint8_t weight_table_420_2_2_priority[21] = {                                                10,  4,  3,  2,  9,  8,  1, NE, NE,  0, NE, NE, 16, 14, 12, 15, 13, 11,  7, 6,5 }; //Added manually
static const uint8_t weight_table_420_2_3_priority[24] = {                         5, 12, 11, 13,  4,  3,  2, 10,  9,  1, NE, NE,  0, NE, NE, 19, 17, 15, 18, 16, 14, 8, 7, 6 }; //Added manually
static const uint8_t weight_table_420_2_4_priority[27] = {            22,  6,  5,  7, 14, 13, 15,  4,  3,  2, 12, 11,  1, NE, NE,  0, NE, NE, 21, 19, 17, 20, 18, 16, 10,  9,  8}; //Added manually
static const uint8_t weight_table_420_2_5_priority[30] = { 0, 13, 12, 25,  7,  6,  8, 17, 16, 18,  5,  4,  3, 15, 14,  2, NE, NE,  1, NE, NE, 24, 22, 20, 23, 21, 19, 11, 10,  9}; //From specification

typedef struct weight_tables {
    const uint8_t *gain;
    const uint8_t *priority;
    uint32_t size;
} weight_tables_t;

#define TABLE_EMPTY() {NULL, NULL, 0}
#define TABLE(array_name) {                                                                                             \
                              weight_table_##array_name##_gain,                                                         \
                              weight_table_##array_name##_priority,                                                     \
                              (sizeof(weight_table_##array_name##_gain)/ sizeof(weight_table_##array_name##_gain[0]))   \
                          }

//Weight tables [horizontal][vertical]
static const weight_tables_t weight_tables_sample_444[6][3] = {
/*H  V:{0,              1,              2}*/
/*0*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*1*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*2*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*3*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*4*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*5*/  {TABLE(444_0_5), TABLE(444_1_5), TABLE(444_2_5)}
};

static const weight_tables_t weight_tables_sample_422[6][3] = {
/*H  V:{0,              1,              2}*/
/*0*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*1*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*2*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*3*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*4*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*5*/  {TABLE(422_0_5), TABLE(422_1_5), TABLE(422_2_5)}
};

static const weight_tables_t weight_tables_sample_420[6][3] = {
/*H  V:{0,              1,              2}*/
/*0*/  {TABLE_EMPTY(),  TABLE_EMPTY(),  TABLE_EMPTY()},
/*1*/  {TABLE_EMPTY(),  TABLE(420_1_1), TABLE_EMPTY()},
/*2*/  {TABLE_EMPTY(),  TABLE(420_1_2), TABLE(420_2_2)},
/*3*/  {TABLE_EMPTY(),  TABLE(420_1_3), TABLE(420_2_3)},
/*4*/  {TABLE_EMPTY(),  TABLE(420_1_4), TABLE(420_2_4)},
/*5*/  {TABLE_EMPTY(),  TABLE(420_1_5), TABLE(420_2_5)}
};
// clang-format on

static int weight_table_recalculate_table_set_default_52(pi_t* pi, weight_tables_t* table, ColourFormat_t color_format) {
    (void)(pi);
    weight_tables_t table_temp = {0};
    if (color_format == COLOUR_FORMAT_PLANAR_YUV422) {
        table_temp = weight_tables_sample_422[5][2];
    }
    else if (color_format == COLOUR_FORMAT_PLANAR_YUV420) {
        table_temp = weight_tables_sample_420[5][2];
    }
    else if (color_format == COLOUR_FORMAT_PLANAR_YUV444_OR_RGB || color_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
        table_temp = weight_tables_sample_444[5][2];
    }
    if (table_temp.gain == NULL) {
        return -1;
    }

    table->size = table_temp.size;
    memcpy((uint8_t*)table->gain, table_temp.gain, table_temp.size * sizeof(table_temp.gain[0]));
    memcpy((uint8_t*)table->priority, table_temp.priority, table_temp.size * sizeof(table_temp.priority[0]));
    return 0;
}

static int weight_table_recalculate_table_reduce_422_h(pi_t* pi, weight_tables_t* table, int v, int h, int dest_h) {
    (void)(pi);
    /* Reduce H */
    if (h < v || dest_h < v) {
        return -1;
    }
    //Remove first 3 bands (1 from any component)
    while (h > dest_h) {
        h--;
        if (h != 1) {
            table->size -= 3;
            memcpy((uint8_t*)table->gain, table->gain + 3, table->size * sizeof(table->gain[0]));
            memcpy((uint8_t*)table->priority, table->priority + 3, table->size * sizeof(table->priority[0]));
        }
        else {
            //Remove last
            table->size -= 3;
        }
    }
    return 0;
}

//When using -flto flag GCC is trying to replace copy_mem_prv() with __builtin_memmove() which then fails with error:
//lto1: error: __builtin_memmove reading 18 bytes from a region of size 15 [-Werror=stringop-overflow=]
#ifdef __GNUC__
#ifdef __clang__
#else
#pragma GCC push_options
#pragma GCC optimize("O2")
#endif
#endif
void copy_mem_prv(uint8_t* dst, const uint8_t* src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
#ifdef __GNUC__
#ifdef __clang__
#else
#pragma GCC pop_options
#endif
#endif

static int weight_table_recalculate_table_reduce_422_v(pi_t* pi, weight_tables_t* table, int v, int h, int dest_v) {
    (void)(pi);
    /* Reduce V */
    if (h < v || dest_v < 0) {
        return -1;
    }
    //Remove 6 bands (2 from any component, 1 from begin, 2 in the middle)
    while (v > dest_v) {
        int remove = (h - 1); //Remove last in first line
        v--;

        copy_mem_prv((uint8_t*)table->gain + 3 * remove,
                     table->gain + 3 * (remove + 1),
                     (table->size - 3 * (remove)) * sizeof(table->gain[0]));
        copy_mem_prv((uint8_t*)table->priority + 3 * remove,
                     table->priority + 3 * (remove + 1),
                     (table->size - 3 * (remove)) * sizeof(table->priority[0]));
        table->size -= 3;
        //Remove first
        table->size -= 3;
        copy_mem_prv((uint8_t*)table->gain, table->gain + 3, table->size * sizeof(table->gain[0]));
        copy_mem_prv((uint8_t*)table->priority, table->priority + 3, table->size * sizeof(table->priority[0]));
    }
    return 0;
}

static int weight_table_recalculate_table_rebuild_prio(weight_tables_t* table) {
    for (uint32_t i = 0; i < table->size; ++i) {
        int find = 0;
        do {
            for (uint32_t j = 0; j < table->size; ++j) {
                if (table->priority[j] == i) {
                    find = 1;
                    break;
                }
            }
            if (!find) {
                for (uint32_t j = 0; j < table->size; ++j) {
                    if (table->priority[j] > i) {
                        *((uint8_t*)(&table->priority[j])) = table->priority[j] - 1;
                        break;
                    }
                }
            }
        } while (!find);
    }

    return 0;
}

int weight_table_recalculate_table_print_research(pi_t* pi, weight_tables_t* table, ColourFormat_t color_format) {
    int ret = 0;
    int v = 2;
    int h = 5;
    /* Reduce H */
    for (int dest_v = v; dest_v >= 0; --dest_v) {
        for (int dest_h = 5; dest_h >= dest_v; --dest_h) {
            ret = weight_table_recalculate_table_set_default_52(pi, table, color_format);
            if (ret) {
                return ret;
            }
            ret = weight_table_recalculate_table_reduce_422_v(pi, table, v, h, dest_v);
            if (ret) {
                return ret;
            }
            ret = weight_table_recalculate_table_reduce_422_h(pi, table, dest_v, h, dest_h);
            if (ret) {
                return ret;
            }
#ifndef NDEBUG
            printf("GAIN V:%i H:%i: ", dest_v, dest_h);
            for (uint32_t i = 0; i < table->size; ++i) {
                printf("%i ", table->gain[i]);
            }
            printf("\n");
            printf("PRIO1 V:%i H:%i: ", dest_v, dest_h);
            for (uint32_t i = 0; i < table->size; ++i) {
                printf("%i ", table->priority[i]);
            }
            printf("\n");
            weight_table_recalculate_table_rebuild_prio(table);
            printf("PRIO2 V:%i H:%i: ", dest_v, dest_h);
            for (uint32_t i = 0; i < table->size; ++i) {
                printf("%i ", table->priority[i]);
            }
            printf("\n");
#endif
        }
    }
    return 0;
}

static int weight_table_recalculate_table(pi_t* pi, weight_tables_t* table, ColourFormat_t color_format, uint8_t verbose) {
    //Recalculate table
    int ret = 0;
    assert(pi->comps_num == 3);
    if (verbose) {
        printf("Warning: Weight table not defined. Use recalculated table! H:%i V:%i sy:%i%i%i sx:%i%i%i \n",
               pi->decom_h,
               pi->decom_v,
               pi->components[0].Sy,
               pi->components[1].Sy,
               pi->components[2].Sy,
               pi->components[0].Sx,
               pi->components[1].Sx,
               pi->components[2].Sx);
    }
    //pi_dump(pi, 1);

    if (color_format != COLOUR_FORMAT_PLANAR_YUV422 && color_format != COLOUR_FORMAT_PLANAR_YUV444_OR_RGB &&
        color_format != COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
        return -1;
    }

    int v = 2;
    int h = 5;
    ret = weight_table_recalculate_table_set_default_52(pi, table, color_format);
    if (ret) {
        return ret;
    }
    ret = weight_table_recalculate_table_reduce_422_v(pi, table, v, h, pi->decom_v);
    if (ret) {
        return ret;
    }
    ret = weight_table_recalculate_table_reduce_422_h(pi, table, pi->decom_v, h, pi->decom_h);
    if (ret) {
        return ret;
    }

    weight_table_recalculate_table_rebuild_prio(table);

    if (verbose) {
        printf("GAIN V:%i H:%i: ", pi->decom_v, pi->decom_h);
        for (uint32_t i = 0; i < table->size; ++i) {
            printf("%i ", table->gain[i]);
        }
        printf("\n");
        printf("PRIO V:%i H:%i: ", pi->decom_v, pi->decom_h);
        for (uint32_t i = 0; i < table->size; ++i) {
            printf("%i ", table->priority[i]);
        }
        printf("\n");
    }
    return 0;
}

int weight_table_calculate(pi_t* pi, uint8_t verbose, ColourFormat_t color_format) {
    weight_tables_t table = {0};
    if (pi->comps_num == 3) {
        if (color_format == COLOUR_FORMAT_PLANAR_YUV422) {
            table = weight_tables_sample_422[pi->decom_h][pi->decom_v];
        }
        else if (color_format == COLOUR_FORMAT_PLANAR_YUV420) {
            table = weight_tables_sample_420[pi->decom_h][pi->decom_v];
        }
        else if (color_format == COLOUR_FORMAT_PLANAR_YUV444_OR_RGB || color_format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
            table = weight_tables_sample_444[pi->decom_h][pi->decom_v];
        }
    }

    if (color_format == COLOUR_FORMAT_INVALID) {
        fprintf(stderr, "Error: Weight table not supported format!\n");
        return -1;
    }

    uint8_t temp_gain[30];
    uint8_t temp_priority[30];
    if (table.gain == NULL) {
        table.gain = temp_gain;
        table.priority = temp_priority;
        int ret = weight_table_recalculate_table(pi, &table, color_format, verbose);
        if (ret) {
            if (verbose) {
                fprintf(stderr,
                        "Error: Weight table not defined. Can not recalculated table! H:%i V:%i sy:%i%i%i sx:%i%i%i \n",
                        pi->decom_h,
                        pi->decom_v,
                        pi->components[0].Sy,
                        pi->components[1].Sy,
                        pi->components[2].Sy,
                        pi->components[0].Sx,
                        pi->components[1].Sx,
                        pi->components[2].Sx);
            }
            return -1;
        }
    }

    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            pi_band_t* band = &pi->components[c].bands[b];
            if ((uint32_t)band->band_id >= table.size) {
                return -1;
            }
            band->gain = table.gain[band->band_id];
            band->priority = table.priority[band->band_id];
        }
    }

#ifndef NDEBUG
    //Validate consistency of table
    uint8_t global_prioity[MAX_BANDS_NUM]; /* Indexing of global band [0 .. bands_num_all-1]*/
    memset(global_prioity, 0, sizeof(global_prioity));
    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; ++b) {
            uint8_t pri = pi->components[c].bands[b].priority;
            if (pri >= pi->bands_num_exists) {
                assert(0);
                return -1;
            }
            global_prioity[pri] += 1;
        }
    }
    for (uint32_t p = 0; p < pi->bands_num_exists; ++p) {
        if (global_prioity[p] != 1) {
            fprintf(stderr, "Error: Weight table priority not consistent!\n");
            assert(0);
            return -1;
        }
    }

#endif
    return 0;
}
