/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsEnc.h>

#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "profiles.h"

typedef struct SvtJpegXsEncodeContext {
    AVClass* class;

    char* bpp_str;

    int slice_height;
    int decomp_v;
    int decomp_h;
    int quant;
    int coding_signs_handling;
    int coding_significance;
    int coding_vpred;
    int coding_raw;
    int cap_compat;

    svt_jpeg_xs_encoder_api_t encoder;
    int bitstream_frame_size;
} SvtJpegXsEncodeContext;

static int svt_jpegxs_enc_encode(AVCodecContext* avctx, AVPacket* pkt, const AVFrame* frame, int* got_packet) {
    SvtJpegXsEncodeContext* svt_enc = avctx->priv_data;

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    svt_jpeg_xs_image_buffer_t in_buf;
    svt_jpeg_xs_frame_t enc_input;
    svt_jpeg_xs_frame_t enc_output;

    SvtJxsErrorType_t err = SvtJxsErrorNone;
    uint32_t pixel_size = svt_enc->encoder.input_bit_depth <= 8 ? 1 : 2;

    int ret = ff_alloc_packet(avctx, pkt, svt_enc->bitstream_frame_size);
    if (ret < 0) {
        return ret;
    }

    out_buf.buffer = pkt->data;          // output bitstream ptr
    out_buf.allocation_size = pkt->size; // output bitstream size
    out_buf.used_size = 0;

    for (int comp = 0; comp < 3; comp++) {
        // svt-jpegxs require stride in pixel's not in bytes, this means that for 10 bit-depth, stride is half the linesize
        in_buf.stride[comp] = frame->linesize[comp] / pixel_size;
        in_buf.data_yuv[comp] = frame->data[comp];
        in_buf.alloc_size[comp] = in_buf.stride[comp] * svt_enc->encoder.source_height * pixel_size;
    }

    enc_input.bitstream = out_buf;
    enc_input.image = in_buf;
    enc_input.user_prv_ctx_ptr = pkt;

    err = svt_jpeg_xs_encoder_send_picture(&(svt_enc->encoder), &enc_input, 1 /*blocking*/);
    if (err != SvtJxsErrorNone) {
        av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_encoder_send_picture failed\n");
        return AVERROR_UNKNOWN;
    }

    err = svt_jpeg_xs_encoder_get_packet(&(svt_enc->encoder), &enc_output, 1 /*blocking*/);
    if (err != SvtJxsErrorNone) {
        av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_encoder_get_packet failed\n");
        return AVERROR_UNKNOWN;
    }

    if (enc_output.user_prv_ctx_ptr != pkt) {
        av_log(NULL, AV_LOG_ERROR, "Returned different user_prv_ctx_ptr than expected\n");
        return AVERROR_UNKNOWN;
    }

    pkt->size = enc_output.bitstream.used_size;

    *got_packet = 1;
    return 0;
}

static av_cold int svt_jpegxs_enc_free(AVCodecContext* avctx) {
    SvtJpegXsEncodeContext* svt_enc = avctx->priv_data;
    svt_jpeg_xs_encoder_close(&(svt_enc->encoder));
    av_log(NULL, AV_LOG_DEBUG, "svt_jpeg_xs_encoder_close called\n");

    return 0;
}

static int set_pix_fmt(AVCodecContext* avctx, svt_jpeg_xs_encoder_api_t* encoder) {
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        encoder->input_bit_depth = 8;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        return 0;
    case AV_PIX_FMT_YUV422P:
        encoder->input_bit_depth = 8;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        return 0;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_GBRP:
        encoder->input_bit_depth = 8;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        return 0;
    case AV_PIX_FMT_YUV420P10LE:
        encoder->input_bit_depth = 10;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        return 0;
    case AV_PIX_FMT_YUV422P10LE:
        encoder->input_bit_depth = 10;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        return 0;
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_GBRP10LE:
        encoder->input_bit_depth = 10;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        return 0;
    case AV_PIX_FMT_YUV420P12LE:
        encoder->input_bit_depth = 12;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        return 0;
    case AV_PIX_FMT_YUV422P12LE:
        encoder->input_bit_depth = 12;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        return 0;
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_GBRP12LE:
        encoder->input_bit_depth = 12;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        return 0;
    case AV_PIX_FMT_YUV420P14LE:
        encoder->input_bit_depth = 14;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        return 0;
    case AV_PIX_FMT_YUV422P14LE:
        encoder->input_bit_depth = 14;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        return 0;
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_GBRP14LE:
        encoder->input_bit_depth = 14;
        encoder->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        return 0;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        encoder->input_bit_depth = 8;
        encoder->colour_format = COLOUR_FORMAT_PACKED_YUV444_OR_RGB;
        return 0;
    default:
        break;
    }
    av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format.\n");
    return AVERROR_INVALIDDATA;
}

