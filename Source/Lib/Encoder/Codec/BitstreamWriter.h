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

/* Bit writer for long runs of unary codes.
 *
 * Built like the nibble writer below and for the same reason: the flush is a
 * direct four-byte store rather than a call to write_N_bits. That one is
 * generic - a branch on alignment up front and a per-byte loop inside - so
 * every thirty-two bits cost a call doing four single-byte stores instead of
 * one word-sized store.
 *
 * Bits sit in the accumulator most significant first, right aligned. Before an
 * append there are fewer than thirty-two of them and a portion is at most
 * thirty-two bits long, so sixty-four positions are always enough.
 *
 * If the stream does not sit on a byte boundary, the already started positions
 * of the current byte are pulled into the accumulator. bitw_finish is mandatory
 * before any other write to the same stream. */
typedef struct bit_writer {
    uint8_t* base;  /* bitstream->mem, fixed for the writer's lifetime */
    size_t pos;     /* byte position from base; see bitw_init() on why this can exceed limit */
    size_t limit;   /* bitstream->size. size_t, not uint32_t: a narrower position would have to be
                     * zero-extended for every indexed store below, which costs ~15% .text in the
                     * hot packing translation units for nothing. */
    uint64_t acc;
    uint32_t nbits;
} bit_writer_t;

static INLINE void bitw_init(bit_writer_t* w, bitstream_writer_t* bitstream) {
    w->base = bitstream->mem;
    w->pos = bitstream->offset;
    w->limit = bitstream->size;
    const uint32_t used = bitstream->bits_used;
    if (used) {
        /* bitstream->offset can already be at/past size here (a prior write into an undersized
         * caller-provided buffer - see BitstreamWriter.c) even though bits_used was still updated;
         * guard the read the same way every store below guards its write. Checked on the integer
         * position, not on a pointer formed from it: `base + pos` would itself be out-of-bounds
         * pointer arithmetic once pos is already past limit, so it is only formed (as part of the
         * indexing below) once the bound is known to hold. */
        const uint8_t byte = (w->pos < w->limit) ? w->base[w->pos] : 0;
        w->acc = (uint64_t)(byte >> (8 - used));
        w->nbits = used;
    }
    else {
        w->acc = 0;
        w->nbits = 0;
    }
}

/* len is at most thirty-two; value carries len significant positions on the
 * right. */
static INLINE void bitw_put(bit_writer_t* w, uint64_t value, uint32_t len) {
    assert(len <= 32);
    w->acc = (w->acc << len) | value;
    w->nbits += len;
    if (w->nbits >= 32) {
        w->nbits -= 32;
        const uint32_t be = (uint32_t)(w->acc >> w->nbits);
        /* Guard, don't assert: a mis-sized (or adversarially undersized, e.g. coding_raw_disable=1's
         * unary-GCLI worst case) caller-provided buffer must fail safely here, not corrupt whatever
         * follows it. bitstream->offset (patched from w->pos in bitw_finish) still ends up reflecting
         * the true requested size either way, so the existing real-bytes-vs-window check downstream
         * (PackStageProcess.c) still detects and fails the frame - this only stops the write itself
         * from landing outside the buffer. Checked and indexed via the integer position (see
         * bitw_init()), not `w->base + w->pos` formed ahead of the check. */
        if (w->pos + 4 <= w->limit) {
            w->base[w->pos + 0] = (uint8_t)(be >> 24);
            w->base[w->pos + 1] = (uint8_t)(be >> 16);
            w->base[w->pos + 2] = (uint8_t)(be >> 8);
            w->base[w->pos + 3] = (uint8_t)be;
        }
        w->pos += 4;
        w->acc &= ((uint64_t)1 << w->nbits) - 1;
    }
}

static INLINE void bitw_finish(bit_writer_t* w, bitstream_writer_t* bitstream) {
    uint32_t n = w->nbits;
    const uint64_t a = w->acc;
    while (n >= 8) {
        n -= 8;
        if (w->pos < w->limit) {
            w->base[w->pos] = (uint8_t)(a >> n);
        }
        w->pos++;
    }
    if (n) {
        /* the tail is shorter than a byte: it is the high part of its byte, the
         * low positions are written later */
        if (w->pos < w->limit) {
            w->base[w->pos] = (uint8_t)((a & (((uint64_t)1 << n) - 1)) << (8 - n));
        }
    }
    bitstream->offset = (uint32_t)w->pos;
    bitstream->bits_used = n;
}

/* Nibble writer for packing group data.
 *
 * At this point the stream always sits on a nibble boundary, and
 * write_4_bits_align4 relies on that, but every nibble still costs a byte load
 * from memory in order to fill in its low half. A load-store dependency on one
 * and the same address is dragged across the whole line, and it takes a quarter
 * of the packing time.
 *
 * Here the nibbles accumulate in a register and are flushed eight at a time by
 * a single four-byte store. The flush goes straight to memory rather than
 * through write_N_bits: that one is generic, with branches on alignment and a
 * per-byte loop, and trading eight cheap stores for one such call would be a
 * loss.
 *
 * If the stream sits on a low nibble, the high half of the current byte is
 * pulled into the accumulator as the first nibble - everything is byte aligned
 * from there on. nibw_finish is mandatory before any other write to the same
 * stream. */
