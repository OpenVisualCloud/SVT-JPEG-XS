/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "ParseHeader.h"
#include "BitstreamReader.h"
#include "SvtJpegxsDec.h"
#include "Pi.h"
#include "Codestream.h"
#include "SvtJpegxsDec.h"

void get_packet_header(bitstream_reader_t* bitstream, int32_t use_long_header, packet_header_t* out_pkt) {
    const uint8_t* mem = (const uint8_t*)bitstream->mem + bitstream->offset;
    //Read 1 bit
    out_pkt->raw_mode_flag = mem[0] >> 7;

    if (use_long_header) {
        //Read next 20 bits
        out_pkt->data_len = (((uint32_t)mem[0] & 0x7F) << 13) | ((uint32_t)mem[1] << 5) | (((uint32_t)mem[2] >> 3) & 0x1F);
        //Read next 20 bits
        out_pkt->gcli_len = (((uint32_t)mem[2] & 0x07) << 17) | ((uint32_t)mem[3] << 9) | ((uint32_t)mem[4] << 1) |
            ((uint32_t)mem[5] >> 7);
        //Read next 15 bits
        out_pkt->sign_len = (((uint32_t)mem[5] & 0x7F) << 8) | mem[6];
        bitstream->offset += 7;
    }
    else {
        //Read next 15 bits
        out_pkt->data_len = (((uint32_t)mem[0] & 0x7F) << 8) | (uint32_t)mem[1];
        //Read next 13 bits
        out_pkt->gcli_len = ((((uint32_t)mem[2] << 3) >> 2) << 4) | (mem[3] >> 3);
        //Read next 11 bits
        out_pkt->sign_len = (((uint32_t)mem[3] & 0x07) << 8) | mem[4];
        bitstream->offset += 5;
    }
}

