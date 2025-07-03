/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "Packing.h"
#include "SvtUtility.h"
#include "BitstreamReader.h"
#include "EncDec.h"
#include "Codestream.h"
#include "decoder_dsp_rtcd.h"

typedef struct vlc_reader {
    uint8_t const* mem;
    uint32_t bits_to_use;
    uint32_t bits_used;
    uint64_t register64; //Always keep 32 bits in left boundary
    uint32_t register_bits;
} vlc_reader_t;

static INLINE void vlc_reader_init(vlc_reader_t* vlc_reader, bitstream_reader_t* bitstream, int32_t param_bits_to_use) {
    vlc_reader->mem = bitstream->mem + bitstream->offset;
    vlc_reader->register64 = 0; //Always keep 32 bits in left boundary
    vlc_reader->register_bits = 0;
    vlc_reader->bits_used = 0;
    vlc_reader->bits_to_use = param_bits_to_use;
    if (bitstream->bits_used) {
        vlc_reader->register64 = ((uint64_t)((~*vlc_reader->mem) & ((1 << (8 - bitstream->bits_used)) - 1)))
            << (56 + bitstream->bits_used);
        vlc_reader->register_bits = 8 - bitstream->bits_used;
        vlc_reader->mem++;
    }
}

static INLINE uint32_t vlc_reader_end(vlc_reader_t* vlc_reader, bitstream_reader_t* bitstream) {
    bitstream->offset += (bitstream->bits_used + vlc_reader->bits_used) / 8;
    bitstream->bits_used = (bitstream->bits_used + vlc_reader->bits_used) % 8;
    return vlc_reader->bits_used;
}

static INLINE int8_t vlc_reader_get_next_value(vlc_reader_t* vlc_reader) {
    if (!(vlc_reader->register64 >> 32)) { //Because can not be more than 32 bits for test
        if (vlc_reader->register_bits <= 32) {
            if (vlc_reader->bits_to_use >= vlc_reader->bits_used + 64) {
                //Load 32
                vlc_reader->register64 |= ((uint64_t)((uint32_t) ~(
                                              (((uint32_t)vlc_reader->mem[0]) << 24) | (((uint32_t)vlc_reader->mem[1]) << 16) |
                                              (((uint32_t)vlc_reader->mem[2]) << 8) | ((uint32_t)vlc_reader->mem[3]))))
                    << (32 - vlc_reader->register_bits);
                /*const __m128i shuffle_mask = _mm_setr_epi8(3, 2, 1, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255);
                __m128i val_128 = _mm_cvtsi32_si128(*(int32_t*)mem);
                val_128 = _mm_shuffle_epi8(val_128, shuffle_mask);
                val_128 = _mm_xor_si128(val_128, _mm_set1_epi32(-1));
                uint32_t val = _mm_cvtsi128_si32(val_128);
                val64 = val64 << 32 | val;
                */
                vlc_reader->mem += 4;
                vlc_reader->register_bits += 32;

                while (vlc_reader->register_bits <= 48) {
                    //Load 16
                    //vlc_reader->register64 |= ((uint64_t)((~((((uint32_t)vlc_reader->mem[0]) << 8) | (((uint32_t)vlc_reader->mem[1])))) & 0xFFFF)) << (48 - vlc_reader->register_bits);
                    vlc_reader->register64 |=
                        ((uint64_t)((uint16_t)(~((((uint16_t)vlc_reader->mem[0]) << 8) | (((uint16_t)vlc_reader->mem[1]))))))
                        << (48 - vlc_reader->register_bits);
                    vlc_reader->mem += 2;
                    vlc_reader->register_bits += 16;
                }

                /* Not see benefits to load an end in bytes.
                while (vlc_reader->register_bits <= 56) {
                    //Load 8
                    vlc_reader->register64 |= ((uint64_t)((uint8_t)(~vlc_reader->mem[0]))) << (56 - vlc_reader->register_bits);
                    vlc_reader->mem += 1;
                    vlc_reader->register_bits += 8;
                }*/
            }
            else {
                /*When bitstream can be too short, fill 0*/
                if (vlc_reader->bits_to_use > vlc_reader->bits_used + vlc_reader->register_bits) {
                    uint32_t left_bits = vlc_reader->bits_to_use - vlc_reader->bits_used - vlc_reader->register_bits;
                    for (uint8_t byte = 0; byte < 4; ++byte) {
                        if (left_bits >= 8) {
                            vlc_reader->register64 |= ((uint64_t)((uint8_t)(~(uint8_t)*vlc_reader->mem)))
                                << (56 - vlc_reader->register_bits);
                            vlc_reader->mem++;
                            left_bits -= 8;
                        }
                        else if (left_bits) {
                            vlc_reader->register64 |= ((uint64_t)(((uint8_t)(~(uint8_t)*vlc_reader->mem)) >> (8 - left_bits)))
                                << (64 - vlc_reader->register_bits - left_bits);
                            left_bits = 0;
                        }
                        /*When bitstream is too short fill 0 to not find 1 in bitstream (values in val64 are negative).*/
                        vlc_reader->register_bits += 8;
                    }
                }
                else {
                    /*Fill 0*/
                    vlc_reader->register_bits = 64;
                }
            }
        }
        assert(vlc_reader->register_bits >= 32);
        if (!(vlc_reader->register64 >> 32)) { //Because can not be more than 32 bits for test
            return -1;
        }
    }

    int8_t res = 32 - svt_log2_32(vlc_reader->register64 >> 32); //Possible values: 1-32
    vlc_reader->register_bits -= res;
    vlc_reader->bits_used += res;
    vlc_reader->register64 <<= res;
    return res - 1;
}

