/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pack_avx2.h"
#include "PackPrecinct.h"
#include "pack_group_helper.h"

void pack_data_single_group_avx2(bitstream_writer_t* bitstream, uint16_t* buf, uint8_t gcli, uint8_t gtli) {
    pack_data_single_group_sse(bitstream, buf, gcli, gtli);
}

void pack_data_groups_avx2(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups, uint8_t gtli,
                           uint8_t sign_flag) {
    pack_data_groups_sse(bitstream, buf_16bit, gclis, groups, gtli, sign_flag);
}

void pack_data_groups_avx2_bmi2(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis, uint32_t groups,
                                uint8_t gtli, uint8_t sign_flag) {
    if (fbw_fits(bitstream, PACK_GROUP_MAX_BYTES(groups))) {
        if (sign_flag == 0) {
            pack_groups_masked_pdep_fast(bitstream, buf_16bit, gclis, groups, gtli, 1);
        }
        else {
            pack_groups_masked_pdep_fast(bitstream, buf_16bit, gclis, groups, gtli, 0);
        }
        return;
    }
    pack_data_groups_pdep_guarded(bitstream, buf_16bit, gclis, groups, gtli, sign_flag);
}

/* pack_data_groups_avx512_gfni() (see Pack_avx512.c) on 256-bit vectors: four groups per
 * vector, for hosts with GFNI but without AVX-512. Without masked loads the last groups of a
 * line, fewer than four, go through PDEP. */
