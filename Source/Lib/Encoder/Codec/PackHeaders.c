/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "PackHeaders.h"
#include "Codestream.h"
#include "Encoder.h"
#include "PrecinctEnc.h"

void write_capabilities_marker(bitstream_writer_t* bitstream, svt_jpeg_xs_encoder_common_t* enc_common) {
    write_16_bits(bitstream, CODESTREAM_CAP);
    /* For support 420 and enc_common->picture_header_dynamic.hdr_Rl encoder generate output
     * for specification ISO/IEC 21122-1:2022 */

    uint8_t capability[9];
    uint8_t elements = 9;
    uint8_t support_420 = enc_common->colour_format == COLOUR_FORMAT_PLANAR_YUV420;
    capability[0] = 0;           //Unused
    capability[1] = 0;           //Support for Star-Tetrix transform and CTS marker required
    capability[2] = 0;           //Support for quadratic non-linear transform required
    capability[3] = 0;           //Support for extended non-linear transform required
    capability[4] = support_420; //0: sy[i] = 1 for all components i 1: component i with sy[i]>1 present
    capability[5] = 0;           //Support for component-dependent wavelet decomposition required
    capability[6] = 0;           //Support for lossless decoding required
    capability[7] = 0;           //Unused
    capability[8] = enc_common->picture_header_dynamic.hdr_Rl; //Support for packet-based raw-mode switch required

    uint16_t size_bytes = BITS_TO_BYTE_WITH_ALIGN(16 + elements); //Lcap + flags
    write_16_bits(bitstream, size_bytes);
    for (uint8_t i = 0; i < elements; ++i) {
        write_1_bit(bitstream, capability[i]);
    }
    align_bitstream_writer_to_next_byte(bitstream);
}

void write_picture_header(bitstream_writer_t* bitstream, pi_t* pi, svt_jpeg_xs_encoder_common_t* enc_common) {
    write_16_bits(bitstream, CODESTREAM_PIH);                              //PIH
    write_16_bits(bitstream, PICTURE_HEADER_SIZE_BYTES);                   //Lpih
    write_32_bits(bitstream, enc_common->picture_header_dynamic.hdr_Lcod); //Lcod
    write_16_bits(bitstream, 0);                                           //Ppih
    write_16_bits(bitstream, 0);                                           //Plev
    write_16_bits(bitstream, pi->width);                                   //Wf
    write_16_bits(bitstream, pi->height);                                  //Hf
    write_16_bits(bitstream, enc_common->Cw);                              //Cw

    write_16_bits(bitstream, pi->precincts_per_slice);                       //Hsl
    write_8_bits(bitstream, pi->comps_num);                                  //Nc
    write_8_bits(bitstream, pi->coeff_group_size);                           //Ng
    write_8_bits(bitstream, pi->significance_group_size);                    //Ss
    write_8_bits(bitstream, enc_common->picture_header_dynamic.hdr_Bw);      //Bw
    write_2x4_bits(bitstream, enc_common->picture_header_dynamic.hdr_Fq, 4); //Fq   | Br
    write_134_bits(bitstream, 0, 0, 0);                                      //Fslc | PPoc | Cpih
    write_2x4_bits(bitstream, pi->decom_h, pi->decom_v);                     //Nlx  | Nly

    write_1_bit(bitstream, !pi->use_short_header);                        //Lh
    write_1_bit(bitstream, enc_common->picture_header_dynamic.hdr_Rl);    //Rl
    write_2_bits(bitstream, enc_common->picture_header_dynamic.hdr_Qpih); //Qpih - Quantization type
    write_2_bits(bitstream, enc_common->picture_header_dynamic.hdr_Fs);   //Fs
    write_2_bits(bitstream, enc_common->picture_header_dynamic.hdr_Rm);   //Rm
}

