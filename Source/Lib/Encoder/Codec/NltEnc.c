/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltEnc.h"
#include "Codestream.h"
#include "encoder_dsp_rtcd.h"
#include "EncDec.h"
#include <assert.h>

void image_shift_c(uint16_t* out_coeff_16bit, int32_t* in_coeff_32bit, uint32_t width, int32_t shift, int32_t offset) {
    //Part of insert_coefficients
    //in-place convert
    for (uint32_t i = 0; i < width; i++) {
        int32_t val = in_coeff_32bit[i];
        if (val >= 0) {
            val = (val + offset) >> shift;
            assert(((int32_t)val) <= ((((int32_t)1) << TRUNCATION_MAX) - 1));
        }
        else {
            val = ((-val + offset) >> shift);
            if (val) {
                //Avoid keep -0 value
                val |= BITSTREAM_MASK_SIGN;
            }
            assert((((int32_t)val) & (~BITSTREAM_MASK_SIGN)) <= ((((int32_t)1) << TRUNCATION_MAX) - 1));
        }
        out_coeff_16bit[i] = val; //<< Convert from 32 bits to 16 bits
    }
}

void linear_input_scaling_line_8bit_c(const uint8_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset) {
    for (uint32_t j = 0; j < w; j++) {
        dst[j] = ((uint32_t)src[j] << shift) - offset;
    }
}

void linear_input_scaling_line_16bit_c(const uint16_t* src, int32_t* dst, uint32_t w, uint8_t shift, int32_t offset,
                                       uint8_t bit_depth) {
    uint16_t input_mask = (1 << bit_depth) - 1;
    for (uint32_t j = 0; j < w; j++) {
        dst[j] = ((src[j] & input_mask) << shift) - offset;
    }
}

void linear_input_scaling_line(const void* src, int32_t* dst, uint32_t width, uint8_t input_bit_depth, uint8_t shift,
                               int32_t offset) {
    if (input_bit_depth <= 8) {
        linear_input_scaling_line_8bit((uint8_t*)src, dst, width, shift, offset);
    }
    else {
        assert(input_bit_depth > 8 && input_bit_depth <= 16);
        linear_input_scaling_line_16bit((uint16_t*)src, dst, width, shift, offset, input_bit_depth);
    }
}

void nlt_input_scaling_line(const void* src, int32_t* dst, uint32_t width, picture_header_dynamic_t* hdr,
                            uint8_t input_bit_depth) {
    const uint8_t shift = hdr->hdr_Bw - input_bit_depth;
    const int32_t offset = 1 << (hdr->hdr_Bw - 1);
    switch (hdr->hdr_Tnlt) {
    case 0:
        linear_input_scaling_line(src, dst, width, input_bit_depth, shift, offset);
        break;
    case 1:
    case 2:
    default:
        assert(0);
        break;
    }
}
