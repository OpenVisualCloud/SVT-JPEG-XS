/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __BITSTREAM_WRITER_ENCODER_H__
#define __BITSTREAM_WRITER_ENCODER_H__
#include <stdint.h>
#include <stddef.h>
#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitstream_writer {
    uint8_t* mem;
    uint32_t offset;
    uint32_t bits_used;
    uint32_t size;
} bitstream_writer_t;

void bitstream_writer_init(bitstream_writer_t* bitstream, uint8_t* bitstream_buf, size_t bitstream_buf_size);

///*Write from one input to the bitstream*/
void write_8_bits(bitstream_writer_t* bitstream, uint8_t input);
void write_16_bits(bitstream_writer_t* bitstream, uint16_t input);
void write_24_bits(bitstream_writer_t* bitstream, uint32_t input);
void write_32_bits(bitstream_writer_t* bitstream, uint32_t input);

///*Write from multiple inputs to the bitstream*/
void write_422_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2, uint8_t input_3);
void write_134_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2, uint8_t input_3);
void write_2x4_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2);

///*Write partial across Bytes*/
void write_1_bit(bitstream_writer_t* bitstream, uint8_t input);
void write_2_bits(bitstream_writer_t* bitstream, uint8_t input);
void write_4_bits(bitstream_writer_t* bitstream, uint8_t input);
void write_N_bits(bitstream_writer_t* bitstream, uint32_t input, uint8_t bits);
void update_N_bits(bitstream_writer_t* bitstream, uint32_t offset_bits, uint32_t input, uint8_t bits);

/*
* write_4_bits_align4() Can be used only when bitstream is padded to 0 or 4 bits, otherwise data will be corrupted
*/
static INLINE void write_4_bits_align4(bitstream_writer_t* bitstream, uint8_t input) {
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    assert(input <= 0xf);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    if (bitstream->bits_used == 4) {
        mem[0] |= input;
        bitstream->bits_used = 0;
        bitstream->offset++;
    }
    else { // bitstream->bits_used == 0;
        mem[0] = (input << 4);
        bitstream->bits_used = 4;
    }
}

/*Align memory*/
uint32_t bitstream_writer_get_used_bytes(bitstream_writer_t* bitstream);
uint32_t bitstream_writer_get_used_bits(bitstream_writer_t* bitstream);
void align_bitstream_writer_to_next_byte(bitstream_writer_t* bitstream);
void bitstream_writer_add_padding_bits(bitstream_writer_t* bitstream, uint32_t nbits);
void bitstream_writer_add_padding_bytes(bitstream_writer_t* bitstream, uint32_t nbytes);

#ifdef __cplusplus
}
#endif
#endif /*__BITSTREAM_WRITER_ENCODER_H__*/