typedef struct nib_writer {
    uint8_t* base;  /* bitstream->mem, fixed for the writer's lifetime */
    size_t pos;     /* byte position from base; see nibw_init() on why this can exceed limit */
    size_t limit;   /* bitstream->size; size_t for the same reason as bit_writer_t's */
    uint64_t acc;
    uint32_t nnib;
} nib_writer_t;

static INLINE void nibw_init(nib_writer_t* w, bitstream_writer_t* bitstream) {
    w->base = bitstream->mem;
    w->pos = bitstream->offset;
    w->limit = bitstream->size;
    if (bitstream->bits_used) {
        /* See bitw_init(): offset can already be at/past size from a prior guarded-but-skipped
         * write. Checked on the integer position, not on a pointer formed from it - see bitw_init(). */
        const uint8_t byte = (w->pos < w->limit) ? w->base[w->pos] : 0;
        w->acc = (uint64_t)(byte >> 4);
        w->nnib = 1;
    }
    else {
        w->acc = 0;
        w->nnib = 0;
    }
}

static INLINE void nibw_put(nib_writer_t* w, uint32_t nibble) {
    w->acc = (w->acc << 4) | nibble;
    if (++w->nnib == 8) {
        const uint32_t be = (uint32_t)w->acc;
        /* See bitw_put(): guard, don't assert - a mis-sized buffer must fail safely, not corrupt
         * memory past it. Checked and indexed via the integer position, not a pointer formed ahead
         * of the check. */
        if (w->pos + 4 <= w->limit) {
            w->base[w->pos + 0] = (uint8_t)(be >> 24);
            w->base[w->pos + 1] = (uint8_t)(be >> 16);
            w->base[w->pos + 2] = (uint8_t)(be >> 8);
            w->base[w->pos + 3] = (uint8_t)be;
        }
        w->pos += 4;
        w->acc = 0;
        w->nnib = 0;
    }
}

/* A batch of nibbles at once: value carries count nibbles, right aligned, and
 * the most significant of them is emitted first. At most eight per call - then
 * the accumulator certainly cannot overflow (it already holds at most seven). */
static INLINE void nibw_put_chunk(nib_writer_t* w, uint64_t value, uint32_t count) {
    assert(count <= 8);
    w->acc = (w->acc << (4 * count)) | value;
    w->nnib += count;
    if (w->nnib >= 8) {
        const uint32_t rest = w->nnib - 8;
        const uint32_t be = (uint32_t)(w->acc >> (4 * rest));
        if (w->pos + 4 <= w->limit) {
            w->base[w->pos + 0] = (uint8_t)(be >> 24);
            w->base[w->pos + 1] = (uint8_t)(be >> 16);
            w->base[w->pos + 2] = (uint8_t)(be >> 8);
            w->base[w->pos + 3] = (uint8_t)be;
        }
        w->pos += 4;
        w->acc &= ((uint64_t)1 << (4 * rest)) - 1;
        w->nnib = rest;
    }
}

/* All the nibbles of one group in a single call.
 *
 * The "have eight accumulated" branch used to be taken per nibble, and a group
 * has up to sixteen of them. The planes are computed one after another anyway,
 * so accumulating them in a register is free, and it spares the writer a chain
 * of short dependent steps. There are never more than sixteen: fifteen planes
 * and a sign. */
static INLINE void nibw_put_group(nib_writer_t* w, uint64_t value, uint32_t count) {
    assert(count <= 16);
    if (count > 8) {
        nibw_put_chunk(w, value >> (4 * (count - 8)), 8);
        count -= 8;
        value &= ((uint64_t)1 << (4 * count)) - 1;
    }
    nibw_put_chunk(w, value, count);
}

static INLINE void nibw_finish(nib_writer_t* w, bitstream_writer_t* bitstream) {
    uint32_t n = w->nnib;
    const uint64_t a = w->acc;
    while (n >= 2) {
        if (w->pos < w->limit) {
            w->base[w->pos] = (uint8_t)((a >> ((n - 2) * 4)) & 0xFF);
        }
        w->pos++;
        n -= 2;
    }
    if (n == 1) {
        /* one nibble is left: it is the high half of its byte, the low half is
         * written later */
        if (w->pos < w->limit) {
            w->base[w->pos] = (uint8_t)((a & 0xF) << 4);
        }
    }
    bitstream->offset = (uint32_t)w->pos;
    bitstream->bits_used = n * 4;
}

/*
* write_4_bits_align4() Can be used only when bitstream is padded to 0 or 4 bits, otherwise data will be corrupted
*/
static INLINE void write_4_bits_align4(bitstream_writer_t* bitstream, uint8_t input) {
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    assert(input <= 0xf);
    /* Guard, don't assert: see BitstreamWriter.c's write_8_bits() for why this can't rely on
     * align_bitstream_writer_to_next_byte()'s NDEBUG-only assert as its only protection. Checked and
     * indexed via the integer offset, not a `bitstream->mem + bitstream->offset` pointer formed ahead
     * of the check - that pointer would itself be out-of-bounds once offset is already past size. */
    const uint8_t in_bounds = bitstream->offset < bitstream->size;

    if (bitstream->bits_used == 4) {
        if (in_bounds) {
            bitstream->mem[bitstream->offset] |= input;
        }
        bitstream->bits_used = 0;
        bitstream->offset++;
    }
    else { // bitstream->bits_used == 0;
        if (in_bounds) {
            bitstream->mem[bitstream->offset] = (input << 4);
        }
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
