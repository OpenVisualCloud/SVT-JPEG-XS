/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <immintrin.h>
#include "RateControl_avx2.h"
#include "rate_control_helper_avx2.h"
#include "encoder_dsp_rtcd.h"
#include "EncDec.h"
#include <string.h>

uint32_t rate_control_calc_vpred_cost_nosigf_avx2(uint32_t gcli_width, uint8_t *gcli_data_top_ptr, uint8_t *gcli_data_ptr,
                                                  uint8_t *vpred_bits_pack, uint8_t gtli, uint8_t gtli_max) {
    uint32_t pack_size_gcli_no_sigf = 0;
    uint32_t leftover = gcli_width % 16;

    __m256i sum_avx2 = _mm256_setzero_si256();
    const __m256i gtli_max_avx2 = _mm256_set1_epi16(gtli_max);
    const __m256i gtli_avx2 = _mm256_set1_epi16(gtli);

    for (uint32_t i = 0; i < gcli_width / 16; ++i) {
        //uint8_t m_top = MAX(gcli_data_top_ptr[i], gtli_max);
        __m256i m_top = _mm256_max_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((__m128i *)gcli_data_top_ptr)), gtli_max_avx2);

        //uint8_t gcli = MAX(gcli_data_ptr[i], gtli);
        __m256i gcli = _mm256_max_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((__m128i *)gcli_data_ptr)), gtli_avx2);

        //int8_t delta_m = gcli - m_top; //Can be negative!!!
        __m256i delta_m = _mm256_sub_epi16(gcli, m_top);

        //uint8_t bits = vlc_encode_get_bits(delta_m, m_top, gtli);
        __m256i bits = vlc_encode_get_bits_avx2(delta_m, m_top, gtli_avx2);

        //vpred_bits_pack[i] = bits;
        _mm_storeu_si128((__m128i *)vpred_bits_pack,
                         _mm_packs_epi16(_mm256_castsi256_si128(bits), _mm256_extracti128_si256(bits, 0x1)));

        //pack_size_gcli_no_sigf += bits;
        sum_avx2 = _mm256_add_epi16(sum_avx2, bits);

        gcli_data_top_ptr += 16;
        gcli_data_ptr += 16;
        vpred_bits_pack += 16;
    }

    __m128i sum_sse = _mm_hadd_epi16(_mm256_castsi256_si128(sum_avx2), _mm256_extracti128_si256(sum_avx2, 0x1)); // 0..7 16bit
    if (leftover) {
        uint32_t leftover_sse = leftover % 8;
        const __m128i gtli_max_sse = _mm_set1_epi16(gtli_max);
        const __m128i gtli_sse = _mm_set1_epi16(gtli);
        if (leftover / 8) {
            //uint8_t m_top = MAX(gcli_data_top_ptr[i], gtli_max);
            __m128i m_top = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_data_top_ptr)), gtli_max_sse);

            //uint8_t gcli = MAX(gcli_data_ptr[i], gtli);
            __m128i gcli = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_data_ptr)), gtli_sse);

            //int8_t delta_m = gcli - m_top; //Can be negative!!!
            __m128i delta_m = _mm_sub_epi16(gcli, m_top);

            //uint8_t bits = vlc_encode_get_bits(delta_m, m_top, gtli);
            __m128i bits = vlc_encode_get_bits_sse(delta_m, m_top, gtli_sse);

            //vpred_bits_pack[i] = bits;
            _mm_storel_epi64((__m128i *)vpred_bits_pack, _mm_packs_epi16(bits, bits));

            //pack_size_gcli_no_sigf += bits;
            sum_sse = _mm_add_epi16(sum_sse, bits);

            gcli_data_top_ptr += 8;
            gcli_data_ptr += 8;
            vpred_bits_pack += 8;
        }

        if (leftover_sse) {
            // Zero-init: only leftover_sse of 8 bytes get memcpy'd below, but the SIMD load
            // right after reads all 8 unconditionally (MSan use-of-uninitialized-value). The
            // padding lanes' computed result is never read back (output copy/sum below are
            // both bounded by leftover_sse), so zero-filling them cannot change behavior.
            DECLARE_ALIGNED(16, uint8_t, gcli_top_tmp[8]) = {0};
            DECLARE_ALIGNED(16, uint8_t, gcli_tmp[8]) = {0};
            DECLARE_ALIGNED(16, uint8_t, vpred_bits_tmp[8]);

            memcpy(gcli_top_tmp, gcli_data_top_ptr, sizeof(uint8_t) * leftover_sse);
            memcpy(gcli_tmp, gcli_data_ptr, sizeof(uint8_t) * leftover_sse);

            __m128i m_top = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_top_tmp)), gtli_max_sse);
            __m128i gcli = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_tmp)), gtli_sse);
            __m128i delta_m = _mm_sub_epi16(gcli, m_top);
            __m128i bits = vlc_encode_get_bits_sse(delta_m, m_top, gtli_sse);

            _mm_storel_epi64((__m128i *)vpred_bits_tmp, _mm_packs_epi16(bits, bits));
            memcpy(vpred_bits_pack, vpred_bits_tmp, sizeof(uint8_t) * leftover_sse);

            for (uint32_t i = 0; i < leftover_sse; ++i) {
                pack_size_gcli_no_sigf += vpred_bits_tmp[i];
            }
        }
    }
    sum_sse = _mm_hadd_epi16(sum_sse, sum_sse);                 // 0..3, 0..3 16bit
    sum_sse = _mm_unpacklo_epi16(sum_sse, _mm_setzero_si128()); //0..3 32bit
    sum_sse = _mm_hadd_epi32(sum_sse, sum_sse);                 // 0..1, 0..1 32bit
    pack_size_gcli_no_sigf += _mm_cvtsi128_si32(sum_sse);
    pack_size_gcli_no_sigf += _mm_extract_epi32(sum_sse, 1);
    return pack_size_gcli_no_sigf;
}

