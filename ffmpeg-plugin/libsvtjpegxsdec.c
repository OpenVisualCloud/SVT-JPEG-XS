/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsDec.h>

#include "libavutil/mem.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "profiles.h"

#define SVT_JPEGXS_DEC_MAX_IN_FLIGHT 5

typedef struct SvtJpegXsDecInFlight {
    AVPacket* pkt;
    AVFrame* frame;
    int in_use;
} SvtJpegXsDecInFlight;

typedef struct SvtJpegXsDecodeContext {
    AVClass* class;
    svt_jpeg_xs_image_config_t config;
    svt_jpeg_xs_decoder_api_t decoder;
    uint32_t decoder_initialized;

    /*0- AVPacket* avpkt have full frame*/
    /*1- AVPacket* avpkt have chunk of frame, need another buffer to merge packets*/
    uint32_t chunk_decoding;
    uint32_t frame_size;
    uint32_t buffer_filled_len;
    uint8_t* bitstream_buffer;
    int proxy_mode;
    int msb_aligned;
    int rgb;
    // FFmpeg plane index for each library component (gbrp* planes are G, B, R; the library outputs R, G, B)
    int plane_map[MAX_COMPONENTS_NUM];

    SvtJpegXsDecInFlight slots[SVT_JPEGXS_DEC_MAX_IN_FLIGHT];
    int in_flight;
    int draining;
} SvtJpegXsDecodeContext;

/* Output RGB (gbrp* or gbrap*) or YUV for a 4:4:4 stream. The library reports PLANAR_RGB/RGBA for
 * Cpih=1 streams; Cpih=0 streams do not signal RGB, so the "rgb" option can override.
 * 13/14-bit 4-component streams can only be output as gbrap14le (no upstream yuva444p14 exists),
 * so they are treated as RGB unless the "rgb" option is false, matching the encoder, which takes
 * gbrap14le planes as R, G, B, A. */
static int output_is_rgb(const SvtJpegXsDecodeContext* svt_dec, svt_jpeg_xs_image_config_t config) {
    if (config.format == COLOUR_FORMAT_PLANAR_RGB || config.format == COLOUR_FORMAT_PLANAR_RGBA) {
        return svt_dec->rgb != 0;
    }
    if (config.format == COLOUR_FORMAT_PLANAR_4_COMPONENTS && config.bit_depth > 12) {
        return svt_dec->rgb != 0;
    }
    if (config.format == COLOUR_FORMAT_PLANAR_YUV444 || config.format == COLOUR_FORMAT_PLANAR_4_COMPONENTS) {
        return svt_dec->rgb == 1;
    }
    return 0;
}


