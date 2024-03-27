/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "PackPrecinct.h"
#include "Codestream.h"
#include "SvtUtility.h"
#include "PackHeaders.h"
#include <stdio.h>
#include <assert.h>
#include "encoder_dsp_rtcd.h"

void pack_significance(bitstream_writer_t* bitstream, uint8_t gtli, uint8_t* significance_data_max_ptr, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) {
        /*Flag 0 when used GCLI, 1 when significance group will be empty*/
        write_1_bit(bitstream, (significance_data_max_ptr[i] <= gtli));
    }
}

void pack_significance_compare(bitstream_writer_t* bitstream, uint8_t* significance_flags, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) {
        write_1_bit(bitstream, significance_flags[i]);
    }
}

static void pack_bitplane_count_raw(bitstream_writer_t* bitstream, uint8_t* bitplane, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) {
        write_4_bits_align4(bitstream, bitplane[i]);
    }
}

static void pack_bitplane_count_no_significance(bitstream_writer_t* bitstream, uint8_t* bitplane, uint32_t width, int8_t gtli) {
    for (uint32_t i = 0; i < width; i++) {
        if (bitplane[i] > gtli) {
            vlc_encode_simple(bitstream, bitplane[i] - gtli);
        }
        else {
            vlc_encode_simple(bitstream, 0);
        }
    }
}

static void pack_bitplane_count_vpred_no_significance(bitstream_writer_t* bitstream, uint8_t* bitplane_bits, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) {
        vlc_encode_pack_bits(bitstream, bitplane_bits[i]);
    }
}

static void pack_bitplane_count_vpred_significance(bitstream_writer_t* bitstream, uint8_t* bitplane_bits, uint32_t width,
                                                   uint8_t* significance_flags, int32_t group_size) {
    UNUSED(group_size);
    assert(group_size == SIGNIFICANCE_GROUP_SIZE);
    uint32_t groups = width / SIGNIFICANCE_GROUP_SIZE;
    uint32_t leftover = width % SIGNIFICANCE_GROUP_SIZE;
    for (uint32_t g = 0; g < groups; ++g) {
        if (!significance_flags[g]) {
            for (uint32_t i = 0; i < SIGNIFICANCE_GROUP_SIZE; ++i) {
                vlc_encode_pack_bits(bitstream, bitplane_bits[i]);
            }
        }
        bitplane_bits += SIGNIFICANCE_GROUP_SIZE;
    }
    if (leftover && !significance_flags[groups]) {
        for (uint32_t i = 0; i < leftover; i++) {
            vlc_encode_pack_bits(bitstream, bitplane_bits[i]);
        }
    }
}

static void pack_bitplane_count_significance(bitstream_writer_t* bitstream, uint8_t* bitplane, uint32_t width, int8_t gtli,
                                             uint8_t* significance_data_max_ptr, int32_t group_size) {
    UNUSED(group_size);
    assert(group_size == SIGNIFICANCE_GROUP_SIZE);
    uint32_t groups = width / SIGNIFICANCE_GROUP_SIZE;
    uint32_t leftover = width % SIGNIFICANCE_GROUP_SIZE;
    for (uint32_t g = 0; g < groups; ++g) {
        if (significance_data_max_ptr[g] > gtli) {
            for (uint32_t i = 0; i < SIGNIFICANCE_GROUP_SIZE; ++i) {
                if (bitplane[i] > gtli) {
                    vlc_encode_simple(bitstream, bitplane[i] - gtli);
                }
                else {
                    vlc_encode_simple(bitstream, 0);
                }
            }
        }
        bitplane += SIGNIFICANCE_GROUP_SIZE;
    }
    if (leftover) {
        if (significance_data_max_ptr[groups] > gtli) {
            for (uint32_t i = 0; i < leftover; i++) {
                if (bitplane[i] > gtli) {
                    vlc_encode_simple(bitstream, bitplane[i] - gtli);
                }
                else {
                    vlc_encode_simple(bitstream, 0);
                }
            }
        }
    }
}