SvtJxsErrorType_t get_tail(bitstream_reader_t* bitstream) {
    if (!bitstream_reader_is_enough_bytes(bitstream, 2)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    uint16_t val = read_16_bits(bitstream);
    if (val != CODESTREAM_EOC) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_slice_header(bitstream_reader_t* bitstream, uint16_t* slice_idx) {
    if (!bitstream_reader_is_enough_bytes(bitstream, 6)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    uint16_t val = read_16_bits(bitstream);
    if (val != CODESTREAM_SLH) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    val = read_16_bits(bitstream);
    if (val != 4) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    *slice_idx = read_16_bits(bitstream);
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_capabilities(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                                   uint32_t verbose) {
    if (picture_header_const->marker_exist_CAP) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unexpected Capabilities marker!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_const->marker_exist_CAP = 1;

    if (!bitstream_reader_is_enough_bytes(bitstream, 4)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint16_t val = read_16_bits(bitstream);
    if (val != CODESTREAM_CAP) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Capabilities marker not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    uint16_t size_bytes = read_16_bits(bitstream);
    size_bytes -= 2;
    if (size_bytes * 8 > MAX_CAPABILITY) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (!bitstream_reader_is_enough_bytes(bitstream, size_bytes)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    for (int32_t i = 0; i < size_bytes * 8; i++) {
        picture_header_const->hdr_capability[i] = read_1_bit(bitstream);
    }

    align_bitstream_reader_to_next_byte(bitstream);

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_picture_header(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                                     picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose) {
    if (picture_header_const->marker_exist_CAP == 0) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Capabilities marker Not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    if (picture_header_const->marker_exist_PIH) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unexpected Picture header!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_const->marker_exist_PIH = 1;

    if (!bitstream_reader_is_enough_bytes(bitstream, PICTURE_HEADER_SIZE_BYTES + 2)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    uint16_t val = read_16_bits(bitstream);
    if (val != CODESTREAM_PIH) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Picture header marker not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    uint16_t size_bytes = read_16_bits(bitstream);
    if (size_bytes != PICTURE_HEADER_SIZE_BYTES) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Picture header size invalid, expected=26, read=%d!\n", size_bytes);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    uint8_t val_array[4] = {0};

    picture_header_dynamic->hdr_Lcod = read_32_bits(bitstream);
    picture_header_const->hdr_Ppih = read_16_bits(bitstream);
    picture_header_const->hdr_Plev = read_16_bits(bitstream);
    picture_header_const->hdr_width = read_16_bits(bitstream);
    picture_header_const->hdr_height = read_16_bits(bitstream);
    picture_header_const->hdr_precinct_width = read_16_bits(bitstream);
    picture_header_const->hdr_Hsl = read_16_bits(bitstream);
    if (picture_header_const->hdr_Hsl < 1) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Picture header invalid Height of a slice in precincts, read=%d!\n", picture_header_const->hdr_Hsl);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_const->hdr_comps_num = read_8_bits(bitstream);
    if (picture_header_const->hdr_comps_num < 1 || picture_header_const->hdr_comps_num > 8) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Picture header invalid number of components expected 1-8, read=%d!\n",
                    picture_header_const->hdr_comps_num);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_const->hdr_coeff_group_size = read_8_bits(bitstream);
    if (picture_header_const->hdr_coeff_group_size != 4) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(
                stderr, "Picture header invalid coeff group expected 4, read=%d!\n", picture_header_const->hdr_coeff_group_size);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_const->hdr_significance_group_size = read_8_bits(bitstream);
    if (picture_header_const->hdr_significance_group_size != 8) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Picture header invalid coeff group expected 8, read=%d!\n",
                    picture_header_const->hdr_significance_group_size);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_dynamic->hdr_Bw = read_8_bits(bitstream);

    read_2x4_bits(bitstream, val_array);
    picture_header_dynamic->hdr_Fq = val_array[0];
    picture_header_dynamic->hdr_Br = val_array[1];
    if (!((picture_header_dynamic->hdr_Fq == 0) ||
          (picture_header_dynamic->hdr_Bw == 18 && picture_header_dynamic->hdr_Fq == 6) ||
          (picture_header_dynamic->hdr_Bw == 20 && picture_header_dynamic->hdr_Fq == 8))) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_dynamic->hdr_Br != 4) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Picture header invalid Br expected 4, read=%d!\n", picture_header_dynamic->hdr_Br);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    read_134_bits(bitstream, val_array);
    picture_header_dynamic->hdr_Fslc = val_array[0];
    if (picture_header_dynamic->hdr_Fslc != 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_dynamic->hdr_Ppoc = val_array[1];
    if (picture_header_dynamic->hdr_Ppoc != 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_const->hdr_Cpih = val_array[2];
    if (picture_header_const->hdr_Cpih != 0 && picture_header_const->hdr_Cpih != 1 && picture_header_const->hdr_Cpih != 3) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    read_2x4_bits(bitstream, val_array);
    picture_header_const->hdr_decom_h = (uint8_t)val_array[0];
    picture_header_const->hdr_decom_v = (uint8_t)val_array[1];

    read_2222_bits(bitstream, val_array);
    picture_header_dynamic->hdr_Lh = val_array[0] >> 1;
    picture_header_dynamic->hdr_Rl = val_array[0] & 1;
    picture_header_dynamic->hdr_Qpih = val_array[1];
    if (picture_header_dynamic->hdr_Qpih > 1) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_dynamic->hdr_Fs = val_array[2];
    if (picture_header_dynamic->hdr_Fs > 1) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    picture_header_dynamic->hdr_Rm = val_array[3];
    if (picture_header_dynamic->hdr_Rm > 1) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_component_table(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                                      uint32_t verbose) {
    uint8_t val_array[2] = {0};

    if (picture_header_const->marker_exist_CAP == 0) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Capabilities marker Not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_const->marker_exist_PIH == 0) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Picture header Not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_const->marker_exist_CDT) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unexpected Component table!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_const->marker_exist_CDT = 1;

    if (!bitstream_reader_is_enough_bytes(bitstream, picture_header_const->hdr_comps_num * 2 + 2)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint16_t size_bytes = read_16_bits(bitstream);
    size_bytes -= 2;
    if (size_bytes != 2 * picture_header_const->hdr_comps_num) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_const->hdr_comps_num > MAX_COMPONENTS_NUM ||
        picture_header_const->hdr_comps_num >
            sizeof(picture_header_const->hdr_bit_depth) / sizeof(picture_header_const->hdr_bit_depth[0]) ||
        picture_header_const->hdr_comps_num > sizeof(picture_header_const->hdr_Sx) / sizeof(picture_header_const->hdr_Sx[0]) ||
        picture_header_const->hdr_comps_num > sizeof(picture_header_const->hdr_Sy) / sizeof(picture_header_const->hdr_Sy[0])) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Not supported number of components! Max: %u\n", MAX_COMPONENTS_NUM);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    for (int32_t c = 0; c < picture_header_const->hdr_comps_num; c++) {
        picture_header_const->hdr_bit_depth[c] = read_8_bits(bitstream);
        if (picture_header_const->hdr_bit_depth[c] < 8 || picture_header_const->hdr_bit_depth[c] > 16) {
            return SvtJxsErrorDecoderInvalidBitstream;
        }
        read_2x4_bits(bitstream, val_array);
        picture_header_const->hdr_Sx[c] = val_array[0];
        picture_header_const->hdr_Sy[c] = val_array[1];
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_weights_table(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                                    uint32_t verbose) {
    if (picture_header_const->marker_exist_CAP == 0) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Capabilities marker Not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_const->marker_exist_PIH == 0) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Picture header Not found!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_const->marker_exist_WGT) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unexpected Component table!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_const->marker_exist_WGT = 1;

    if (!bitstream_reader_is_enough_bytes(bitstream, 2)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint16_t size_bytes = read_16_bits(bitstream);
    size_bytes -= 2;
    uint32_t bands_exist = size_bytes / 2;

    if (size_bytes & 1) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Invalid Odd bytes in weight table !\n");
        }
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    //When value is smaller than 2 then overflow and detect invalid number
    if (bands_exist > MAX_BANDS_NUM ||
        bands_exist > sizeof(picture_header_const->hdr_gain) / sizeof(picture_header_const->hdr_gain[0]) ||
        bands_exist > sizeof(picture_header_const->hdr_priority) / sizeof(picture_header_const->hdr_priority[0])) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Not supported elements in weight table! Max: %u\n", MAX_BANDS_NUM);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (!bitstream_reader_is_enough_bytes(bitstream, size_bytes)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    for (uint32_t b = 0; b < bands_exist; b++) {
        picture_header_const->hdr_gain[b] = read_8_bits(bitstream);
        picture_header_const->hdr_priority[b] = read_8_bits(bitstream);
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_nonlinearity(bitstream_reader_t* bitstream, picture_header_dynamic_t* picture_header_dynamic,
                                   uint32_t verbose) {
    if (!bitstream_reader_is_enough_bytes(bitstream, 3)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint16_t size_bytes = read_16_bits(bitstream);
    UNUSED(size_bytes);
    picture_header_dynamic->hdr_Tnlt = read_8_bits(bitstream);
    if (picture_header_dynamic->hdr_Tnlt == 1) {
        if (!bitstream_reader_is_enough_bytes(bitstream, 2)) {
            return SvtJxsErrorDecoderBitstreamTooShort;
        }
        uint16_t val = read_16_bits(bitstream);
        picture_header_dynamic->hdr_Tnlt_sigma = val >> 15;
        picture_header_dynamic->hdr_Tnlt_alpha = val & 0x7FFF;
    }
    else if (picture_header_dynamic->hdr_Tnlt == 2) {
        if (!bitstream_reader_is_enough_bytes(bitstream, 9)) {
            return SvtJxsErrorDecoderBitstreamTooShort;
        }
        picture_header_dynamic->hdr_Tnlt_t1 = read_32_bits(bitstream);
        picture_header_dynamic->hdr_Tnlt_t2 = read_32_bits(bitstream);
        picture_header_dynamic->hdr_Tnlt_e = read_8_bits(bitstream);
    }
    else {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unrecognized nonlinearity type=%d\n", picture_header_dynamic->hdr_Tnlt);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_cwd(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const) {
    if (!bitstream_reader_is_enough_bytes(bitstream, 3)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint16_t size_bytes = read_16_bits(bitstream);
    if (size_bytes != 3) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_const->hdr_Sd = read_8_bits(bitstream);
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_cts(bitstream_reader_t* bitstream, picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose) {
    if (picture_header_dynamic->marker_exist_CTS) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unexpected Colour transformation specification marker!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_dynamic->marker_exist_CTS = 1;

    if (!bitstream_reader_is_enough_bytes(bitstream, 4)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint8_t val_array[2] = {0};
    uint16_t size_bytes = read_16_bits(bitstream);
    if (size_bytes != 4) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    read_2x4_bits(bitstream, val_array);
    //val_array[0] - Reserved for ISO/IEC purposes
    picture_header_dynamic->hdr_Cf = val_array[1];

    read_2x4_bits(bitstream, val_array);
    picture_header_dynamic->hdr_Cf_e1 = val_array[0];
    picture_header_dynamic->hdr_Cf_e2 = val_array[1];
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_crg(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                          picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose) {
    if (picture_header_dynamic->marker_exist_CRG) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Unexpected Component registration marker!\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    picture_header_dynamic->marker_exist_CRG = 1;

    if (!bitstream_reader_is_enough_bytes(bitstream, 2 + picture_header_const->hdr_comps_num * 4)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint16_t size_bytes = read_16_bits(bitstream);
    if (size_bytes != 2 + picture_header_const->hdr_comps_num * 4) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    if (picture_header_const->hdr_comps_num > MAX_COMPONENTS_NUM ||
        picture_header_const->hdr_comps_num >
            sizeof(picture_header_dynamic->hdr_Xcrg) / sizeof(picture_header_dynamic->hdr_Xcrg[0]) ||
        picture_header_const->hdr_comps_num >
            sizeof(picture_header_dynamic->hdr_Ycrg) / sizeof(picture_header_dynamic->hdr_Ycrg[0])) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Not supported number of components! Max: %u\n", MAX_COMPONENTS_NUM);
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    for (int32_t c = 0; c < picture_header_const->hdr_comps_num; c++) {
        picture_header_dynamic->hdr_Xcrg[c] = read_16_bits(bitstream);
        picture_header_dynamic->hdr_Ycrg[c] = read_16_bits(bitstream);
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_extension(bitstream_reader_t* bitstream) {
    if (!bitstream_reader_is_enough_bytes(bitstream, 4)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    uint8_t val;
    uint16_t size_bytes = read_16_bits(bitstream);
    uint16_t Tcom = read_16_bits(bitstream);
    UNUSED(Tcom);
    size_bytes -= 4;

    if (!bitstream_reader_is_enough_bytes(bitstream, size_bytes)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }
    //Read all user-defined data
    for (uint32_t i = 0; i < size_bytes; i++) {
        val = read_8_bits(bitstream);
        UNUSED(val);
    }
    return SvtJxsErrorNone;
}

SvtJxsErrorType_t get_header(bitstream_reader_t* bitstream, picture_header_const_t* picture_header_const,
                             picture_header_dynamic_t* picture_header_dynamic, uint32_t verbose) {
    assert(picture_header_const != NULL);
    assert(bitstream != NULL);
    uint16_t val;
    picture_header_dynamic_t picture_header_dynamic_tmp;
    if (picture_header_dynamic == NULL) {
        picture_header_dynamic = &picture_header_dynamic_tmp;
    }
    memset(picture_header_dynamic, 0, sizeof(picture_header_dynamic_t));

    if (!bitstream_reader_is_enough_bytes(bitstream, 2)) {
        return SvtJxsErrorDecoderBitstreamTooShort;
    }

    //TODO: Check bitstream size!!!
    val = read_16_bits(bitstream);
    if (val != CODESTREAM_SOC) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr, "Start of codestream marker not found\n");
        }
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    SvtJxsErrorType_t ret = get_capabilities(bitstream, picture_header_const, verbose);
    if (ret) {
        return ret;
    }
    ret = get_picture_header(bitstream, picture_header_const, picture_header_dynamic, verbose);
    if (ret) {
        return ret;
    }

    do {
        if (!bitstream_reader_is_enough_bytes(bitstream, 2)) {
            return SvtJxsErrorDecoderBitstreamTooShort;
        }
        val = read_16_bits(bitstream);
        switch (val) {
        case CODESTREAM_CDT:
            ret = get_component_table(bitstream, picture_header_const, verbose);
            break;
        case CODESTREAM_WGT:
            ret = get_weights_table(bitstream, picture_header_const, verbose);
            break;
        case CODESTREAM_NLT:
            ret = get_nonlinearity(bitstream, picture_header_dynamic, verbose);
            break;
        case CODESTREAM_CWD:
            ret = get_cwd(bitstream, picture_header_const);
            break;
        case CODESTREAM_CTS:
            ret = get_cts(bitstream, picture_header_dynamic, verbose);
            break;
        case CODESTREAM_CRG:
            ret = get_crg(bitstream, picture_header_const, picture_header_dynamic, verbose);
            break;
        case CODESTREAM_COM:
            ret = get_extension(bitstream); //Ignore data, only parse
            break;
        case CODESTREAM_SLH:
            // Revert bitstream to Slice marker
            bitstream->offset -= 2;

            if (picture_header_dynamic->hdr_Fq == 0) {
                //lossless coding
                for (int32_t c = 1; c < picture_header_const->hdr_comps_num; c++) {
                    if (picture_header_const->hdr_bit_depth[0] != picture_header_const->hdr_bit_depth[c]) {
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                }
            }

            if (picture_header_const->marker_exist_CAP == 0 || picture_header_const->marker_exist_PIH == 0 ||
                picture_header_const->marker_exist_CDT == 0 || picture_header_const->marker_exist_WGT == 0) {
                if (verbose >= VERBOSE_ERRORS) {
                    fprintf(stderr, "Mandatory markers not found!\n");
                }
                return SvtJxsErrorDecoderInvalidBitstream;
            }

            if (picture_header_const->hdr_Cpih) {
                for (int32_t c = 1; c < picture_header_const->hdr_comps_num; c++) {
                    if (picture_header_const->hdr_Sx[c] != picture_header_const->hdr_Sx[0] ||
                        picture_header_const->hdr_Sy[c] != picture_header_const->hdr_Sy[0]) {
                        if (verbose >= VERBOSE_ERRORS) {
                            fprintf(stderr, "Invalid YUV format for filter Color Transform!\n");
                        }
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                }
            }

            if (picture_header_const->hdr_Cpih == 3) {
                if (picture_header_dynamic->marker_exist_CTS == 0 || picture_header_dynamic->marker_exist_CRG == 0) {
                    if (verbose >= VERBOSE_ERRORS) {
                        fprintf(stderr, "Mandatory markers not found!\n");
                    }
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
            }

            return SvtJxsErrorNone;
        default:
            if (verbose >= VERBOSE_ERRORS) {
                fprintf(stderr, "Unrecognized marker found!\n");
            }
            return SvtJxsErrorDecoderInvalidBitstream;
        }
        if (ret) {
            return ret;
        }
    } while (1);
    return SvtJxsErrorDecoderInvalidBitstream;
}

static uint8_t get_8_bits(const uint8_t* buf) {
    return buf[0];
}

static uint16_t get_16_bits(const uint8_t* buf) {
    uint16_t ret_val = (uint16_t)buf[0] << 8;
    return ret_val | buf[1];
}

static uint32_t get_32_bits(const uint8_t* mem) {
    unsigned int val;
    val = ((unsigned int)mem[0]) << 24;
    val |= mem[1] << 16;
    val |= mem[2] << 8;
    val |= mem[3];
    return val;
}

SvtJxsErrorType_t static_get_single_frame_size(const uint8_t* bitstream_buf, size_t bitstream_buf_size,
                                               svt_jpeg_xs_image_config_t* out_image_config, uint32_t* frame_size,
                                               uint32_t fast_search, proxy_mode_t proxy_mode) {
    if (frame_size == NULL) {
        return SvtJxsErrorDecoderInvalidPointer;
    }
    if (proxy_mode >= proxy_mode_max) {
        return SvtJxsErrorBadParameter;
    }
    *frame_size = 0;

    int32_t comps_num = -1;
    int32_t Sd = 0;
    int32_t decomp_h = -1;
    int32_t decomp_v = -1;
    uint32_t Sx[8] = {99};
    uint32_t Sy[8] = {99};
    int32_t bands_num_exists = -1;
    uint32_t expected_codestream_size = 0;

    uint32_t offset_bytes = 0;
    uint32_t header_size = 0;
    uint16_t marker = 0;
    uint32_t precinct_headers_begin = 0;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bit_depth = 0;

    if (out_image_config) {
        memset(out_image_config, 0, sizeof(*out_image_config));
    }

    if (bitstream_buf == NULL || bitstream_buf_size == 0) {
        //end of stream
        return SvtJxsErrorDecoderInvalidPointer;
    }

    do {
        if (offset_bytes + 2 > bitstream_buf_size) {
            // return frame not full
            return SvtJxsErrorDecoderBitstreamTooShort;
        }
        else {
            marker = get_16_bits(bitstream_buf + offset_bytes);
        }
        switch (marker) {
        case CODESTREAM_SOC:
            offset_bytes += 2;
            break;
        case CODESTREAM_PIH:
            if (precinct_headers_begin != 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }

            if (offset_bytes + 28 > bitstream_buf_size) {
                // return frame not full
                return SvtJxsErrorDecoderBitstreamTooShort;
            }

            header_size = get_16_bits(bitstream_buf + offset_bytes + 2);
            if (offset_bytes + header_size + 2 > bitstream_buf_size) {
                // return frame not full1`
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            //Lcod - Size of the entire codestream in bytes from SOC to EOC,
            //       including all markers, if constant bitrate coding is used.
            //       0 if variable bitrate coding is used
            expected_codestream_size = get_32_bits(bitstream_buf + offset_bytes + 4);
            width = get_16_bits(bitstream_buf + offset_bytes + 12);
            height = get_16_bits(bitstream_buf + offset_bytes + 14);
            comps_num = get_8_bits(bitstream_buf + offset_bytes + 20);
            decomp_h = get_8_bits(bitstream_buf + offset_bytes + 26) >> 4;
            decomp_v = get_8_bits(bitstream_buf + offset_bytes + 26) & 0xF;
            offset_bytes += 2 + header_size;
            break;
        case CODESTREAM_CWD:
            if (precinct_headers_begin != 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            if (offset_bytes + 4 > bitstream_buf_size) {
                // return frame not full
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            header_size = get_16_bits(bitstream_buf + offset_bytes + 2);
            Sd = get_8_bits(bitstream_buf + offset_bytes + 4);
            offset_bytes += 2 + header_size;
            break;
        case CODESTREAM_CDT:
            if (precinct_headers_begin != 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            if (offset_bytes + 4 > bitstream_buf_size) {
                // return frame not full
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            header_size = get_16_bits(bitstream_buf + offset_bytes + 2);
            if (offset_bytes + header_size + 2 > bitstream_buf_size) {
                // return frame not full
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            if (comps_num < 1 || comps_num > 8) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            uint32_t offset = offset_bytes + 4;

            if ((uint32_t)(2 * comps_num + 2) > header_size) {
                // return frame not full
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            for (int32_t c = 0; c < comps_num; c++) {
                if (c == 0) {
                    bit_depth = get_8_bits(bitstream_buf + offset);
                }
                Sx[c] = get_8_bits(bitstream_buf + offset + 1) >> 4;
                Sy[c] = get_8_bits(bitstream_buf + offset + 1) & 0xF;
                offset += 2;
            }
            offset_bytes += 2 + header_size;
            break;
        case CODESTREAM_CAP:
        case CODESTREAM_WGT:
        case CODESTREAM_NLT:
        case CODESTREAM_CTS:
        case CODESTREAM_CRG:
        case CODESTREAM_COM:
            if (precinct_headers_begin != 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            if (offset_bytes + 4 > bitstream_buf_size) {
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            header_size = get_16_bits(bitstream_buf + offset_bytes + 2);
            offset_bytes += 2 + header_size;
            break;
        case CODESTREAM_SLH:
            if (offset_bytes + 6 > bitstream_buf_size) {
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
            header_size = get_16_bits(bitstream_buf + offset_bytes + 2);
            offset_bytes += 2 + header_size;

            if (precinct_headers_begin == 0) {
                precinct_headers_begin = 1;
                if (comps_num < 1 || decomp_h < 0 || decomp_v < 0 || Sd >= comps_num) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                bands_num_exists = Sd;
                for (int32_t c = 0; c < comps_num - Sd; c++) {
                    if (Sx[c] < 1 || Sy[c] < 1 || Sx[c] > 2 || Sy[c] > Sx[c]) {
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                    int Nx = decomp_h;
                    int Ny = decomp_v - (Sy[c] - 1);
                    if (Ny < 0 || Ny > Nx) {
                        return SvtJxsErrorDecoderInvalidBitstream;
                    }
                    bands_num_exists += 2 * Ny + Nx + 1;
                }

                if (out_image_config) {
                    out_image_config->width = width;
                    out_image_config->height = height;
                    out_image_config->bit_depth = bit_depth;
                    out_image_config->components_num = comps_num;
                    out_image_config->format = svt_jpeg_xs_get_format_from_params(comps_num, Sx, Sy);
                    int32_t c = 0;
                    for (; c < comps_num - Sd; c++) {
                        out_image_config->components[c].width = width >> (Sx[c] - 1);
                        out_image_config->components[c].height = height >> (Sy[c] - 1);
                    }
                    for (; c < comps_num; c++) {
                        out_image_config->components[c].width = width;
                        out_image_config->components[c].height = height;
                    }

                    if (proxy_mode != proxy_mode_full) {
                        uint8_t ss = 0;
                        if (proxy_mode == proxy_mode_half) {
                            ss = 1;
                        }
                        if (proxy_mode == proxy_mode_quarter) {
                            ss = 2;
                        }

                        out_image_config->width = (width + (1 << ss) - 1) >> ss;
                        out_image_config->height = (height + (1 << ss) - 1) >> ss;
                        for (c = 0; c < comps_num; c++) {
                            out_image_config->components[c].width = (out_image_config->components[c].width + (1 << ss) - 1) >> ss;
                            out_image_config->components[c].height = (out_image_config->components[c].height + (1 << ss) - 1) >> ss;
                        }
                    }

                    for (c = 0; c < comps_num; c++) {
                        uint32_t pixel_size = out_image_config->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
                        out_image_config->components[c].byte_size = out_image_config->components[c].width *
                            out_image_config->components[c].height * pixel_size;
                    }
                }
                //If fast_search is enabled but expected_codestream_size is 0, then VBR is used and we need to parse entire stream
                if (fast_search && expected_codestream_size != 0) {
                    *frame_size = expected_codestream_size;
                    return SvtJxsErrorNone;
                }
            }
            break;
        case CODESTREAM_EOC:
            offset_bytes += 2;
            /*Invalid value of code stream size can be still decodable
             *if (expected_codestream_size != 0 && expected_codestream_size != offset_bytes) {
             *    return SvtJxsErrorDecoderInvalidBitstream;
             *}*/
            *frame_size = offset_bytes;
            return SvtJxsErrorNone;
        default:
            if (precinct_headers_begin == 0 || bands_num_exists < 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }
            if ((marker >> 12) != 0) {
                return SvtJxsErrorDecoderInvalidBitstream;
            }

            if (offset_bytes + 3 <= bitstream_buf_size) {
                uint32_t Lprc_8lsb = bitstream_buf[offset_bytes + 2];
                int32_t precinct_size = marker << 8 | Lprc_8lsb;
                if (precinct_size > PRECINCT_MAX_BYTES_SIZE) {
                    return SvtJxsErrorDecoderInvalidBitstream;
                }
                offset_bytes += precinct_size + 5; //Lprc + Q[p] + R[p]
                /*Calculate Bit-plane count coding mode with alignment*/
                int spb_bits = bands_num_exists * 2;
                int spb_bytes = spb_bits / 8;
                if (spb_bits & 7) {
                    spb_bytes++;
                }
                offset_bytes += spb_bytes;
            }
            else {
                return SvtJxsErrorDecoderBitstreamTooShort;
            }
        }
    } while (1);
    return SvtJxsErrorDecoderInvalidBitstream;
}
