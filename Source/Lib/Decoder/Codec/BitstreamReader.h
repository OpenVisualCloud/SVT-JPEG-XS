/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __BITSTREAM_READER_H__
#define __BITSTREAM_READER_H__
#include <stdint.h>
#include <stddef.h>
#include "common_dsp_rtcd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitstream_reader {
    /*TODO: Optimize mem_start and mem, remove offset.*/
    const uint8_t* mem;
    uint32_t offset;
    uint32_t bits_used;
    uint32_t size;
} bitstream_reader_t;

void bitstream_reader_init(bitstream_reader_t* bitstream, const uint8_t* bitstream_buf, size_t bitstream_buf_size);

/*Read value by return*/
uint8_t read_8_bits(bitstream_reader_t* bitstream);
uint16_t read_16_bits(bitstream_reader_t* bitstream);
uint32_t read_24_bits(bitstream_reader_t* bitstream);
uint32_t read_32_bits(bitstream_reader_t* bitstream);

/*Read to array val*/
void read_422_bits(bitstream_reader_t* bitstream, uint8_t* val);
void read_2222_bits(bitstream_reader_t* bitstream, uint8_t* val);
void read_134_bits(bitstream_reader_t* bitstream, uint8_t* val);
void read_2x4_bits(bitstream_reader_t* bitstream, uint8_t* val);

/*Copy first MSB nbits to array val*/
void read_8x1_bit(bitstream_reader_t* bitstream, uint8_t* val);
void read_Nx1_bit(bitstream_reader_t* bitstream, uint8_t* val, uint8_t nbits);

/*Read partial across Byte boundary*/
uint8_t read_1_bit(bitstream_reader_t* bitstream);
uint8_t read_2_bit(bitstream_reader_t* bitstream);
uint8_t read_4_bits(bitstream_reader_t* bitstream);

/*
* read_4_bits_align4() Can be used only when bitstream is padded to 0 or 4 bits, otherwise data will be corrupted
*/
static INLINE uint8_t read_4_bits_align4(bitstream_reader_t* bitstream) {
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    uint8_t const* mem = bitstream->mem + bitstream->offset;
    uint8_t val = 0;

    if (bitstream->bits_used == 4) {
        val = (*mem) & 0xF;
        bitstream->bits_used = 0;
        bitstream->offset++;
    }
    else { // bitstream->bits_used == 0;
        val = (*mem >> 4);
        bitstream->bits_used = 4;
    }
    return val;
}

/*Align memory*/
void align_bitstream_reader_to_next_byte(bitstream_reader_t* bitstream);

uint32_t bitstream_reader_get_used_bytes(bitstream_reader_t* bitstream);
uint32_t bitstream_reader_get_left_bytes(bitstream_reader_t* bitstream);
uint64_t bitstream_reader_get_left_bits(bitstream_reader_t* bitstream);
uint8_t bitstream_reader_is_enough_bytes(bitstream_reader_t* bitstream, uint32_t nbytes);
uint8_t bitstream_reader_is_enough_bits(bitstream_reader_t* bitstream, uint32_t nbits);
void bitstream_reader_add_padding(bitstream_reader_t* bitstream, uint32_t nbytes);
void bitstream_reader_skip_bits(bitstream_reader_t* bitstream, uint32_t nbits);

#ifdef __cplusplus
}
#endif

#endif /*__BITSTREAM_READER_H__*/
