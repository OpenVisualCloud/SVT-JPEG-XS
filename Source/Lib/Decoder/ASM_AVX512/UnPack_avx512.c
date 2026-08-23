/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <string.h>
#include <immintrin.h>
#include "UnPack_avx512.h"
#include "UnPack_avx2.h"
#include "Definitions.h"

/* Parsing a group is the inverse of packing it: every nibble of a bit plane
 * carries one bit of all four coefficients, most significant plane first.
 *
 * The former parser handled one plane per pass: the nibble was broadcast across
 * the vector lanes (VPBROADCASTD out of a general purpose register), spread
 * over the bits by a mask and merged into an accumulator with a shift. That
 * gave a long dependent chain per group, and the broadcast out of a general
 * purpose register is an expensive operation in itself.
 *
 * Here the nibbles are first collected in an ordinary 64-bit word: there are at
 * most fifteen planes, that is sixty bits, so everything fits. Splitting them
 * into four coefficients is then one instruction per coefficient - PEXT gathers
 * the bits selected by a mask into the low positions.
 *
 * PEXT is used here without hesitation: it is microcoded and slow only on Zen 1
 * and Zen 2, and this function is only reached when AVX-512 is present, that is
 * on Skylake-X and newer or Zen 4 and newer, where PEXT is a single uop. */

/* Sign nibble: its bit (3 - i) belongs to coefficient i and has to become the
 * top bit of the i-th 16-bit word. The table replaces four shifts. */
static const uint64_t unpack_sign_spread[16] = {
    0x0000000000000000ULL,
    0x8000000000000000ULL,
    0x0000800000000000ULL,
    0x8000800000000000ULL,
    0x0000000080000000ULL,
    0x8000000080000000ULL,
    0x0000800080000000ULL,
    0x8000800080000000ULL,
    0x0000000000008000ULL,
    0x8000000000008000ULL,
    0x0000800000008000ULL,
    0x8000800000008000ULL,
    0x0000000080008000ULL,
    0x8000000080008000ULL,
    0x0000800080008000ULL,
    0x8000800080008000ULL,
};

/* Returns the four coefficients of a group packed into a 64-bit word, sixteen
 * bits per coefficient, already shifted by gtli. */
static INLINE uint64_t unpack_group_magnitudes_avx512(reader_short_t* r, int32_t size, uint8_t gtli) {
    uint64_t acc = 0;
    for (int32_t i = 0; i < size; i++) {
        acc = (acc << 4) | read_4_bits_align4_fast(r);
    }
    const uint64_t v0 = _pext_u64(acc, 0x8888888888888888ULL);
    const uint64_t v1 = _pext_u64(acc, 0x4444444444444444ULL);
    const uint64_t v2 = _pext_u64(acc, 0x2222222222222222ULL);
    const uint64_t v3 = _pext_u64(acc, 0x1111111111111111ULL);
    /* After the shift a coefficient occupies at most gcli <= 15 bits, so a
     * common shift of the packed word never carries bits into a neighbouring
     * lane. */
    return (v0 | (v1 << 16) | (v2 << 32) | (v3 << 48)) << gtli;
}

void unpack_n_groups_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups) {
    for (uint32_t group = 0; group < n_groups; group++) {
        uint64_t out = 0;
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            /* Signs precede the group data in the stream. */
            const uint64_t signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            out = unpack_group_magnitudes_avx512(r, size, gtli) | signs;
        }
        memcpy(buf, &out, sizeof(out));
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups_nosign_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups) {
    for (uint32_t group = 0; group < n_groups; group++) {
        uint64_t out = 0;
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            out = unpack_group_magnitudes_avx512(r, size, gtli);
        }
        memcpy(buf, &out, sizeof(out));
        buf += GROUP_SIZE;
    }
}

SvtJxsErrorType_t unpack_data_avx512(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                     uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                     int32_t* precinct_bits_left) {
    return unpack_data_common(bitstream,
                              buf,
                              w,
                              gclis,
                              group_size,
                              gtli,
                              sign_flag,
                              leftover_signs_num,
                              precinct_bits_left,
                              unpack_n_groups_avx512,
                              unpack_n_groups_nosign_avx512);
}
