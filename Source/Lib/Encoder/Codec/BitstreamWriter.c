/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "BitstreamWriter.h"
#include <string.h>
#include <assert.h>

void bitstream_writer_init(bitstream_writer_t* bitstream, uint8_t* bitstream_buf, size_t bitstream_buf_size) {
    bitstream->offset = 0;
    bitstream->bits_used = 0;
    bitstream->size = (int32_t)bitstream_buf_size;
    bitstream->mem = bitstream_buf;
#ifndef NDEBUG
    memset(bitstream->mem, 0xFF, bitstream->size);
#endif
}

void write_8_bits(bitstream_writer_t* bitstream, uint8_t input) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[0] = input;
    bitstream->offset += 1;
}

void write_16_bits(bitstream_writer_t* bitstream, uint16_t input) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[0] = input >> 8;
    mem[1] = (uint8_t)input;
    bitstream->offset += 2;
}

void write_24_bits(bitstream_writer_t* bitstream, uint32_t input) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[2] = input;
    mem[1] = input >> 8;
    mem[0] = input >> 16;
    bitstream->offset += 3;
}

void write_32_bits(bitstream_writer_t* bitstream, uint32_t input) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[3] = input;
    mem[2] = input >> 8;
    mem[1] = input >> 16;
    mem[0] = input >> 24;
    bitstream->offset += 4;
}

void write_422_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2, uint8_t input_3) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[0] = (input_1 << 4) | ((input_2 & 0x03) << 2) | (input_3 & 0x03);
    bitstream->offset += 1;
}

void write_134_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2, uint8_t input_3) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[0] = (input_1 << 7) | ((input_2 & 0xF) << 4) | (input_3 & 0xF);
    bitstream->offset += 1;
}

void write_2x4_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;

    mem[0] = (input_1 << 4) | (input_2 & 0xF);
    bitstream->offset += 1;
}

void write_1_bit(bitstream_writer_t* bitstream, uint8_t input) {
    uint8_t* mem = bitstream->mem + bitstream->offset;

    if (bitstream->bits_used == 0) {
        mem[0] = ((input & 0x1) << (7 - bitstream->bits_used));
    }
    else {
        mem[0] |= ((input & 0x1) << (7 - bitstream->bits_used));
    }

    (bitstream->bits_used)++;

    if (bitstream->bits_used == 8) {
        bitstream->bits_used = 0;
        bitstream->offset++;
    }
}

void write_2_bits(bitstream_writer_t* bitstream, uint8_t input) {
    uint8_t* mem = bitstream->mem + bitstream->offset;

    if (bitstream->bits_used < 7) {
        if (bitstream->bits_used == 0) {
            mem[0] = ((input & 0x3) << (6));
        }
        else {
            mem[0] |= ((input & 0x3) << (6 - bitstream->bits_used));
        }
        bitstream->bits_used += 2;
        if (bitstream->bits_used == 8) {
            bitstream->bits_used = 0;
            bitstream->offset++;
        }
    }
    else {
        mem[0] |= ((input & 0x2) >> 1);
        mem[1] = ((input & 0x1) << 7);
        bitstream->bits_used = 1;
        bitstream->offset++;
    }
}

void write_4_bits(bitstream_writer_t* bitstream, uint8_t input) {
    uint8_t* mem = bitstream->mem + bitstream->offset;

    if (bitstream->bits_used < 4) {
        if (bitstream->bits_used == 0) {
            mem[0] = ((input & 0xF) << (4));
        }
        else {
            mem[0] |= ((input & 0xF) << (4 - bitstream->bits_used));
        }
        bitstream->bits_used += 4;
    }
    else {
        switch (bitstream->bits_used) {
        case 4: {
            mem[0] |= input & 0xF;
            bitstream->bits_used = 0;
            break;
        }
        case 5: {
            mem[0] |= (((input) >> 1) & 0x7);
            mem[1] = (((input)&0x1) << 7);
            bitstream->bits_used = 1;
            break;
        }
        case 6: {
            mem[0] |= (((input) >> 2) & 0x3);
            mem[1] = (((input)&0x3) << 6);
            bitstream->bits_used = 2;
            break;
        }
        case 7: {
            mem[0] |= (((input) >> 3) & 0x1);
            mem[1] = (((input)&0x7) << 5);
            bitstream->bits_used = 3;
            break;
        }
        };
        bitstream->offset++;
    }
}

