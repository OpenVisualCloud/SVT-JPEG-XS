/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Dequant_avx2.h"
#include "Dequant.h"
#include "Definitions.h"
#include "Codestream.h"
#include <immintrin.h>
#include <string.h>

/* Sixteen coefficients are four code groups. Each lane here holds, in both of its bytes, how far the GCLI of the
 * lane's group is above GTLI, saturated to [0, 255] like a GCLI below GTLI is 0. */
static INLINE __m256i gcli_above_gtli_x16(const uint8_t* gclis, __m256i gtli_x32) {
    /* Both bytes of lane i take byte i / 4: lanes 0-7 are groups 0 and 1, lanes 8-15 groups 2 and 3 */
    const __m256i spread = _mm256_setr_epi8(
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3);
    uint32_t gclis4;
    memcpy(&gclis4, gclis, sizeof(gclis4));
    const __m256i above = _mm256_subs_epu8(_mm256_set1_epi32((int32_t)gclis4), gtli_x32);
    return _mm256_shuffle_epi8(above, spread);
}

/* Uniform inverse quantization adds the magnitude shifted right by zeta = GCLI - GTLI + 1 again and again until
 * nothing is left (see inv_quant_uniform in Dequant.c).
 *
 * zeta changes from one group to the next and AVX2 has no 16-bit shift by a count per lane, so the shift is done as a
 * high multiply: (x * 2^(16 - s)) >> 16 == x >> s for any 16-bit x and 1 <= s <= 16. The multiplier depends only on
 * d = GCLI - GTLI, and a table lookup gives it. Each step of the loop adds two terms, x >> zeta and x >> 2 * zeta, so
 * there are two multipliers:
 *   - d = 0: the group is not above GTLI and its coefficients stay as they are; multipliers of 0 add nothing.
 *   - zeta: 2^(15 - d) for d = 1 .. 14; for d >= 15, zeta >= 16 shifts any 15-bit magnitude to 0, as 0 does.
 *   - 2 * zeta: 2^(14 - 2 * d) for d = 1 .. 7, and 0 above, where the shift leaves nothing either.
 * The index has bit 7 set for d >= 16, for which pshufb gives 0. */
typedef struct {
    __m256i zeta;
    __m256i zeta2;
} uniform_multipliers_t;

static INLINE __m256i lookup_16bit(__m256i lut_lo, __m256i lut_hi, __m256i idx) {
    const __m256i lo = _mm256_and_si256(_mm256_shuffle_epi8(lut_lo, idx), _mm256_set1_epi16(0x00ff));
    const __m256i hi = _mm256_slli_epi16(_mm256_shuffle_epi8(lut_hi, idx), 8);
    return _mm256_or_si256(lo, hi);
}