void gc_precinct_stage_scalar_avx2(uint8_t *gcli_data_ptr, uint16_t *coeff_data_ptr_16bit, uint32_t group_size, uint32_t width) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    const __m256i perm = _mm256_setr_epi32(0, 4, 2, 6, 1, 5, 3, 7);
    const __m256i one = _mm256_set1_epi16(1);

    uint32_t full_group_256 = width / 64;
    uint32_t leftover = width % 64;
    for (uint32_t g = 0; g < full_group_256; g++) {
        __m256i merge_or_0 = _mm256_loadu_si256((__m256i *)(coeff_data_ptr_16bit + 0));
        __m256i merge_or_1 = _mm256_loadu_si256((__m256i *)(coeff_data_ptr_16bit + 16));
        __m256i merge_or_2 = _mm256_loadu_si256((__m256i *)(coeff_data_ptr_16bit + 32));
        __m256i merge_or_3 = _mm256_loadu_si256((__m256i *)(coeff_data_ptr_16bit + 48));

        //transpose
        __m256i a0 = _mm256_unpacklo_epi32(merge_or_0, merge_or_1);
        __m256i a1 = _mm256_unpackhi_epi32(merge_or_0, merge_or_1);
        __m256i a2 = _mm256_unpacklo_epi32(merge_or_2, merge_or_3);
        __m256i a3 = _mm256_unpackhi_epi32(merge_or_2, merge_or_3);

        __m256i tmp_0 = _mm256_unpacklo_epi16(a0, a1);
        __m256i tmp_1 = _mm256_unpacklo_epi16(a2, a3);
        __m256i tmp_2 = _mm256_unpackhi_epi16(a0, a1);
        __m256i tmp_3 = _mm256_unpackhi_epi16(a2, a3);

        a0 = _mm256_or_si256(tmp_0, tmp_2);
        a1 = _mm256_or_si256(tmp_1, tmp_3);
        a0 = _mm256_permutevar8x32_epi32(a0, perm);
        a1 = _mm256_permutevar8x32_epi32(a1, perm);
        merge_or_1 = _mm256_permute2x128_si256(a0, a1, 0x20);
        merge_or_2 = _mm256_permute2x128_si256(a0, a1, 0x31);
        __m256i merge_or = _mm256_or_si256(merge_or_1, merge_or_2);

        //merge_or <<= 1; //Remove sign bit
        merge_or = _mm256_slli_epi16(merge_or, 1);
        merge_or = _mm256_or_si256(merge_or, one);

        /* The MSB of each of the sixteen values, without a round trip through memory and sixteen BSRs: converted to
         * float, a value below 2^24 is exact and its biased exponent is MSB + 127. The unpacks and packs stay within
         * the 128-bit lanes, so the packs put the values back in their order. */
        const __m256i zero = _mm256_setzero_si256();
        const __m256 values_lo = _mm256_cvtepi32_ps(_mm256_unpacklo_epi16(merge_or, zero));
        const __m256 values_hi = _mm256_cvtepi32_ps(_mm256_unpackhi_epi16(merge_or, zero));
        const __m256i exp_lo = _mm256_srli_epi32(_mm256_castps_si256(values_lo), 23);
        const __m256i exp_hi = _mm256_srli_epi32(_mm256_castps_si256(values_hi), 23);
        const __m256i exp_16 = _mm256_packus_epi32(exp_lo, exp_hi);
        const __m256i exp_8 = _mm256_permute4x64_epi64(_mm256_packus_epi16(exp_16, exp_16), 0x08);
        _mm_storeu_si128((__m128i *)gcli_data_ptr, _mm_sub_epi8(_mm256_castsi256_si128(exp_8), _mm_set1_epi8(127)));

        gcli_data_ptr += 16;
        coeff_data_ptr_16bit += 64;
    }

    if (leftover) {
        uint32_t line_groups_num = leftover / GROUP_SIZE;
        uint32_t line_groups_leftover = leftover % GROUP_SIZE;

        if (line_groups_num) {
            gc_precinct_stage_scalar_loop(line_groups_num, coeff_data_ptr_16bit, gcli_data_ptr);
        }

        if (line_groups_leftover) {
            coeff_data_ptr_16bit += line_groups_num * GROUP_SIZE;
            gcli_data_ptr += line_groups_num;
            uint16_t merge_or = 0;
            for (uint32_t i = 0; i < line_groups_leftover; i++) {
                merge_or |= coeff_data_ptr_16bit[i];
            }
            merge_or <<= 1; //Remove sign bit
            if (merge_or) {
                gcli_data_ptr[0] = svt_log2_32(merge_or); //MSB
                assert(gcli_data_ptr[0] <= TRUNCATION_MAX);
            }
            else {
                gcli_data_ptr[0] = 0;
            }
        }
    }
}

