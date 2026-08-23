/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <string.h>
#include <immintrin.h>
#include "UnPack_avx512.h"
#include "unpack_common.h"
#include "Definitions.h"
#include "EncDec.h" /* TRUNCATION_MAX: the single load holds fifteen planes at most */
#include "SvtUtility.h"

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

/* Spreads count nibbles, right aligned, into four coefficients. */
static INLINE uint64_t unpack_planes_to_lanes(uint64_t acc, uint8_t gtli) {
    const uint64_t v0 = _pext_u64(acc, 0x8888888888888888ULL);
    const uint64_t v1 = _pext_u64(acc, 0x4444444444444444ULL);
    const uint64_t v2 = _pext_u64(acc, 0x2222222222222222ULL);
    const uint64_t v3 = _pext_u64(acc, 0x1111111111111111ULL);
    return (v0 | (v1 << 16) | (v2 << 32) | (v3 << 48)) << gtli;
}

void unpack_n_groups_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    /* The vast majority of groups are empty: about eighty-seven per cent on
     * 1080p at 4 bits per pixel. Each of them used to cost a GCLI load, a
     * compare, a poorly predicted branch and a store of eight zeroes. Now the
     * output is zeroed in one pass and the walk follows the bits of a non-empty
     * mask, so an empty group costs nothing and the per-group branch is gone. */
    memset(buf, 0, (size_t)n_groups * GROUP_SIZE * sizeof(uint16_t));

    /* Fast path. A group's address comes from adding up lengths rather than
     * from reading the previous group, so the dependency between groups is a
     * chain of single-cycle additions while the loads and the parsing itself
     * proceed in parallel. */
    const __m512i gtli_vec = _mm512_set1_epi8((char)gtli);
    while (group < n_groups) {
        const uint32_t chunk = MIN(n_groups - group, 64u);
        const __mmask64 valid = (chunk >= 64) ? ~(__mmask64)0 : (((__mmask64)1 << chunk) - 1);
        __mmask64 todo = _mm512_mask_cmpgt_epu8_mask(valid, _mm512_maskz_loadu_epi8(valid, gclis + group), gtli_vec);
        while (todo) {
            const uint32_t k = unpack_first_set_bit(todo);
            const uint32_t size = (uint32_t)gclis[group + k] - gtli;
            /* A corrupt stream can carry a GCLI above the truncation maximum:
             * the unary code yields up to thirty-one. Such a group holds more
             * bit planes than the single load can take - fifteen - and the
             * shift that would extract it would be undefined, so the group is
             * left to the sequential reader below. */
            if (size > TRUNCATION_MAX || ((nib + 1) >> 1) >= safe_bytes) {
                group += k;
                goto tail;
            }
            const uint64_t signs = unpack_sign_spread[unpack_one_nibble(base, nib)];
            const uint64_t out = unpack_planes_to_lanes(unpack_load_nibbles(base, nib + 1, size), gtli) | signs;
            memcpy(buf + (size_t)(group + k) * GROUP_SIZE, &out, sizeof(out));
            nib += size + 1;
            todo &= todo - 1;
        }
        group += chunk;
    }

tail:
    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);
    buf += (size_t)group * GROUP_SIZE;

    /* End of the line is read sequentially, without reading past the data.
     * Zeroes need not be written: the buffer is already cleared. */
    for (; group < n_groups; group++) {
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            const uint64_t signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            uint64_t acc = 0;
            for (int32_t i = 0; i < size; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes(acc, gtli) | signs;
            memcpy(buf, &out, sizeof(out));
        }
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups_nosign_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                   uint32_t safe_bytes) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    /* The vast majority of groups are empty: about eighty-seven per cent on
     * 1080p at 4 bits per pixel. Each of them used to cost a GCLI load, a
     * compare, a poorly predicted branch and a store of eight zeroes. Now the
     * output is zeroed in one pass and the walk follows the bits of a non-empty
     * mask, so an empty group costs nothing and the per-group branch is gone. */
    memset(buf, 0, (size_t)n_groups * GROUP_SIZE * sizeof(uint16_t));

    const __m512i gtli_vec = _mm512_set1_epi8((char)gtli);
    while (group < n_groups) {
        const uint32_t chunk = MIN(n_groups - group, 64u);
        const __mmask64 valid = (chunk >= 64) ? ~(__mmask64)0 : (((__mmask64)1 << chunk) - 1);
        __mmask64 todo = _mm512_mask_cmpgt_epu8_mask(valid, _mm512_maskz_loadu_epi8(valid, gclis + group), gtli_vec);
        while (todo) {
            const uint32_t k = unpack_first_set_bit(todo);
            const uint32_t size = (uint32_t)gclis[group + k] - gtli;
            /* A corrupt stream can carry a GCLI above the truncation maximum:
             * the unary code yields up to thirty-one. Such a group holds more
             * bit planes than the single load can take - fifteen - and the
             * shift that would extract it would be undefined, so the group is
             * left to the sequential reader below. */
            if (size > TRUNCATION_MAX || (nib >> 1) >= safe_bytes) {
                group += k;
                goto tail;
            }
            const uint64_t out = unpack_planes_to_lanes(unpack_load_nibbles(base, nib, size), gtli);
            memcpy(buf + (size_t)(group + k) * GROUP_SIZE, &out, sizeof(out));
            nib += size;
            todo &= todo - 1;
        }
        group += chunk;
    }

tail:
    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);
    buf += (size_t)group * GROUP_SIZE;

    for (; group < n_groups; group++) {
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            uint64_t acc = 0;
            for (int32_t i = 0; i < size; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes(acc, gtli);
            memcpy(buf, &out, sizeof(out));
        }
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
