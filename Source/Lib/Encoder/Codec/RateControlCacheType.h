/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __RATE_CONTROL_CACHE_TYPE_H__
#define __RATE_CONTROL_CACHE_TYPE_H__
#include "Pi.h"
#include "EncDec.h"

#define SIGN_NOZERO_COEFF_LUT 0 /*Sign handling strategy, LUT slow down now*/

/* For small bands when gtli_width <=16 faster can work direct look to array for width <= 64
 * similar for significance_width <= 16 faster can work direct for width <= 256 */
#define LUT_SIGNIFICANE 1
/*Special optimization to not go through the entire table every time when use LUT.*/
#define LUT_SIGNIFICANE_SERIAL_SUMMATION 1
#define LUT_GC_SERIAL_SUMMATION          1

typedef struct rc_cache_band_line {
    /* LookUpTable table summary all elements from line in band with that same value of Group Coding
     * [Band][Line][Truncation Value] - summarize exist value in line
     * [Band][Line][Truncation Value + 1] - maximum exist value
     * Optimize to uint16_t because max precinct width is (2^16-1)*/
    uint16_t gc_lookup_table[TRUNCATION_MAX + 1];
#if LUT_GC_SERIAL_SUMMATION
    uint32_t gc_lookup_table_size_data_no_sign_handling[TRUNCATION_MAX + 1];
    uint32_t gc_lookup_table_size_data_sign_handling[TRUNCATION_MAX + 1];
#endif
#if LUT_SIGNIFICANE
    /*Get LUT table values without leftover from last group*/
    uint16_t significance_max_lookup_table[TRUNCATION_MAX + 1];
#endif
#if SIGN_NOZERO_COEFF_LUT
    /*LookUpTable: Sum of indexes: svt_log2_32((coeff_16bit[i] & ~BITSTREAM_MASK_SIGN)<<1)
     * Sum of zero on index 0*/
    uint16_t coeff_lookup_table[TRUNCATION_MAX + 1];
#endif
} rc_cache_band_line_t;

#endif /*__RATE_CONTROL_CACHE_TYPE_H__*/
