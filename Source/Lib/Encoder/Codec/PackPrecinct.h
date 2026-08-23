/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef __PACK_PRECINCT_H__
#define __PACK_PRECINCT_H__
#include "BitstreamWriter.h"
#include "PrecinctEnc.h"
#include "Definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

SvtJxsErrorType_t pack_precinct(bitstream_writer_t* bitstream, pi_t* pi, precinct_enc_t* precinct,
                                SignHandlingStrategy coding_signs_handling);

/* Batched writing of unary codes.
 *
 * Every GCLI code is n ones and a terminating zero, that is from one to
 * thirty-two bits, and each of them used to reach the stream through its own
 * write_N_bits call: a branch on alignment, a branch on length, a per-byte
 * store. There are hundreds of such calls per line.
 *
 * Here the codes are first glued together in a 64-bit accumulator and the
 * stream is touched once per thirty-two bits, that is about six times more
 * rarely (the average code is shorter than six bits). The accumulator keeps the
 * bits most significant first, right aligned; before an append it holds fewer
 * than thirty-two significant bits and a code is at most thirty-two bits long,
 * so sixty-four are always enough.
 *
 * Flushing the accumulator is mandatory before any other write to the same
 * stream. */
typedef struct vlc_batch {
    uint64_t acc;
    uint32_t nbits;
} vlc_batch_t;

static INLINE void vlc_batch_init(vlc_batch_t* b) {
    b->acc = 0;
    b->nbits = 0;
}

static INLINE void vlc_batch_put(bitstream_writer_t* bitstream, vlc_batch_t* b, uint8_t nbits) {
    assert(nbits < 32);
    const uint32_t len = (uint32_t)nbits + 1;
    const uint64_t code = (((uint64_t)1 << nbits) - 1) << 1;
    b->acc = (b->acc << len) | code;
    b->nbits += len;
    if (b->nbits >= 32) {
        b->nbits -= 32;
        write_N_bits(bitstream, (uint32_t)(b->acc >> b->nbits), 32);
    }
}

static INLINE void vlc_batch_flush(bitstream_writer_t* bitstream, vlc_batch_t* b) {
    if (b->nbits) {
        write_N_bits(bitstream, (uint32_t)(b->acc & ((((uint64_t)1) << b->nbits) - 1)), (uint8_t)b->nbits);
        b->nbits = 0;
    }
}

/*Variable Length Coding
 * Params:
 * nbits - bits to coding
 * Write nbits with value 1, and one 0 bit at end.
 * Size of bits write to bitstream: nbits + 1
 */
static INLINE void vlc_encode_pack_bits(bitstream_writer_t* bitstream, uint8_t nbits) {
    //Estimate that this path will be faster for more bits
    if (nbits > 1) {
        uint32_t vlc_bits = ((1u << (nbits)) - 1) << 1;
        write_N_bits(bitstream, vlc_bits, nbits + 1);
    }
    else {
        if (nbits) { //(nbits == 1)
            write_2_bits(bitstream, 2);
        }
        else {
            write_1_bit(bitstream, 0);
        }
    }
}

/*Variable Length Coding.
* Params:
* x - Coding signed value
* r - Coding range param
* t - Coding range param
* To decode values r and t have to be known.
* Value of X can get 32 different values in range [xMin, xMax] where
* xMin = MAX(-16, MIN(t - r, 0));
* xMax = xMin + 31
* Range [xMin, xMax] = [xMin, -xMin] U (-xMin, xMax] = [xMin, 0] U (0, -xMin] U (-xMin, xMax]
* Least coding size is always for value 0.
* Each value further to the right or left in range [xMin, -xMin] takes up more ~2 bits
* Each value further to right in range [-xMin, xMax] takes up more 1 bits
*/
static INLINE void vlc_encode(bitstream_writer_t* bitstream, int32_t x, int32_t r, int32_t t) {
    assert(x < 32);
    int32_t max = r - t;
    if (max < 0) {
        max = 0;
    }
    int n = 0;
    if (x > max) {
        n = x + max;
    }
    else {
        x = x * 2;
        if (x < 0) {
            n = -x - 1;
        }
        else {
            n = x;
        }
    }
    vlc_encode_pack_bits(bitstream, n);
}

/*Variable Length Coding. when r and t are that same.
* Value of X can get 32 different values in range [0, 31]
*/
static INLINE void vlc_encode_simple(bitstream_writer_t* bitstream, int32_t x) {
    assert(x < 32);
    vlc_encode_pack_bits(bitstream, x);
}

/*Return number of bits -1 (no last negative bit) to coding x with variable length coding.*/
static INLINE uint8_t vlc_encode_get_bits(int32_t x, int32_t r, int32_t t) {
    assert(x < 32);
    int32_t max = r - t;
    if (max < 0) {
        max = 0;
    }
    int n = 0;
    if (x > max) {
        n = x + max;
    }
    else {
        x = x * 2;
        if (x < 0) {
            n = -x - 1;
        }
        else {
            n = x;
        }
    }
    return n;
}

void pack_data_single_group_c(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t gcli, uint8_t gtli);

#ifdef __cplusplus
}
#endif

#endif /*__PACK_PRECINCT_H__*/