static int set_pix_fmt(AVCodecContext* avctx, svt_jpeg_xs_image_config_t config, int rgb) {
    if (rgb && config.components_num == 3) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_GBRP;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_GBRP10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_GBRP12LE;
        }
        else {
            avctx->pix_fmt = AV_PIX_FMT_GBRP14LE;
        }
    }
    else if (rgb && config.components_num == 4) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_GBRAP10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_GBRAP12LE;
        }
        else {
            avctx->pix_fmt = AV_PIX_FMT_GBRAP14LE;
        }
    }
    else if (config.format == COLOUR_FORMAT_PLANAR_YUV420) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUV420P10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUV420P12LE;
        }
        else {
            avctx->pix_fmt = AV_PIX_FMT_YUV420P14LE;
        }
    }
    else if (config.format == COLOUR_FORMAT_PLANAR_YUV422) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P12LE;
        }
        else {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P14LE;
        }
    }
    else if (config.format == COLOUR_FORMAT_PLANAR_YUV444 || config.format == COLOUR_FORMAT_PLANAR_RGB) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUV444P10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUV444P12LE;
        }
        else {
            avctx->pix_fmt = AV_PIX_FMT_YUV444P14LE;
        }
    }
    else if (config.format == COLOUR_FORMAT_PLANAR_YUV422_ALPHA) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUVA422P10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUVA422P12LE;
        }
        else {
            // no upstream YUVA422P14LE exists.
            av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth %d for 4:2:2:4.\n", config.bit_depth);
            return AVERROR_INVALIDDATA;
        }
    }
    else if (config.format == COLOUR_FORMAT_PLANAR_4_COMPONENTS || config.format == COLOUR_FORMAT_PLANAR_RGBA) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUVA444P10LE;
        }
        else if (config.bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUVA444P12LE;
        }
        else {
            // no upstream YUVA444P14LE exists; GBRAP14LE is the only 14-bit 4-component pix_fmt.
            // Only reached with the "rgb" option false: planes stay in stream order.
            avctx->pix_fmt = AV_PIX_FMT_GBRAP14LE;
        }
    }
    else if (config.format == COLOUR_FORMAT_GRAY || config.format == COLOUR_FORMAT_PLANAR_YUV400) {
        if (config.bit_depth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        }
        else if (config.bit_depth == 9) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY9LE;
        }
        else if (config.bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY10LE;
        }
        else if (config.bit_depth <= 12) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY12LE;
        }
        else if (config.bit_depth <= 14) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY14LE;
        }
        else {
            avctx->pix_fmt = AV_PIX_FMT_GRAY16LE;
        }
    }
    else {
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format.\n");
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int svt_jpegxs_dec_send_slot(AVCodecContext* avctx, SvtJpegXsDecInFlight* slot) {
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;
    AVPacket* avpkt = slot->pkt;
    SvtJxsErrorType_t err = SvtJxsErrorNone;
    svt_jpeg_xs_frame_t dec_input = {0};
    uint32_t pixel_size;
    int ret, rgb;

    if (!svt_dec->decoder_initialized) {
        err = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
            avpkt->data, avpkt->size, NULL, &svt_dec->frame_size, 1 /*quick search*/, svt_dec->decoder.proxy_mode);
        if (err) {
            av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_single_frame_size_with_proxy failed, err=%d\n", err);
            av_packet_unref(avpkt);
            return err;
        }
        if (avpkt->size < svt_dec->frame_size) {
            svt_dec->chunk_decoding = 1;
            svt_dec->bitstream_buffer = av_malloc(svt_dec->frame_size);
            if (!svt_dec->bitstream_buffer) {
                av_log(avctx, AV_LOG_ERROR, "Failed to allocate svt_dec->bitstream_buffer.\n");
                av_packet_unref(avpkt);
                return AVERROR(ENOMEM);
            }
            av_log(NULL, AV_LOG_DEBUG, "svt_jpegxs_dec_decode, bitstream_size=%d, chunk = %d\n", svt_dec->frame_size, avpkt->size);
        }
        if (avpkt->size > svt_dec->frame_size) {
            av_log(avctx, AV_LOG_ERROR, "Single packet have data for more than one frame.\n");
            av_packet_unref(avpkt);
            return AVERROR_UNKNOWN;
        }

        err = svt_jpeg_xs_decoder_init(
            SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &(svt_dec->decoder), avpkt->data, avpkt->size, &(svt_dec->config));
        if (err) {
            av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_decoder_init failed, err=%d\n", err);
            av_packet_unref(avpkt);
            return err;
        }

        rgb = output_is_rgb(svt_dec, svt_dec->config);
        ret = set_pix_fmt(avctx, svt_dec->config, rgb);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "set_pix_fmt failed, err=%d\n", ret);
            svt_jpeg_xs_decoder_close(&(svt_dec->decoder));
            av_packet_unref(avpkt);
            return ret;
        }
        for (int comp = 0; comp < MAX_COMPONENTS_NUM; comp++) {
            svt_dec->plane_map[comp] = comp;
        }
        if (rgb) {
            svt_dec->plane_map[0] = 2;
            svt_dec->plane_map[1] = 0;
            svt_dec->plane_map[2] = 1;
            avctx->colorspace = AVCOL_SPC_RGB;
        }

        ret = ff_set_dimensions(avctx, svt_dec->config.width, svt_dec->config.height);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "ff_set_dimensions failed, err=%d\n", ret);
            svt_jpeg_xs_decoder_close(&(svt_dec->decoder));
            av_packet_unref(avpkt);
            return ret;
        }

        svt_dec->decoder_initialized = 1;
    }

    if (svt_dec->chunk_decoding) {
        uint8_t* bitstrream_addr = svt_dec->bitstream_buffer + svt_dec->buffer_filled_len;
        int bytes_to_copy = avpkt->size;
        //Do not copy more data than allocation
        if ((bytes_to_copy + svt_dec->buffer_filled_len) > svt_dec->frame_size) {
            bytes_to_copy = svt_dec->frame_size - svt_dec->buffer_filled_len;
        }

        memcpy(bitstrream_addr, avpkt->data, bytes_to_copy);
        svt_dec->buffer_filled_len += avpkt->size;
        if (svt_dec->buffer_filled_len >= svt_dec->frame_size) {
            dec_input.bitstream.buffer = svt_dec->bitstream_buffer;
            dec_input.bitstream.allocation_size = svt_dec->frame_size;
            dec_input.bitstream.used_size = svt_dec->frame_size;
        }
        else {
            av_packet_unref(avpkt);
            return AVERROR(EAGAIN);
        }
    }
    else { // svt_dec->chunk_decoding == 0
        dec_input.bitstream.buffer = avpkt->data;
        dec_input.bitstream.allocation_size = avpkt->size;
        dec_input.bitstream.used_size = avpkt->size;
    }

    ret = ff_get_buffer(avctx, slot->frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "ff_get_buffer failed, err=%d\n", ret);
        av_packet_unref(avpkt);
        return ret;
    }

    pixel_size = svt_dec->config.bit_depth <= 8 ? 1 : 2;
    for (int comp = 0; comp < svt_dec->config.components_num; comp++) {
        const int plane = svt_dec->plane_map[comp];
        dec_input.image.data_yuv[comp] = slot->frame->data[plane];
        dec_input.image.stride[comp] = slot->frame->linesize[plane] / pixel_size;
        dec_input.image.alloc_size[comp] = slot->frame->linesize[plane] * svt_dec->config.components[comp].height;
    }

    dec_input.user_prv_ctx_ptr = slot;
    slot->in_use = 1;

    err = svt_jpeg_xs_decoder_send_frame(&(svt_dec->decoder), &dec_input, 1 /*blocking*/);
    if (err) {
        slot->in_use = 0;
        av_frame_unref(slot->frame);
        av_packet_unref(avpkt);
        av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_decoder_send_frame failed, err=%d\n", err);
        return err;
    }

    return 0;
}