void convert_packed_to_planar_rgb_8bit_avx2(const void *in_rgb, void *out_comp1, void *out_comp2, void *out_comp3,
                                            uint32_t line_width) {
    const uint8_t *in = in_rgb;
    uint8_t *out_c1 = out_comp1;
    uint8_t *out_c2 = out_comp2;
    uint8_t *out_c3 = out_comp3;

    const uint8_t mask[] = {0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11, 12, 13, 14, 15,
                            0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11, 12, 13, 14, 15};
    __m256i shuffle_mask = _mm256_loadu_si256((__m256i *)mask);
    __m128i shuffle_mask_sse = _mm_loadu_si128((__m128i *)mask);

    uint32_t pix = 0;
    for (; (pix + 32) < line_width; pix += 32) {
        __m128i rgb_t1 = _mm_loadu_si128((__m128i *)(in));
        __m128i rgb_t2 = _mm_loadu_si128((__m128i *)(in + 48));
        __m256i rgb1 = _mm256_inserti128_si256(_mm256_castsi128_si256(rgb_t1), (rgb_t2), 0x1);

        rgb_t1 = _mm_loadu_si128((__m128i *)(in + 16));
        rgb_t2 = _mm_loadu_si128((__m128i *)(in + 64));
        __m256i rgb2 = _mm256_inserti128_si256(_mm256_castsi128_si256(rgb_t1), (rgb_t2), 0x1);

        rgb_t1 = _mm_loadu_si128((__m128i *)(in + 32));
        rgb_t2 = _mm_loadu_si128((__m128i *)(in + 80));
        __m256i rgb3 = _mm256_inserti128_si256(_mm256_castsi128_si256(rgb_t1), (rgb_t2), 0x1);

        __m256i pack_lo = _mm256_shuffle_epi8(rgb1, shuffle_mask);
        rgb1 = _mm256_alignr_epi8(rgb2, rgb1, 12);
        __m256i pack_hi = _mm256_shuffle_epi8(rgb1, shuffle_mask);
        rgb2 = _mm256_alignr_epi8(rgb3, rgb2, 8);
        __m256i pack_lo2 = _mm256_shuffle_epi8(rgb2, shuffle_mask);
        rgb3 = _mm256_srli_si256(rgb3, 4);
        __m256i pack_hi2 = _mm256_shuffle_epi8(rgb3, shuffle_mask);

        __m256i tmp1 = _mm256_unpacklo_epi32(pack_lo, pack_hi);
        __m256i tmp2 = _mm256_unpacklo_epi32(pack_lo2, pack_hi2);
        _mm256_storeu_si256((__m256i *)out_c1, _mm256_unpacklo_epi64(tmp1, tmp2));
        _mm256_storeu_si256((__m256i *)out_c2, _mm256_unpackhi_epi64(tmp1, tmp2));

        tmp1 = _mm256_unpackhi_epi32(pack_lo, pack_hi);
        tmp2 = _mm256_unpackhi_epi32(pack_lo2, pack_hi2);
        _mm256_storeu_si256((__m256i *)out_c3, _mm256_unpacklo_epi64(tmp1, tmp2));

        out_c1 += 32;
        out_c2 += 32;
        out_c3 += 32;
        in += 96;
    }
    for (; (pix + 4) < line_width; pix += 4) {
        __m128i input = _mm_loadu_si128((__m128i *)in);
        input = _mm_shuffle_epi8(input, shuffle_mask_sse);

        (void)(*(int *)(out_c1) = _mm_cvtsi128_si32(input));
        input = _mm_srli_si128(input, 4);
        (void)(*(int *)(out_c2) = _mm_cvtsi128_si32(input));
        input = _mm_srli_si128(input, 4);
        (void)(*(int *)(out_c3) = _mm_cvtsi128_si32(input));

        out_c1 += 4;
        out_c2 += 4;
        out_c3 += 4;
        in += 12;
    }
    for (; pix < line_width; pix++) {
        out_c1[0] = in[0];
        out_c2[0] = in[1];
        out_c3[0] = in[2];
        out_c1++;
        out_c2++;
        out_c3++;
        in += 3;
    }
}