void pack_data_single_group_c(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint8_t gcli, uint8_t gtli) {
    uint16_t tmp[4];
    tmp[0] = buf_16bit[0] << ((BITSTREAM_BIT_POSITION_SIGN + 1) - gcli);
    tmp[1] = buf_16bit[1] << ((BITSTREAM_BIT_POSITION_SIGN + 1) - gcli);
    tmp[2] = buf_16bit[2] << ((BITSTREAM_BIT_POSITION_SIGN + 1) - gcli);
    tmp[3] = buf_16bit[3] << ((BITSTREAM_BIT_POSITION_SIGN + 1) - gcli);

    uint16_t val = 0;
    for (int32_t bits = ((int32_t)gcli - gtli - 1); bits >= 0; bits--) {
        val = (tmp[0] & BITSTREAM_MASK_SIGN);
        tmp[0] <<= 1;
        val |= (tmp[1] & BITSTREAM_MASK_SIGN) >> 1;
        tmp[1] <<= 1;
        val |= (tmp[2] & BITSTREAM_MASK_SIGN) >> 2;
        tmp[2] <<= 1;
        val |= (tmp[3] & BITSTREAM_MASK_SIGN) >> 3;
        tmp[3] <<= 1;

        val >>= (BITSTREAM_BIT_POSITION_SIGN - 3); //>>12

        write_4_bits_align4(bitstream, (uint8_t)val);
    }
}

static void pack_data_c(bitstream_writer_t* bitstream, uint16_t* buf_16bit, uint32_t width, uint8_t* gclis, int32_t group_size,
                        uint8_t gtli, uint8_t sign_flag) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    const uint32_t groups = DIV_ROUND_DOWN(width, GROUP_SIZE);
    const uint32_t leftover = width % GROUP_SIZE;
    if (sign_flag == 0) {
        uint32_t group = 0;
        uint8_t signs;
        for (; group < groups; group++) {
            if (gclis[group] > gtli) {
                signs = (buf_16bit[0] & BITSTREAM_MASK_SIGN) >> (BITSTREAM_BIT_POSITION_SIGN - 3);
                signs |= (buf_16bit[1] & BITSTREAM_MASK_SIGN) >> (BITSTREAM_BIT_POSITION_SIGN - 2);
                signs |= (buf_16bit[2] & BITSTREAM_MASK_SIGN) >> (BITSTREAM_BIT_POSITION_SIGN - 1);
                signs |= (buf_16bit[3] & BITSTREAM_MASK_SIGN) >> (BITSTREAM_BIT_POSITION_SIGN - 0);

                write_4_bits_align4(bitstream, signs);

                pack_data_single_group(bitstream, buf_16bit, gclis[group], gtli);
            }
            buf_16bit += GROUP_SIZE;
        }

        if (leftover) {
            if (gclis[group] > gtli) {
                for (uint32_t i = 0; i < GROUP_SIZE; i++) {
                    if (group * GROUP_SIZE + i < width) {
                        write_1_bit(bitstream, (uint8_t)(buf_16bit[i] >> BITSTREAM_BIT_POSITION_SIGN));
                    }
                    else {
                        write_1_bit(bitstream, 0);
                    }
                }
                for (int32_t bits = gclis[group] - 1; bits >= gtli; --bits) {
                    for (int32_t i = 0; i < group_size; i++) {
                        if (group * GROUP_SIZE + i < width) {
                            write_1_bit(bitstream, (uint8_t)(buf_16bit[i] >> bits));
                        }
                        else {
                            write_1_bit(bitstream, 0);
                        }
                    }
                }
            }
        }
    }
    else {
        uint32_t group = 0;
        for (; group < groups; group++) {
            if (gclis[group] > gtli) {
                pack_data_single_group(bitstream, buf_16bit, gclis[group], gtli);
            }
            buf_16bit += GROUP_SIZE;
        }

        if (leftover) {
            if (gclis[group] > gtli) {
                for (int32_t bits = gclis[group] - 1; bits >= gtli; --bits) {
                    for (int32_t i = 0; i < group_size; i++) {
                        if (group * GROUP_SIZE + i < width) {
                            write_1_bit(bitstream, (uint8_t)(buf_16bit[i] >> bits));
                        }
                        else {
                            write_1_bit(bitstream, 0);
                        }
                    }
                }
            }
        }
    }
}