TARGET_GFNI_BMI2 static INLINE void pack_groups_gfni_avx2(bitstream_writer_t* bitstream, const uint16_t* buf_16bit,
                                                          const uint8_t* gclis, uint32_t groups, uint8_t gtli,
                                                          const int emit_signs) {
    const __m256i byte_order = _mm256_broadcastsi128_si256(_mm_setr_epi8(0, 2, 4, 6, 1, 3, 5, 7, 8, 10, 12, 14, 9, 11, 13, 15));
    const __m256i unit = _mm256_set1_epi64x((long long)0x8040201008040201ULL);
    const __m256i even_planes = _mm256_broadcastsi128_si256(_mm_setr_epi8(0, 2, 4, 6, 0, 2, 4, 6, 8, 10, 12, 14, 8, 10, 12, 14));
    const __m256i odd_planes = _mm256_broadcastsi128_si256(_mm_setr_epi8(1, 3, 5, 7, 1, 3, 5, 7, 9, 11, 13, 15, 9, 11, 13, 15));
    const __m256i low_nibbles = _mm256_set1_epi8(0x0F);
    const __m256i one = _mm256_set1_epi64x(1);
    const __m128i gtli_vec = _mm_set1_epi8((char)gtli);
    const __m256i gtli_vec32 = _mm256_set1_epi8((char)gtli);
    const __m256i zero32 = _mm256_setzero_si256();
    const __m128i gtli_shift = _mm_cvtsi32_si128(4 * gtli);

    DECLARE_ALIGNED(32, uint64_t, words[4]);
    DECLARE_ALIGNED(32, uint64_t, lens[4]);

    fast_bit_writer_t local;
    fbw_init(&local, bitstream);
    fast_bit_writer_t* const w = &local;

    uint32_t base = 0;
    for (; base + 32 <= groups; base += 32) {
        const __m256i g32 = _mm256_loadu_si256((const __m256i*)(gclis + base));
        const __m256i p32 = _mm256_subs_epu8(g32, gtli_vec32);
        uint32_t active32 = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(p32, zero32));
        while (active32) {
            const uint32_t k4 = svt_first_set_bit(active32) >> 2;
            uint32_t todo = (active32 >> (k4 * 4)) & 0xFu;
            active32 &= ~(0xFu << (k4 * 4));
            const uint32_t gbase = base + k4 * 4;

            uint32_t g4;
            memcpy(&g4, gclis + gbase, sizeof(g4));
            const __m128i planes8 = _mm_subs_epu8(_mm_cvtsi32_si128((int)g4), gtli_vec);
            const __m256i coeffs = _mm256_loadu_si256((const __m256i*)(buf_16bit + (size_t)gbase * GROUP_SIZE));
            const __m256i t = _mm256_gf2p8affine_epi64_epi8(unit, _mm256_shuffle_epi8(coeffs, byte_order), 0);
            const __m256i t_swapped = _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(t, 4), low_nibbles),
                                                      _mm256_andnot_si256(low_nibbles, _mm256_slli_epi16(t, 4)));
            const __m256i even = _mm256_blend_epi32(
                _mm256_shuffle_epi8(t, even_planes), _mm256_shuffle_epi8(t_swapped, even_planes), 0x55);
            const __m256i odd = _mm256_blend_epi32(
                _mm256_shuffle_epi8(t_swapped, odd_planes), _mm256_shuffle_epi8(t, odd_planes), 0x55);
            const __m256i all = _mm256_or_si256(_mm256_and_si256(even, low_nibbles), _mm256_andnot_si256(low_nibbles, odd));

            __m256i len = _mm256_slli_epi64(_mm256_cvtepu8_epi64(planes8), 2);
            __m256i word = _mm256_and_si256(_mm256_srl_epi64(all, gtli_shift),
                                            _mm256_sub_epi64(_mm256_sllv_epi64(one, len), one));
            if (emit_signs) {
                word = _mm256_or_si256(word, _mm256_sllv_epi64(_mm256_srli_epi64(all, 60), len));
                len = _mm256_add_epi64(len, _mm256_set1_epi64x(4));
            }
            _mm256_store_si256((__m256i*)words, word);
            _mm256_store_si256((__m256i*)lens, len);

            while (todo) {
                const uint32_t j = svt_first_set_bit(todo);
                todo &= todo - 1;
                fbw_put_group(w, words[j], (uint32_t)lens[j]);
            }
        }
    }
    for (; base + 4 <= groups; base += 4) {
        uint32_t g4;
        memcpy(&g4, gclis + base, sizeof(g4));
        const __m128i planes8 = _mm_subs_epu8(_mm_cvtsi32_si128((int)g4), gtli_vec);
        uint32_t todo = ~(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(planes8, _mm_setzero_si128())) & 0xF;
        if (!todo) {
            continue;
        }
        const __m256i coeffs = _mm256_loadu_si256((const __m256i*)(buf_16bit + (size_t)base * GROUP_SIZE));
        const __m256i t = _mm256_gf2p8affine_epi64_epi8(unit, _mm256_shuffle_epi8(coeffs, byte_order), 0);
        const __m256i t_swapped = _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(t, 4), low_nibbles),
                                                  _mm256_andnot_si256(low_nibbles, _mm256_slli_epi16(t, 4)));
        /* the first four bytes of each group are its low dword: immediate 0x55 */
        const __m256i even = _mm256_blend_epi32(
            _mm256_shuffle_epi8(t, even_planes), _mm256_shuffle_epi8(t_swapped, even_planes), 0x55);
        const __m256i odd = _mm256_blend_epi32(
            _mm256_shuffle_epi8(t_swapped, odd_planes), _mm256_shuffle_epi8(t, odd_planes), 0x55);
        const __m256i all = _mm256_or_si256(_mm256_and_si256(even, low_nibbles), _mm256_andnot_si256(low_nibbles, odd));

        __m256i len = _mm256_slli_epi64(_mm256_cvtepu8_epi64(planes8), 2);
        __m256i word = _mm256_and_si256(_mm256_srl_epi64(all, gtli_shift), _mm256_sub_epi64(_mm256_sllv_epi64(one, len), one));
        if (emit_signs) {
            word = _mm256_or_si256(word, _mm256_sllv_epi64(_mm256_srli_epi64(all, 60), len));
            len = _mm256_add_epi64(len, _mm256_set1_epi64x(4));
        }
        _mm256_store_si256((__m256i*)words, word);
        _mm256_store_si256((__m256i*)lens, len);

        while (todo) {
            const uint32_t j = svt_first_set_bit(todo);
            todo &= todo - 1;
            fbw_put_group(w, words[j], (uint32_t)lens[j]);
        }
    }
    for (; base < groups; base++) {
        if (gclis[base] <= gtli) {
            continue;
        }
        uint32_t count;
        const uint64_t word = pack_group_word_pdep(buf_16bit + (size_t)base * GROUP_SIZE, gclis[base], gtli, emit_signs, &count);
        fbw_put_group(w, word, 4 * count);
    }
    fbw_finish(w, bitstream);
}