static int svt_jpegxs_dec_receive_frame(AVCodecContext* avctx, AVFrame* frame) {
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;
    svt_jpeg_xs_frame_t dec_output = {0};
    SvtJpegXsDecInFlight* slot = NULL;
    SvtJxsErrorType_t err;
    int max_in_flight = svt_dec->chunk_decoding ? 1 : SVT_JPEGXS_DEC_MAX_IN_FLIGHT;
    int ret;

    if (!svt_dec->draining && svt_dec->in_flight < max_in_flight) {
        for (int i = 0; i < SVT_JPEGXS_DEC_MAX_IN_FLIGHT; i++) {
            if (!svt_dec->slots[i].in_use) {
                slot = &svt_dec->slots[i];
                break;
            }
        }
        av_assert0(slot);

        ret = ff_decode_get_packet(avctx, slot->pkt);
        if (ret == 0) {
            ret = svt_jpegxs_dec_send_slot(avctx, slot);
            if (ret == 0) {
                svt_dec->in_flight++;
                max_in_flight = svt_dec->chunk_decoding ? 1 : SVT_JPEGXS_DEC_MAX_IN_FLIGHT;
            }
            else if (ret != AVERROR(EAGAIN)) {
                return ret;
            }
        }
        else if (ret == AVERROR_EOF) {
            svt_dec->draining = 1;
        }
        else if (ret != AVERROR(EAGAIN)) {
            return ret;
        }
    }

    if (svt_dec->in_flight == 0) {
        return svt_dec->draining ? AVERROR_EOF : AVERROR(EAGAIN);
    }

    /* Non-blocking while the pipeline still has room so FFmpeg feeds the next packet
     * immediately; blocking once full, during chunk_decoding, or while draining at EOF. */
    err = svt_jpeg_xs_decoder_get_frame(&(svt_dec->decoder),
                                        &dec_output,
                                        (svt_dec->draining || svt_dec->chunk_decoding || svt_dec->in_flight >= max_in_flight) ? 1 : 0);
    if (err == SvtJxsErrorNoErrorEmptyQueue) {
        return AVERROR(EAGAIN);
    }
    if (err) {
        slot = dec_output.user_prv_ctx_ptr;
        if (slot && slot->in_use) {
            av_frame_unref(slot->frame);
            av_packet_unref(slot->pkt);
            slot->in_use = 0;
            svt_dec->in_flight--;
        }
        if (err == SvtJxsErrorDecoderConfigChange) {
            av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_frame return SvtJxsErrorDecoderConfigChange\n");
            return AVERROR_INPUT_CHANGED;
        }
        av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_frame failed, err=%d\n", err);
        return err;
    }

    slot = dec_output.user_prv_ctx_ptr;
    if (!slot || !slot->in_use) {
        av_log(NULL, AV_LOG_ERROR, "Returned different user_prv_ctx_ptr than expected\n");
        return AVERROR_UNKNOWN;
    }

    //Copy leftover from AVPacket if it contain data from two frames
    if (svt_dec->chunk_decoding) {
        int bytes_to_copy = svt_dec->buffer_filled_len % svt_dec->frame_size;
        int packet_offset = slot->pkt->size - bytes_to_copy;
        uint8_t* packet_addr = slot->pkt->data + packet_offset;
        memcpy(svt_dec->bitstream_buffer, packet_addr, bytes_to_copy);
        svt_dec->buffer_filled_len = bytes_to_copy;
    }

    av_frame_move_ref(frame, slot->frame);
    av_packet_unref(slot->pkt);
    slot->in_use = 0;
    svt_dec->in_flight--;

    return 0;
}

