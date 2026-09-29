/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "BitstreamWriter.h"
#include <string.h>
#include <assert.h>

/* Every write below is guarded instead of relying on align_bitstream_writer_to_next_byte()'s assert,
 * which is compiled out under NDEBUG and only runs after a whole slice's writes anyway: a mis-sized
 * (or adversarially undersized - e.g. coding_raw_disable=1's unary-GCLI worst case, which the old
 * lossless_worst_case_bytes_per_frame() metadata margin underestimated) caller-provided buffer must
 * fail safely, not corrupt whatever memory follows it. offset/bits_used bookkeeping still advances
 * exactly as before regardless of whether the write actually lands, so the existing
 * real-bytes-vs-window check downstream (PackStageProcess.c) still detects and fails the frame.
 *
 * Positions are carried as integer byte offsets from bitstream->mem, never as pointers, and a pointer
 * is only formed once the bound is known to hold. bitstream->offset can legitimately run past
 * bitstream->size here (a preceding guarded-but-skipped write still advances it), and
 * `bitstream->mem + bitstream->offset` would then be out-of-bounds pointer arithmetic - undefined
 * even if the pointer is never dereferenced. Offsets have no such rule, so the guards below are exact
 * rather than merely working in practice. */
static INLINE uint8_t bw_byte_fits(const bitstream_writer_t* bitstream, uint64_t at) {
    return at < (uint64_t)bitstream->size;
}

/* True if `count` bytes starting at bitstream->offset are all inside the buffer. */
static INLINE uint8_t bw_bytes_fit(const bitstream_writer_t* bitstream, uint32_t count) {
    return (uint64_t)bitstream->offset + count <= (uint64_t)bitstream->size;
}

static INLINE void bw_put_byte(const bitstream_writer_t* bitstream, uint64_t at, uint8_t value) {
    if (bw_byte_fits(bitstream, at)) {
        bitstream->mem[at] = value;
    }
}

static INLINE void bw_or_byte(const bitstream_writer_t* bitstream, uint64_t at, uint8_t value) {
    if (bw_byte_fits(bitstream, at)) {
        bitstream->mem[at] |= value;
    }
}

/* Clamp a byte count to what's actually left in the buffer from `at` onward, for memset-based
 * writes (bitstream_writer_add_padding_bits/bytes) where a single call can span many bytes. */
static INLINE uint32_t bw_clamp_count(const bitstream_writer_t* bitstream, uint64_t at, uint32_t count) {
    const uint64_t remaining = at < (uint64_t)bitstream->size ? (uint64_t)bitstream->size - at : 0;
    return (uint32_t)(remaining < count ? remaining : count);
}

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

    bw_put_byte(bitstream, bitstream->offset, input);
    bitstream->offset += 1;
}

void write_16_bits(bitstream_writer_t* bitstream, uint16_t input) {
    assert(bitstream->bits_used == 0);

    if (bw_bytes_fit(bitstream, 2)) {
        uint8_t* mem = bitstream->mem + bitstream->offset;
        mem[0] = input >> 8;
        mem[1] = (uint8_t)input;
    }
    bitstream->offset += 2;
}

void write_24_bits(bitstream_writer_t* bitstream, uint32_t input) {
    assert(bitstream->bits_used == 0);

    if (bw_bytes_fit(bitstream, 3)) {
        uint8_t* mem = bitstream->mem + bitstream->offset;
        mem[2] = input;
        mem[1] = input >> 8;
        mem[0] = input >> 16;
    }
    bitstream->offset += 3;
}

void write_32_bits(bitstream_writer_t* bitstream, uint32_t input) {
    assert(bitstream->bits_used == 0);

    if (bw_bytes_fit(bitstream, 4)) {
        uint8_t* mem = bitstream->mem + bitstream->offset;
        mem[3] = input;
        mem[2] = input >> 8;
        mem[1] = input >> 16;
        mem[0] = input >> 24;
    }
    bitstream->offset += 4;
}