void write_N_bits(bitstream_writer_t* bitstream, uint32_t input, uint8_t bits) {
    assert(bits == 32 || (input >> bits) == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;
    if (bitstream->bits_used) {
        uint32_t left = (8 - bitstream->bits_used);
        uint8_t bits_to_copy = bits;
        if (bits_to_copy > left) {
            bits_to_copy = left;
        }
        *mem |= (input >> (bits - bits_to_copy)) << (left - bits_to_copy);
        if (left > bits_to_copy) {
            bitstream->bits_used += bits_to_copy;
            return;
        }
        bits -= bits_to_copy;
        bitstream->offset++;
        bitstream->bits_used = 0;
        mem++;
    }

    while (bits > 7) {
        assert(bitstream->bits_used == 0);
        *mem = (input >> (bits - 8)) & 0xFF;
        bits -= 8;
        bitstream->offset++;
        ++mem;
    }

    if (bits) {
        *mem = (input & ((1 << bits) - 1)) << (8 - bits);
        bitstream->bits_used = bits;
    }
}

void update_N_bits(bitstream_writer_t* bitstream, uint32_t offset_bits, uint32_t input, uint8_t bits) {
    assert(bits == 32 || (input >> bits) == 0);
    uint8_t* mem = bitstream->mem + (offset_bits >> 3);
    uint32_t bits_used = offset_bits & 7;
    if (bits_used) {
        uint32_t left = (8 - bits_used);
        uint8_t bits_to_copy = bits;
        if (bits_to_copy > left) {
            bits_to_copy = left;
        }
        *mem &= ~(((1 << bits_to_copy) - 1) << (left - bits_to_copy));
        *mem |= (input >> (bits - bits_to_copy)) << (left - bits_to_copy);
        if (left > bits_to_copy) {
            bits_used += bits_to_copy;
            return;
        }
        bits -= bits_to_copy;
        bits_used = 0;
        mem++;
    }

    while (bits > 7) {
        assert(bits_used == 0);
        *mem = (input >> (bits - 8)) & 0xFF;
        bits -= 8;
        ++mem;
    }

    if (bits) {
        *mem &= ~(((1 << bits) - 1) << (8 - bits));
        *mem |= (input & ((1 << bits) - 1)) << (8 - bits);
    }
}

uint32_t bitstream_writer_get_used_bytes(bitstream_writer_t* bitstream) {
    return bitstream->offset;
}
uint32_t bitstream_writer_get_used_bits(bitstream_writer_t* bitstream) {
    return bitstream->offset * 8 + bitstream->bits_used;
}

void align_bitstream_writer_to_next_byte(bitstream_writer_t* bitstream) {
    if (bitstream->bits_used) {
        bitstream->offset++;
        bitstream->bits_used = 0;
    }
    assert(bitstream->offset <= bitstream->size);
    assert(bitstream->bits_used == 0);
}

void bitstream_writer_add_padding_bits(bitstream_writer_t* bitstream, uint32_t nbits) {
    if (bitstream->bits_used) {
        uint32_t left = (8 - bitstream->bits_used);
        if (left <= nbits) {
            nbits -= left;
            bitstream->offset++;
            bitstream->bits_used = 0;
        }
        else {
            bitstream->bits_used += nbits;
            nbits = 0;
        }
    }

    uint8_t* mem = bitstream->mem + bitstream->offset;
    if (nbits > 7) {
        assert(bitstream->bits_used == 0);
        memset(mem, 0, nbits / 8);
        bitstream->offset += nbits / 8;
        nbits -= (nbits / 8) * 8;
    }

    if (nbits) {
        bitstream->bits_used = nbits;
        mem = bitstream->mem + bitstream->offset;
        mem[0] = 0;
    }
}

void bitstream_writer_add_padding_bytes(bitstream_writer_t* bitstream, uint32_t nbytes) {
    assert(bitstream->bits_used == 0);
    uint8_t* mem = bitstream->mem + bitstream->offset;
    memset(mem, 0, nbytes);
    bitstream->offset += nbytes;
}