static void svt_jpegxs_dec_flush(AVCodecContext* avctx) {
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;
    while (svt_dec->in_flight > 0) {
        svt_jpeg_xs_frame_t dec_output = {0};
        svt_jpeg_xs_decoder_get_frame(&(svt_dec->decoder), &dec_output, 1 /*blocking*/);
        if (dec_output.user_prv_ctx_ptr) {
            SvtJpegXsDecInFlight* slot = dec_output.user_prv_ctx_ptr;
            av_frame_unref(slot->frame);
            av_packet_unref(slot->pkt);
            slot->in_use = 0;
            svt_dec->in_flight--;
        }
        else {
            break;
        }
    }
    svt_dec->draining = 0;
    svt_dec->buffer_filled_len = 0;
}

static av_cold int svt_jpegxs_dec_free(AVCodecContext* avctx) {
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;
    if (svt_dec->decoder_initialized) {
        svt_jpeg_xs_decoder_close(&(svt_dec->decoder));
        svt_dec->decoder_initialized = 0;
        av_log(NULL, AV_LOG_DEBUG, "svt_jpeg_xs_decoder_close called\n");
    }

    for (int i = 0; i < SVT_JPEGXS_DEC_MAX_IN_FLIGHT; i++) {
        av_frame_free(&svt_dec->slots[i].frame);
        av_packet_free(&svt_dec->slots[i].pkt);
        svt_dec->slots[i].in_use = 0;
    }

    av_freep(&svt_dec->bitstream_buffer);

    return 0;
}