void write_422_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2, uint8_t input_3) {
    assert(bitstream->bits_used == 0);

    bw_put_byte(bitstream, bitstream->offset, (input_1 << 4) | ((input_2 & 0x03) << 2) | (input_3 & 0x03));
    bitstream->offset += 1;
}

void write_134_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2, uint8_t input_3) {
    assert(bitstream->bits_used == 0);

    bw_put_byte(bitstream, bitstream->offset, (input_1 << 7) | ((input_2 & 0xF) << 4) | (input_3 & 0xF));
    bitstream->offset += 1;
}

void write_2x4_bits(bitstream_writer_t* bitstream, uint8_t input_1, uint8_t input_2) {
    assert(bitstream->bits_used == 0);

    bw_put_byte(bitstream, bitstream->offset, (input_1 << 4) | (input_2 & 0xF));
    bitstream->offset += 1;
}

void write_1_bit(bitstream_writer_t* bitstream, uint8_t input) {
    const uint8_t bit = (uint8_t)((input & 0x1) << (7 - bitstream->bits_used));

    if (bitstream->bits_used == 0) {
        bw_put_byte(bitstream, bitstream->offset, bit);
    }
    else {
        bw_or_byte(bitstream, bitstream->offset, bit);
    }

    (bitstream->bits_used)++;

    if (bitstream->bits_used == 8) {
        bitstream->bits_used = 0;
        bitstream->offset++;
    }
}

void write_2_bits(bitstream_writer_t* bitstream, uint8_t input) {
    const uint64_t at = bitstream->offset;

    if (bitstream->bits_used < 7) {
        if (bitstream->bits_used == 0) {
            bw_put_byte(bitstream, at, (uint8_t)((input & 0x3) << (6)));
        }
        else {
            bw_or_byte(bitstream, at, (uint8_t)((input & 0x3) << (6 - bitstream->bits_used)));
        }
        bitstream->bits_used += 2;
        if (bitstream->bits_used == 8) {
            bitstream->bits_used = 0;
            bitstream->offset++;
        }
    }
    else {
        bw_or_byte(bitstream, at, (uint8_t)((input & 0x2) >> 1));
        bw_put_byte(bitstream, at + 1, (uint8_t)((input & 0x1) << 7));
        bitstream->bits_used = 1;
        bitstream->offset++;
    }
}

void write_4_bits(bitstream_writer_t* bitstream, uint8_t input) {
    const uint64_t at = bitstream->offset;

    if (bitstream->bits_used < 4) {
        if (bitstream->bits_used == 0) {
            bw_put_byte(bitstream, at, (uint8_t)((input & 0xF) << (4)));
        }
        else {
            bw_or_byte(bitstream, at, (uint8_t)((input & 0xF) << (4 - bitstream->bits_used)));
        }
        bitstream->bits_used += 4;
    }
    else {
        switch (bitstream->bits_used) {
        case 4: {
            bw_or_byte(bitstream, at, input & 0xF);
            bitstream->bits_used = 0;
            break;
        }
        case 5: {
            bw_or_byte(bitstream, at, (uint8_t)(((input) >> 1) & 0x7));
            bw_put_byte(bitstream, at + 1, (uint8_t)((((input)&0x1)) << 7));
            bitstream->bits_used = 1;
            break;
        }
        case 6: {
            bw_or_byte(bitstream, at, (uint8_t)(((input) >> 2) & 0x3));
            bw_put_byte(bitstream, at + 1, (uint8_t)((((input)&0x3)) << 6));
            bitstream->bits_used = 2;
            break;
        }
        case 7: {
            bw_or_byte(bitstream, at, (uint8_t)(((input) >> 3) & 0x1));
            bw_put_byte(bitstream, at + 1, (uint8_t)((((input)&0x7)) << 5));
            bitstream->bits_used = 3;
            break;
        }
        };
        bitstream->offset++;
    }
}