static void set_bpp(const char* value, svt_jpeg_xs_encoder_api_t* encoder) {
    char* end;
    encoder->bpp_numerator = strtoul(value, &end, 0);
    encoder->bpp_denominator = 1;
    if (*end != '\0') {
        while (*value) {
            if (*value == '.' || *value == ',') {
                value++;
                break;
            }
            value++;
        }
        if (*value) {
            uint32_t fraction = strtoul(value, &end, 0);
            uint32_t chars = (uint32_t)(end - value);
            for (uint32_t i = 0; i < chars; ++i) {
                encoder->bpp_denominator *= 10;
            }
            encoder->bpp_numerator = encoder->bpp_numerator * encoder->bpp_denominator + fraction;
        }
    }
}

/* Derive a lossless-equivalent bpp default from the already-resolved
 * colour_format/input_bit_depth (set by set_pix_fmt()), used only when the
 * user does not pass -bpp explicitly. This keeps a single source of truth
 * for pixel-format support instead of a separate pix_fmt table, so it
 * automatically covers every format set_pix_fmt() supports, present and
 * future. Returns 0 if colour_format is not recognized. */
static uint32_t default_bpp_numerator(const svt_jpeg_xs_encoder_api_t* encoder) {
    switch (encoder->colour_format) {
    case COLOUR_FORMAT_PLANAR_YUV420:
        return encoder->input_bit_depth * 3 / 2;
    case COLOUR_FORMAT_PLANAR_YUV422:
        return encoder->input_bit_depth * 2;
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
    case COLOUR_FORMAT_PACKED_YUV444_OR_RGB:
        return encoder->input_bit_depth * 3;
    default:
        return 0;
    }
}

/* Stream profile (Ppih) / level (Plev) are exposed via ffmpeg's generic "-profile"/"-level"
 * AVCodecContext options (see avctx->profile / avctx->level below) rather than private options,
 * matching the convention used by libsvtav1.c and most other encoders. Named values for both are
 * registered as AV_OPT_TYPE_CONST entries against the shared "avctx.profile"/"avctx.level" units in
 * svtjpegxs_enc_options[] below. */

