/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <immintrin.h>
#include "RateControl_avx2.h"
#include "rate_control_helper_avx2.h"
#include "encoder_dsp_rtcd.h"
#include "EncDec.h"

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
            DECLARE_ALIGNED(16, uint8_t, gcli_top_tmp[8]);
            DECLARE_ALIGNED(16, uint8_t, gcli_tmp[8]);
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
    DECLARE_ALIGNED(16, uint16_t, data[16]);
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
        _mm256_storeu_si256((__m256i *)data, merge_or);

        msb_x16_ASM(data, gcli_data_ptr);

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