void convert_packed_to_planar_rgb_16bit_avx2(const void *in_rgb, void *out_comp1, void *out_comp2, void *out_comp3,
                                             uint32_t line_width) {
    const uint16_t *in = in_rgb;
    uint16_t *out_c1 = out_comp1;
    uint16_t *out_c2 = out_comp2;
    uint16_t *out_c3 = out_comp3;

    const uint8_t mask[] = {0, 1, 6, 7, 2, 3, 8, 9, 4, 5, 10, 11, 12, 13, 14, 15,
                            0, 1, 6, 7, 2, 3, 8, 9, 4, 5, 10, 11, 12, 13, 14, 15};
    __m256i shuffle_mask = _mm256_loadu_si256((__m256i *)mask);

    uint32_t pix = 0;
    for (; (pix + 16) < line_width; pix += 16) {
        __m128i rgb_t1 = _mm_loadu_si128((__m128i *)(in));
        __m128i rgb_t2 = _mm_loadu_si128((__m128i *)(in + 24));
        __m256i rgb1 = _mm256_inserti128_si256(_mm256_castsi128_si256(rgb_t1), (rgb_t2), 0x1);

        rgb_t1 = _mm_loadu_si128((__m128i *)(in + 8));
        rgb_t2 = _mm_loadu_si128((__m128i *)(in + 32));
        __m256i rgb2 = _mm256_inserti128_si256(_mm256_castsi128_si256(rgb_t1), (rgb_t2), 0x1);

        rgb_t1 = _mm_loadu_si128((__m128i *)(in + 16));
        rgb_t2 = _mm_loadu_si128((__m128i *)(in + 40));
        __m256i rgb3 = _mm256_inserti128_si256(_mm256_castsi128_si256(rgb_t1), (rgb_t2), 0x1);

        __m256i pack_lo = _mm256_shuffle_epi8(rgb1, shuffle_mask);
        rgb1 = _mm256_alignr_epi8(rgb2, rgb1, 12);
        __m256i pack_hi = _mm256_shuffle_epi8(rgb1, shuffle_mask);
        rgb2 = _mm256_alignr_epi8(rgb3, rgb2, 8);
        __m256i pack_lo2 = _mm256_shuffle_epi8(rgb2, shuffle_mask);
        rgb3 = _mm256_srli_si256(rgb3, 4);
        __m256i pack_hi2 = _mm256_shuffle_epi8(rgb3, shuffle_mask);

        __m256i tmp1 = _mm256_unpacklo_epi32(pack_lo, pack_hi);
        __m256i tmp2 = _mm256_unpacklo_epi32(pack_lo2, pack_hi2);
        _mm256_storeu_si256((__m256i *)out_c1, _mm256_unpacklo_epi64(tmp1, tmp2));
        _mm256_storeu_si256((__m256i *)out_c2, _mm256_unpackhi_epi64(tmp1, tmp2));

        tmp1 = _mm256_unpackhi_epi32(pack_lo, pack_hi);
        tmp2 = _mm256_unpackhi_epi32(pack_lo2, pack_hi2);
        _mm256_storeu_si256((__m256i *)out_c3, _mm256_unpacklo_epi64(tmp1, tmp2));

        out_c1 += 16;
        out_c2 += 16;
        out_c3 += 16;
        in += 48;
    }
    for (; pix < line_width; pix++) {
        out_c1[0] = in[0];
        out_c2[0] = in[1];
        out_c3[0] = in[2];
        out_c1++;
        out_c2++;
        out_c3++;
        in += 3;
    }
}

/* Histogram of sixteen bins over bytes from [0, TRUNCATION_MAX].
 *
 * Every bin is counted as the population of a compare mask. This variant was
 * chosen not for peak throughput on long lines but because it has neither a
 * prologue nor an epilogue: sixteen byte accumulators would have to be cleared
 * and folded, which is some fifty extra operations per call. Meanwhile on a
 * 1080p422 frame more than half of the calls arrive with a width below sixteen
 * (bands of the upper decomposition levels are narrow), and there the fixed
 * cost decides everything.
 *
 * The tail is copied into a buffer padded with 0xFF: that value matches no bin,
 * so the extra bytes land nowhere and no separate scalar tail is needed - which
 * is essential for narrow bands, where the whole array is the tail. */
#define GC_HIST_BIN_AVX2(v, K) \
    total[K] += (uint32_t)_mm_popcnt_u32((uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8((v), _mm256_set1_epi8((char)(K)))))