static void unpack_data_single_group(bitstream_reader_t* bitstream, uint16_t* buf, int32_t size, int8_t gtli) {
    uint8_t val;
    uint32_t tmp_buf[4] = {0};
    for (int32_t bits = 0; bits < (size - 1); bits++) {
        val = read_4_bits_align4(bitstream);
        tmp_buf[3] = (tmp_buf[3] | (val & 1)) << 1;
        tmp_buf[2] = (tmp_buf[2] | (val & 2)) << 1;
        tmp_buf[1] = (tmp_buf[1] | (val & 4)) << 1;
        tmp_buf[0] = (tmp_buf[0] | (val & 8)) << 1;
    }
    val = read_4_bits_align4(bitstream);
    buf[3] = (uint16_t)(((tmp_buf[3] | (val & 1)) >> 0) << gtli);
    buf[2] = (uint16_t)(((tmp_buf[2] | (val & 2)) >> 1) << gtli);
    buf[1] = (uint16_t)(((tmp_buf[1] | (val & 4)) >> 2) << gtli);
    buf[0] = (uint16_t)(((tmp_buf[0] | (val & 8)) >> 3) << gtli);
}

SvtJxsErrorType_t unpack_data_c(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint8_t* gclis, uint32_t group_size,
                                uint8_t gtli, uint8_t sign_flag, uint8_t* leftover_signs_num, int32_t* precinct_bits_left) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    const uint32_t group_num = w / group_size;
    const uint32_t leftover = w % group_size;

    if (sign_flag == 0) {
        uint8_t val;
        uint16_t s0, s1, s2, s3;
        for (uint32_t group = 0; group < group_num; group++) {
            memset(buf, 0, sizeof(uint16_t) * GROUP_SIZE);
            int32_t bitplanes_size = gclis[group] - gtli;
            if (bitplanes_size > 0) {
                *precinct_bits_left -= 4 * (bitplanes_size + 1);
                if (*precinct_bits_left < 0) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                val = read_4_bits_align4(bitstream);
                s3 = val << (BITSTREAM_BIT_POSITION_SIGN);
                s2 = (val & 2) << (BITSTREAM_BIT_POSITION_SIGN - 1);
                s1 = (val & 4) << (BITSTREAM_BIT_POSITION_SIGN - 2);
                s0 = (val & 8) << (BITSTREAM_BIT_POSITION_SIGN - 3);
                unpack_data_single_group(bitstream, buf, bitplanes_size, gtli);

                buf[0] |= s0;
                buf[1] |= s1;
                buf[2] |= s2;
                buf[3] |= s3;
            }
            buf += GROUP_SIZE;
        }
        if (leftover) {
            uint16_t buf_tmp[GROUP_SIZE] = {0};
            int32_t bitplanes_size = gclis[group_num] - gtli;
            if (bitplanes_size > 0) {
                *precinct_bits_left -= 4 * (bitplanes_size + 1);
                if (*precinct_bits_left < 0) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                val = read_4_bits_align4(bitstream);
                s3 = val << (BITSTREAM_BIT_POSITION_SIGN);
                s2 = (val & 2) << (BITSTREAM_BIT_POSITION_SIGN - 1);
                s1 = (val & 4) << (BITSTREAM_BIT_POSITION_SIGN - 2);
                s0 = (val & 8) << (BITSTREAM_BIT_POSITION_SIGN - 3);
                unpack_data_single_group(bitstream, buf_tmp, bitplanes_size, gtli);
                buf_tmp[0] |= s0;
                buf_tmp[1] |= s1;
                buf_tmp[2] |= s2;
                buf_tmp[3] |= s3;
            }
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
    }
    else {
        for (uint32_t group = 0; group < group_num; group++) {
            memset(buf, 0, sizeof(uint16_t) * GROUP_SIZE);
            int32_t bitplanes_size = gclis[group] - gtli;
            if (bitplanes_size > 0) {
                *precinct_bits_left -= 4 * (bitplanes_size);
                if (*precinct_bits_left < 0) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                unpack_data_single_group(bitstream, buf, bitplanes_size, gtli);
            }
            buf += GROUP_SIZE;
        }
        if (leftover) {
            uint16_t buf_tmp[GROUP_SIZE] = {0};
            int32_t bitplanes_size = gclis[group_num] - gtli;
            if (bitplanes_size > 0) {
                *precinct_bits_left -= 4 * (bitplanes_size);
                if (*precinct_bits_left < 0) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                unpack_data_single_group(bitstream, buf_tmp, bitplanes_size, gtli);
            }
            *leftover_signs_num = 0;
            for (uint32_t leftover_id = leftover; leftover_id < GROUP_SIZE; leftover_id++) {
                *leftover_signs_num += !!buf_tmp[leftover_id];
            }
            memcpy(buf, buf_tmp, sizeof(uint16_t) * (leftover));
        }
    }
    return SvtJxsErrorNone;
}

