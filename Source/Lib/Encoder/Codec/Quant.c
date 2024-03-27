/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Quant.h"
#include "Codestream.h"

static void quant_deadzone_c(uint16_t* buff_16bit, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli) {
    for (uint32_t coeff_idx = 0; coeff_idx < size; coeff_idx++) {
        uint8_t gcli = gclis[coeff_idx / group_size];
        if (gcli > gtli) {
            uint32_t sign = buff_16bit[coeff_idx] & BITSTREAM_MASK_SIGN;
            //remove sign and quantize
            buff_16bit[coeff_idx] = ((buff_16bit[coeff_idx] & ~BITSTREAM_MASK_SIGN) >> gtli) << gtli;
            //if coeff after quantization have data, then restore sign
            if (buff_16bit[coeff_idx]) {
                buff_16bit[coeff_idx] |= sign;
            }
        }
        else {
            buff_16bit[coeff_idx] = 0;
        }
    }
}

static void quant_uniform_c(uint16_t* buff_16bit, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli) {
    for (uint32_t coeff_idx = 0; coeff_idx < size; coeff_idx++) {
        uint8_t gcli = gclis[coeff_idx / group_size];
        if (gcli > gtli) {
            uint16_t sign = buff_16bit[coeff_idx] & BITSTREAM_MASK_SIGN;
            uint8_t scale_value = gcli - gtli + 1;
            uint16_t d = buff_16bit[coeff_idx] & ~BITSTREAM_MASK_SIGN;
            d = ((d << scale_value) - d + (1 << gcli)) >> (gcli + 1);
            buff_16bit[coeff_idx] = d << gtli;
            //if coeff after quantization have data, then restore sign
            if (buff_16bit[coeff_idx]) {
                buff_16bit[coeff_idx] |= sign;
            }
        }
        else {
            buff_16bit[coeff_idx] = 0;
        }
    }
}

void quantization_c(uint16_t* coeff_16bit, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli,
                    QUANT_TYPE quant_type) {
    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        quant_uniform_c(coeff_16bit, size, gclis, group_size, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        quant_deadzone_c(coeff_16bit, size, gclis, group_size, gtli);
        return;
    default:
        assert("unknown quantization");
    }
    return;
}