static INLINE size_t pack_groups_exact_bytes_avx2(const uint8_t* gclis, uint32_t groups, uint8_t gtli, const int emit_signs) {
    const __m256i gtli_vec = _mm256_set1_epi8((char)gtli);
    const __m256i zero = _mm256_setzero_si256();
    __m256i sum_planes = zero;
    uint64_t active_groups = 0;
    uint32_t i = 0;

    for (; i + 32 <= groups; i += 32) {
        const __m256i diff = _mm256_subs_epu8(_mm256_loadu_si256((const __m256i*)(gclis + i)), gtli_vec);
        sum_planes = _mm256_add_epi64(sum_planes, _mm256_sad_epu8(diff, zero));
        if (emit_signs) {
            const uint32_t nz = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(diff, zero));
            active_groups += (uint32_t)_mm_popcnt_u32(nz);
        }
    }
    const __m128i s128 = _mm_add_epi64(_mm256_castsi256_si128(sum_planes), _mm256_extracti128_si256(sum_planes, 1));
    uint64_t total_nibbles = (uint64_t)_mm_cvtsi128_si64(s128) + (uint64_t)_mm_extract_epi64(s128, 1) + active_groups;
    for (; i < groups; ++i) {
        if (gclis[i] > gtli) {
            total_nibbles += (uint64_t)(gclis[i] - gtli) + (emit_signs ? 1u : 0u);
        }
    }
    return (size_t)((total_nibbles + 1u) >> 1);
}

TARGET_GFNI_BMI2 void pack_data_groups_avx2_gfni(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis,
                                                 uint32_t groups, uint8_t gtli, uint8_t sign_flag) {
    const int emit_signs = (sign_flag == 0);
    if (!fbw_fits(bitstream, PACK_GROUP_MAX_BYTES(groups))) {
        if (!fbw_fits(bitstream, pack_groups_exact_bytes_avx2(gclis, groups, gtli, emit_signs))) {
            pack_data_groups_pdep_guarded(bitstream, buf_16bit, gclis, groups, gtli, sign_flag);
            return;
        }
    }
    if (emit_signs) {
        pack_groups_gfni_avx2(bitstream, buf_16bit, gclis, groups, gtli, 1);
    }
    else {
        pack_groups_gfni_avx2(bitstream, buf_16bit, gclis, groups, gtli, 0);
    }
}

/* See pack_raw_gclis_bytes_c(). PMADDUBSW multiplies the first GCLI of every pair by 16 and adds the
 * second, giving the byte in a 16-bit lane; PACKUSWB narrows the lanes back to bytes, within each
 * 128-bit half, so a cross-lane permute restores the order. */
void pack_raw_gclis_bytes_avx2(const uint8_t* gclis, uint8_t* mem, uint32_t bytes) {
    const __m256i weights = _mm256_set1_epi16(0x0110);
    uint32_t i = 0;
    for (; i + 32 <= bytes; i += 32) {
        const __m256i lo = _mm256_maddubs_epi16(_mm256_loadu_si256((const __m256i*)(gclis + 2 * i)), weights);
        const __m256i hi = _mm256_maddubs_epi16(_mm256_loadu_si256((const __m256i*)(gclis + 2 * i + 32)), weights);
        _mm256_storeu_si256((__m256i*)(mem + i), _mm256_permute4x64_epi64(_mm256_packus_epi16(lo, hi), _MM_SHUFFLE(3, 1, 2, 0)));
    }
    for (; i < bytes; i++) {
        mem[i] = (uint8_t)((gclis[2 * i] << 4) | gclis[2 * i + 1]);
    }
}

