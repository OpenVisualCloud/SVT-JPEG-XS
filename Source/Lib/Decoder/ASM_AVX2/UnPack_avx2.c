/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "UnPack_avx2.h"
#include "unpack_common.h"
#include "SvtUtility.h"
#include "EncDec.h" /* TRUNCATION_MAX: the single load holds fifteen planes at most */

uint8_t read_4_bits_align4_fast(reader_short_t* r) {
    if (r->bits_used) {
        r->bits_used = 0;
        return (*r->mem++) & 0xF;
    }
    r->bits_used = 4;
    return (*r->mem >> 4);
}

/* Spreads count nibbles, right aligned, into four coefficients.
 *
 * PEXT would do this in four instructions, but this path also runs on Zen 1 and
 * Zen 2 where it is microcoded and costs tens of cycles. So the nibbles are
 * first unpacked one per byte (byte j holds nibble j), and then, for each
 * coefficient, the required bit of every byte is raised into the sign position
 * and collected by VPMOVMSKB. The value is exactly the same: bit j of the
 * result is bit (3 - k) of nibble j, that is, the plane with weight 2^j.
 *
 * Nibbles past count are zero in acc, so the result needs no masking. */
static INLINE uint64_t unpack_planes_to_lanes_sse(uint64_t acc, uint8_t gtli) {
    const __m128i lo_mask = _mm_set1_epi8(0x0F);
    const __m128i packed = _mm_cvtsi64_si128((int64_t)acc);
    const __m128i lo = _mm_and_si128(packed, lo_mask);
    const __m128i hi = _mm_and_si128(_mm_srli_epi16(packed, 4), lo_mask);
    /* byte 2i is the low nibble of source byte i, byte 2i+1 is the high one */
    const __m128i nibbles = _mm_unpacklo_epi8(lo, hi);

    const uint64_t v0 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 4));
    const uint64_t v1 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 5));
    const uint64_t v2 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 6));
    const uint64_t v3 = (uint32_t)_mm_movemask_epi8(_mm_slli_epi16(nibbles, 7));
    return (v0 | (v1 << 16) | (v2 << 32) | (v3 << 48)) << gtli;
}

void unpack_n_groups(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups, uint32_t safe_bytes) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    /* Fast path: a group's position in the stream comes from adding up lengths
     * rather than from finishing the previous group, so the group loads are
     * independent of each other. */
    for (; group < n_groups; group++) {
        if ((nib >> 1) >= safe_bytes) {
            break;
        }
        uint64_t out = 0;
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        /* A corrupt stream can carry a GCLI above the truncation maximum: the
         * unary code yields up to thirty-one. Such a group holds more bit
         * planes than the single load can take, so it is left to the sequential
         * reader below. */
        if (size > TRUNCATION_MAX) {
            break;
        }
        if (size > 0) {
            const uint64_t signs = unpack_sign_spread[unpack_one_nibble(base, nib)];
            out = unpack_planes_to_lanes_sse(unpack_load_nibbles(base, nib + 1, (uint32_t)size), gtli) | signs;
            nib += (uint32_t)size + 1;
        }
        memcpy(buf, &out, sizeof(out));
        buf += GROUP_SIZE;
    }

    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);

    /* End of the line is read sequentially so the load never runs past the data. */
    for (; group < n_groups; group++) {
        uint64_t out = 0;
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            const uint64_t signs = unpack_sign_spread[read_4_bits_align4_fast(r)];
            uint64_t acc = 0;
            for (int32_t i = 0; i < size; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            out = unpack_planes_to_lanes_sse(acc, gtli) | signs;
        }
        memcpy(buf, &out, sizeof(out));
        buf += GROUP_SIZE;
    }
}

void unpack_n_groups_nosign(uint8_t* gclis, uint8_t gtli, reader_short_t* r, uint16_t* buf, uint32_t n_groups,
                            uint32_t safe_bytes) {
    uint8_t* const base = r->mem;
    uint32_t nib = r->bits_used ? 1u : 0u;
    uint32_t group = 0;

    for (; group < n_groups; group++) {
        if ((nib >> 1) >= safe_bytes) {
            break;
        }
        uint64_t out = 0;
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        /* A corrupt stream can carry a GCLI above the truncation maximum: the
         * unary code yields up to thirty-one. Such a group holds more bit
         * planes than the single load can take, so it is left to the sequential
         * reader below. */
        if (size > TRUNCATION_MAX) {
            break;
        }
        if (size > 0) {
            out = unpack_planes_to_lanes_sse(unpack_load_nibbles(base, nib, (uint32_t)size), gtli);
            nib += (uint32_t)size;
        }
        memcpy(buf, &out, sizeof(out));
        buf += GROUP_SIZE;
    }

    r->mem = base + (nib >> 1);
    r->bits_used = (uint8_t)((nib & 1) * 4);

    for (; group < n_groups; group++) {
        uint64_t out = 0;
        const int32_t size = (int32_t)gclis[group] - (int32_t)gtli;
        if (size > 0) {
            uint64_t acc = 0;
            for (int32_t i = 0; i < size; i++) {
                acc = (acc << 4) | read_4_bits_align4_fast(r);
            }
            out = unpack_planes_to_lanes_sse(acc, gtli);
        }
        memcpy(buf, &out, sizeof(out));
        buf += GROUP_SIZE;
    }
}

SvtJxsErrorType_t unpack_data_common(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis,
                                     uint32_t group_size, uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num,
                                     int32_t* precinct_bits_left, unpack_groups_fn groups_sign, unpack_groups_fn groups_nosign) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    assert((bitstream->bits_used != 0) || (bitstream->bits_used != 4));
    const uint32_t group_num = w / GROUP_SIZE;
    const uint32_t leftover = w % GROUP_SIZE;
    const __m128i gtli_const = _mm_set1_epi8(gtli);
    const uint32_t safe_bytes = unpack_safe_byte_count(bitstream->size > bitstream->offset ? bitstream->size - bitstream->offset
                                                                                           : 0);

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
            *precinct_bits_left -= (int32_t)(bits_sum * 4);
            if (*precinct_bits_left < 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        groups_sign(gclis, gtli, &reader, buf, group_num, safe_bytes);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            groups_sign(gclis, gtli, &reader, buf_tmp, 1, 0);
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
            *precinct_bits_left -= (int32_t)(bits_sum * 4);
            if (*precinct_bits_left < 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }
        reader_short_t reader;
        reader.mem = (uint8_t*)(bitstream->mem) + bitstream->offset;
        reader.bits_used = bitstream->bits_used;
        groups_nosign(gclis, gtli, &reader, buf, group_num, safe_bytes);
        if (leftover) {
            buf += group_num * GROUP_SIZE;
            gclis += group_num;
            uint16_t buf_tmp[GROUP_SIZE];
            groups_nosign(gclis, gtli, &reader, buf_tmp, 1, 0);
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

SvtJxsErrorType_t unpack_data_avx2(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                   uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
    return unpack_data_common(bitstream,
                              buf,
                              w,
                              gclis,
                              group_size,
                              gtli,
                              sign_flag,
                              leftover_signs_num,
                              precinct_bits_left,
                              unpack_n_groups,
                              unpack_n_groups_nosign);
}
