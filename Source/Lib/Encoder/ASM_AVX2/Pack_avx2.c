/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pack_avx2.h"
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
    const __m128i gtli_shift = _mm_cvtsi32_si128(4 * gtli);

    DECLARE_ALIGNED(32, uint64_t, words[4]);
    DECLARE_ALIGNED(32, uint64_t, lens[4]);

    fast_bit_writer_t local;
    fbw_init(&local, bitstream);
    fast_bit_writer_t* const w = &local;

    uint32_t base = 0;
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

TARGET_GFNI_BMI2 void pack_data_groups_avx2_gfni(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t* gclis,
                                                 uint32_t groups, uint8_t gtli, uint8_t sign_flag) {
    if (!fbw_fits(bitstream, PACK_GROUP_MAX_BYTES(groups))) {
        pack_data_groups_pdep_guarded(bitstream, buf_16bit, gclis, groups, gtli, sign_flag);
        return;
    }
    if (sign_flag == 0) {
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