void pack_significance_avx2(bitstream_writer_t* bitstream, uint8_t gtli, uint8_t* significance_data_max_ptr, uint32_t width) {
    if (!fbw_fits(bitstream, ((size_t)width + 7u) >> 3)) {
        pack_significance_c(bitstream, gtli, significance_data_max_ptr, width);
        return;
    }
    fast_bit_writer_t fw;
    fbw_init(&fw, bitstream);
    const __m256i gtli_vec = _mm256_set1_epi8((char)gtli);
    const __m256i zero = _mm256_setzero_si256();
    const __m128i nib_rev = _mm_setr_epi8(0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15);
    const __m128i lo_mask = _mm_set1_epi8(0x0F);
    uint32_t i = 0;
    for (; i + 32 <= width; i += 32) {
        const __m256i v = _mm256_loadu_si256((const __m256i*)(significance_data_max_ptr + i));
        const uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_subs_epu8(v, gtli_vec), zero));
        const __m128i xm = _mm_cvtsi32_si128((int)m);
        const __m128i rev_bytes = _mm_or_si128(_mm_slli_epi16(_mm_shuffle_epi8(nib_rev, _mm_and_si128(xm, lo_mask)), 4),
                                               _mm_shuffle_epi8(nib_rev, _mm_and_si128(_mm_srli_epi16(xm, 4), lo_mask)));
        const uint32_t rev32 = (uint32_t)(SVT_BSWAP64((uint64_t)(uint32_t)_mm_cvtsi128_si32(rev_bytes)) >> 32);
        fbw_put(&fw, (uint64_t)rev32, 32);
    }
    if (i < width) {
        const uint32_t n = width - i;
        uint8_t tail[32] = {0};
        memcpy(tail, significance_data_max_ptr + i, n);
        const __m256i v = _mm256_loadu_si256((const __m256i*)tail);
        const uint32_t valid = (1u << n) - 1u;
        const uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_subs_epu8(v, gtli_vec), zero)) & valid;
        const __m128i xm = _mm_cvtsi32_si128((int)m);
        const __m128i rev_bytes = _mm_or_si128(_mm_slli_epi16(_mm_shuffle_epi8(nib_rev, _mm_and_si128(xm, lo_mask)), 4),
                                               _mm_shuffle_epi8(nib_rev, _mm_and_si128(_mm_srli_epi16(xm, 4), lo_mask)));
        const uint32_t rev32 = (uint32_t)(SVT_BSWAP64((uint64_t)(uint32_t)_mm_cvtsi128_si32(rev_bytes)) >> 32);
        fbw_put(&fw, (uint64_t)(rev32 >> (32 - n)), n);
    }
    fbw_finish(&fw, bitstream);
}

static INLINE void vlc_put_unary_fast_avx2(fast_bit_writer_t* fw, uint8_t val) {
    const uint32_t len = (uint32_t)val + 1u;
    const uint64_t code = (((uint64_t)1 << val) - 1u) << 1;
    fbw_put(fw, code, len);
}

