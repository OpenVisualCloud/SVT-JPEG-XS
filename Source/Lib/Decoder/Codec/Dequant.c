/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dequant.h"
#include "Codestream.h"

void inv_quant_deadzone(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli) {
    if (gtli == 0) {
        return;
    }
    for (uint32_t coeff_idx = 0; coeff_idx < size; coeff_idx++) {
        int8_t gcli = gclis[coeff_idx / group_size];
        //  Check whether a non-zero number of bit-planes is included in this code group and
        //  whether the coefficient is non-zero
        if ((gcli > gtli) && (buf[coeff_idx] & ~BITSTREAM_MASK_SIGN)) {
            buf[coeff_idx] |= (1 << (gtli - 1));
        }
    }
}

void inv_quant_uniform(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli) {
    if (gtli == 0) {
        return;
    }
    for (uint32_t coeff_idx = 0; coeff_idx < size; coeff_idx++) {
        int8_t gcli = gclis[coeff_idx / group_size];
        //  Check whether a non-zero number of bit-planes is included in this code group and
        //  whether the coefficient is non-zero
        if ((gcli > gtli) && (buf[coeff_idx] & ~BITSTREAM_MASK_SIGN)) {
            uint16_t sign = buf[coeff_idx] & BITSTREAM_MASK_SIGN;
            uint16_t val = (buf[coeff_idx] & ~BITSTREAM_MASK_SIGN);
            uint8_t scale_value = gcli - gtli + 1;
            buf[coeff_idx] = 0;
            for (; val > 0; val >>= scale_value) {
                buf[coeff_idx] += val;
            }
            //insert sign
            buf[coeff_idx] |= sign;
        }
    }
}

void dequant_c(uint16_t* buf, uint32_t size, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE quant_type) {
    switch (quant_type) {
    case QUANT_TYPE_UNIFORM:
        inv_quant_uniform(buf, size, gclis, group_size, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        inv_quant_deadzone(buf, size, gclis, group_size, gtli);
        return;
    default:
        assert("unknown quantization");
    }
    return;
}