static uint32_t pack_sign_c(bitstream_writer_t* bitstream, uint16_t* buf_16bit, int32_t width, uint8_t* gclis, int32_t group_size,
                            uint8_t gtli) {
    UNUSED(group_size);
    assert(group_size == GROUP_SIZE);
    const uint32_t groups = DIV_ROUND_DOWN(width, GROUP_SIZE);
    const uint32_t leftover = width % GROUP_SIZE;
    uint32_t bits = 0;
    uint32_t group = 0;
    for (; group < groups; group++) {
        if (gclis[group] > gtli) {
            for (int32_t i = 0; i < GROUP_SIZE; i++) {
                assert(buf_16bit[i] != BITSTREAM_MASK_SIGN); //-0 never should happens
                if (buf_16bit[i]) {
                    write_1_bit(bitstream, (uint8_t)(buf_16bit[i] >> BITSTREAM_BIT_POSITION_SIGN));
                    bits++;
                }
            }
        }
        buf_16bit += GROUP_SIZE;
    }

    if (leftover) {
        if (gclis[group] > gtli) {
            for (uint32_t i = 0; i < leftover; i++) {
                assert(buf_16bit[i] != BITSTREAM_MASK_SIGN); //-0 never should happens
                if (buf_16bit[i]) {
                    write_1_bit(bitstream, (uint8_t)(buf_16bit[i] >> BITSTREAM_BIT_POSITION_SIGN));
                    bits++;
                }
            }
        }
    }
    return bits;
}