static av_cold int svt_jpegxs_enc_init(AVCodecContext* avctx) {
    SvtJpegXsEncodeContext* svt_enc = avctx->priv_data;
    int ret;
    SvtJxsErrorType_t err = svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &(svt_enc->encoder));

    if (err != SvtJxsErrorNone) {
        av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_encoder_load_default_parameters failed\n");
        return AVERROR_UNKNOWN;
    }
    av_log(NULL, AV_LOG_DEBUG, "svt_jpeg_xs_encoder_load_default_parameters ok\n");

    svt_enc->encoder.source_width = avctx->width;
    svt_enc->encoder.source_height = avctx->height;

    if ((ret = set_pix_fmt(avctx, &(svt_enc->encoder))) < 0) {
        return ret;
    }

    svt_enc->encoder.threads_num = FFMIN(avctx->thread_count ? avctx->thread_count : av_cpu_count(), 64);

    if (av_log_get_level() < AV_LOG_DEBUG) {
        svt_enc->encoder.verbose = VERBOSE_ERRORS;
    }
    else if (av_log_get_level() == AV_LOG_DEBUG) {
        svt_enc->encoder.verbose = VERBOSE_SYSTEM_INFO;
    }
    else {
        svt_enc->encoder.verbose = VERBOSE_WARNINGS;
    }

    if (svt_enc->bpp_str) {
        set_bpp(svt_enc->bpp_str, &(svt_enc->encoder));
    }
    else {
        uint32_t default_bpp = default_bpp_numerator(&(svt_enc->encoder));
        if (!default_bpp) {
            // TODO: Consider using avctx->bit_rate to specify bpp_num/bpp_denom in this case
            av_log(NULL, AV_LOG_ERROR, "libsvtjpegxs Encoder require -bpp(bits per pixel) param\n");
            return AVERROR_OPTION_NOT_FOUND;
        }
        av_log(avctx,
               AV_LOG_WARNING,
               "-bpp not set; defaulting to uncompressed-equivalent %u bpp for %s "
               "(no compression will occur). Set -bpp explicitly to target a specific bitrate.\n",
               default_bpp,
               av_get_pix_fmt_name(avctx->pix_fmt));
        svt_enc->encoder.bpp_numerator = default_bpp;
        svt_enc->encoder.bpp_denominator = 1;
    }

    if (svt_enc->decomp_v != -1) {
        svt_enc->encoder.ndecomp_v = svt_enc->decomp_v;
    }
    if (svt_enc->decomp_h != -1) {
        svt_enc->encoder.ndecomp_h = svt_enc->decomp_h;
    }
    if (svt_enc->quant != -1) {
        svt_enc->encoder.quantization = svt_enc->quant;
    }
    if (svt_enc->coding_signs_handling != -1) {
        svt_enc->encoder.coding_signs_handling = svt_enc->coding_signs_handling;
    }
    if (svt_enc->coding_significance != -1) {
        svt_enc->encoder.coding_significance = svt_enc->coding_significance;
    }
    if (svt_enc->coding_vpred != -1) {
        svt_enc->encoder.coding_vertical_prediction_mode = svt_enc->coding_vpred;
    }
    if (svt_enc->coding_raw != -1) {
        svt_enc->encoder.coding_raw_disable = svt_enc->coding_raw ? 0 : 1;
    }
    if (svt_enc->cap_compat != -1) {
        svt_enc->encoder.cap_compat = svt_enc->cap_compat ? 1 : 0;
    }
    if (avctx->profile != AV_PROFILE_UNKNOWN) {
        if (avctx->profile < 0 || avctx->profile > 0xFFFF) {
            av_log(avctx, AV_LOG_ERROR, "Invalid -profile value %d for libsvtjpegxs (must be 0-65535).\n", avctx->profile);
            return AVERROR(EINVAL);
        }
        svt_enc->encoder.profile_ppih_override = (uint16_t)avctx->profile;
    }
    if (avctx->level != AV_LEVEL_UNKNOWN) {
        if (avctx->level < 0 || avctx->level > 0xFFFF) {
            av_log(avctx, AV_LOG_ERROR, "Invalid -level value %d for libsvtjpegxs (must be 0-65535).\n", avctx->level);
            return AVERROR(EINVAL);
        }
        svt_enc->encoder.level_plev_override = (uint16_t)avctx->level;
    }
    if (svt_enc->slice_height > 0) {
        svt_enc->encoder.slice_height = svt_enc->slice_height;
    }

    err = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &(svt_enc->encoder));
    if (err != SvtJxsErrorNone) {
        av_log(NULL, AV_LOG_ERROR, "svt_jpeg_xs_encoder_init failed\n");
        return AVERROR_UNKNOWN;
    }
    av_log(NULL, AV_LOG_DEBUG, "svt_jpeg_xs_encoder_init ok\n");

    svt_enc->bitstream_frame_size =
        ((avctx->width * avctx->height * svt_enc->encoder.bpp_numerator / svt_enc->encoder.bpp_denominator + 7) / 8);

    return 0;
}

