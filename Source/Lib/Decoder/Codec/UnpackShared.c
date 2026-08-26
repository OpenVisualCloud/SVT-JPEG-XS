/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "UnpackShared.h"

uint8_t read_4_bits_align4_fast(reader_short_t* r) {
    if (r->bits_used) {
        r->bits_used = 0;
        return (*r->mem++) & 0xF;
    }
    r->bits_used = 4;
    return (*r->mem >> 4);
}