SvtJxsErrorType_t pack_precinct(bitstream_writer_t* bitstream, pi_t* pi, precinct_enc_t* precinct,
                                SignHandlingStrategy coding_signs_handling) {
    int bits_pos_begin = bitstream_writer_get_used_bits(bitstream);
    const uint8_t sign_flag = !!coding_signs_handling;
    uint32_t precinct_signs_retrieve_bytes = 0;

    /***************Precinct header BEGIN****************/
    uint32_t header_len_bytes = BITS_TO_BYTE_WITH_ALIGN(PRECINCT_HEADER_SIZE_BYTES * 8 + 2 * pi->bands_num_exists);
    uint32_t packet_bytes_size = precinct->pack_total_bytes - header_len_bytes;
    assert(packet_bytes_size <= PRECINCT_MAX_BYTES_SIZE);
    uint32_t offset_packet_bytes_size = bitstream_writer_get_used_bits(bitstream);
    write_24_bits(bitstream, packet_bytes_size);
    write_8_bits(bitstream, precinct->pack_quantization);
    write_8_bits(bitstream, precinct->pack_refinement);

    for (uint32_t band = 0; band < pi->bands_num_all; ++band) {
        if (pi->global_band_info[band].band_id == BAND_NOT_EXIST) {
            continue;
        }
        uint8_t c = pi->global_band_info[band].comp_id;
        uint8_t b = pi->global_band_info[band].band_id;
        uint32_t height_lines = precinct->p_info->b_info[c][b].height;
        uint8_t type_Dpb = 0;
        if (height_lines > 0) {
            CodingMethodBand method_band = precinct->bands[c][b].cache_actual->pack_method;
            switch (method_band) {
            case METHOD_ZERO_SIGNIFICANCE_DISABLE:
                type_Dpb = 0;
                break;
            case METHOD_VPRED_SIGNIFICANCE_DISABLE:
                type_Dpb = CODING_MODE_FLAG_VERTICAL_PRED;
                break;
            case METHOD_ZERO_SIGNIFICANCE_ENABLE:
                type_Dpb = CODING_MODE_FLAG_SIGNIFICANCE;
                break;
            case METHOD_VPRED_SIGNIFICANCE_ENABLE:
                type_Dpb = CODING_MODE_FLAG_SIGNIFICANCE | CODING_MODE_FLAG_VERTICAL_PRED;
                break;
            default:
                assert(0);
            }
        }                                  //else if band not exist in this precinct write 0
        write_2_bits(bitstream, type_Dpb); //D[p,b] Bit-plane count coding mode of band b
    }
    align_bitstream_writer_to_next_byte(bitstream);
    /***************Precinct header END****************/

    for (uint32_t packet_idx = 0; packet_idx < pi->packets_num; packet_idx++) {
        /***************Packet header BEGIN****************/
        // Packet header exist if at least one band is included, for 420 some bands does not exist
        // Empty packets can happen for last precinct in slice
        int8_t skip_packet = 1;
        uint32_t offset_sign_size_bytes = 0;
        for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop; band_idx++) {
            assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
            const uint8_t b = pi->global_band_info[band_idx].band_id;
            const uint8_t c = pi->global_band_info[band_idx].comp_id;
            const uint8_t line_idx = (uint8_t)pi->packets[packet_idx].line_idx;
            uint32_t height_lines = precinct->p_info->b_info[c][b].height;
            if (line_idx < height_lines) {
                //packet_header(p,s) Begin
                offset_sign_size_bytes = write_packet_header(bitstream,
                                                             !pi->use_short_header,
                                                             precinct->packet_methods_raw[packet_idx],
                                                             (uint64_t)precinct->packet_size_data_bytes[packet_idx],
                                                             (uint64_t)precinct->packet_size_gcli_bytes[packet_idx],
                                                             (uint64_t)precinct->packet_size_signs_handling_bytes[packet_idx]);

                align_bitstream_writer_to_next_byte(bitstream);
                //packet_header(p,s) End

                skip_packet = 0;
                break;
            }
        }
        if (skip_packet) {
            continue;
        }
        /***************Packet header END****************/

        if (precinct->packet_methods_raw[packet_idx]) {
            //RAW CODING
            int bits_pos_last = bitstream_writer_get_used_bits(bitstream);
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t line_idx = pi->packets[packet_idx].line_idx;
                struct band_data_enc* band = &(precinct->bands[c][b]);
                uint32_t height_lines = precinct->p_info->b_info[c][b].height;
                if (line_idx < height_lines) {
                    pack_bitplane_count_raw(
                        bitstream, band->lines_common[line_idx].gcli_data_ptr, precinct->p_info->b_info[c][b].gcli_width);
                }
            }
            align_bitstream_writer_to_next_byte(bitstream);
            if (((int)bitstream_writer_get_used_bits(bitstream) - bits_pos_last) !=
                (int)(precinct->packet_size_gcli_bytes[packet_idx] << 3)) {
                fprintf(stderr,
                        "Error[%s:%i]: Pack Raw Coding length mismatch: rate control=%d packing=%d\n",
                        __FUNCTION__,
                        __LINE__,
                        precinct->packet_size_gcli_bytes[packet_idx] << 3,
                        bitstream_writer_get_used_bits(bitstream) - bits_pos_last);
                return SvtJxsErrorEncodeFrameError;
            }
        }
        else {
            //NORMAL CODING
            /***************Significance sub-packet BEGIN****************/
            int bits_pos_last = bitstream_writer_get_used_bits(bitstream);
            //This sub-packet is optional. It is only included if bit #1 of the D[p,b] field of the precinct header is set to 1 and
            //the raw mode override flag Dr[p,s] is set to 0.
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t line_idx = pi->packets[packet_idx].line_idx;
                uint32_t height_lines = precinct->p_info->b_info[c][b].height;
                if (line_idx < height_lines) {
                    struct band_data_enc* band = &(precinct->bands[c][b]);
                    struct band_data_enc_cache* band_cache = band->cache_actual;
                    CodingMethodBand method_band = band_cache->pack_method;
                    if (method_band == METHOD_ZERO_SIGNIFICANCE_ENABLE) {
                        uint32_t significance_width = precinct->p_info->b_info[c][b].significance_width;
                        pack_significance(
                            bitstream, band->gtli, band->lines_common[line_idx].significance_data_max_ptr, significance_width);
                    }
                    else if (method_band == METHOD_VPRED_SIGNIFICANCE_ENABLE) {
                        uint32_t significance_width = precinct->p_info->b_info[c][b].significance_width;
                        pack_significance_compare(bitstream, band_cache->lines[line_idx].vpred_significance, significance_width);
                    }
                }
            }
            align_bitstream_writer_to_next_byte(bitstream);
            if (((int)bitstream_writer_get_used_bits(bitstream) - bits_pos_last) !=
                (int)(precinct->packet_size_significance_bytes[packet_idx] << 3)) {
                fprintf(stderr,
                        "Error[%s:%i]: Pack Significance length mismatch: rate control=%d packing=%d\n",
                        __FUNCTION__,
                        __LINE__,
                        (precinct->packet_size_significance_bytes[packet_idx] << 3),
                        bitstream_writer_get_used_bits(bitstream) - bits_pos_last);
                return SvtJxsErrorEncodeFrameError;
            }
            /***************Significance sub-packet END****************/

            /*************BITPLANE COUNT sub-packet BEGIN****************************/
            bits_pos_last = bitstream_writer_get_used_bits(bitstream);

            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t line_idx = pi->packets[packet_idx].line_idx;
                uint32_t height_lines = precinct->p_info->b_info[c][b].height;
                if (line_idx < height_lines) {
                    struct band_data_enc* band = &(precinct->bands[c][b]);
                    struct band_data_enc_cache* band_cache = band->cache_actual;
                    CodingMethodBand method_band = band_cache->pack_method;
                    uint32_t gcli_width = precinct->p_info->b_info[c][b].gcli_width;
                    if (method_band == METHOD_ZERO_SIGNIFICANCE_ENABLE) {
                        pack_bitplane_count_significance(bitstream,
                                                         band->lines_common[line_idx].gcli_data_ptr,
                                                         gcli_width,
                                                         band->gtli,
                                                         band->lines_common[line_idx].significance_data_max_ptr,
                                                         pi->significance_group_size);
                    }
                    else if (method_band == METHOD_ZERO_SIGNIFICANCE_DISABLE) {
                        pack_bitplane_count_no_significance(
                            bitstream, band->lines_common[line_idx].gcli_data_ptr, gcli_width, band->gtli);
                    }
                    else if (method_band == METHOD_VPRED_SIGNIFICANCE_DISABLE) {
                        pack_bitplane_count_vpred_no_significance(
                            bitstream, band_cache->lines[line_idx].vpred_bits_pack, gcli_width);
                    }
                    else if (method_band == METHOD_VPRED_SIGNIFICANCE_ENABLE) {
                        pack_bitplane_count_vpred_significance(bitstream,
                                                               band_cache->lines[line_idx].vpred_bits_pack,
                                                               gcli_width,
                                                               band_cache->lines[line_idx].vpred_significance,
                                                               pi->significance_group_size);
                    }
                    else {
                        assert(0);
                    }
                }
            }
            align_bitstream_writer_to_next_byte(bitstream);
            if (((int)bitstream_writer_get_used_bits(bitstream) - bits_pos_last) !=
                (int)(precinct->packet_size_gcli_bytes[packet_idx] << 3)) {
                fprintf(stderr,
                        "Error[%s:%i]: Pack bit-plane count length mismatch: rate control=%d packing=%d\n",
                        __FUNCTION__,
                        __LINE__,
                        precinct->packet_size_gcli_bytes[packet_idx] << 3,
                        bitstream_writer_get_used_bits(bitstream) - bits_pos_last);
                return SvtJxsErrorEncodeFrameError;
            }
            /*************BITPLANE COUNT sub-packet END****************************/
        }

        /*************Data sub-packet BEGIN****************************/
        int bits_pos_last = bitstream_writer_get_used_bits(bitstream);

        for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop; band_idx++) {
            assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
            const uint32_t b = pi->global_band_info[band_idx].band_id;
            const uint32_t c = pi->global_band_info[band_idx].comp_id;
            const uint32_t line_idx = pi->packets[packet_idx].line_idx;
            uint32_t height_lines = precinct->p_info->b_info[c][b].height;
            if (line_idx < height_lines) {
                struct band_data_enc* band = &(precinct->bands[c][b]);
                pack_data_c(bitstream,
                            band->lines_common[line_idx].coeff_data_ptr_16bit,
                            precinct->p_info->b_info[c][b].width,
                            band->lines_common[line_idx].gcli_data_ptr,
                            pi->coeff_group_size,
                            band->gtli,
                            sign_flag);
            }
        }
        align_bitstream_writer_to_next_byte(bitstream);
        if (((int)bitstream_writer_get_used_bits(bitstream) - bits_pos_last) !=
            (int)(precinct->packet_size_data_bytes[packet_idx] << 3)) {
            fprintf(stderr,
                    "Error[%s:%i]: Pack %i data length mismatch: rate control=%d packing=%d\n",
                    __FUNCTION__,
                    __LINE__,
                    packet_idx,
                    precinct->packet_size_data_bytes[packet_idx] << 3,
                    bitstream_writer_get_used_bits(bitstream) - bits_pos_last);
            return SvtJxsErrorEncodeFrameError;
        }
        /*************Data sub-packet END****************************/

        /*************Sign sub-packet BEGIN****************************/
        if (sign_flag) {
            uint32_t packet_signs_size_bits = 0;
            int bits_pos_last = bitstream_writer_get_used_bits(bitstream);
            for (uint32_t band_idx = pi->packets[packet_idx].band_start; band_idx < pi->packets[packet_idx].band_stop;
                 band_idx++) {
                assert(pi->global_band_info[band_idx].band_id != BAND_NOT_EXIST);
                const uint32_t b = pi->global_band_info[band_idx].band_id;
                const uint32_t c = pi->global_band_info[band_idx].comp_id;
                const uint32_t line_idx = pi->packets[packet_idx].line_idx;
                struct band_data_enc* band = &(precinct->bands[c][b]);
                uint32_t height_lines = precinct->p_info->b_info[c][b].height;
                if (line_idx < height_lines) {
                    packet_signs_size_bits += pack_sign_c(bitstream,
                                                          band->lines_common[line_idx].coeff_data_ptr_16bit,
                                                          precinct->p_info->b_info[c][b].width,
                                                          band->lines_common[line_idx].gcli_data_ptr,
                                                          pi->coeff_group_size,
                                                          band->gtli);
                }
            }
            align_bitstream_writer_to_next_byte(bitstream);
            uint32_t packet_signs_size_bytes = BITS_TO_BYTE_WITH_ALIGN(packet_signs_size_bits);

            if (SIGN_HANDLING_STRATEGY_FULL == coding_signs_handling) {
                if (((int)bitstream_writer_get_used_bits(bitstream) - bits_pos_last) !=
                    (int)(precinct->packet_size_signs_handling_bytes[packet_idx] << 3)) {
                    fprintf(stderr,
                            "Error[%s:%i]: Pack %i SIGN length mismatch: rate control=%d packing=%d\n",
                            __FUNCTION__,
                            __LINE__,
                            packet_idx,
                            precinct->packet_size_signs_handling_bytes[packet_idx] << 3,
                            bitstream_writer_get_used_bits(bitstream) - bits_pos_last);
                    return SvtJxsErrorEncodeFrameError;
                }
            }
            else {
                //Lazy calculate PACK
                assert(SIGN_HANDLING_STRATEGY_FAST == coding_signs_handling);
                if (packet_signs_size_bytes > precinct->packet_size_signs_handling_bytes[packet_idx]) {
                    fprintf(stderr,
                            "Error[%s:%i]: Pack %i SIGN length too big: rate control=%d packing=%d\n",
                            __FUNCTION__,
                            __LINE__,
                            packet_idx,
                            precinct->packet_size_signs_handling_bytes[packet_idx],
                            packet_signs_size_bytes);
                    return SvtJxsErrorEncodeFrameError;
                }
                /*Update packet header sings size:*/
                if (pi->use_short_header) {
                    //11 bits
                    update_N_bits(bitstream, offset_sign_size_bytes, packet_signs_size_bytes, 11);
                }
                else {
                    //15 bits
                    update_N_bits(bitstream, offset_sign_size_bytes, packet_signs_size_bytes, 15);
                }
                precinct_signs_retrieve_bytes += precinct->packet_size_signs_handling_bytes[packet_idx] - packet_signs_size_bytes;
            }
        }
        /*************Sign sub-packet END****************************/
    }

    int bits_pos_end = bitstream_writer_get_used_bits(bitstream);
    if (bits_pos_end - bits_pos_begin !=
        (int)((precinct->pack_total_bytes - precinct->pack_padding_bytes - precinct_signs_retrieve_bytes) << 3)) {
        fprintf(stderr,
                "Error[%s:%i]: Precinct size invalid  Precinct=%d expected=%d\n",
                __FUNCTION__,
                __LINE__,
                bits_pos_end - bits_pos_begin,
                (int)((precinct->pack_total_bytes - precinct->pack_padding_bytes) << 3));
        return SvtJxsErrorEncodeFrameError;
    }
    if (bits_pos_end % 8) {
        fprintf(stderr, "Error[%s:%i]: Precinct invalid alignment to 8 bits\n", __FUNCTION__, __LINE__);
        return SvtJxsErrorEncodeFrameError;
    }

    if (SIGN_HANDLING_STRATEGY_FAST == coding_signs_handling) {
        //Add padding from retrieve bytes in Lazy sign coding
        precinct->pack_signs_handling_fast_retrieve_bytes = precinct_signs_retrieve_bytes;
        if (precinct->pack_signs_handling_cut_precing) {
            //Update Header
            update_N_bits(bitstream, offset_packet_bytes_size, packet_bytes_size - precinct_signs_retrieve_bytes, 24);
        }
        else {
            bitstream_writer_add_padding_bytes(bitstream, precinct_signs_retrieve_bytes);
        }
    }

    if (precinct->pack_padding_bytes) {
        bitstream_writer_add_padding_bytes(bitstream, precinct->pack_padding_bytes);
    }
    return SvtJxsErrorNone;
}