void write_weight_table(bitstream_writer_t* bitstream, pi_t* pi) {
    int bands_exists = pi->bands_num_exists;

    write_16_bits(bitstream, CODESTREAM_WGT);
    write_16_bits(bitstream, 2 * bands_exists + 2);

    for (uint32_t band = 0; band < pi->bands_num_all; band++) {
        uint8_t comp_id = pi->global_band_info[band].comp_id;
        uint8_t band_id = pi->global_band_info[band].band_id;
        if (comp_id != BAND_NOT_EXIST) {
            write_8_bits(bitstream, pi->components[comp_id].bands[band_id].gain);
            write_8_bits(bitstream, pi->components[comp_id].bands[band_id].priority);
        }
    }
}

void write_component_table(bitstream_writer_t* bitstream, pi_t* pi, uint8_t bit_depth) {
    int size = 2 * pi->comps_num + 2;
    write_16_bits(bitstream, CODESTREAM_CDT);
    write_16_bits(bitstream, size);

    for (uint32_t c = 0; c < pi->comps_num; ++c) {
        write_8_bits(bitstream, bit_depth);
        write_4_bits(bitstream, pi->components[c].Sx);
        write_4_bits(bitstream, pi->components[c].Sy);
    }
}

void write_slice_header(bitstream_writer_t* bitstream, int slice_idx) {
    write_16_bits(bitstream, CODESTREAM_SLH);
    write_16_bits(bitstream, 4);
    write_16_bits(bitstream, slice_idx);
}

/*Return bits offset on sign_size_bytes*/
uint32_t write_packet_header(bitstream_writer_t* bitstream, uint32_t long_hdr, uint8_t raw_coding, uint64_t data_size_bytes,
                             uint64_t bitplane_count_size_bytes, uint64_t sign_size_bytes) {
    uint8_t* mem = bitstream->mem + bitstream->offset;

    //Write 1 bit
    mem[0] = raw_coding << 7;
    uint32_t offset_sign_size_bytes;
    if (long_hdr) {
        mem[0] |= (data_size_bytes >> 13) & 0x7F;
        mem[1] = (data_size_bytes >> 5) & 0xFF;
        mem[2] = ((data_size_bytes & 0x1F) << 3) | ((bitplane_count_size_bytes >> 17) & 0x07);

        mem[3] = ((bitplane_count_size_bytes >> 9) & 0xFF);
        mem[4] = ((bitplane_count_size_bytes >> 1) & 0xFF);
        mem[5] = ((bitplane_count_size_bytes & 0x1) << 7) | ((sign_size_bytes >> 8) & 0x7F);
        mem[6] = sign_size_bytes & 0xFF;
        bitstream->offset += 7;
        offset_sign_size_bytes = bitstream_writer_get_used_bits(bitstream) - 15;
    }
    else {
        mem[0] |= ((data_size_bytes >> 8) & 0x7F);
        mem[1] = data_size_bytes & 0xFF;
        mem[2] = ((bitplane_count_size_bytes >> 5) & 0xFF);
        mem[3] = ((bitplane_count_size_bytes & 0x1F) << 3) | ((sign_size_bytes >> 8) & 0x07);
        mem[4] = sign_size_bytes & 0xFF;
        bitstream->offset += 5;
        offset_sign_size_bytes = bitstream_writer_get_used_bits(bitstream) - 11;
    }
    return offset_sign_size_bytes;
}

void write_tail(bitstream_writer_t* bitstream) {
    write_16_bits(bitstream, CODESTREAM_EOC);
}

void write_header(bitstream_writer_t* bitstream, svt_jpeg_xs_encoder_common_t* enc_common) {
    write_16_bits(bitstream, CODESTREAM_SOC);
    write_capabilities_marker(bitstream, enc_common);
    write_picture_header(bitstream, &enc_common->pi, enc_common);
    write_component_table(bitstream, &enc_common->pi, enc_common->bit_depth);
    write_weight_table(bitstream, &enc_common->pi);
    align_bitstream_writer_to_next_byte(bitstream);
}
