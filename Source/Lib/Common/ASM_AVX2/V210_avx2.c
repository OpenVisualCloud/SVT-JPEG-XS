/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <immintrin.h>
#include "V210_avx2.h"
#include "V210.h"

/* Each 128-bit lane holds one group of 6 pixels (16 bytes). A sample is read as the 16-bit word of the 2 bytes
 * that hold it, at a bit offset of 0, 2 or 4 in that word. The multiplication by 16, 4 or 1 moves every sample to
 * bits 4-13, then a shift right by 4 and the mask leave the 10 bits. */
void convert_packed_to_planar_v210_avx2(const void* in_v210, void* out_y, void* out_cb, void* out_cr, uint32_t width) {
    const uint8_t* in = (const uint8_t*)in_v210;
    uint16_t* y = (uint16_t*)out_y;
    uint16_t* cb = (uint16_t*)out_cb;
    uint16_t* cr = (uint16_t*)out_cr;

    // clang-format off
    // Y0 Y1 Y2 Y3 Y4 Y5 of a group, then 2 zero lanes.
    const __m256i shuffle_y = _mm256_setr_epi8(
        1, 2, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, -1, -1, -1, -1, 1, 2, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, -1, -1, -1, -1);
    const __m256i multiply_y = _mm256_setr_epi16(4, 16, 1, 4, 16, 1, 0, 0, 4, 16, 1, 4, 16, 1, 0, 0);
    // Cb0 Cb1 Cb2, a zero lane, Cr0 Cr1 Cr2, a zero lane.
    const __m256i shuffle_c = _mm256_setr_epi8(
        0, 1, 5, 6, 10, 11, -1, -1, 2, 3, 8, 9, 13, 14, -1, -1, 0, 1, 5, 6, 10, 11, -1, -1, 2, 3, 8, 9, 13, 14, -1, -1);
    const __m256i multiply_c = _mm256_setr_epi16(16, 4, 1, 0, 1, 16, 4, 0, 16, 4, 1, 0, 1, 16, 4, 0);
    // The 3 samples of each 64-bit half side by side, then 2 zero lanes.
    const __m256i compact_c = _mm256_setr_epi8(
        0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1);
    const __m256i compact_y = _mm256_setr_epi32(0, 1, 2, 4, 5, 6, 3, 7);
    // clang-format on
    const __m256i mask = _mm256_set1_epi16(0x3FF);

    uint32_t x = 0;
    /* 2 groups (12 pixels) per step. The stores write 16 Y and 8 + 8 chroma samples, the samples after the 12
     * pixels are written again by the next step or by the C tail. */
    for (; x + 16 <= width; x += 2 * V210_PIXELS_PER_GROUP) {
        const __m256i groups = _mm256_loadu_si256((const __m256i*)in);

        __m256i luma = _mm256_shuffle_epi8(groups, shuffle_y);
        luma = _mm256_and_si256(_mm256_srli_epi16(_mm256_mullo_epi16(luma, multiply_y), 4), mask);
        luma = _mm256_permutevar8x32_epi32(luma, compact_y);
        _mm256_storeu_si256((__m256i*)y, luma);

        __m256i chroma = _mm256_shuffle_epi8(groups, shuffle_c);
        chroma = _mm256_and_si256(_mm256_srli_epi16(_mm256_mullo_epi16(chroma, multiply_c), 4), mask);
        // Cb of both groups in the low 128 bits, Cr of both groups in the high 128 bits.
        chroma = _mm256_permute4x64_epi64(chroma, 0xD8);
        chroma = _mm256_shuffle_epi8(chroma, compact_c);
        _mm_storeu_si128((__m128i*)cb, _mm256_castsi256_si128(chroma));
        _mm_storeu_si128((__m128i*)cr, _mm256_extracti128_si256(chroma, 1));

        in += 2 * V210_BYTES_PER_GROUP;
        y += 2 * V210_PIXELS_PER_GROUP;
        cb += V210_PIXELS_PER_GROUP;
        cr += V210_PIXELS_PER_GROUP;
    }
    if (x < width) {
        convert_packed_to_planar_v210_c(in, y, cb, cr, width - x);
    }
}