static INLINE uniform_multipliers_t uniform_multipliers(__m256i above) {
    /* Low and high bytes of the multipliers, for d = 0 .. 15 */
    const __m256i lut_lo = _mm256_setr_epi8(
        0, 0, 0, 0, 0, 0, 0, 0, -128, 64, 32, 16, 8, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, -128, 64, 32, 16, 8, 4, 2, 0);
    const __m256i lut_hi = _mm256_setr_epi8(
        0, 64, 32, 16, 8, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 32, 16, 8, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i lut2_lo = _mm256_setr_epi8(
        0, 0, 0, 0, 64, 16, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 16, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i lut2_hi = _mm256_setr_epi8(
        0, 16, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i idx = _mm256_adds_epu8(above, _mm256_set1_epi8(0x70));
    uniform_multipliers_t m;
    m.zeta = lookup_16bit(lut_lo, lut_hi, idx);
    m.zeta2 = lookup_16bit(lut2_lo, lut2_hi, idx);
    return m;
}

/* Two vectors of sixteen coefficients at a time: each step of the loop waits for a multiply, the second vector fills
 * that wait. */
static INLINE void dequant_uniform_x32(uint16_t* buf, const uint8_t* gclis, __m256i gtli_x32) {
    const __m256i sign_mask = _mm256_set1_epi16((short)BITSTREAM_MASK_SIGN);
    const uniform_multipliers_t m0 = uniform_multipliers(gcli_above_gtli_x16(gclis, gtli_x32));
    const uniform_multipliers_t m1 = uniform_multipliers(gcli_above_gtli_x16(gclis + 4, gtli_x32));
    const __m256i dq0 = _mm256_loadu_si256((const __m256i*)buf);
    const __m256i dq1 = _mm256_loadu_si256((const __m256i*)(buf + 16));
    __m256i rest0 = _mm256_andnot_si256(sign_mask, dq0);
    __m256i rest1 = _mm256_andnot_si256(sign_mask, dq1);
    __m256i rho0 = rest0;
    __m256i rho1 = rest1;
    __m256i more;
    do {
        rho0 = _mm256_add_epi16(rho0, _mm256_mulhi_epu16(rest0, m0.zeta));
        rho1 = _mm256_add_epi16(rho1, _mm256_mulhi_epu16(rest1, m1.zeta));
        rest0 = _mm256_mulhi_epu16(rest0, m0.zeta2);
        rest1 = _mm256_mulhi_epu16(rest1, m1.zeta2);
        rho0 = _mm256_add_epi16(rho0, rest0);
        rho1 = _mm256_add_epi16(rho1, rest1);
        more = _mm256_or_si256(rest0, rest1);
    } while (!_mm256_testz_si256(more, more));
    _mm256_storeu_si256((__m256i*)buf, _mm256_or_si256(rho0, _mm256_and_si256(dq0, sign_mask)));
    _mm256_storeu_si256((__m256i*)(buf + 16), _mm256_or_si256(rho1, _mm256_and_si256(dq1, sign_mask)));
}

static void dequant_uniform_avx2(uint16_t* buf, uint32_t buf_len, uint8_t* gclis, uint8_t gtli) {
    const __m256i gtli_x32 = _mm256_set1_epi8((char)gtli);
    uint32_t i = 0;
    for (; i + 32 <= buf_len; i += 32) {
        dequant_uniform_x32(buf + i, gclis + i / GROUP_SIZE, gtli_x32);
    }
    if (i < buf_len) {
        /* The rest of the line, through a buffer padded with zero coefficients in groups with a GCLI of zero, which
         * stay zero */
        DECLARE_ALIGNED(32, uint16_t, tmp_buf[32]) = {0};
        uint8_t tmp_gclis[8] = {0};
        const uint32_t left = buf_len - i;
        memcpy(tmp_buf, buf + i, left * sizeof(uint16_t));
        memcpy(tmp_gclis, gclis + i / GROUP_SIZE, (left + GROUP_SIZE - 1) / GROUP_SIZE);
        dequant_uniform_x32(tmp_buf, tmp_gclis, gtli_x32);
        memcpy(buf + i, tmp_buf, left * sizeof(uint16_t));
    }
}

static INLINE void dequant_deadzone_x16(uint16_t* buf, const uint8_t* gclis, __m256i gtli_x32, __m256i half_step) {
    const __m256i magnitude_mask = _mm256_set1_epi16((short)~BITSTREAM_MASK_SIGN);
    const __m256i zero = _mm256_setzero_si256();
    const __m256i above = gcli_above_gtli_x16(gclis, gtli_x32);
    const __m256i dq = _mm256_loadu_si256((const __m256i*)buf);
    /* The half step goes to the non-zero coefficients of the groups above GTLI */
    const __m256i nonzero = _mm256_cmpgt_epi16(_mm256_and_si256(dq, magnitude_mask), zero);
    const __m256i add = _mm256_andnot_si256(_mm256_cmpeq_epi16(above, zero), _mm256_and_si256(nonzero, half_step));
    _mm256_storeu_si256((__m256i*)buf, _mm256_or_si256(dq, add));
}

static void dequant_deadzone_avx2(uint16_t* buf, uint32_t buf_len, uint8_t* gclis, uint8_t gtli) {
    const __m256i gtli_x32 = _mm256_set1_epi8((char)gtli);
    const __m256i half_step = _mm256_set1_epi16((short)(1 << (gtli - 1)));
    uint32_t i = 0;
    for (; i + 16 <= buf_len; i += 16) {
        dequant_deadzone_x16(buf + i, gclis + i / GROUP_SIZE, gtli_x32, half_step);
    }
    if (i < buf_len) {
        /* The rest of the line, through a padded buffer like the uniform one */
        DECLARE_ALIGNED(32, uint16_t, tmp_buf[16]) = {0};
        uint8_t tmp_gclis[4] = {0};
        const uint32_t left = buf_len - i;
        memcpy(tmp_buf, buf + i, left * sizeof(uint16_t));
        memcpy(tmp_gclis, gclis + i / GROUP_SIZE, (left + GROUP_SIZE - 1) / GROUP_SIZE);
        dequant_deadzone_x16(tmp_buf, tmp_gclis, gtli_x32, half_step);
        memcpy(buf + i, tmp_buf, left * sizeof(uint16_t));
    }
}

void dequant_avx2(uint16_t* buf, uint32_t buf_len, uint8_t* gclis, uint32_t group_size, uint8_t gtli, QUANT_TYPE dq_type) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    if (gtli == 0) {
        return;
    }
    switch (dq_type) {
    case QUANT_TYPE_UNIFORM:
        dequant_uniform_avx2(buf, buf_len, gclis, gtli);
        return;
    case QUANT_TYPE_DEADZONE:
        dequant_deadzone_avx2(buf, buf_len, gclis, gtli);
        return;
    default:
        assert("unknown quantization");
    }
}