#define GC_HIST_LO_AVX2(v)      \
    do {                        \
        GC_HIST_BIN_AVX2(v, 1); \
        GC_HIST_BIN_AVX2(v, 2); \
        GC_HIST_BIN_AVX2(v, 3); \
        GC_HIST_BIN_AVX2(v, 4); \
        GC_HIST_BIN_AVX2(v, 5); \
        GC_HIST_BIN_AVX2(v, 6); \
        GC_HIST_BIN_AVX2(v, 7); \
    } while (0)

#define GC_HIST_HI_AVX2(v)       \
    do {                         \
        GC_HIST_BIN_AVX2(v, 8);  \
        GC_HIST_BIN_AVX2(v, 9);  \
        GC_HIST_BIN_AVX2(v, 10); \
        GC_HIST_BIN_AVX2(v, 11); \
        GC_HIST_BIN_AVX2(v, 12); \
        GC_HIST_BIN_AVX2(v, 13); \
        GC_HIST_BIN_AVX2(v, 14); \
        GC_HIST_BIN_AVX2(v, 15); \
    } while (0)

void gc_histogram_16_avx2(const uint8_t *data, uint32_t width, uint16_t *hist) {
    if (width <= 8) {
        _mm256_storeu_si256((__m256i *)hist, _mm256_setzero_si256());
        for (uint32_t i = 0; i < width; ++i) {
            hist[data[i]]++;
        }
        return;
    }

    uint32_t total[TRUNCATION_MAX + 1] = {0};
    const __m256i high_bit3 = _mm256_set1_epi8(0x08);
    uint32_t i = 0;

    for (; i + 32 <= width; i += 32) {
        const __m256i v = _mm256_loadu_si256((const __m256i *)(data + i));
        GC_HIST_LO_AVX2(v);
        if (!_mm256_testz_si256(v, high_bit3)) {
            GC_HIST_HI_AVX2(v);
        }
    }
    if (i < width) {
        /* Pad unused bytes with 0x10: outside [0, 15] and bit 3 clear. */
        uint8_t tail[32];
        memset(tail, 0x10, sizeof(tail));
        memcpy(tail, data + i, width - i);
        const __m256i v = _mm256_loadu_si256((const __m256i *)tail);
        GC_HIST_LO_AVX2(v);
        if (!_mm256_testz_si256(v, high_bit3)) {
            GC_HIST_HI_AVX2(v);
        }
    }
    uint32_t nonzero = 0;
    for (uint32_t k = 1; k <= TRUNCATION_MAX; ++k) {
        hist[k] = (uint16_t)total[k];
        nonzero += total[k];
    }
    hist[0] = (uint16_t)(width - nonzero);
}

void gc_precinct_sigflags_max_avx2(uint8_t *significance_data_max_ptr, uint8_t *gcli_data_ptr, uint32_t group_sign_size,
                                   uint32_t gcli_width) {
    UNUSED(group_sign_size);
    assert(group_sign_size == SIGNIFICANCE_GROUP_SIZE);
    const __m256i pack_shuf = _mm256_setr_epi8(0,
                                               8,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               0,
                                               8,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1,
                                               -1);
    uint32_t i = 0;
    for (; i + 32 <= gcli_width; i += 32) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(gcli_data_ptr + i));
        v = _mm256_max_epu8(v, _mm256_srli_epi64(v, 32));
        v = _mm256_max_epu8(v, _mm256_srli_epi64(v, 16));
        v = _mm256_max_epu8(v, _mm256_srli_epi64(v, 8));
        v = _mm256_shuffle_epi8(v, pack_shuf);
        const uint16_t lo16 = (uint16_t)_mm_cvtsi128_si32(_mm256_castsi256_si128(v));
        const uint16_t hi16 = (uint16_t)_mm_cvtsi128_si32(_mm256_extracti128_si256(v, 1));
        const uint32_t out4 = (uint32_t)lo16 | ((uint32_t)hi16 << 16);
        memcpy(significance_data_max_ptr + (i >> 3), &out4, sizeof(out4));
    }
    const uint32_t rem = gcli_width - i;
    if (rem) {
        uint8_t tail[32] = {0};
        memcpy(tail, gcli_data_ptr + i, rem);
        __m256i v = _mm256_loadu_si256((const __m256i *)tail);
        v = _mm256_max_epu8(v, _mm256_srli_epi64(v, 32));
        v = _mm256_max_epu8(v, _mm256_srli_epi64(v, 16));
        v = _mm256_max_epu8(v, _mm256_srli_epi64(v, 8));
        v = _mm256_shuffle_epi8(v, pack_shuf);
        const uint16_t lo16 = (uint16_t)_mm_cvtsi128_si32(_mm256_castsi256_si128(v));
        const uint16_t hi16 = (uint16_t)_mm_cvtsi128_si32(_mm256_extracti128_si256(v, 1));
        const uint32_t out4 = (uint32_t)lo16 | ((uint32_t)hi16 << 16);
        const uint32_t out_n = (rem + SIGNIFICANCE_GROUP_SIZE - 1) >> 3;
        memcpy(significance_data_max_ptr + (i >> 3), &out4, out_n);
    }
}