/* Each 128-bit lane makes the 4 words of one group. The 3 fields of the words are gathered from the Y samples and
 * from the interleaved chroma samples C (Cb0 Cr0 Cb1 Cr1 Cb2 Cr2) into the low 16 bits of 32-bit lanes:
 *   bits 0-9:   C0 Y1 C3 Y4
 *   bits 10-19: Y0 C2 Y3 C5
 *   bits 20-29: C1 Y2 C4 Y5 */
void convert_planar_to_packed_v210_avx2(const uint16_t* in_y, const uint16_t* in_cb, const uint16_t* in_cr, void* out_v210,
                                        uint32_t width) {
    uint8_t* out = (uint8_t*)out_v210;

    // clang-format off
    const __m256i samples_0_3_to_words_0_2 = _mm256_setr_epi8(
        0, 1, -1, -1, -1, -1, -1, -1, 6, 7, -1, -1, -1, -1, -1, -1, 0, 1, -1, -1, -1, -1, -1, -1, 6, 7, -1, -1, -1, -1, -1, -1);
    const __m256i samples_1_4_to_words_1_3 = _mm256_setr_epi8(
        -1, -1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, 8, 9, -1, -1, -1, -1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, 8, 9, -1, -1);
    const __m256i samples_2_5_to_words_1_3 = _mm256_setr_epi8(
        -1, -1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1, 10, 11, -1, -1, -1, -1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1, 10, 11, -1, -1);
    const __m256i samples_1_4_to_words_0_2 = _mm256_setr_epi8(
        2, 3, -1, -1, -1, -1, -1, -1, 8, 9, -1, -1, -1, -1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, 8, 9, -1, -1, -1, -1, -1, -1);
    // clang-format on
    // The second group starts at sample 6: words 0-3 to the low lane, words 3-6 to the high lane.
    const __m256i second_group = _mm256_setr_epi32(0, 1, 2, 3, 3, 4, 5, 6);
    const __m256i mask = _mm256_set1_epi16(0x3FF);

    uint32_t x = 0;
    // 2 groups (12 pixels) per step. The loads read 16 Y and 8 + 8 chroma samples.
    for (; x + 16 <= width; x += 2 * V210_PIXELS_PER_GROUP) {
        __m256i luma = _mm256_and_si256(_mm256_loadu_si256((const __m256i*)in_y), mask);
        const __m128i cb = _mm_loadu_si128((const __m128i*)in_cb);
        const __m128i cr = _mm_loadu_si128((const __m128i*)in_cr);
        __m256i chroma = _mm256_set_m128i(_mm_unpackhi_epi16(cb, cr), _mm_unpacklo_epi16(cb, cr));
        chroma = _mm256_and_si256(chroma, mask);
        luma = _mm256_permutevar8x32_epi32(luma, second_group);
        chroma = _mm256_permutevar8x32_epi32(chroma, second_group);

        const __m256i field_0 = _mm256_or_si256(_mm256_shuffle_epi8(chroma, samples_0_3_to_words_0_2),
                                                _mm256_shuffle_epi8(luma, samples_1_4_to_words_1_3));
        const __m256i field_1 = _mm256_or_si256(_mm256_shuffle_epi8(luma, samples_0_3_to_words_0_2),
                                                _mm256_shuffle_epi8(chroma, samples_2_5_to_words_1_3));
        const __m256i field_2 = _mm256_or_si256(_mm256_shuffle_epi8(chroma, samples_1_4_to_words_0_2),
                                                _mm256_shuffle_epi8(luma, samples_2_5_to_words_1_3));
        const __m256i words = _mm256_or_si256(field_0,
                                              _mm256_or_si256(_mm256_slli_epi32(field_1, 10), _mm256_slli_epi32(field_2, 20)));
        _mm256_storeu_si256((__m256i*)out, words);

        out += 2 * V210_BYTES_PER_GROUP;
        in_y += 2 * V210_PIXELS_PER_GROUP;
        in_cb += V210_PIXELS_PER_GROUP;
        in_cr += V210_PIXELS_PER_GROUP;
    }
    if (x < width) {
        convert_planar_to_packed_v210_c(in_y, in_cb, in_cr, out, width - x);
    }
}