void write_N_bits(bitstream_writer_t* bitstream, uint32_t input, uint8_t bits) {
    assert(bits == 32 || (input >> bits) == 0);
    uint64_t at = bitstream->offset;
    if (bitstream->bits_used) {
        uint32_t left = (8 - bitstream->bits_used);
        uint8_t bits_to_copy = bits;
        if (bits_to_copy > left) {
            bits_to_copy = left;
        }
        bw_or_byte(bitstream, at, (uint8_t)((input >> (bits - bits_to_copy)) << (left - bits_to_copy)));
        if (left > bits_to_copy) {
            bitstream->bits_used += bits_to_copy;
            return;
        }
        bits -= bits_to_copy;
        bitstream->offset++;
        bitstream->bits_used = 0;
        at++;
    }

    while (bits > 7) {
        assert(bitstream->bits_used == 0);
        bw_put_byte(bitstream, at, (uint8_t)((input >> (bits - 8)) & 0xFF));
        bits -= 8;
        bitstream->offset++;
        ++at;
    }

    if (bits) {
        bw_put_byte(bitstream, at, (uint8_t)((input & ((1 << bits) - 1)) << (8 - bits)));
        bitstream->bits_used = bits;
    }
}

void update_N_bits(bitstream_writer_t* bitstream, uint32_t offset_bits, uint32_t input, uint8_t bits) {
    assert(bits == 32 || (input >> bits) == 0);
    uint64_t at = offset_bits >> 3;
    uint32_t bits_used = offset_bits & 7;
    if (bits_used) {
        uint32_t left = (8 - bits_used);
        uint8_t bits_to_copy = bits;
        if (bits_to_copy > left) {
            bits_to_copy = left;
        }
        if (bw_byte_fits(bitstream, at)) {
            bitstream->mem[at] &= ~(((1 << bits_to_copy) - 1) << (left - bits_to_copy));
            bitstream->mem[at] |= (input >> (bits - bits_to_copy)) << (left - bits_to_copy);
        }
        if (left > bits_to_copy) {
            bits_used += bits_to_copy;
            return;
        }
        bits -= bits_to_copy;
        bits_used = 0;
        at++;
    }

    while (bits > 7) {
        assert(bits_used == 0);
        if (bw_byte_fits(bitstream, at)) {
            bitstream->mem[at] = (input >> (bits - 8)) & 0xFF;
        }
        bits -= 8;
        ++at;
    }

    if (bits) {
        if (bw_byte_fits(bitstream, at)) {
            bitstream->mem[at] &= ~(((1 << bits) - 1) << (8 - bits));
            bitstream->mem[at] |= (input & ((1 << bits) - 1)) << (8 - bits);
        }
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

    if (nbits > 7) {
        assert(bitstream->bits_used == 0);
        uint32_t full_bytes = nbits / 8;
        /* memset wants a valid pointer even for a zero length, so only form one when something
         * actually lands inside the buffer. */
        const uint32_t clamped = bw_clamp_count(bitstream, bitstream->offset, full_bytes);
        if (clamped) {
            memset(bitstream->mem + bitstream->offset, 0, clamped);
        }
        bitstream->offset += full_bytes;
        nbits -= full_bytes * 8;
    }

    if (nbits) {
        bitstream->bits_used = nbits;
        bw_put_byte(bitstream, bitstream->offset, 0);
    }
}

void bitstream_writer_add_padding_bytes(bitstream_writer_t* bitstream, uint32_t nbytes) {
    assert(bitstream->bits_used == 0);
    /* See bitstream_writer_add_padding_bits(): no pointer unless the memset has something to do. */
    const uint32_t clamped = bw_clamp_count(bitstream, bitstream->offset, nbytes);
    if (clamped) {
        memset(bitstream->mem + bitstream->offset, 0, clamped);
    }
    bitstream->offset += nbytes;
}