void forward_rct_line_avx2(int32_t *comp0, int32_t *comp1, int32_t *comp2, uint32_t width) {
    uint32_t x = 0;
    for (; x + 8 <= width; x += 8) {
        const __m256i o0 = _mm256_loadu_si256((const __m256i *)(comp0 + x));
        const __m256i o1 = _mm256_loadu_si256((const __m256i *)(comp1 + x));
        const __m256i o2 = _mm256_loadu_si256((const __m256i *)(comp2 + x));
        const __m256i i1 = _mm256_sub_epi32(o2, o1);
        const __m256i i2 = _mm256_sub_epi32(o0, o1);
        const __m256i i0 = _mm256_add_epi32(o1, _mm256_srai_epi32(_mm256_add_epi32(i1, i2), 2));
        _mm256_storeu_si256((__m256i *)(comp0 + x), i0);
        _mm256_storeu_si256((__m256i *)(comp1 + x), i1);
        _mm256_storeu_si256((__m256i *)(comp2 + x), i2);
    }
    for (; x < width; x++) {
        const int32_t o0 = comp0[x];
        const int32_t o1 = comp1[x];
        const int32_t o2 = comp2[x];
        const int32_t i1 = o2 - o1;
        const int32_t i2 = o0 - o1;
        comp0[x] = o1 + ((i1 + i2) >> 2);
        comp1[x] = i1;
        comp2[x] = i2;
    }
}

void gc_lossless_stats_gtli0_avx2(const uint8_t *data, uint32_t width, uint32_t *out_sum, uint16_t *out_zero) {
    const __m256i zero = _mm256_setzero_si256();
    __m256i sum_v = zero;
    uint32_t zcnt = 0;
    uint32_t i = 0;

    for (; i + 32 <= width; i += 32) {
        const __m256i v = _mm256_loadu_si256((const __m256i *)(data + i));
        sum_v = _mm256_add_epi64(sum_v, _mm256_sad_epu8(v, zero));
        zcnt += (uint32_t)_mm_popcnt_u32((uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, zero)));
    }
    const __m128i s128 = _mm_add_epi64(_mm256_castsi256_si128(sum_v), _mm256_extracti128_si256(sum_v, 1));
    uint32_t sum = (uint32_t)(_mm_cvtsi128_si64(s128) + _mm_extract_epi64(s128, 1));
    for (; i < width; ++i) {
        sum += data[i];
        zcnt += (data[i] == 0);
    }
    *out_sum = sum;
    *out_zero = (uint16_t)zcnt;
}

static INLINE __m256i suffix_sum_8x32_avx2(__m256i v) {
    v = _mm256_add_epi32(v, _mm256_srli_si256(v, 4));
    v = _mm256_add_epi32(v, _mm256_srli_si256(v, 8));
    const __m128i hi_sum = _mm_shuffle_epi32(_mm256_extracti128_si256(v, 1), 0x00);
    return _mm256_add_epi32(v, _mm256_inserti128_si256(_mm256_setzero_si256(), hi_sum, 0));
}

static INLINE __m256i prefix_sum_16x16_avx2(__m256i v) {
    v = _mm256_add_epi16(v, _mm256_slli_si256(v, 2));
    v = _mm256_add_epi16(v, _mm256_slli_si256(v, 4));
    v = _mm256_add_epi16(v, _mm256_slli_si256(v, 8));
    const __m128i s7 = _mm_set1_epi16((short)_mm_extract_epi16(_mm256_castsi256_si128(v), 7));
    return _mm256_add_epi16(v, _mm256_inserti128_si256(_mm256_setzero_si256(), s7, 1));
}