int32_t unpack_sign(bitstream_reader_t* bitstream, uint16_t* buf, uint32_t w, uint32_t group_size, uint8_t leftover_signs_num,
                    int32_t* precinct_bits_left) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);

    for (uint32_t i = 0; i < w; i++) {
        if (buf[i] != 0) {
            (*precinct_bits_left)--;
            if (*precinct_bits_left >= 0) {
                uint16_t sign = read_1_bit(bitstream);
                buf[i] |= sign << BITSTREAM_BIT_POSITION_SIGN;
            }
            else {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }
    }

    if (leftover_signs_num && (w % GROUP_SIZE)) {
        *precinct_bits_left -= leftover_signs_num;
        if (*precinct_bits_left < 0) {
            return SvtJxsErrorDecoderInvalidBitstream;
        }
        bitstream_reader_skip_bits(bitstream, leftover_signs_num);
    }
    return SvtJxsErrorNone;
}

void unpack_raw_gclis(bitstream_reader_t* bitstream, uint8_t* gclis, uint32_t w) {
    for (uint32_t i = 0; i < w; i++) {
        gclis[i] = (int8_t)read_4_bits_align4(bitstream);
    }
}

static INLINE void unpack_gclis_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data, precinct_band_info_t* b_info,
                                             uint32_t ypos) {
    uint8_t* data = b_data->significance_data + ypos * b_info->significance_width;

    // If bit is set to 1, then significance group is insignificant.
    for (uint32_t i = 0; i < b_info->significance_width; i++) {
        data[i] = read_1_bit(bitstream); //TODO: Optimize read one bit.
    }
}

/*Return 0 if read success, Negative when return error.
  Pointer to precinct_bits_left, if success subtract number of reads bits.*/