static INLINE void vlc_put_unary_group8_avx2(fast_bit_writer_t* fw, const uint8_t* bp, __m128i gtli_vec, __m128i one8,
                                             __m256i one64, int8_t gtli) {
    const __m128i v = _mm_subs_epu8(_mm_loadl_epi64((const __m128i*)bp), gtli_vec);
    __m128i p = _mm_add_epi8(v, one8);
    p = _mm_add_epi8(p, _mm_slli_si128(p, 1));
    p = _mm_add_epi8(p, _mm_slli_si128(p, 2));
    p = _mm_add_epi8(p, _mm_slli_si128(p, 4));
    const uint32_t total = (uint32_t)_mm_extract_epi8(p, 7);
    if (total == 8) {
        fbw_put(fw, 0, 8);
    }
    else if (total <= 56) {
        const __m128i shifts = _mm_sub_epi8(_mm_set1_epi8((char)total), p);
        const __m256i z_lo = _mm256_sllv_epi64(one64, _mm256_cvtepu8_epi64(shifts));
        const __m256i z_hi = _mm256_sllv_epi64(one64, _mm256_cvtepu8_epi64(_mm_srli_si128(shifts, 4)));
        const __m256i z_or = _mm256_or_si256(z_lo, z_hi);
        const __m128i z128 = _mm_or_si128(_mm256_castsi256_si128(z_or), _mm256_extracti128_si256(z_or, 1));
        const uint64_t zeros = (uint64_t)_mm_cvtsi128_si64(z128) | (uint64_t)_mm_extract_epi64(z128, 1);
        const uint64_t acc = (((uint64_t)1 << total) - 1) ^ zeros;
        fbw_put(fw, acc, total);
    }
    else {
        for (uint32_t i = 0; i < 8; ++i) {
            const int32_t d = (int32_t)bp[i] - gtli;
            vlc_put_unary_fast_avx2(fw, (uint8_t)(d & ~(d >> 31)));
        }
    }
}

static INLINE size_t pack_bitplane_count_exact_bytes_nosig_avx2(const uint8_t* bitplane, uint32_t width, uint8_t gtli) {
    const __m256i gtli_vec = _mm256_set1_epi8((char)gtli);
    const __m256i zero = _mm256_setzero_si256();
    __m256i sum = zero;
    uint32_t i = 0;

    for (; i + 32 <= width; i += 32) {
        const __m256i v = _mm256_subs_epu8(_mm256_loadu_si256((const __m256i*)(bitplane + i)), gtli_vec);
        sum = _mm256_add_epi64(sum, _mm256_sad_epu8(v, zero));
    }
    const __m128i s128 = _mm_add_epi64(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1));
    uint64_t total_bits = (uint64_t)_mm_cvtsi128_si64(s128) + (uint64_t)_mm_extract_epi64(s128, 1) + (uint64_t)width;
    for (; i < width; ++i) {
        if (bitplane[i] > gtli) {
            total_bits += (uint64_t)(bitplane[i] - gtli);
        }
    }
    return (size_t)((total_bits + 7u) >> 3);
}

void pack_bitplane_count_significance_avx2(bitstream_writer_t* bitstream, uint8_t* bitplane, uint32_t width, int8_t gtli,
                                           uint8_t* significance_data_max_ptr, int32_t group_size) {
    if (gtli < 0) {
        pack_bitplane_count_significance_c(bitstream, bitplane, width, gtli, significance_data_max_ptr, group_size);
        return;
    }
    const uint32_t max_bits_per_elem = (gtli <= TRUNCATION_MAX) ? (uint32_t)(TRUNCATION_MAX + 1 - gtli)
                                                                : (uint32_t)(TRUNCATION_MAX + 1);
    const size_t max_bytes = ((size_t)width * max_bits_per_elem + 7u) >> 3;
    if (!fbw_fits(bitstream, max_bytes)) {
        if (!fbw_fits(bitstream, pack_bitplane_count_exact_bytes_nosig_avx2(bitplane, width, (uint8_t)gtli))) {
            pack_bitplane_count_significance_c(bitstream, bitplane, width, gtli, significance_data_max_ptr, group_size);
            return;
        }
    }
    const uint32_t groups = width / SIGNIFICANCE_GROUP_SIZE;
    const uint32_t leftover = width % SIGNIFICANCE_GROUP_SIZE;
    fast_bit_writer_t fw;
    fbw_init(&fw, bitstream);
    const __m256i gtli256 = _mm256_set1_epi8((char)gtli);
    const __m256i zero256 = _mm256_setzero_si256();
    const __m128i gtli128 = _mm256_castsi256_si128(gtli256);
    const __m128i one8 = _mm_set1_epi8(1);
    const __m256i one64 = _mm256_set1_epi64x(1);

    uint32_t base = 0;
    for (; base + 32 <= groups; base += 32) {
        const __m256i sig = _mm256_loadu_si256((const __m256i*)(significance_data_max_ptr + base));
        uint32_t active = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_subs_epu8(sig, gtli256), zero256));
        while (active) {
            const uint32_t j = svt_first_set_bit(active);
            active &= active - 1;
            vlc_put_unary_group8_avx2(&fw, bitplane + (size_t)(base + j) * SIGNIFICANCE_GROUP_SIZE, gtli128, one8, one64, gtli);
        }
    }
    for (; base < groups; ++base) {
        if (significance_data_max_ptr[base] > gtli) {
            vlc_put_unary_group8_avx2(&fw, bitplane + (size_t)base * SIGNIFICANCE_GROUP_SIZE, gtli128, one8, one64, gtli);
        }
    }
    if (leftover && significance_data_max_ptr[groups] > gtli) {
        const uint8_t* tail_bp = bitplane + (size_t)groups * SIGNIFICANCE_GROUP_SIZE;
        for (uint32_t i = 0; i < leftover; i++) {
            const int32_t d = (int32_t)tail_bp[i] - gtli;
            vlc_put_unary_fast_avx2(&fw, (uint8_t)(d & ~(d >> 31)));
        }
    }
    fbw_finish(&fw, bitstream);
}

