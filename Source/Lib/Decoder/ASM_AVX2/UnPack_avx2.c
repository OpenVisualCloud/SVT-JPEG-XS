/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "UnPack_avx2.h"
#include "SvtUtility.h"

DECLARE_ALIGNED(16, static const uint32_t, shift_data[4]) = {3, 2, 1, 0};
DECLARE_ALIGNED(16, static const uint32_t, bits_offset[4]) = {8, 4, 2, 1};
DECLARE_ALIGNED(16, static const uint32_t, sign_shift[4]) = {BITSTREAM_BIT_POSITION_SIGN - 3,
                                                             BITSTREAM_BIT_POSITION_SIGN - 2,
                                                             BITSTREAM_BIT_POSITION_SIGN - 1,
                                                             BITSTREAM_BIT_POSITION_SIGN};

typedef struct reader_short {
    uint8_t* mem;
    uint8_t bits_used;
} reader_short_t;

uint8_t read_4_bits_align4_fast(reader_short_t* r) {
    if (r->bits_used) {
        r->bits_used = 0;
        return (*r->mem++) & 0xF;
    }
    r->bits_used = 4;
    return (*r->mem >> 4);
}

static INLINE void unpack_data_single_group_avx2(reader_short_t* r, __m128i* vals, int32_t size, int8_t gtli) {
    __m128i val;

    for (int32_t bits = 0; bits < (size - 1); bits++) {
        val = _mm_set1_epi32(read_4_bits_align4_fast(r));
        val = _mm_and_si128(val, *(__m128i*)bits_offset);
        *vals = _mm_or_si128(*vals, val);
        *vals = _mm_slli_epi32(*vals, 1);
    }
    val = _mm_set1_epi32(read_4_bits_align4_fast(r));
    val = _mm_and_si128(val, *(__m128i*)bits_offset);
    *vals = _mm_or_si128(*vals, val);
    *vals = _mm_srlv_epi32(*vals, *(__m128i*)shift_data);
    *vals = _mm_slli_epi32(*vals, gtli);
}

