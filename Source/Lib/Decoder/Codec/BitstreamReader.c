/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "BitstreamReader.h"
#include <assert.h>

void bitstream_reader_init(bitstream_reader_t *bitstream, const uint8_t *bitstream_buf, size_t bitstream_buf_size) {
    bitstream->offset = 0;
    bitstream->bits_used = 0;
    bitstream->size = (uint32_t)bitstream_buf_size;
    bitstream->mem = bitstream_buf;
}

uint8_t read_8_bits(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 1));
    unsigned int val;
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val = mem[0];
    bitstream->offset += 1;
    return val;
}
uint16_t read_16_bits(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 2));
    unsigned int val;
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val = mem[0] << 8;
    val |= mem[1];
    bitstream->offset += 2;
    return val;
}

uint32_t read_24_bits(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 3));
    unsigned int val;
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val = ((unsigned int)mem[0]) << 16;
    val |= mem[1] << 8;
    val |= mem[2];
    bitstream->offset += 3;
    return val;
}

uint32_t read_32_bits(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 4));
    unsigned int val;
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val = ((unsigned int)mem[0]) << 24;
    val |= mem[1] << 16;
    val |= mem[2] << 8;
    val |= mem[3];
    bitstream->offset += 4;
    return val;
}

void read_422_bits(bitstream_reader_t *bitstream, uint8_t *val) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 1));
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val[0] = mem[0] >> 4;
    val[1] = (mem[0] >> 2) & 0x03;
    val[2] = (mem[0]) & 0x03;
    bitstream->offset += 1;
}

void read_2222_bits(bitstream_reader_t *bitstream, uint8_t *val) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 1));
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val[0] = (mem[0] & 0xC0) >> 6;
    val[1] = (mem[0] & 0x30) >> 4;
    val[2] = (mem[0] & 0x0C) >> 2;
    val[3] = mem[0] & 0x03;
    bitstream->offset += 1;
}

void read_134_bits(bitstream_reader_t *bitstream, uint8_t *val) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 1));
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val[0] = mem[0] >> 7;         //:1
    val[1] = (mem[0] >> 4) & 0x7; //:3
    val[2] = mem[0] & 0xF;        //:4
    bitstream->offset += 1;
}

void read_2x4_bits(bitstream_reader_t *bitstream, uint8_t *val) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 1));
    uint8_t const *mem = bitstream->mem + bitstream->offset;

    val[0] = mem[0] >> 4;
    val[1] = mem[0] & 0xF;
    bitstream->offset += 1;
}

void read_8x1_bit(bitstream_reader_t *bitstream, uint8_t *val) {
    assert(bitstream_reader_is_enough_bytes(bitstream, 1));
    uint8_t const mem = *bitstream->mem;

    val[0] = (mem) >> 7;
    val[1] = (mem & 0x40) >> 6;
    val[2] = (mem & 0x20) >> 5;
    val[3] = (mem & 0x10) >> 4;
    val[4] = (mem & 0x08) >> 3;
    val[5] = (mem & 0x04) >> 2;
    val[6] = (mem & 0x02) >> 1;
    val[7] = mem & 0x01;
}

/*Copy first MSB nbits to array val*/
void read_Nx1_bit(bitstream_reader_t *bitstream, uint8_t *val, uint8_t nbits) {
    assert(bitstream_reader_is_enough_bits(bitstream, nbits));
    uint8_t const mem = *bitstream->mem;

    if (nbits > 0) {
        if (nbits > 3) {
            val[0] = (mem) >> 7;
            val[1] = (mem & 0x40) >> 6;
            val[2] = (mem & 0x20) >> 5;
            val[3] = (mem & 0x10) >> 4;
            if (nbits > 4)
                val[4] = (mem & 0x08) >> 3;
            if (nbits > 5)
                val[5] = (mem & 0x04) >> 2;
            if (nbits > 6)
                val[6] = (mem & 0x02) >> 1;
            if (nbits > 7)
                val[7] = mem & 0x01;
        }
        else {
            val[0] = (mem) >> 7;
            if (nbits > 1)
                val[1] = (mem & 0x40) >> 6;
            if (nbits > 2)
                val[2] = (mem & 0x20) >> 5;
        }
    }
}