void pack_bitplane_count_no_significance_avx2(bitstream_writer_t* bitstream, uint8_t* bitplane, uint32_t width, int8_t gtli) {
    if (gtli < 0) {
        pack_bitplane_count_no_significance_c(bitstream, bitplane, width, gtli);
        return;
    }
    const uint32_t max_bits_per_elem = (gtli <= TRUNCATION_MAX) ? (uint32_t)(TRUNCATION_MAX + 1 - gtli)
                                                                : (uint32_t)(TRUNCATION_MAX + 1);
    const size_t max_bytes = ((size_t)width * max_bits_per_elem + 7u) >> 3;
    if (!fbw_fits(bitstream, max_bytes)) {
        if (!fbw_fits(bitstream, pack_bitplane_count_exact_bytes_nosig_avx2(bitplane, width, (uint8_t)gtli))) {
            pack_bitplane_count_no_significance_c(bitstream, bitplane, width, gtli);
            return;
        }
    }
    fast_bit_writer_t fw;
    fbw_init(&fw, bitstream);
    const __m256i gtli256 = _mm256_set1_epi8((char)gtli);
    const __m128i gtli128 = _mm256_castsi256_si128(gtli256);
    const __m128i one8 = _mm_set1_epi8(1);
    const __m256i one64 = _mm256_set1_epi64x(1);
    uint32_t i = 0;
    for (; i + 32 <= width; i += 32) {
        const __m256i sub = _mm256_subs_epu8(_mm256_loadu_si256((const __m256i*)(bitplane + i)), gtli256);
        if (_mm256_testz_si256(sub, sub)) {
            fbw_put(&fw, 0, 32);
        }
        else {
            vlc_put_unary_group8_avx2(&fw, bitplane + i, gtli128, one8, one64, gtli);
            vlc_put_unary_group8_avx2(&fw, bitplane + i + 8, gtli128, one8, one64, gtli);
            vlc_put_unary_group8_avx2(&fw, bitplane + i + 16, gtli128, one8, one64, gtli);
            vlc_put_unary_group8_avx2(&fw, bitplane + i + 24, gtli128, one8, one64, gtli);
        }
    }
    for (; i + 8 <= width; i += 8) {
        vlc_put_unary_group8_avx2(&fw, bitplane + i, gtli128, one8, one64, gtli);
    }
    for (; i < width; i++) {
        const int32_t d = (int32_t)bitplane[i] - gtli;
        vlc_put_unary_fast_avx2(&fw, (uint8_t)(d & ~(d >> 31)));
    }
    fbw_finish(&fw, bitstream);
}