#define OFFSET(x) offsetof(SvtJpegXsEncodeContext, x)
#define VE        AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption svtjpegxs_enc_options[] = {
    {"bpp",
     "Bits per pixel, can be passed as integer or float (example: 0.5, 3, 3.75, 5 etc.). "
     "If not set, defaults to an uncompressed-equivalent value based on the input pixel format.",
     OFFSET(bpp_str),
     AV_OPT_TYPE_STRING,
     {.str = NULL},
     0,
     0,
     VE},
    {"slice_height",
     "Specify number of lines calculated in one thread",
     OFFSET(slice_height),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     10000,
     VE},
    {"decomp_v", "vertical decomposition level", OFFSET(decomp_v), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 2, VE},
    {"decomp_h", "horizontal decomposition level", OFFSET(decomp_h), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 5, VE},
    {"quantization", "Quantization algorithm", OFFSET(quant), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, VE, .unit = "quantization"},
    {"deadzone", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, VE, .unit = "quantization"},
    {"uniform", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, VE, .unit = "quantization"},
    {"coding-signs",
     "Enable Signs handling strategy",
     OFFSET(coding_signs_handling),
     AV_OPT_TYPE_INT,
     {.i64 = -1},
     -1,
     2,
     VE,
     .unit = "coding-signs"},
    {"disable", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, VE, .unit = "coding-signs"},
    {"fast", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, VE, .unit = "coding-signs"},
    {"full", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2}, INT_MIN, INT_MAX, VE, .unit = "coding-signs"},
    {"coding-sigf", "Enable Significance coding", OFFSET(coding_significance), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, VE},
    {"coding-vpred",
     "Enable Vertical Prediction coding",
     OFFSET(coding_vpred),
     AV_OPT_TYPE_INT,
     {.i64 = -1},
     -1,
     2,
     VE,
     .unit = "coding-vpred"},
    {"disable", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, VE, .unit = "coding-vpred"},
    {"no_residuals", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, VE, .unit = "coding-vpred"},
    {"no_coeffs", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2}, INT_MIN, INT_MAX, VE, .unit = "coding-vpred"},
    {"coding-raw",
     "Enable packet-based raw-mode coding (disable for legacy-decoder compatibility)",
     OFFSET(coding_raw),
     AV_OPT_TYPE_BOOL,
     {.i64 = -1},
     -1,
     1,
     VE},
    {"cap-compat",
     "Emit an empty CAP marker for legacy-decoder compatibility when no capability bit is required",
     OFFSET(cap_compat),
     AV_OPT_TYPE_BOOL,
     {.i64 = -1},
     -1,
     1,
     VE},
    /* Named Ppih (profile) values for the generic "-profile" option (see avctx->profile in
     * svt_jpegxs_enc_init()), matching ISO/IEC 21122-2 Annex A and the App's --stream-profile table
     * (Source/App/EncApp/EncAppConfig.c). A raw 0-65535 value (e.g. -profile 0x3540) is also
     * accepted. "-profile unknown" (the global default when unset) selects auto-derive from the
     * input pixel format. */
    {"light422", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x1500}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"light444", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x1A00}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"lightsubline422", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x2500}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"main420", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x3240}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"main422", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x3540}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"main444", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x3A40}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"main4444", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x3E40}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"high420", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x4240}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"high444", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x4A40}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    {"high4444", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x4E40}, INT_MIN, INT_MAX, VE, .unit = "avctx.profile"},
    /* Named Plev (level) values for the generic "-level" option (see avctx->level in
     * svt_jpegxs_enc_init()). Named entries only cover the resolution/level portion (bits [15:10]);
     * use a raw value (e.g. -level 0x0810) instead of a name to also set an explicit sublevel.
     * "-level unknown" (the global default when unset) selects auto-derive from resolution and bpp. */
    {"unrestricted", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x0000}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"1k-1", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x0001 << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"2k-1", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x0004 << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"4k-1", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x0008 << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"4k-2", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x0009 << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"4k-3", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x000A << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"5k-1", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x000B << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"8k-1", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x000C << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"8k-2", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x000D << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"8k-3", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x000E << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {"10k-1", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0x0010 << 10}, INT_MIN, INT_MAX, VE, .unit = "avctx.level"},
    {NULL},
};

static const AVClass svtjpegxs_enc_class = {
    .class_name = "libsvtjpegxs",
    .item_name = av_default_item_name,
    .option = svtjpegxs_enc_options,
    .version = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libsvtjpegxs_encoder = {
    .p.name = "libsvtjpegxs",
    CODEC_LONG_NAME("SVT JPEG XS(Scalable Video Technology for JPEG XS) encoder"),
    .p.type = AVMEDIA_TYPE_VIDEO,
    .p.id = AV_CODEC_ID_JPEGXS,
    .priv_data_size = sizeof(SvtJpegXsEncodeContext),
    .init = svt_jpegxs_enc_init,
    .close = svt_jpegxs_enc_free,
    FF_CODEC_ENCODE_CB(svt_jpegxs_enc_encode),
    .p.capabilities = AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_DR1,
    .caps_internal = FF_CODEC_CAP_NOT_INIT_THREADSAFE | FF_CODEC_CAP_AUTO_THREADS,
    .p.pix_fmts = (const enum AVPixelFormat[]){AV_PIX_FMT_YUV420P,
                                               AV_PIX_FMT_YUV422P,
                                               AV_PIX_FMT_YUV444P,
                                               AV_PIX_FMT_YUV420P10LE,
                                               AV_PIX_FMT_YUV422P10LE,
                                               AV_PIX_FMT_YUV444P10LE,
                                               AV_PIX_FMT_YUV420P12LE,
                                               AV_PIX_FMT_YUV422P12LE,
                                               AV_PIX_FMT_YUV444P12LE,
                                               AV_PIX_FMT_YUV420P14LE,
                                               AV_PIX_FMT_YUV422P14LE,
                                               AV_PIX_FMT_YUV444P14LE,
                                               AV_PIX_FMT_GBRP,
                                               AV_PIX_FMT_GBRP10LE,
                                               AV_PIX_FMT_GBRP12LE,
                                               AV_PIX_FMT_GBRP14LE,
                                               AV_PIX_FMT_RGB24,
                                               AV_PIX_FMT_BGR24,
                                               AV_PIX_FMT_NONE},
    .p.wrapper_name = "libsvtjpegxs",
    .p.priv_class = &svtjpegxs_enc_class,
};
