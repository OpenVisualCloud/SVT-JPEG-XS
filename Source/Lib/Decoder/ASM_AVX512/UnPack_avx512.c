/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <string.h>
#include <immintrin.h>
#include "UnPack_avx512.h"
#include "unpack_common.h"
#include "unpack_common_avx512.h"
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

/* Shared walk for unpack_n_groups_avx512/unpack_n_groups_nosign_avx512: same
 * merge as unpack_n_groups_impl in UnPack_avx2.c, for the same reason - see
 * that file's comment. */
static INLINE void unpack_n_groups_avx512_impl(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf,
                                               uint32_t n_groups, uint32_t safe_bytes, const int has_sign) {
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
            const uint32_t k = svt_first_set_bit(todo);
            const uint32_t size = (uint32_t)gclis[group + k] - gtli;
            /* A corrupt stream can carry a GCLI above the truncation maximum:
             * the unary code yields up to thirty-one. Such a group holds more
             * bit planes than the single load can take - fifteen - and the
             * shift that would extract it would be undefined, so the group is
             * left to the sequential reader below. */
            if (size > TRUNCATION_MAX || ((nib + (uint32_t)has_sign) >> 1) >= safe_bytes) {
                group += k;
                goto tail;
            }
            uint64_t out = unpack_planes_to_lanes(unpack_load_nibbles(base, nib + (uint32_t)has_sign, size), gtli);
            if (has_sign) {
                out |= unpack_sign_spread[unpack_one_nibble(base, nib)];
            }
            memcpy(buf + (size_t)(group + k) * GROUP_SIZE, &out, sizeof(out));
            nib += size + (uint32_t)has_sign;
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
            uint64_t signs = 0;
            if (has_sign) {
                signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            }
            /* A corrupt stream can carry a GCLI far above the truncation maximum: the
             * unary code yields up to thirty-one, and vertical prediction can wrap it
             * to anything up to two hundred and fifty-five. The fast path above
             * already declines to touch such a group at all; this sequential reader
             * has to decline the same way, or it reads size nibbles - unbounded by
             * anything - straight past the end of whatever buffer backs it. */
            const int32_t read_planes = (size > TRUNCATION_MAX) ? TRUNCATION_MAX : size;
            uint64_t acc = 0;
            for (int32_t i = 0; i < read_planes; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            const uint64_t out = unpack_planes_to_lanes(acc, gtli) | signs;
            memcpy(buf, &out, sizeof(out));
        }
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes) {
    unpack_n_groups_avx512_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 1);
}

void unpack_n_groups_nosign_avx512(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                                   uint32_t safe_bytes) {
    unpack_n_groups_avx512_impl(gclis, gtli, r, buf, n_groups, safe_bytes, 0);
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

/* The same run of GCLI unary codes as unpack_gclis_unary_run_avx2, a 64-bit window at a time.
 *
 * In the inverted window every set bit ends one code. VPCOMPRESSB with the window as the mask packs
 * the lane numbers of the set bits, from the lowest bit up; the lane numbers are stored as distances
 * from the top of the window, so the packed bytes are where the codes end, last code first. One byte
 * permute puts them in stream order, a second one shifts them by a lane, and the difference of the
 * two is the length of every code, which is its value plus one.
 *
 * The cost of a window does not depend on how many codes it holds, so all of them are taken, up to
 * sixty-four, and a masked store writes exactly as many as are decoded: the run can go on to the end
 * of the line. What is left is only the last bytes before byte_limit. A window without any code end,
 * or a code longer than thirty-two bits, is a corrupt stream, as for the scalar reader. */
TARGET_AVX512_VBMI2 uint32_t unpack_gclis_unary_run_avx512_vbmi2(const uint8_t* mem, uint32_t* bit_pos, uint32_t byte_limit,
                                                                 uint8_t* out, uint32_t count, int8_t gtli, int32_t* error) {
    // clang-format off
    /* lane i: 63 - i, the distance of bit i from the top of the window */
    const __m512i from_top = _mm512_set_epi8(0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                             16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                                             32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
                                             48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63);
    /* lane i: i */
    const __m512i lane = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48,
                                         47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
                                         31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
                                         15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0);
    // clang-format on
    const __m512i lane_down = _mm512_sub_epi8(lane, _mm512_set1_epi8(1));
    const __m512i minus_one = _mm512_set1_epi8(-1);
    const __m512i max_length = _mm512_set1_epi8(32);
    const __m512i value_offset = _mm512_set1_epi8((char)(gtli - 1));
    uint32_t pos = *bit_pos;
    uint32_t done = 0;
    int32_t err = 0;
    while (done < count && (pos >> 3) + 8 <= byte_limit) {
        uint64_t word;
        memcpy(&word, mem + (pos >> 3), sizeof(word));
        const uint64_t w = ~UNPACK_BSWAP64(word) << (pos & 7);
        const uint32_t n = (uint32_t)_mm_popcnt_u64(w);
        if (!n) {
            err = 1;
            break;
        }
        const uint32_t m = MIN(n, count - done);
        /* the end of code k, as a distance from the top: lane n - 1 - k of the packed bytes */
        const __m512i packed = _mm512_maskz_compress_epi8(w, from_top);
        const __m512i end = _mm512_permutexvar_epi8(_mm512_sub_epi8(_mm512_set1_epi8((char)(n - 1)), lane), packed);
        /* the end of code k - 1, and -1 before the first code */
        const __m512i end_prev = _mm512_mask_permutexvar_epi8(minus_one, ~(__mmask64)1, lane_down, end);
        const __m512i length = _mm512_sub_epi8(end, end_prev);
        const __mmask64 keep = _bzhi_u64(~0ULL, m);
        /* a code longer than thirty-two bits is a corrupt stream, as for the scalar reader */
        err |= _mm512_mask_cmpgt_epi8_mask(keep, length, max_length) != 0;
        _mm512_mask_storeu_epi8(out + done, keep, _mm512_add_epi8(length, value_offset));
        pos += 64 - svt_first_set_bit(_pdep_u64((uint64_t)1 << (n - m), w));
        done += m;
    }
    *bit_pos = pos;
    *error = err;
    return done;
}

/* See unpack_raw_gclis_bytes_avx2(). The bytes left after the last full vector are loaded and
 * stored under a mask, so nothing outside the line is touched. */
void unpack_raw_gclis_bytes_avx512(const uint8_t* mem, uint8_t* gclis, uint32_t bytes) {
    const __m512i low_nibble = _mm512_set1_epi16(0x0F);
    uint32_t i = 0;
    for (; i + 32 <= bytes; i += 32) {
        const __m512i v = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i*)(mem + i)));
        const __m512i out = _mm512_or_si512(_mm512_srli_epi16(v, 4), _mm512_slli_epi16(_mm512_and_si512(v, low_nibble), 8));
        _mm512_storeu_si512((__m512i*)(gclis + 2 * i), out);
    }
    if (i < bytes) {
        const uint32_t rest = bytes - i;
        const __m512i v = _mm512_cvtepu8_epi16(_mm256_maskz_loadu_epi8((__mmask32)((1ULL << rest) - 1), mem + i));
        const __m512i out = _mm512_or_si512(_mm512_srli_epi16(v, 4), _mm512_slli_epi16(_mm512_and_si512(v, low_nibble), 8));
        _mm512_mask_storeu_epi8(gclis + 2 * i, (__mmask64)((1ULL << (2 * rest)) - 1), out);
    }
}
