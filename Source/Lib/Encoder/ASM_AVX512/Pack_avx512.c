/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Pack_avx512.h"
#include "RateControl_avx2.h"
#include "rate_control_helper_avx2.h"
#include <immintrin.h>
#include "SvtUtility.h"

void pack_data_single_group_avx512(bitstream_writer_t *bitstream, uint16_t *buf, uint8_t gcli, uint8_t gtli) {
    const __m128i mask = _mm_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const __m128i shift = _mm_setr_epi16(12, 13, 14, 15, 0, 0, 0, 0);

    __m128i tmp_sse = _mm_loadl_epi64((__m128i *)buf);
    tmp_sse = _mm_slli_epi16(tmp_sse, 16 - gcli);

    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        __m128i val_sse = _mm_and_si128(tmp_sse, mask);
        tmp_sse = _mm_slli_epi16(tmp_sse, 1);
        val_sse = _mm_srlv_epi16(val_sse, shift);
        val_sse = _mm_hadd_epi16(val_sse, val_sse);
        val_sse = _mm_hadd_epi16(val_sse, val_sse);
        uint8_t val = _mm_cvtsi128_si32(val_sse);

        write_4_bits_align4(bitstream, (uint8_t)val);
    }
}

static INLINE __m512i vlc_encode_get_bits_avx512(__m512i x, __m512i r, __m512i t) {
    //    assert(x < 32);
    //    int32_t max = r - t;
    //    if (max < 0) {
    //        max = 0;
    //    }
    __m512i max_avx512 = _mm512_subs_epu16(r, t);
    //    int n = 0;
    //    if (x > max) {
    const __mmask32 cmp = _mm512_cmpgt_epi16_mask(x, max_avx512);
    //        n = x + max;
    __m512i n1 = _mm512_add_epi16(x, max_avx512);
    //    }
    //    else {
    //        x = x * 2;
    x = _mm512_slli_epi16(x, 1);
    //        if (x < 0) {
    const __mmask32 cmp2 = _mm512_cmpgt_epi16_mask(_mm512_setzero_si512(), x);
    //            n = -x - 1;
    __m512i n2 = _mm512_sub_epi16(_mm512_abs_epi16(x), _mm512_set1_epi16(1));
    //        }
    //        else {
    //            n = x;
    //        }
    __m512i n3 = _mm512_mask_blend_epi16(cmp2, x, n2);
    //    }
    __m512i n = _mm512_mask_blend_epi16(cmp, n3, n1);
    return n;
}

uint32_t rate_control_calc_vpred_cost_nosigf_avx512(uint32_t gcli_width, uint8_t *gcli_data_top_ptr, uint8_t *gcli_data_ptr,
                                                    uint8_t *vpred_bits_pack, uint8_t gtli, uint8_t gtli_max) {
    uint32_t pack_size_gcli_no_sigf = 0;
    uint32_t leftover = gcli_width % 32;

    if (gcli_width / 32) {
        __m512i sum_avx512 = _mm512_setzero_si512();
        const __m512i gtli_max_avx512 = _mm512_set1_epi16(gtli_max);
        const __m512i gtli_avx512 = _mm512_set1_epi16(gtli);
        for (uint32_t i = 0; i < gcli_width / 32; ++i) {
            //uint8_t m_top = MAX(gcli_data_top_ptr[i], gtli_max);
            __m512i m_top = _mm512_max_epi16(_mm512_cvtepu8_epi16(_mm256_loadu_si256((__m256i *)gcli_data_top_ptr)),
                                             gtli_max_avx512);

            //uint8_t gcli = MAX(gcli_data_ptr[i], gtli);
            __m512i gcli = _mm512_max_epi16(_mm512_cvtepu8_epi16(_mm256_loadu_si256((__m256i *)gcli_data_ptr)), gtli_avx512);

            //int8_t delta_m = gcli - m_top; //Can be negative!!!
            __m512i delta_m = _mm512_sub_epi16(gcli, m_top);

            //uint8_t bits = vlc_encode_get_bits(delta_m, m_top, gtli);
            __m512i bits = vlc_encode_get_bits_avx512(delta_m, m_top, gtli_avx512);

            //vpred_bits_pack[i] = bits;
            _mm_storeu_si128((__m128i *)vpred_bits_pack,
                             _mm_packs_epi16(_mm512_castsi512_si128(bits), _mm512_extracti64x2_epi64(bits, 0x1)));
            _mm_storeu_si128((__m128i *)(vpred_bits_pack + 16),
                             _mm_packs_epi16(_mm512_extracti64x2_epi64(bits, 0x2), _mm512_extracti64x2_epi64(bits, 0x3)));

            //pack_size_gcli_no_sigf += bits;
            sum_avx512 = _mm512_add_epi16(sum_avx512, bits);

            gcli_data_top_ptr += 32;
            gcli_data_ptr += 32;
            vpred_bits_pack += 32;
        }
        __m256i sum_avx2 = _mm256_add_epi16(_mm512_castsi512_si256(sum_avx512), _mm512_extracti64x4_epi64(sum_avx512, 0x1));
        __m128i sum_sse = _mm_hadd_epi16(_mm256_castsi256_si128(sum_avx2), _mm256_extracti128_si256(sum_avx2, 0x1)); // 0..7 16bit
        sum_sse = _mm_hadd_epi16(sum_sse, sum_sse);                 // 0..3, 0..3 16bit
        sum_sse = _mm_unpacklo_epi16(sum_sse, _mm_setzero_si128()); //0..3 32bit
        sum_sse = _mm_hadd_epi32(sum_sse, sum_sse);                 // 0..1, 0..1 32bit
        pack_size_gcli_no_sigf += _mm_cvtsi128_si32(sum_sse);
        pack_size_gcli_no_sigf += _mm_extract_epi32(sum_sse, 1);
    }

    if (leftover) {
        pack_size_gcli_no_sigf += rate_control_calc_vpred_cost_nosigf_avx2(
            leftover, gcli_data_top_ptr, gcli_data_ptr, vpred_bits_pack, gtli, gtli_max);
    }
    return pack_size_gcli_no_sigf;
}

