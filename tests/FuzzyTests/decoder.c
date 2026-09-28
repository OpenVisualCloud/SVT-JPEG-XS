/*
* Copyright(c) 2025 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsDec.h>
#include <stdio.h>
#include <stdlib.h>

void decode_data(svt_jpeg_xs_decoder_api_t* dec, const uint8_t* Data, size_t Size) {
    svt_jpeg_xs_bitstream_buffer_t bitstream = {0};
    bitstream.allocation_size = Size;
    bitstream.buffer = (uint8_t*)Data;
    bitstream.used_size = Size;

    svt_jpeg_xs_image_buffer_t image_buffer = {0};
    for (uint8_t i = 0; i < MAX_COMPONENTS_NUM; i++) {
        image_buffer.stride[i] = 0;
        image_buffer.alloc_size[i] = 0;
        image_buffer.data_yuv[i] = NULL;
    }

    svt_jpeg_xs_image_config_t image_config = {0};

    SvtJxsErrorType_t err = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, dec, bitstream.buffer, bitstream.used_size, &image_config);
    if (err) {
        svt_jpeg_xs_decoder_close(dec);
        return;
    }

    uint32_t pixel_size = image_config.bit_depth <= 8 ? 1 : 2;
    for (uint8_t i = 0; i < image_config.components_num; i++) {
        image_buffer.stride[i] = image_config.components[i].width;
        image_buffer.alloc_size[i] = image_buffer.stride[i] * image_config.components[i].height * pixel_size;
        image_buffer.data_yuv[i] = malloc(image_buffer.alloc_size[i]);
        if (!image_buffer.data_yuv[i]) {
            for (uint8_t j = 0; j < i; j++) {
                free(image_buffer.data_yuv[j]);
            }
            svt_jpeg_xs_decoder_close(dec);
            return;
        }
    }

    svt_jpeg_xs_frame_t dec_input;
    dec_input.bitstream = bitstream;
    dec_input.image = image_buffer;
    dec_input.user_prv_ctx_ptr = NULL;
    if (dec->packetization_mode) {
        //Feed the bitstream in several chunks to exercise the internal slice reassembly
        size_t chunk_size = Size / 4 + 1;
        size_t offset = 0;
        do {
            uint32_t bytes_used = 0;
            dec_input.bitstream.buffer = (uint8_t*)Data + offset;
            dec_input.bitstream.used_size = (uint32_t)(Size - offset < chunk_size ? Size - offset : chunk_size);
            dec_input.bitstream.allocation_size = dec_input.bitstream.used_size;
            err = svt_jpeg_xs_decoder_send_packet(dec, &dec_input, &bytes_used);
            offset += bytes_used;
            if (bytes_used == 0) {
                break; //No progress, decoder does not accept more data for this frame
            }
        } while (err == SvtJxsErrorDecoderBitstreamTooShort && offset < Size);
    }
    else {
        err = svt_jpeg_xs_decoder_send_frame(dec, &dec_input, 1 /*blocking*/);
    }
    //In packet mode SvtJxsErrorNone means the whole frame was scheduled, otherwise no frame is produced
    if (err == SvtJxsErrorNone) {
        svt_jpeg_xs_frame_t dec_output;
        svt_jpeg_xs_decoder_get_frame(dec, &dec_output, 1 /*blocking*/);
    }

    //Close first: slice threads of a partially sent frame (packet mode) may still write the output buffers
    svt_jpeg_xs_decoder_close(dec);
    for (uint8_t i = 0; i < image_config.components_num; i++) {
        if (image_buffer.data_yuv[i]) {
            free(image_buffer.data_yuv[i]);
        }
    }
}

//Skip streams declaring a picture larger than 8K (same cap as the encoder fuzzer): the decoder
//legitimately allocates frame-sized buffers (e.g. RCT buffers are Wf*Hf*4 bytes per component),
//which for Wf/Hf up to 65535 exceed the fuzzer memory limit without indicating a bug.
static int is_picture_too_large(const uint8_t* Data, size_t Size) {
    //Walk the marker segments like the decoder does (SOC has no length, every other header segment
    //is marker(2) + length(2) + payload), so an FF12 byte pair inside e.g. the CAP payload is not
    //mistaken for the picture header.
    //PIH marker segment: FF12, Lpih(2), Lcod(4), Ppih(2), Plev(2), Wf(2), Hf(2)
    size_t i = 0;
    while (i + 4 <= Size) {
        if (Data[i] != 0xFF) {
            return 0; //Not a marker, the decoder rejects the stream before allocating the picture
        }
        if (Data[i + 1] == 0x10) { //SOC
            i += 2;
            continue;
        }
        if (Data[i + 1] == 0x12) { //PIH
            if (i + 16 > Size) {
                return 0;
            }
            uint32_t width = ((uint32_t)Data[i + 12] << 8) | Data[i + 13];
            uint32_t height = ((uint32_t)Data[i + 14] << 8) | Data[i + 15];
            return (uint64_t)width * height > 7680 * 4320;
        }
        i += 2 + (((size_t)Data[i + 2] << 8) | Data[i + 3]);
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    svt_jpeg_xs_decoder_api_t dec = {0};

    if (is_picture_too_large(Data, Size)) {
        return 0;
    }

    dec.verbose = VERBOSE_NONE;
    dec.threads_num = 6;
    dec.use_cpu_flags = CPU_FLAGS_ALL;

    //Frame based mode for all proxy modes, alternating the output bit alignment
    dec.packetization_mode = 0;
    dec.proxy_mode = proxy_mode_full;
    dec.output_bit_depth_msb_aligned = 0;
    decode_data(&dec, Data, Size);
    dec.proxy_mode = proxy_mode_half;
    dec.output_bit_depth_msb_aligned = 1;
    decode_data(&dec, Data, Size);
    dec.proxy_mode = proxy_mode_quarter;
    dec.output_bit_depth_msb_aligned = 0;
    decode_data(&dec, Data, Size);

    //Packet based mode
    dec.packetization_mode = 1;
    dec.proxy_mode = proxy_mode_full;
    dec.output_bit_depth_msb_aligned = 1;
    decode_data(&dec, Data, Size);

    return 0;
}
