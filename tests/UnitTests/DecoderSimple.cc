/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "DecoderSimple.h"
#include "decoder_dsp_rtcd.h"
#include "common_dsp_rtcd.h"
#include "ParseHeader.h"

void decoder_simple_init_rtcd(CPU_FLAGS use_cpu_flags) {
    setup_common_rtcd_internal(use_cpu_flags);
    setup_decoder_rtcd_internal(use_cpu_flags);
}

int32_t find_bitstream_header(uint8_t* bitstream_buf_ref, size_t bitstream_length) {
    for (size_t i = 0; i < bitstream_length - 4; ++i) {
        if ((bitstream_buf_ref[i] == 0xff) && (bitstream_buf_ref[i + 1] == 0x10) && (bitstream_buf_ref[i + 2] == 0xff) &&
            (bitstream_buf_ref[i + 3] == 0x50)) {
            fprintf(stderr, "Detect Bitstream header offset: %zu\n", i);
            return (int32_t)i;
        }
    }
    fprintf(stderr, "Detect Bitstream header FAILED!\n");
    return -1;
}

void decoder_simple_free(DecoderSimple_t* decoder) {
    svt_jpeg_xs_image_buffer_free(decoder->image_out);
    decoder->image_out = NULL;

    if (decoder->instance_ctx) {
        if (decoder->dec_thread_context) {
            svt_jpeg_xs_dec_thread_context_free(decoder->dec_thread_context, &decoder->dec_common.pi);
            decoder->dec_thread_context = NULL;
        }
        svt_jpeg_xs_dec_instance_free(decoder->instance_ctx);
        decoder->instance_ctx = NULL;
    }
}

SvtJxsErrorType_t decoder_simple_alloc(DecoderSimple_t* decoder, const uint8_t* bitstream_buf, size_t bitstream_length) {
    //Create common decoder
    decoder->instance_ctx = NULL;
    decoder->image_out = NULL;

    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_probe(
        bitstream_buf, bitstream_length, &decoder->dec_common.picture_header_const, NULL, decoder->verbose);
    if (ret) {
        return ret;
    }

    ret = svt_jpeg_xs_dec_init_common(&decoder->dec_common, &decoder->image_config, proxy_mode_full, decoder->verbose);
    if (ret) {
        return ret;
    }

    decoder->dec_common.max_frame_bitstream_size = 0;
    decoder->instance_ctx = svt_jpeg_xs_dec_instance_alloc(&decoder->dec_common);
    if (!decoder->instance_ctx) {
        return SvtJxsErrorInsufficientResources;
    }

    decoder->dec_thread_context = svt_jpeg_xs_dec_thread_context_alloc(&decoder->dec_common.pi);
    if (decoder->dec_thread_context == NULL) {
        decoder_simple_free(decoder);
        return SvtJxsErrorInsufficientResources;
    }

    decoder->image_out = svt_jpeg_xs_image_buffer_alloc(&decoder->image_config);
    if (!decoder->image_out) {
        decoder_simple_free(decoder);
        return ret;
    }

    return SvtJxsErrorNone;
}

// On success return positive value that indicate single frame size in provided bitstream, otherwise
// return error code from SvtJxsErrorType_t (negative)
SvtJxsErrorType_t svt_jpeg_xs_decode(svt_jpeg_xs_decoder_instance_t* ctx, svt_jpeg_xs_decoder_thread_context* dec_thread_context,
                                     const uint8_t* bitstream_buf, size_t bitstream_buf_size, svt_jpeg_xs_image_buffer_t* out,
                                     uint32_t verbose) {
    SvtJxsErrorType_t ret = SvtJxsErrorNone;

    uint32_t header_size;
    ret = svt_jpeg_xs_decode_header(ctx, bitstream_buf, bitstream_buf_size, &header_size, verbose);
    if (ret) {
        return ret;
    }
    uint32_t offset = header_size;
    pi_t* pi = &ctx->dec_common->pi;

    ctx->sync_slices_idwt = 0; /*do not sync slices*/

    for (uint32_t slice = 0; slice < pi->slice_num; slice++) {
        uint32_t out_slice_size;
        ret = svt_jpeg_xs_decode_slice(
            ctx, dec_thread_context, bitstream_buf + offset, bitstream_buf_size - offset, slice, &out_slice_size, out, verbose);
        if (ret) {
            return ret;
        }
        offset += out_slice_size;
    }

    bitstream_reader_t bitstream;
    bitstream_reader_init(&bitstream, bitstream_buf + offset, bitstream_buf_size - offset);
    ret = get_tail(&bitstream);
    if (ret) {
        return ret;
    }
    if (ctx->dec_common->picture_header_const.hdr_Cpih) {
        ret = svt_jpeg_xs_decode_final(ctx, out);
    }
    else {
        for (uint32_t slice = 0; slice < pi->slice_num; slice++) {
            ret = svt_jpeg_xs_decode_final_slice_overlap(ctx, out, slice);
            if (ret) {
                return ret;
            }
        }
    }
    if (ret) {
        return ret;
    }

    uint32_t frame_size = offset + bitstream_reader_get_used_bytes(&bitstream);
    if (ctx->picture_header_dynamic.hdr_Lcod != 0 && ctx->picture_header_dynamic.hdr_Lcod != frame_size) {
        if (verbose >= VERBOSE_ERRORS) {
            fprintf(stderr,
                    "Warning: Frame decoded but may be broken! Decoded different stream size than expected from header get=%u, "
                    "expected=%u\n",
                    frame_size,
                    ctx->picture_header_dynamic.hdr_Lcod);
        }
    }

    return (SvtJxsErrorType_t)frame_size;
}

SvtJxsErrorType_t decoder_simple_get_frame(DecoderSimple_t* decoder, const uint8_t* bitstream_buf, size_t bitstream_buf_size) {
    return svt_jpeg_xs_decode(decoder->instance_ctx,
                              decoder->dec_thread_context,
                              bitstream_buf,
                              bitstream_buf_size,
                              decoder->image_out,
                              decoder->verbose);
}
