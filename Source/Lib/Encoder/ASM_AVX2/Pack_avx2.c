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