uint8_t read_1_bit(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bits(bitstream, 1));
    uint8_t const *mem = bitstream->mem + bitstream->offset;
    uint8_t val;

    val = (*mem >> (7 - bitstream->bits_used)) & 0x1;

    (bitstream->bits_used)++;

    if (bitstream->bits_used == 8) {
        bitstream->bits_used = 0;
        bitstream->offset++;
    }

    return val;
}

uint8_t read_2_bit(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bits(bitstream, 2));
    uint8_t const *mem = bitstream->mem + bitstream->offset;
    uint8_t val;

    if (bitstream->bits_used < 7) {
        val = (*mem >> (6 - bitstream->bits_used)) & 0x3;
        bitstream->bits_used += 2;
        if (bitstream->bits_used == 8) {
            bitstream->bits_used = 0;
            bitstream->offset++;
        }
    }
    else {
        val = ((*mem & 0x1) << 1) & ((*mem + 1) >> 7);
        bitstream->bits_used = 1;
        bitstream->offset++;
    }

    return val;
}

uint8_t read_4_bits(bitstream_reader_t *bitstream) {
    assert(bitstream_reader_is_enough_bits(bitstream, 4));
    uint8_t const *mem = bitstream->mem + bitstream->offset;
    uint8_t val = 0;

    if (bitstream->bits_used < 4) {
        val = (*mem >> (4 - bitstream->bits_used)) & 0xF;
        bitstream->bits_used += 4;
    }
    else {
        switch (bitstream->bits_used) {
        case 4: {
            val = (*mem) & 0xF;
            bitstream->bits_used = 0;
            break;
        }
        case 5: {
            val = (((*mem) & 0x7) << 1) | ((((*(mem + 1)) >> 7) & 0x1));
            bitstream->bits_used = 1;
            break;
        }
        case 6: {
            val = (((*mem) & 0x3) << 2) | ((((*(mem + 1)) >> 6) & 0x3));
            bitstream->bits_used = 2;
            break;
        }
        case 7: {
            val = (((*mem) & 0x1) << 3) | ((((*(mem + 1)) >> 5) & 0x7));
            bitstream->bits_used = 3;
            break;
        }
        };
        bitstream->offset++;
    }
    return val;
}

void align_bitstream_reader_to_next_byte(bitstream_reader_t *bitstream) {
    if (bitstream->bits_used) {
        bitstream->bits_used = 0;
        bitstream->offset++;
    }
}

uint32_t bitstream_reader_get_used_bytes(bitstream_reader_t *bitstream) {
    return bitstream->offset;
}

uint32_t bitstream_reader_get_left_bytes(bitstream_reader_t *bitstream) {
    return bitstream->size - bitstream->offset - !!bitstream->bits_used;
}

uint64_t bitstream_reader_get_left_bits(bitstream_reader_t *bitstream) {
    return ((uint64_t)bitstream->size - bitstream->offset) * 8 - bitstream->bits_used;
}

uint8_t bitstream_reader_is_enough_bytes(bitstream_reader_t *bitstream, uint32_t nbytes) {
    return nbytes <= (bitstream->size - bitstream->offset - !!bitstream->bits_used);
}

uint8_t bitstream_reader_is_enough_bits(bitstream_reader_t *bitstream, uint32_t nbits) {
    return nbits <= (((bitstream->size - bitstream->offset) << 3) - bitstream->bits_used);
}

void bitstream_reader_add_padding(bitstream_reader_t *bitstream, uint32_t nbytes) {
    assert(bitstream_reader_is_enough_bytes(bitstream, nbytes));
    bitstream->offset += nbytes;
}

void bitstream_reader_skip_bits(bitstream_reader_t *bitstream, uint32_t nbits) {
    assert(bitstream_reader_is_enough_bits(bitstream, nbits));
    bitstream->offset += (bitstream->bits_used + nbits) / 8;
    bitstream->bits_used = (bitstream->bits_used + nbits) % 8;
}