SvtJxsErrorType_t unpack_verical_pred_gclis_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                         precinct_band_t* b_data_top, precinct_band_info_t* b_info,
                                                         const int ypos, int run_mode, int32_t band_max_lines,
                                                         int32_t* precinct_bits_left) {
    const int8_t gtli = b_data->gtli;
    const int8_t gtli_top = (ypos == 0) ? (b_data_top->gtli) : (gtli);
    const int8_t t = MAX(gtli_top, gtli);
    const uint32_t gcli_width = b_info->gcli_width;
    uint8_t* gclis_top = NULL;

    if (ypos == 0) {
        gclis_top = b_data_top->gcli_data + (band_max_lines - 1) * gcli_width;
    }
    else {
        gclis_top = b_data->gcli_data + (ypos - 1) * gcli_width;
    }

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;
    uint8_t* significances = b_data->significance_data + ypos * b_info->significance_width;
    uint32_t group_num = (gcli_width / SIGNIFICANCE_GROUP_SIZE);
    uint8_t leftover = (gcli_width % SIGNIFICANCE_GROUP_SIZE);

    vlc_reader_t vlc_reader;
    vlc_reader_init(&vlc_reader, bitstream, *precinct_bits_left);
    int8_t error = 0;

    for (uint32_t group_idx = 0; group_idx < group_num; group_idx++) {
        uint8_t significance_group_is_significant = !significances[0];
        if (significance_group_is_significant) {
            /*VLC decode vertical*/
            for (uint32_t i = 0; i < SIGNIFICANCE_GROUP_SIZE; i++) {
                const int m_top = MAX(gclis_top[i], t);
                int8_t delta_m = 0;
                int8_t x = vlc_reader_get_next_value(&vlc_reader);
                error |= x;
                int treshold = MAX(m_top - gtli, 0);
                if (x > 2 * treshold) {
                    delta_m = x - treshold;
                }
                else if (x > 0) {
                    if (x & 1) {
                        delta_m = -((x + 1) / 2);
                    }
                    else {
                        delta_m = x / 2;
                    }
                }
                gclis[i] = m_top + delta_m;
            }
        }
        else {
            if (run_mode) {
                memset(gclis, gtli, SIGNIFICANCE_GROUP_SIZE);
            }
            else {
                for (uint32_t i = 0; i < SIGNIFICANCE_GROUP_SIZE; i++) {
                    gclis[i] = MAX(gclis_top[i], t);
                }
            }
        }

        significances++;
        gclis += SIGNIFICANCE_GROUP_SIZE;
        gclis_top += SIGNIFICANCE_GROUP_SIZE;
    }

    if (leftover) {
        uint8_t significance_group_is_significant = !significances[0];
        if (significance_group_is_significant) {
            /*VLC decode vertical*/
            for (uint32_t i = 0; i < leftover; i++) {
                const int m_top = MAX(gclis_top[i], t);
                int8_t delta_m = 0;
                int8_t x = vlc_reader_get_next_value(&vlc_reader);
                error |= x;
                int treshold = MAX(m_top - gtli, 0);
                if (x > 2 * treshold) {
                    delta_m = x - treshold;
                }
                else if (x > 0) {
                    if (x & 1) {
                        delta_m = -((x + 1) / 2);
                    }
                    else {
                        delta_m = x / 2;
                    }
                }
                gclis[i] = m_top + delta_m;
            }
        }
        else {
            if (run_mode) {
                memset(gclis, gtli, leftover);
            }
            else {
                for (uint32_t i = 0; i < leftover; i++) {
                    gclis[i] = MAX(gclis_top[i], t);
                }
            }
        }
    }

    /*Check only error at the end.*/
    if (error < 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    *precinct_bits_left -= vlc_reader_end(&vlc_reader, bitstream);
    return SvtJxsErrorNone;
}

/*Return 0 if read success, Negative when return error.
  Pointer to precinct_bits_left, if success subtract number of reads bits.*/
SvtJxsErrorType_t unpack_verical_pred_gclis_no_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                            precinct_band_t* b_data_top, precinct_band_info_t* b_info,
                                                            const int ypos, int32_t band_max_lines, int32_t* precinct_bits_left) {
    const uint8_t gtli = b_data->gtli;
    const uint8_t gtli_top = (ypos == 0) ? (b_data_top->gtli) : (gtli);
    const int8_t t = MAX(gtli_top, gtli);
    const uint32_t gcli_width = b_info->gcli_width;

    uint8_t* gclis_top = NULL;

    if (ypos == 0) {
        gclis_top = b_data_top->gcli_data + (band_max_lines - 1) * gcli_width;
    }
    else {
        gclis_top = b_data->gcli_data + (ypos - 1) * gcli_width;
    }

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;

    vlc_reader_t vlc_reader;
    vlc_reader_init(&vlc_reader, bitstream, *precinct_bits_left);
    int8_t error = 0;

    for (uint32_t i = 0; i < gcli_width; i++) {
        /*VLC decode vertical.*/
        const int m_top = MAX(gclis_top[i], t);
        int8_t x = vlc_reader_get_next_value(&vlc_reader);
        error |= x;
        int8_t delta_m = 0;
        int treshold = MAX(m_top - gtli, 0);

        if (x > 2 * treshold) {
            delta_m = x - treshold;
        }
        else if (x > 0) {
            if (x & 1) {
                delta_m = -((x + 1) / 2);
            }
            else {
                delta_m = x / 2;
            }
        }

        gclis[i] = m_top + delta_m;
    }

    /*Check only error at the end.*/
    if (error < 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    *precinct_bits_left -= vlc_reader_end(&vlc_reader, bitstream);
    return SvtJxsErrorNone;
}

/*Return 0 if read success, Negative when return error.
  Pointer to precinct_bits_left, if success subtract number of reads bits.*/
SvtJxsErrorType_t unpack_pred_zero_gclis_no_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                         precinct_band_info_t* b_info, const int ypos,
                                                         int32_t* precinct_bits_left) {
    const int8_t gtli = b_data->gtli;
    const uint32_t gcli_width = b_info->gcli_width;
    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;

    vlc_reader_t vlc_reader;
    vlc_reader_init(&vlc_reader, bitstream, *precinct_bits_left);
    int8_t error = 0;
    for (uint32_t i = 0; i < gcli_width; i++) {
        int8_t res = vlc_reader_get_next_value(&vlc_reader);
        error |= res;
        gclis[i] = gtli + res;
    }

    /*Check only error at the end.*/
    if (error < 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    *precinct_bits_left -= vlc_reader_end(&vlc_reader, bitstream);
    return SvtJxsErrorNone;
}

/*Return 0 if read success, Negative when return error.
  Pointer to precinct_bits_left, if success subtract number of reads bits.*/
SvtJxsErrorType_t unpack_pred_zero_gclis_significance(bitstream_reader_t* bitstream, precinct_band_t* b_data,
                                                      precinct_band_info_t* b_info, const int ypos, int32_t* precinct_bits_left) {
    const int8_t gtli = b_data->gtli;
    const uint32_t gcli_width = b_info->gcli_width;

    uint8_t* gclis = b_data->gcli_data + ypos * gcli_width;
    uint8_t* significances = b_data->significance_data + ypos * b_info->significance_width;
    uint32_t group_num = (gcli_width / SIGNIFICANCE_GROUP_SIZE);
    uint8_t leftover = (gcli_width % SIGNIFICANCE_GROUP_SIZE);

    vlc_reader_t vlc_reader;
    vlc_reader_init(&vlc_reader, bitstream, *precinct_bits_left);
    int8_t error = 0;
    for (uint32_t group_idx = 0; group_idx < group_num; group_idx++) {
        uint8_t significance_group_is_significant = !significances[0];
        if (significance_group_is_significant) {
            for (uint8_t i = 0; i < SIGNIFICANCE_GROUP_SIZE; i++) {
                int8_t res = vlc_reader_get_next_value(&vlc_reader);
                error |= res;
                gclis[i] = gtli + res;
            }
        }
        else {
            memset(gclis, gtli, SIGNIFICANCE_GROUP_SIZE);
        }
        significances++;
        gclis += SIGNIFICANCE_GROUP_SIZE;
    }

    if (leftover) {
        uint8_t significance_group_is_significant = !significances[0];
        if (significance_group_is_significant) {
            for (uint8_t i = 0; i < leftover; i++) {
                int8_t res = vlc_reader_get_next_value(&vlc_reader);
                error |= res;
                gclis[i] = gtli + res;
            }
        }
        else {
            memset(gclis, gtli, leftover);
        }
    }

    /*Check only error at the end.*/
    if (error < 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    *precinct_bits_left -= vlc_reader_end(&vlc_reader, bitstream);
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t unpack_precinct(bitstream_reader_t* bitstream, precinct_t* prec, precinct_t* prec_top, const pi_t* pi,
                                  const picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose) {
    uint32_t len_before_subpkt_bytes = 0;
    CodingModeFlag coding_modes[MAX_BANDS_NUM];
    uint32_t subpkt_len_bytes;

    /***************Precinct header BEGIN****************/
    if (!bitstream_reader_is_enough_bytes(bitstream, 5 + DIV_ROUND_UP(pi->bands_num_exists * 2, 8))) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    const uint32_t precinct_len_bytes = read_24_bits(bitstream);
    if (precinct_len_bytes > PRECINCT_MAX_BYTES_SIZE) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    const uint8_t quantization = read_8_bits(bitstream);
    const uint8_t refinement = read_8_bits(bitstream);
    const int32_t long_hdr = picture_header_dynamic->hdr_Lh || (!pi->use_short_header);

    for (uint32_t band = 0; band < pi->bands_num_all; ++band) {
        if (pi->global_band_info[band].band_id == BAND_NOT_EXIST) {
            coding_modes[band] = CODING_MODE_FLAG_INVALID;
            continue;
        }
        coding_modes[band] = read_2_bit(bitstream);
        if (prec_top == NULL && (coding_modes[band] & CODING_MODE_FLAG_VERTICAL_PRED)) {
            if (verbose >= VERBOSE_ERRORS) {
                fprintf(stderr, "Error: First precinct in Slice could not have Vertical Prediction!\n");
            }
            return SvtJxsErrorDecoderInvalidBitstream;
        }
    }
    align_bitstream_reader_to_next_byte(bitstream);
    /***************Precinct header END****************/

    /*Bits left calculate from end of header*/
    if ((precinct_len_bytes >= (1 << 20)) || !bitstream_reader_is_enough_bytes(bitstream, precinct_len_bytes)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    int32_t precinct_bits_left = precinct_len_bytes * 8;
    const uint32_t byte_pos_precinct = bitstream_reader_get_used_bytes(bitstream);

    // Equivalent to: Table C.10 - Computation of the truncation position
    precinct_compute_truncation_light(pi, prec, quantization, refinement);
    packet_header_t pkt_header;

    for (uint32_t packet_idx = 0; packet_idx < pi->packets_num; packet_idx++) {
        /***************Packet header BEGIN****************/
        // Packet header exist if at least one band is included, for 420 some bands does not exist
        // Empty packets can happen for last precinct in slice
        int8_t skip_packet = 1;
        for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop; band_idx++) {
            assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
            const uint32_t b = pi->global_band_info[band_idx].band_id;
            const uint32_t c = pi->global_band_info[band_idx].comp_id;
            const uint32_t ypos = pi->packets[packet_idx].line_idx;
            if (ypos < prec->p_info->b_info[c][b].height) {
                const int32_t packet_header_size_bits = long_hdr ? 7 * 8 : 5 * 8;
                precinct_bits_left -= packet_header_size_bits;
                if (precinct_bits_left < 0) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                get_packet_header(bitstream, long_hdr, &pkt_header);
                skip_packet = 0;
                break;
            }
        }
        if (skip_packet) {
            continue;
        }
        /***************Packet header END****************/
        if ((uint32_t)precinct_bits_left < ((pkt_header.data_len + pkt_header.gcli_len + pkt_header.sign_len) * 8)) {
            return SvtJxsErrorDecoderInvalidBitstream;
        }

        if (pkt_header.raw_mode_flag) {
            len_before_subpkt_bytes = (int)bitstream_reader_get_used_bytes(bitstream);
            /*************GCLI sub-packet BEGIN****************************/
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t ypos = pi->packets[packet_idx].line_idx;
                const uint32_t gcli_w = prec->p_info->b_info[c][b].gcli_width;
                if (ypos < prec->p_info->b_info[c][b].height) {
                    precinct_bits_left -= gcli_w * 4;
                    if (precinct_bits_left < 0) {
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                    unpack_raw_gclis(bitstream, prec->bands[c][b].gcli_data + ypos * gcli_w, gcli_w);
                }
            }
        }
        else {
            /***************Significance sub-packet BEGIN****************/
            //This sub-packet is optional. It is only included if bit #1 of the D[p,b] field of the precinct header is set to 1 and
            //the raw mode override flag Dr[p,s] is set to 0.
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t ypos = pi->packets[packet_idx].line_idx;
                if (ypos < prec->p_info->b_info[c][b].height) {
                    assert(coding_modes[band_idx] != CODING_MODE_FLAG_INVALID);
                    if (coding_modes[band_idx] & CODING_MODE_FLAG_SIGNIFICANCE) {
                        precinct_bits_left -= prec->p_info->b_info[c][b].significance_width;
                        if (precinct_bits_left < 0) {
                            return SvtJxsErrorDecoderInvalidBitstream;
                        }
                        unpack_gclis_significance(bitstream, &prec->bands[c][b], &prec->p_info->b_info[c][b], ypos);
                    }
                }
            }

            align_bitstream_reader_to_next_byte(bitstream);
            precinct_bits_left -= precinct_bits_left & 7; /*Align left bits to byte.*/
            /***************Significance sub-packet END****************/
            len_before_subpkt_bytes = (int)bitstream_reader_get_used_bytes(bitstream);

            /*************GCLI sub-packet BEGIN****************************/
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t ypos = pi->packets[packet_idx].line_idx;
                if (ypos < prec->p_info->b_info[c][b].height) {
                    assert(coding_modes[band_idx] != CODING_MODE_FLAG_INVALID);
                    if (coding_modes[band_idx] & CODING_MODE_FLAG_SIGNIFICANCE) {
                        //Vertical prediction
                        if (coding_modes[band_idx] & CODING_MODE_FLAG_VERTICAL_PRED) {
                            SvtJxsErrorType_t ret = unpack_verical_pred_gclis_significance(
                                bitstream,
                                &prec->bands[c][b],
                                &prec_top->bands[c][b],
                                &prec->p_info->b_info[c][b],
                                ypos,
                                picture_header_dynamic->hdr_Rm,
                                pi->components[c].bands[b].height_lines_num,
                                &precinct_bits_left);
                            if (ret) {
                                if (verbose >= VERBOSE_ERRORS) {
                                    fprintf(stderr,
                                            "Error: Invalid Variable Length Coding, Vertical Prediction Significance!!!\n");
                                }
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                        }
                        //prediction from zero
                        else {
                            SvtJxsErrorType_t ret = unpack_pred_zero_gclis_significance(
                                bitstream, &prec->bands[c][b], &prec->p_info->b_info[c][b], ypos, &precinct_bits_left);
                            if (ret) {
                                if (verbose >= VERBOSE_ERRORS) {
                                    fprintf(stderr, "Error: Invalid Variable Length Coding, Predict from zero Significance!!!\n");
                                }
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                        }
                    }
                    else {
                        //Vertical prediction
                        if (coding_modes[band_idx] & CODING_MODE_FLAG_VERTICAL_PRED) {
                            SvtJxsErrorType_t ret = unpack_verical_pred_gclis_no_significance(
                                bitstream,
                                &prec->bands[c][b],
                                &prec_top->bands[c][b],
                                &prec->p_info->b_info[c][b],
                                ypos,
                                pi->components[c].bands[b].height_lines_num,
                                &precinct_bits_left);
                            if (ret) {
                                if (verbose >= VERBOSE_ERRORS) {
                                    fprintf(stderr,
                                            "Error: Invalid Variable Length Coding, Vertical Prediction Non Significance!!!\n");
                                }
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                        }
                        //prediction from zero
                        else {
                            SvtJxsErrorType_t ret = unpack_pred_zero_gclis_no_significance(
                                bitstream, &prec->bands[c][b], &prec->p_info->b_info[c][b], ypos, &precinct_bits_left);
                            if (ret) {
                                if (verbose >= VERBOSE_ERRORS) {
                                    fprintf(stderr,
                                            "Error: Invalid Variable Length Coding, Predict from zero Non Significance!!!\n");
                                }
                                return SvtJxsErrorDecoderInvalidBitstream;
                            }
                        }
                    }
                }
            }
        }
        align_bitstream_reader_to_next_byte(bitstream);
        precinct_bits_left -= precinct_bits_left & 7; /*Align left bits to byte.*/
        /*************GCLI sub-packet END****************************/

        subpkt_len_bytes = bitstream_reader_get_used_bytes(bitstream) - len_before_subpkt_bytes;
        if (subpkt_len_bytes != (pkt_header.gcli_len)) {
            int32_t leftover = (int32_t)pkt_header.gcli_len - subpkt_len_bytes;
            if (leftover > 0 && bitstream_reader_is_enough_bytes(bitstream, leftover)) {
                if (verbose >= VERBOSE_WARNINGS) {
                    fprintf(stderr, "WARNING: (GCLI) skipped=%d\n", leftover);
                }
                bitstream_reader_add_padding(bitstream, leftover);
            }
            else {
                if (verbose >= VERBOSE_ERRORS) {
                    fprintf(stderr,
                            "Error: (GCLI) corruption detected - unpacked=%d , expected=%d\n",
                            subpkt_len_bytes,
                            pkt_header.gcli_len);
                }
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }

        len_before_subpkt_bytes = (int)bitstream_reader_get_used_bytes(bitstream);

        /*************Data sub-packet BEGIN****************************/
        for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop; band_idx++) {
            assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
            const uint32_t b = pi->global_band_info[band_idx].band_id;
            const uint32_t c = pi->global_band_info[band_idx].comp_id;
            const uint32_t data_w = prec->p_info->b_info[c][b].width;
            const uint32_t gcli_w = prec->p_info->b_info[c][b].gcli_width;
            const uint32_t ypos = pi->packets[packet_idx].line_idx;
            assert(ypos < MAX_BAND_LINES);
            if (ypos < prec->p_info->b_info[c][b].height) {
                const int8_t gtli = prec->bands[c][b].gtli;
                SvtJxsErrorType_t ret = unpack_data(bitstream,
                                                    prec->bands[c][b].coeff_data + ypos * pi->components[c].bands[b].width,
                                                    data_w,
                                                    prec->bands[c][b].gcli_data + ypos * gcli_w,
                                                    pi->coeff_group_size,
                                                    gtli,
                                                    picture_header_dynamic->hdr_Fs,
                                                    &prec->bands[c][b].leftover_signs_to_read[ypos],
                                                    &precinct_bits_left);

                if (ret) {
                    if (verbose >= VERBOSE_ERRORS) {
                        fprintf(stderr, "Error: unpack_data, invalid bitstream!!!\n");
                    }
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
            }
        }
        align_bitstream_reader_to_next_byte(bitstream);
        precinct_bits_left -= precinct_bits_left & 7; /*Align left bits to byte.*/
        /*************Data sub-packet END****************************/

        subpkt_len_bytes = bitstream_reader_get_used_bytes(bitstream) - len_before_subpkt_bytes;
        if (subpkt_len_bytes != (pkt_header.data_len)) {
            int32_t leftover = (int32_t)pkt_header.data_len - subpkt_len_bytes;
            if (leftover > 0 && bitstream_reader_is_enough_bytes(bitstream, leftover)) {
                if (verbose >= VERBOSE_WARNINGS) {
                    fprintf(stderr, "WARNING: (DATA) skipped=%d\n", leftover);
                }
                bitstream_reader_add_padding(bitstream, leftover);
            }
            else {
                if (verbose >= VERBOSE_ERRORS) {
                    fprintf(stderr,
                            "Error: (DATA) corruption detected - unpacked=%d , expected=%d\n",
                            subpkt_len_bytes,
                            pkt_header.data_len);
                }
                return SvtJxsErrorDecoderInvalidBitstream;
            }
        }

        /*************Sign sub-packet BEGIN****************************/
        if (picture_header_dynamic->hdr_Fs) {
            len_before_subpkt_bytes = (int)bitstream_reader_get_used_bytes(bitstream);
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t data_w = prec->p_info->b_info[c][b].width;
                const uint32_t ypos = pi->packets[packet_idx].line_idx;
                if (ypos < prec->p_info->b_info[c][b].height) {
                    SvtJxsErrorType_t ret = unpack_sign(bitstream,
                                                        prec->bands[c][b].coeff_data + ypos * pi->components[c].bands[b].width,
                                                        data_w,
                                                        pi->coeff_group_size,
                                                        prec->bands[c][b].leftover_signs_to_read[ypos],
                                                        &precinct_bits_left);
                    if (ret) {
                        if (verbose >= VERBOSE_ERRORS) {
                            fprintf(stderr, "Error: unpack_sign, invalid bitstream!!!\n");
                        }
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                }
            }
            align_bitstream_reader_to_next_byte(bitstream);
            precinct_bits_left -= precinct_bits_left & 7; /*Align left bits to byte.*/
        /*************Sign sub-packet END****************************/
            subpkt_len_bytes = bitstream_reader_get_used_bytes(bitstream) - len_before_subpkt_bytes;
            if (subpkt_len_bytes != ((uint32_t)pkt_header.sign_len)) {
                int32_t leftover = (int32_t)pkt_header.sign_len - subpkt_len_bytes;
                if (leftover > 0 && bitstream_reader_is_enough_bytes(bitstream, leftover)) {
                    if (verbose >= VERBOSE_WARNINGS) {
                        fprintf(stderr, "WARNING: (SIGN) skipped=%d\n", leftover);
                    }
                    bitstream_reader_add_padding(bitstream, leftover);
                }
                else {
                    if (verbose >= VERBOSE_ERRORS) {
                        fprintf(stderr,
                                "Error: (SIGN) corruption detected - unpacked=%d , expected=%d\n",
                                subpkt_len_bytes,
                                pkt_header.sign_len);
                    }
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
            }
        }
    }

    const int32_t padding_len_bytes = (int32_t)precinct_len_bytes -
        ((int32_t)bitstream_reader_get_used_bytes(bitstream) - (int32_t)byte_pos_precinct);
    if (padding_len_bytes < 0) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Error: (PRECINCT) corruption detected size, expected max =%d , get=%d\n",
                    precinct_len_bytes,
                    bitstream_reader_get_used_bytes(bitstream) + (int32_t)byte_pos_precinct);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    bitstream_reader_add_padding(bitstream, padding_len_bytes);
    return SvtJxsErrorNone;
}