void rate_control_calc_vpred_cost_sigf_nosigf_avx512(uint32_t significance_width, uint32_t gcli_width, uint8_t hdr_Rm,
                                                     uint32_t significance_group_size, uint8_t *gcli_data_top_ptr,
                                                     uint8_t *gcli_data_ptr, uint8_t *vpred_bits_pack,
                                                     uint8_t *vpred_significance, uint8_t gtli, uint8_t gtli_max,
                                                     uint32_t *pack_size_gcli_sigf_reduction, uint32_t *pack_size_gcli_no_sigf) {
    UNUSED(significance_width);
    UNUSED(significance_group_size);
    assert(significance_group_size == SIGNIFICANCE_GROUP_SIZE);

    *pack_size_gcli_sigf_reduction = 0;
    *pack_size_gcli_no_sigf = 0;

    __m128i bits_sum_sigf_sse = _mm_setzero_si128();
    __m128i bits_sum_nosigf_sse = _mm_setzero_si128();

    uint32_t leftover = gcli_width % 32;
    uint32_t non_zero_significance = 0;

    if (gcli_width / 32) {
        __m512i bits_sum_nosigf_avx512 = _mm512_setzero_si512();
        __m512i bits_sum_sigf_avx512 = _mm512_setzero_si512();
        const __m512i gtli_max_avx512 = _mm512_set1_epi16(gtli_max);
        const __m512i gtli_avx512 = _mm512_set1_epi16(gtli);
        for (uint32_t i = 0; i < gcli_width / 32; ++i) {
            const __m512i gcli_avx512 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((__m256i *)gcli_data_ptr));
            const __m512i gcli_max_avx512 = _mm512_max_epi16(gcli_avx512, gtli_avx512);
            const __m512i m_top_avx512 = _mm512_max_epi16(_mm512_cvtepu8_epi16(_mm256_loadu_si256((__m256i *)gcli_data_top_ptr)),
                                                          gtli_max_avx512);
            const __m512i delta_m_avx512 = _mm512_sub_epi16(gcli_max_avx512, m_top_avx512);

            *(uint32_t *)vpred_significance = hdr_Rm ? _mm512_cmplt_epu16_mask(gtli_avx512, gcli_avx512)
                                                     : _mm512_cmpneq_epu16_mask(m_top_avx512, gcli_max_avx512);

            vpred_significance[0] = !vpred_significance[0];
            vpred_significance[1] = !vpred_significance[1];
            vpred_significance[2] = !vpred_significance[2];
            vpred_significance[3] = !vpred_significance[3];

            const __m512i bits = vlc_encode_get_bits_avx512(delta_m_avx512, m_top_avx512, gtli_avx512);
            _mm_storeu_si128((__m128i *)vpred_bits_pack,
                             _mm_packs_epi16(_mm512_castsi512_si128(bits), _mm512_extracti64x2_epi64(bits, 0x1)));
            _mm_storeu_si128((__m128i *)(vpred_bits_pack + 16),
                             _mm_packs_epi16(_mm512_extracti64x2_epi64(bits, 0x2), _mm512_extracti64x2_epi64(bits, 0x3)));
            bits_sum_nosigf_avx512 = _mm512_add_epi16(bits_sum_nosigf_avx512, bits);

            non_zero_significance += vpred_significance[0] + vpred_significance[1] + vpred_significance[2] +
                vpred_significance[3];

            __mmask32 mask = (*(uint32_t *)vpred_significance) * 255;

            bits_sum_sigf_avx512 = _mm512_mask_add_epi16(bits_sum_sigf_avx512, mask, bits_sum_sigf_avx512, bits);

            gcli_data_top_ptr += 32;
            gcli_data_ptr += 32;
            vpred_bits_pack += 32;
            vpred_significance += 4;
        }
        bits_sum_sigf_sse = _mm_add_epi16(
            _mm_add_epi16(_mm512_castsi512_si128(bits_sum_sigf_avx512), _mm512_extracti64x2_epi64(bits_sum_sigf_avx512, 0x1)),
            _mm_add_epi16(_mm512_extracti64x2_epi64(bits_sum_sigf_avx512, 0x2),
                          _mm512_extracti64x2_epi64(bits_sum_sigf_avx512, 0x3)));

        bits_sum_nosigf_sse = _mm_add_epi16(
            _mm_add_epi16(_mm512_castsi512_si128(bits_sum_nosigf_avx512), _mm512_extracti64x2_epi64(bits_sum_nosigf_avx512, 0x1)),
            _mm_add_epi16(_mm512_extracti64x2_epi64(bits_sum_nosigf_avx512, 0x2),
                          _mm512_extracti64x2_epi64(bits_sum_nosigf_avx512, 0x3)));
    }

    if (leftover) {
        if (leftover / 16) {
            const __m256i gtli_avx2 = _mm256_set1_epi16(gtli);
            const __m256i gtli_max_avx2 = _mm256_set1_epi16(gtli_max);
            const __m256i gcli_avx2 = _mm256_cvtepu8_epi16(_mm_loadu_si128((__m128i *)gcli_data_ptr));
            const __m256i gcli_max_avx2 = _mm256_max_epu16(gcli_avx2, gtli_avx2);
            const __m256i m_top_avx2 = _mm256_max_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128((__m128i *)gcli_data_top_ptr)),
                                                        gtli_max_avx2);
            const __m256i delta_m_avx2 = _mm256_sub_epi16(gcli_max_avx2, m_top_avx2);

            *(uint16_t *)vpred_significance = hdr_Rm ? _mm256_cmplt_epu16_mask(gtli_avx2, gcli_avx2)
                                                     : _mm256_cmpneq_epu16_mask(m_top_avx2, gcli_max_avx2);

            vpred_significance[0] = !vpred_significance[0];
            vpred_significance[1] = !vpred_significance[1];

            const __m256i bits = vlc_encode_get_bits_avx2(delta_m_avx2, m_top_avx2, gtli_avx2);
            _mm_storeu_si128((__m128i *)vpred_bits_pack,
                             _mm_packs_epi16(_mm256_castsi256_si128(bits), _mm256_extracti128_si256(bits, 0x1)));
            bits_sum_nosigf_sse = _mm_add_epi16(bits_sum_nosigf_sse, _mm256_castsi256_si128(bits));
            bits_sum_nosigf_sse = _mm_add_epi16(bits_sum_nosigf_sse, _mm256_extracti128_si256(bits, 0x1));

            non_zero_significance += vpred_significance[0] + vpred_significance[1];

            bits_sum_sigf_sse = _mm_mask_add_epi16(
                bits_sum_sigf_sse, vpred_significance[0] * 255, bits_sum_sigf_sse, _mm256_castsi256_si128(bits));
            bits_sum_sigf_sse = _mm_mask_add_epi16(
                bits_sum_sigf_sse, vpred_significance[1] * 255, bits_sum_sigf_sse, _mm256_extracti128_si256(bits, 0x1));

            gcli_data_top_ptr += 16;
            gcli_data_ptr += 16;
            vpred_bits_pack += 16;
            vpred_significance += 2;
            leftover -= 16;
        }

        const __m128i gtli_sse = _mm_set1_epi16(gtli);
        const __m128i gtli_max_sse = _mm_set1_epi16(gtli_max);

        if (leftover / 8) {
            const __m128i gcli_sse = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_data_ptr));
            const __m128i gcli_max_sse = _mm_max_epi16(gcli_sse, gtli_sse);
            const __m128i m_top_sse = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_data_top_ptr)),
                                                    gtli_max_sse);
            const __m128i delta_m = _mm_sub_epi16(gcli_max_sse, m_top_sse);

            vpred_significance[0] = !(hdr_Rm ? _mm_cmplt_epu16_mask(gtli_sse, gcli_sse)
                                             : _mm_cmpneq_epu16_mask(m_top_sse, gcli_max_sse));

            __m128i bits = vlc_encode_get_bits_sse(delta_m, m_top_sse, gtli_sse);
            _mm_storel_epi64((__m128i *)vpred_bits_pack, _mm_packs_epi16(bits, bits));
            bits_sum_nosigf_sse = _mm_add_epi16(bits_sum_nosigf_sse, bits);

            non_zero_significance += vpred_significance[0];
            bits_sum_sigf_sse = _mm_mask_add_epi16(bits_sum_sigf_sse, vpred_significance[0] * 255, bits_sum_sigf_sse, bits);

            gcli_data_top_ptr += 8;
            gcli_data_ptr += 8;
            vpred_bits_pack += 8;
            vpred_significance++;
            leftover -= 8;
        }
        if (leftover) {
            DECLARE_ALIGNED(16, uint8_t, gcli_top_tmp[8]);
            DECLARE_ALIGNED(16, uint8_t, gcli_tmp[8]);

            for (uint32_t i = 0; i < leftover; ++i) {
                gcli_top_tmp[i] = gcli_data_top_ptr[i];
                gcli_tmp[i] = gcli_data_ptr[i];
            }

            const __m128i gcli_sse = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_tmp));
            const __m128i gcli_max_sse = _mm_max_epi16(gcli_sse, gtli_sse);
            const __m128i m_top_sse = _mm_max_epi16(_mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i *)gcli_top_tmp)), gtli_max_sse);
            const __m128i delta_m = _mm_sub_epi16(gcli_max_sse, m_top_sse);

            const uint8_t shift = 8 - leftover;
            if (!hdr_Rm) {
                vpred_significance[0] = !(uint8_t)(_mm_cmpneq_epu16_mask(m_top_sse, gcli_max_sse) << shift);
            }
            else {
                vpred_significance[0] = !(uint8_t)(_mm_cmplt_epu16_mask(gtli_sse, gcli_sse) << shift);
            }

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
    bits_sum_sigf_sse = _mm_hadd_epi16(bits_sum_sigf_sse, bits_sum_sigf_sse);       // 0..3, 0..3 16bit
    bits_sum_sigf_sse = _mm_unpacklo_epi16(bits_sum_sigf_sse, _mm_setzero_si128()); //0..3 32bit
    bits_sum_sigf_sse = _mm_hadd_epi32(bits_sum_sigf_sse, bits_sum_sigf_sse);       // 0..1, 0..1 32bit
    *pack_size_gcli_sigf_reduction += _mm_cvtsi128_si32(bits_sum_sigf_sse);
    *pack_size_gcli_sigf_reduction += _mm_extract_epi32(bits_sum_sigf_sse, 1);

    bits_sum_nosigf_sse = _mm_hadd_epi16(bits_sum_nosigf_sse, bits_sum_nosigf_sse);     // 0..3, 0..3 16bit
    bits_sum_nosigf_sse = _mm_unpacklo_epi16(bits_sum_nosigf_sse, _mm_setzero_si128()); //0..3 32bit
    bits_sum_nosigf_sse = _mm_hadd_epi32(bits_sum_nosigf_sse, bits_sum_nosigf_sse);     // 0..1, 0..1 32bit
    *pack_size_gcli_no_sigf += _mm_cvtsi128_si32(bits_sum_nosigf_sse);
    *pack_size_gcli_no_sigf += _mm_extract_epi32(bits_sum_nosigf_sse, 1);
}