void rc_lut_derive_avx2(uint16_t *gc_hist, uint32_t *no_sign_tbl, uint32_t *sign_tbl, uint16_t *sig_hist) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i h16 = _mm256_loadu_si256((const __m256i *)gc_hist);
    const __m256i h_lo = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(h16));
    const __m256i h_hi = _mm256_cvtepu16_epi32(_mm256_extracti128_si256(h16, 1));

    const __m256i hi_s1 = _mm256_alignr_epi8(_mm256_permute2x128_si256(h_hi, zero, 0x21), h_hi, 4);
    const __m256i lo_s1 = _mm256_alignr_epi8(_mm256_permute2x128_si256(h_lo, h_hi, 0x21), h_lo, 4);

    const __m256i sb_hi = suffix_sum_8x32_avx2(hi_s1);
    const __m256i sb_lo = _mm256_add_epi32(suffix_sum_8x32_avx2(lo_s1),
                                           _mm256_set1_epi32(_mm_cvtsi128_si32(_mm256_castsi256_si128(sb_hi))));

    const __m256i ss_hi = suffix_sum_8x32_avx2(sb_hi);
    const __m256i ss_lo = _mm256_add_epi32(suffix_sum_8x32_avx2(sb_lo),
                                           _mm256_set1_epi32(_mm_cvtsi128_si32(_mm256_castsi256_si128(ss_hi))));

    _mm256_storeu_si256((__m256i *)no_sign_tbl, _mm256_add_epi32(ss_lo, sb_lo));
    _mm256_storeu_si256((__m256i *)(no_sign_tbl + 8), _mm256_add_epi32(ss_hi, sb_hi));
    if (sign_tbl) {
        _mm256_storeu_si256((__m256i *)sign_tbl, ss_lo);
        _mm256_storeu_si256((__m256i *)(sign_tbl + 8), ss_hi);
    }

    _mm256_storeu_si256((__m256i *)gc_hist, prefix_sum_16x16_avx2(h16));
    if (sig_hist) {
        const __m256i sh16 = _mm256_loadu_si256((const __m256i *)sig_hist);
        _mm256_storeu_si256((__m256i *)sig_hist, prefix_sum_16x16_avx2(sh16));
    }
}