void unpack_n_groups(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups) {
    __m128i vals;
    for (uint32_t group = 0; group < n_groups; group++) {
        vals = _mm_setzero_si128();
        int32_t bitplanes_size = gclis[group] - gtli;
        if (bitplanes_size > 0) {
            __m128i signs = _mm_set1_epi32(read_4_bits_align4_fast(r));
            signs = _mm_and_si128(signs, *(__m128i*)bits_offset);
            signs = _mm_sllv_epi32(signs, *(__m128i*)sign_shift);
            unpack_data_single_group_avx2(r, &vals, bitplanes_size, gtli);

            vals = _mm_or_si128(vals, signs);
            vals = _mm_packus_epi32(vals, vals);
        }
        _mm_storel_epi64((__m128i*)buf, vals);
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups_nosign(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups) {
    __m128i vals;
    for (uint32_t group = 0; group < n_groups; group++) {
        vals = _mm_setzero_si128();
        int32_t bitplanes_size = gclis[group] - gtli;
        if (bitplanes_size > 0) {
            unpack_data_single_group_avx2(r, &vals, bitplanes_size, gtli);
            vals = _mm_packus_epi32(vals, vals);
        }
        _mm_storel_epi64((__m128i*)buf, vals);
        buf += GROUP_SIZE;
    }
}

SvtJxsErrorType_t unpack_data_avx2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                   uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    const uint32_t group_num = w / GROUP_SIZE;
    const uint32_t leftover = w % GROUP_SIZE;
    const __m128i gtli_const = _mm_set1_epi8(gtli);

    if (sign_flag == 0) {
        //Calculate how many bits will be used from bitstream to avoid reading out of memory allocation
        {
            uint8_t* gclis_ptr = gclis;
            __m256i sum_epi16_avx2 = _mm256_setzero_si256();
            for (uint32_t group = 0; group < (group_num / 16); group++) {
                __m128i bits_epu8 = _mm_subs_epu8(_mm_loadu_si128((__m128i*)gclis_ptr), gtli_const);
                __m256i bits_epu16 = _mm256_cvtepi8_epi16(bits_epu8);
                __m256i signs = _mm256_cmpgt_epi16(bits_epu16, _mm256_setzero_si256());
                signs = _mm256_srli_epi16(signs, 15);
                sum_epi16_avx2 = _mm256_add_epi16(sum_epi16_avx2, bits_epu16);
                sum_epi16_avx2 = _mm256_add_epi16(sum_epi16_avx2, signs);
                gclis_ptr += 16;
            }
            __m128i sum_epi16_sse = _mm_hadd_epi16(_mm256_castsi256_si128(sum_epi16_avx2),
                                                   _mm256_extracti128_si256(sum_epi16_avx2, 0x1)); // 0..7 16bit
            sum_epi16_sse = _mm_hadd_epi16(sum_epi16_sse, sum_epi16_sse);                          // 0..3, 0..3 16bit
            sum_epi16_sse = _mm_unpacklo_epi16(sum_epi16_sse, _mm_setzero_si128());                //0..3 32bit
            sum_epi16_sse = _mm_hadd_epi32(sum_epi16_sse, sum_epi16_sse);                          // 0..1, 0..1 32bit
            uint32_t bits_sum = _mm_cvtsi128_si32(sum_epi16_sse) + _mm_extract_epi32(sum_epi16_sse, 1);
            for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
                if (gclis_ptr[group] > gtli) {
                    bits_sum += (gclis_ptr[group] - gtli) + 1;
                }
            }
            *precinct_bits_left -= bits_sum * 4;
            if (*precinct_bits_left < 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        unpack_n_groups(gclis, gtli, &reader, buf, group_num);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            unpack_n_groups(gclis, gtli, &reader, buf_tmp, 1);
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
        bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
        bitstream->bits_used = reader.bits_used;
    }
    else {
        //Calculate how many bits will be used from bitstream to avoid reading out of memory allocation
        {
            uint8_t* gclis_ptr = gclis;
            __m256i sum_epi16_avx2 = _mm256_setzero_si256();
            for (uint32_t group = 0; group < (group_num / 16); group++) {
                __m128i bits_epu8 = _mm_subs_epu8(_mm_loadu_si128((__m128i*)gclis_ptr), gtli_const);
                sum_epi16_avx2 = _mm256_add_epi16(sum_epi16_avx2, _mm256_cvtepi8_epi16(bits_epu8));
                gclis_ptr += 16;
            }
            __m128i sum_epi16_sse = _mm_hadd_epi16(_mm256_castsi256_si128(sum_epi16_avx2),
                                                   _mm256_extracti128_si256(sum_epi16_avx2, 0x1)); // 0..7 16bit
            sum_epi16_sse = _mm_hadd_epi16(sum_epi16_sse, sum_epi16_sse);                          // 0..3, 0..3 16bit
            sum_epi16_sse = _mm_unpacklo_epi16(sum_epi16_sse, _mm_setzero_si128());                //0..3 32bit
            sum_epi16_sse = _mm_hadd_epi32(sum_epi16_sse, sum_epi16_sse);                          // 0..1, 0..1 32bit
            uint32_t bits_sum = _mm_cvtsi128_si32(sum_epi16_sse) + _mm_extract_epi32(sum_epi16_sse, 1);
            for (uint32_t group = 0; group < (group_num % 16) + !!leftover; group++) {
                if (gclis_ptr[group] > gtli) {
                    bits_sum += (gclis_ptr[group] - gtli);
                }
            }
            *precinct_bits_left -= bits_sum * 4;
            if (*precinct_bits_left < 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        unpack_n_groups_nosign(gclis, gtli, &reader, buf, group_num);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            unpack_n_groups_nosign(gclis, gtli, &reader, buf_tmp, 1);
            *leftover_signs_num = 0;
            for (uint32_t leftover_id = leftover; leftover_id < GROUP_SIZE; leftover_id++) {
                *leftover_signs_num += !!buf_tmp[leftover_id];
            }
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
        bitstream->offset = (uint32_t)(reader.mem - bitstream->mem);
        bitstream->bits_used = reader.bits_used;
    }

    return SvtJxsErrorNone;
}