static av_cold int svt_jpegxs_dec_init(AVCodecContext* avctx) {
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;

    svt_dec->decoder_initialized = 0;
    svt_dec->chunk_decoding = 0;
    svt_dec->buffer_filled_len = 0;
    svt_dec->bitstream_buffer = NULL;

    for (int i = 0; i < SVT_JPEGXS_DEC_MAX_IN_FLIGHT; i++) {
        svt_dec->slots[i].pkt = av_packet_alloc();
        svt_dec->slots[i].frame = av_frame_alloc();
        if (!svt_dec->slots[i].pkt || !svt_dec->slots[i].frame) {
            return AVERROR(ENOMEM);
        }
    }

    if (av_log_get_level() < AV_LOG_DEBUG) {
        svt_dec->decoder.verbose = VERBOSE_ERRORS;
    }
    else if (av_log_get_level() == AV_LOG_DEBUG) {
        svt_dec->decoder.verbose = VERBOSE_SYSTEM_INFO;
    }
    else {
        svt_dec->decoder.verbose = VERBOSE_WARNINGS;
    }

    if (svt_dec->proxy_mode == 1) {
        svt_dec->decoder.proxy_mode = proxy_mode_half;
    }
    else if (svt_dec->proxy_mode == 2) {
        svt_dec->decoder.proxy_mode = proxy_mode_quarter;
    }
    else {
        svt_dec->decoder.proxy_mode = proxy_mode_full;
    }

    svt_dec->decoder.threads_num = FFMIN(avctx->thread_count ? avctx->thread_count : av_cpu_count(), 64);
    svt_dec->decoder.use_cpu_flags = CPU_FLAGS_ALL;
    svt_dec->decoder.packetization_mode = 0;
    svt_dec->decoder.output_bit_depth_msb_aligned = svt_dec->msb_aligned ? 1 : 0;

    av_log(NULL, AV_LOG_DEBUG, "svt_jpegxs_dec_init called\n");

    return 0;
}

#define OFFSET(x) offsetof(SvtJpegXsDecodeContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption svtjpegxs_dec_options[] = {
    {"proxy-mode", "Resolution scaling mode: 0-full, 1-half, 2-quarter", OFFSET(proxy_mode), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 2, VE},
    {"msb_aligned",
     "Non-standard: output 10/12-bit samples are MSB-aligned in each 16-bit word instead of LSB-aligned. "
     "Must match the encoder's msb_aligned setting.",
     OFFSET(msb_aligned),
     AV_OPT_TYPE_BOOL,
     {.i64 = 0},
     0,
     1,
     VE},
    {"rgb",
     "Output pixel format for 4:4:4 and 4:4:4:4 streams. auto: gbrp*/gbrap* for streams with colour transform (Cpih=1), "
     "yuv444p*/yuva444p* otherwise. true: always gbrp*/gbrap* (RGB streams coded without colour transform). "
     "false: always yuv444p*/yuva444p*, planes in stream order. 13/14-bit 4-component streams always output gbrap14le, "
     "read as R, G, B, A unless false.",
     OFFSET(rgb),
     AV_OPT_TYPE_BOOL,
     {.i64 = -1},
     -1,
     1,
     VE},
    {NULL},
};

static const AVClass svtjpegxs_dec_class = {
    .class_name = "libsvtjpegxsdec",
    .item_name = av_default_item_name,
    .option = svtjpegxs_dec_options,
    .version = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libsvtjpegxs_decoder = {
    .p.name         = "libsvtjpegxs",
    CODEC_LONG_NAME("SVT JPEG XS(Scalable Video Technology for JPEG XS) decoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEGXS,
    .priv_data_size = sizeof(SvtJpegXsDecodeContext),
    .init = svt_jpegxs_dec_init,
    .close = svt_jpegxs_dec_free,
    .flush = svt_jpegxs_dec_flush,
    FF_CODEC_RECEIVE_FRAME_CB(svt_jpegxs_dec_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_DR1,
    .caps_internal = FF_CODEC_CAP_NOT_INIT_THREADSAFE | FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "libsvtjpegxs",
    .p.priv_class = &svtjpegxs_dec_class,
};