void rate_control_calc_vpred_cost_sigf_nosigf_avx2(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                   uint32_t significance_group_size, uint8_t *gcli_data_top_ptr,
                                                   uint8_t *gcli_data_ptr, uint8_t *vpred_bits_pack, uint8_t *vpred_significance,
                                                   uint8_t gtli, uint8_t gtli_max, uint32_t *pack_size_gcli_sigf_reduction,
                                                   uint32_t *pack_size_gcli_no_sigf) {
    UNUSED(significance_width);
    UNUSED(significance_group_size);
    assert(significance_group_size == SIGNIFICANCE_GROUP_SIZE);

    *pack_size_gcli_sigf_reduction = 0;
    *pack_size_gcli_no_sigf = 0;

    __m128i bits_sum_sigf_sse = _mm_setzero_si128();
    __m128i bits_sum_nosigf_sse = _mm_setzero_si128();
    uint32_t leftover = gcli_width % 16;
    uint32_t non_zero_significance = 0;

    if (gcli_width / 16) {
        const __m256i gtli_avx2 = _mm256_set1_epi16(gtli);
        const __m256i gtli_max_avx2 = _mm256_set1_epi16(gtli_max);
        __m256i sum_nosigf_avx2 = _mm256_setzero_si256();
        for (uint32_t i = 0; i < gcli_width / 16; ++i) {
            const __m256i gcli_avx2 = _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)gcli_data_ptr));
            const __m256i gcli_max_avx2 = _mm256_max_epi16(gcli_avx2, gtli_avx2);
            const __m256i m_top_avx2 = _mm256_max_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i *)gcli_data_top_ptr)),
                                                        gtli_max_avx2);
            const __m256i delta_m_avx2 = _mm256_sub_epi16(gcli_max_avx2, m_top_avx2);

            const __m256i active = hdr_Rm
                ? _mm256_cmpgt_epi16(gcli_avx2, gtli_avx2)
                : _mm256_andnot_si256(_mm256_cmpeq_epi16(m_top_avx2, gcli_max_avx2), _mm256_set1_epi16(-1));
            const uint32_t active_mask = (uint32_t)_mm256_movemask_epi8(active);
            const uint8_t sig0 = ((active_mask & 0xFFFFu) == 0);
            const uint8_t sig1 = ((active_mask >> 16) == 0);
            vpred_significance[0] = sig0;
            vpred_significance[1] = sig1;
            non_zero_significance += sig0 + sig1;

            const __m256i bits = vlc_encode_get_bits_avx2(delta_m_avx2, m_top_avx2, gtli_avx2);
            const __m128i bits_lo = _mm256_castsi256_si128(bits);
            const __m128i bits_hi = _mm256_extracti128_si256(bits, 1);
            _mm_storeu_si128((__m128i *)vpred_bits_pack, _mm_packs_epi16(bits_lo, bits_hi));
            sum_nosigf_avx2 = _mm256_add_epi16(sum_nosigf_avx2, bits);
            if (sig0) {
                bits_sum_sigf_sse = _mm_add_epi16(bits_sum_sigf_sse, bits_lo);
            }
            if (sig1) {
                bits_sum_sigf_sse = _mm_add_epi16(bits_sum_sigf_sse, bits_hi);
            }

            gcli_data_top_ptr += 16;
            gcli_data_ptr += 16;
            vpred_bits_pack += 16;
            vpred_significance += 2;
        }
        bits_sum_nosigf_sse = _mm_add_epi16(_mm256_castsi256_si128(sum_nosigf_avx2),
                                            _mm256_extracti128_si256(sum_nosigf_avx2, 1));
    }

    if (leftover) {
        const __m128i gtli_sse = _mm_set1_epi16(gtli);
        const __m128i gtli_max_sse = _mm_set1_epi16(gtli_max);
        if (leftover >= 8) {
            const __m128i gcli_sse = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i *)gcli_data_ptr));
            const __m128i gcli_max_sse = _mm_max_epi16(gcli_sse, gtli_sse);
            const __m128i m_top_sse = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i *)gcli_data_top_ptr)),
                                                    gtli_max_sse);
            const __m128i delta_m = _mm_sub_epi16(gcli_max_sse, m_top_sse);
            const __m128i active = hdr_Rm ? _mm_cmpgt_epi16(gcli_sse, gtli_sse)
                                          : _mm_andnot_si128(_mm_cmpeq_epi16(m_top_sse, gcli_max_sse), _mm_set1_epi16(-1));
            const uint8_t sig0 = (_mm_movemask_epi8(active) == 0);
            vpred_significance[0] = sig0;
            non_zero_significance += sig0;

            const __m128i bits = vlc_encode_get_bits_sse(delta_m, m_top_sse, gtli_sse);
            _mm_storel_epi64((__m128i *)vpred_bits_pack, _mm_packs_epi16(bits, bits));
            bits_sum_nosigf_sse = _mm_add_epi16(bits_sum_nosigf_sse, bits);
            if (sig0) {
                bits_sum_sigf_sse = _mm_add_epi16(bits_sum_sigf_sse, bits);
            }

            gcli_data_top_ptr += 8;
            gcli_data_ptr += 8;
            vpred_bits_pack += 8;
            vpred_significance++;
            leftover -= 8;
        }
        if (leftover) {
            DECLARE_ALIGNED(16, uint8_t, gcli_top_tmp[8]) = {0};
            DECLARE_ALIGNED(16, uint8_t, gcli_tmp[8]) = {0};
            for (uint32_t i = 0; i < leftover; ++i) {
                gcli_top_tmp[i] = gcli_data_top_ptr[i];
                gcli_tmp[i] = gcli_data_ptr[i];
            }
            const __m128i gcli_sse = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i *)gcli_tmp));
            const __m128i gcli_max_sse = _mm_max_epi16(gcli_sse, gtli_sse);
            const __m128i m_top_sse = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i *)gcli_top_tmp)),
                                                    gtli_max_sse);
            const __m128i delta_m = _mm_sub_epi16(gcli_max_sse, m_top_sse);
            const __m128i active = hdr_Rm ? _mm_cmpgt_epi16(gcli_sse, gtli_sse)
                                          : _mm_andnot_si128(_mm_cmpeq_epi16(m_top_sse, gcli_max_sse), _mm_set1_epi16(-1));
            const uint32_t active_mask = (uint32_t)_mm_movemask_epi8(active) & ((1u << (2 * leftover)) - 1u);
            vpred_significance[0] = (active_mask == 0);

            __m128i bits = vlc_encode_get_bits_sse(delta_m, m_top_sse, gtli_sse);
            uint16_t sum = 0;
            for (uint32_t i = 0; i < leftover; ++i) {
                vpred_bits_pack[i] = (uint8_t)_mm_cvtsi128_si32(bits);
                sum += vpred_bits_pack[i];
                bits = _mm_srli_si128(bits, 2);
            }
            *pack_size_gcli_no_sigf += sum;
            if (vpred_significance[0]) {
                *pack_size_gcli_sigf_reduction += sum + leftover;
            }
        }
    }

    *pack_size_gcli_sigf_reduction += non_zero_significance * SIGNIFICANCE_GROUP_SIZE;
    bits_sum_sigf_sse = _mm_hadd_epi16(bits_sum_sigf_sse, bits_sum_sigf_sse);
    bits_sum_sigf_sse = _mm_unpacklo_epi16(bits_sum_sigf_sse, _mm_setzero_si128());
    bits_sum_sigf_sse = _mm_hadd_epi32(bits_sum_sigf_sse, bits_sum_sigf_sse);
    *pack_size_gcli_sigf_reduction += _mm_cvtsi128_si32(bits_sum_sigf_sse);
    *pack_size_gcli_sigf_reduction += _mm_extract_epi32(bits_sum_sigf_sse, 1);

    bits_sum_nosigf_sse = _mm_hadd_epi16(bits_sum_nosigf_sse, bits_sum_nosigf_sse);
    bits_sum_nosigf_sse = _mm_unpacklo_epi16(bits_sum_nosigf_sse, _mm_setzero_si128());
    bits_sum_nosigf_sse = _mm_hadd_epi32(bits_sum_nosigf_sse, bits_sum_nosigf_sse);
    *pack_size_gcli_no_sigf += _mm_cvtsi128_si32(bits_sum_nosigf_sse);
    *pack_size_gcli_no_sigf += _mm_extract_epi32(bits_sum_nosigf_sse, 1);
}
