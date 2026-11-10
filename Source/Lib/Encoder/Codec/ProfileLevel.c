/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "ProfileLevel.h"

#include <stdarg.h>
#include <stdio.h>
#include "SvtLog.h"
#include "SvtUtility.h"

/* Chroma sampling formats of ISO/IEC 21122-2 Tables A.1-A.4, as bit flags. */
#define FMT_400  (1u << 0)
#define FMT_420  (1u << 1)
#define FMT_422  (1u << 2)
#define FMT_444  (1u << 3)
#define FMT_4224 (1u << 4)
#define FMT_4444 (1u << 5)

/* B[i] values, as bit flags. */
#define BITS_8_10    ((1u << 8) | (1u << 10))
#define BITS_8_10_12 ((1u << 8) | (1u << 10) | (1u << 12))

/* Quantizer types (Qpih), as bit flags. */
#define QPIH_DZQ (1u << 0)
#define QPIH_ANY ((1u << 0) | (1u << 1))

/* Restrictions of one profile that depend on the encoder configuration. Every row also needs a
 * slice height of 16 rows and Lh=0. Light profiles additionally require a smaller decoder smoothing
 * buffer (Nsbu=4, Light-Subline Nsbu=2) that is not checked here. */
typedef struct ProfileRule {
    uint16_t ppih;
    const char* name;
    uint32_t bit_depths;   /* BITS_* */
    uint8_t ndecomp_v_min; /* NL,y */
    uint8_t ndecomp_v_max;
    uint32_t formats;          /* FMT_* */
    uint32_t rct_formats;      /* formats that allow Cpih=1 */
    uint8_t raw_mode;          /* required Rl */
    uint8_t lossless;          /* 1: Fq=0, Bw=B[i] (MLS); 0: Fq=8, Bw=20 */
    uint32_t quantizers;       /* QPIH_* */
    uint32_t max_column_width; /* 0: any; the encoder writes Cw=0, so the column width is Wf */
    uint32_t max_decoded_bpp;  /* Table A.6, Nbpp of the Full sublevel */
} ProfileRule;

/* Format sets of the profile tables. */
#define FMT_444_SET  (FMT_400 | FMT_422 | FMT_444)
#define FMT_4444_SET (FMT_444_SET | FMT_4224 | FMT_4444)
#define FMT_ALL      (FMT_4444_SET | FMT_420)
#define RCT_444      FMT_444
#define RCT_4444     (FMT_444 | FMT_4444)

/* Tables A.1 (Main), A.2 (Light), A.3 as amended by Amd 1:2022 (High), A.4 (MLS), and CHigh 444.12
 * of the 3rd edition (21122-2:2024, parameters of High 444.12 with Rl=1). */
// clang-format off
static const ProfileRule profile_rules[] = {
    /* ppih                          name                    B[i]          NL,y  formats      Cpih=1    Rl MLS Qpih      Cw max Full */
    {JXS_PPIH_MLS_12,               "MLS.12",               BITS_8_10_12, 0, 2, FMT_ALL,      RCT_4444, 0, 1, QPIH_ANY, 0,    48},
    {JXS_PPIH_MAIN_420_12,          "Main 420.12",          BITS_8_10_12, 1, 1, FMT_420,      0,        0, 0, QPIH_ANY, 0,    18},
    {JXS_PPIH_MAIN_422_10,          "Main 422.10",          BITS_8_10,    0, 1, FMT_400 | FMT_422, 0,   0, 0, QPIH_ANY, 0,    20},
    {JXS_PPIH_MAIN_444_12,          "Main 444.12",          BITS_8_10_12, 0, 1, FMT_444_SET,  RCT_444,  0, 0, QPIH_ANY, 0,    36},
    {JXS_PPIH_MAIN_4444_12,         "Main 4444.12",         BITS_8_10_12, 0, 1, FMT_4444_SET, RCT_4444, 0, 0, QPIH_ANY, 0,    48},
    {JXS_PPIH_HIGH_420_12,          "High 420.12",          BITS_8_10_12, 1, 2, FMT_420,      0,        0, 0, QPIH_ANY, 0,    18},
    {JXS_PPIH_HIGH_444_12,          "High 444.12",          BITS_8_10_12, 0, 2, FMT_444_SET,  RCT_444,  0, 0, QPIH_ANY, 0,    36},
    {JXS_PPIH_HIGH_4444_12,         "High 4444.12",         BITS_8_10_12, 0, 2, FMT_4444_SET, RCT_4444, 0, 0, QPIH_ANY, 0,    48},
    {JXS_PPIH_CHIGH_444_12,         "CHigh 444.12",         BITS_8_10_12, 0, 2, FMT_444_SET,  RCT_444,  1, 0, QPIH_ANY, 0,    36},
    /* Light profiles: only checked for profile_ppih_override, never auto-selected. */
    {JXS_PPIH_LIGHT_422_10,         "Light 422.10",         BITS_8_10,    0, 1, FMT_400 | FMT_422, 0,   0, 0, QPIH_DZQ, 0,    20},
    {JXS_PPIH_LIGHT_444_12,         "Light 444.12",         BITS_8_10_12, 0, 1, FMT_444_SET,  RCT_444,  0, 0, QPIH_DZQ, 0,    36},
    {JXS_PPIH_LIGHT_SUBLINE_422_10, "Light-Subline 422.10", BITS_8_10,    0, 0, FMT_400 | FMT_422, 0,   0, 0, QPIH_ANY, 2048, 20},
};
// clang-format on

/* Rows derive_stream_profile_ppih() may select, in preference order. */
#define AUTO_PROFILE_RULES 9

static const ProfileRule* find_profile_rule(uint16_t ppih) {
    for (uint32_t i = 0; i < sizeof(profile_rules) / sizeof(profile_rules[0]); ++i) {
        if (profile_rules[i].ppih == ppih) {
            return &profile_rules[i];
        }
    }
    return NULL;
}

static uint32_t colour_format_flag(ColourFormat_t colour_format) {
    switch (colour_format) {
    case COLOUR_FORMAT_PLANAR_YUV400:
    case COLOUR_FORMAT_GRAY:
        return FMT_400;
    case COLOUR_FORMAT_PLANAR_YUV420:
        return FMT_420;
    case COLOUR_FORMAT_PLANAR_YUV422:
        return FMT_422;
    case COLOUR_FORMAT_PLANAR_YUV444:
    case COLOUR_FORMAT_PLANAR_RGB:
    case COLOUR_FORMAT_PACKED_YUV444_OR_RGB:
        return FMT_444;
    case COLOUR_FORMAT_PLANAR_YUV422_ALPHA:
        return FMT_4224;
    case COLOUR_FORMAT_PLANAR_4_COMPONENTS:
    case COLOUR_FORMAT_PLANAR_RGBA:
        return FMT_4444;
    default:
        return 0;
    }
}

/* Appends one "; "-separated reason to buf; output is silently truncated if buf is full. */
static void append_reason(char* buf, size_t buf_size, size_t* used, const char* format, ...) {
    va_list args;
    if (*used >= buf_size) {
        return;
    }
    if (*used > 0) {
        int n = snprintf(buf + *used, buf_size - *used, "; ");
        *used = (n > 0) ? MIN(*used + (size_t)n, buf_size) : *used;
        if (*used >= buf_size) {
            return;
        }
    }
    va_start(args, format);
    int n = vsnprintf(buf + *used, buf_size - *used, format, args);
    va_end(args);
    *used = (n > 0) ? MIN(*used + (size_t)n, buf_size) : *used;
}

/* Collects every restriction of rule the stream violates; returns their number. */
static uint32_t profile_rule_violations(const ProfileRule* rule, const StreamProfileParams* params, char* buf, size_t buf_size) {
    size_t used = 0;
    uint32_t count = 0;
    const uint32_t format = colour_format_flag(params->colour_format);
    buf[0] = '\0';

    if (params->bit_depth >= 32 || !(rule->bit_depths & (1u << params->bit_depth))) {
        append_reason(buf, buf_size, &used, "bit depth %u is not allowed", params->bit_depth);
        count++;
    }
    if (!(rule->formats & format)) {
        append_reason(buf, buf_size, &used, "colour format is not allowed");
        count++;
    }
    if (params->ndecomp_v < rule->ndecomp_v_min || params->ndecomp_v > rule->ndecomp_v_max) {
        append_reason(buf,
                      buf_size,
                      &used,
                      "%u vertical decompositions, allowed %u to %u",
                      params->ndecomp_v,
                      rule->ndecomp_v_min,
                      rule->ndecomp_v_max);
        count++;
    }
    if (params->raw_mode_per_packet != rule->raw_mode) {
        append_reason(buf,
                      buf_size,
                      &used,
                      "raw-mode selection per packet Rl=%u, required Rl=%u",
                      params->raw_mode_per_packet,
                      rule->raw_mode);
        count++;
    }
    if (params->lossless != rule->lossless) {
        append_reason(
            buf, buf_size, &used, rule->lossless ? "requires lossless coding (Fq=0)" : "lossless coding (Fq=0) is not allowed");
        count++;
    }
    if (params->colour_transform && !(rule->rct_formats & format)) {
        append_reason(buf, buf_size, &used, "colour transform (Cpih=1) is not allowed");
        count++;
    }
    if (params->quantization >= 32 || !(rule->quantizers & (1u << params->quantization))) {
        append_reason(buf, buf_size, &used, "quantizer type Qpih=%u is not allowed", params->quantization);
        count++;
    }
    if (rule->max_column_width != 0 && params->width > rule->max_column_width) {
        append_reason(buf,
                      buf_size,
                      &used,
                      "column width %u exceeds %u (the encoder writes a single full-width column)",
                      params->width,
                      rule->max_column_width);
        count++;
    }
    if (params->slice_height_rows != 16) {
        append_reason(buf, buf_size, &used, "slice height is %u rows, not 16", params->slice_height_rows);
        count++;
    }
    if (params->long_header) {
        append_reason(buf, buf_size, &used, "long precinct headers are enforced (Lh=1)");
        count++;
    }
    return count;
}

uint16_t derive_stream_profile_ppih(const StreamProfileParams* params, uint32_t verbose) {
    char reasons[384];
    for (uint32_t i = 0; i < AUTO_PROFILE_RULES; ++i) {
        if (profile_rule_violations(&profile_rules[i], params, reasons, sizeof(reasons)) == 0) {
            return profile_rules[i].ppih;
        }
    }

    if (verbose >= VERBOSE_WARNINGS) {
        /* Name the restrictions every candidate shares, plus the raw-mode ones that decide between
         * MLS.12/Main/High (Rl=0) and CHigh 444.12 (Rl=1). */
        size_t used = 0;
        reasons[0] = '\0';
        if (params->bit_depth != 8 && params->bit_depth != 10 && params->bit_depth != 12) {
            append_reason(reasons, sizeof(reasons), &used, "bit depth %u is not 8, 10 or 12", params->bit_depth);
        }
        if (params->slice_height_rows != 16) {
            append_reason(reasons, sizeof(reasons), &used, "slice height is %u rows, not 16", params->slice_height_rows);
        }
        if (params->long_header) {
            append_reason(reasons, sizeof(reasons), &used, "long precinct headers are enforced (Lh=1)");
        }
        if (params->raw_mode_per_packet && params->lossless) {
            append_reason(reasons, sizeof(reasons), &used, "MLS.12 requires raw-mode selection per packet to be disabled (Rl=0)");
        }
        else if (params->raw_mode_per_packet && !(colour_format_flag(params->colour_format) & (FMT_400 | FMT_422 | FMT_444))) {
            append_reason(reasons,
                          sizeof(reasons),
                          &used,
                          "raw-mode selection per packet (Rl=1) needs CHigh 444.12, which does not cover this colour "
                          "format, and Main/High require Rl=0");
        }
        if (used == 0) {
            append_reason(reasons, sizeof(reasons), &used, "no profile allows this combination");
        }
        SVT_WARN(
            "Warning: no ISO/IEC 21122-2 profile matches this configuration (%s); declaring the Unrestricted "
            "profile (Ppih=0x0000). Use profile_ppih_override if a different declaration is required.\n",
            reasons);
    }
    return JXS_PPIH_UNRESTRICTED;
}

uint32_t check_stream_profile_ppih(uint16_t ppih, const StreamProfileParams* params, uint32_t verbose) {
    if (ppih == JXS_PPIH_UNRESTRICTED) {
        return 0;
    }
    const ProfileRule* rule = find_profile_rule(ppih);
    if (rule == NULL) {
        if (verbose >= VERBOSE_SYSTEM_INFO) {
            SVT_WARN(
                "Warning: profile_ppih_override 0x%04X is not a profile this encoder can produce (Bayer, TDC and "
                "MLS.16 profiles need coding tools it does not use, other values are not ISO/IEC 21122-2 profiles); "
                "the stream does not meet it.\n",
                ppih);
        }
        return 1;
    }
    char reasons[512];
    const uint32_t count = profile_rule_violations(rule, params, reasons, sizeof(reasons));
    if (count && verbose >= VERBOSE_SYSTEM_INFO) {
        SVT_WARN("Warning: profile_ppih_override 0x%04X (%s) does not match the stream: %s.\n", ppih, rule->name, reasons);
    }
    return count;
}

/* ISO/IEC 21122-2 Table A.8 levels, in table order, plus 5k-1 of 21122-2:2024/Amd 1. */
typedef struct LevelBucket {
    uint32_t max_width;      /* Wmax */
    uint32_t max_height;     /* Hmax */
    uint64_t max_pixels;     /* Lmax */
    uint64_t max_pixel_rate; /* Rs,max, sampling grid points per second */
    uint16_t level_bits;
    uint8_t auto_select; /* 0: only accepted via level_plev_override */
    const char* name;
} LevelBucket;

/* derive_level_bits() picks the first auto-selectable row that bounds the picture size and the
 * sampling grid point rate, which is the lowest such level. 5k-1 is not auto-selected: the
 * ISO/IEC 21122-5 edition 3 reference decoder rejects it, and 8k-1 bounds every picture 5k-1 does. */
static const LevelBucket levels_ascending[] = {
    {1280, 5120, 2621440ULL, 83558400ULL, JXS_PLEV_LEVEL_1K_1, 1, "1k-1"},
    {2048, 8192, 4194304ULL, 133693440ULL, JXS_PLEV_LEVEL_2K_1, 1, "2k-1"},
    {4096, 16384, 8912896ULL, 267386880ULL, JXS_PLEV_LEVEL_4K_1, 1, "4k-1"},
    {4096, 16384, 16777216ULL, 534773760ULL, JXS_PLEV_LEVEL_4K_2, 1, "4k-2"},
    {4096, 16384, 16777216ULL, 1069547520ULL, JXS_PLEV_LEVEL_4K_3, 1, "4k-3"},
    {5120, 16384, 16777216ULL, 1069547520ULL, JXS_PLEV_LEVEL_5K_1, 0, "5k-1"},
    {8192, 32768, 35651584ULL, 1069547520ULL, JXS_PLEV_LEVEL_8K_1, 1, "8k-1"},
    {8192, 32768, 67108864ULL, 2139095040ULL, JXS_PLEV_LEVEL_8K_2, 1, "8k-2"},
    {8192, 32768, 67108864ULL, 4278190080ULL, JXS_PLEV_LEVEL_8K_3, 1, "8k-3"},
    {10240, 40960, 104857600ULL, 3342336000ULL, JXS_PLEV_LEVEL_10K_1, 1, "10k-1"},
};

#define PLEV_LEVEL_MASK    0xFC00u
#define PLEV_FBBLEVEL_MASK 0x0360u /* Frame buffer bandwidth level, TDC profiles only. */
#define PLEV_SUBLEVEL_MASK 0x009Fu

static int level_bounds_picture(const LevelBucket* bucket, uint32_t width, uint32_t height) {
    const uint64_t pixels = (uint64_t)width * height;
    return width <= bucket->max_width && height <= bucket->max_height && pixels <= bucket->max_pixels;
}

/* pixels * fps <= Rs,max, cross-multiplied. Only called once pixels <= Lmax (at most 104857600, below
 * 2^27), so pixels * fps_numerator stays below 2^59; Rs,max * fps_denominator is below 2^64. */
static int level_bounds_rate(const LevelBucket* bucket, uint32_t width, uint32_t height, uint32_t fps_numerator,
                             uint32_t fps_denominator) {
    const uint64_t pixels = (uint64_t)width * height;
    return pixels * fps_numerator <= bucket->max_pixel_rate * fps_denominator;
}

static uint16_t derive_level_bits(uint32_t width, uint32_t height, uint32_t fps_numerator, uint32_t fps_denominator) {
    for (uint32_t i = 0; i < sizeof(levels_ascending) / sizeof(levels_ascending[0]); ++i) {
        const LevelBucket* bucket = &levels_ascending[i];
        if (bucket->auto_select && level_bounds_picture(bucket, width, height) &&
            level_bounds_rate(bucket, width, height, fps_numerator, fps_denominator)) {
            return bucket->level_bits;
        }
    }
    return JXS_PLEV_LEVEL_UNRESTRICTED;
}

/* Table A.9 sublevels with a fixed nominal bpp, ascending, plus Sublev4bpp of Amd 1:2022. */
typedef struct SublevelBucket {
    uint32_t nominal_bpp;
    uint16_t sublevel_bits;
} SublevelBucket;

static const SublevelBucket sublevels_ascending[] = {
    {2, JXS_PLEV_SUBLEVEL_2BPP},
    {3, JXS_PLEV_SUBLEVEL_3BPP},
    {4, JXS_PLEV_SUBLEVEL_4BPP},
    {6, JXS_PLEV_SUBLEVEL_6BPP},
    {9, JXS_PLEV_SUBLEVEL_9BPP},
    {12, JXS_PLEV_SUBLEVEL_12BPP},
};

/* frame_bytes * 8 <= Nbpp * width * height: the bits per pixel of the codestream stay within Nbpp. */
static int frame_fits_nominal_bpp(uint64_t frame_bytes, uint32_t width, uint32_t height, uint32_t nominal_bpp) {
    return frame_bytes * 8 <= (uint64_t)nominal_bpp * width * height;
}

static uint16_t derive_sublevel_bits(uint64_t frame_bytes, uint32_t width, uint32_t height) {
    for (uint32_t i = 0; i < sizeof(sublevels_ascending) / sizeof(sublevels_ascending[0]); ++i) {
        if (frame_fits_nominal_bpp(frame_bytes, width, height, sublevels_ascending[i].nominal_bpp)) {
            return sublevels_ascending[i].sublevel_bits;
        }
    }
    return JXS_PLEV_SUBLEVEL_UNRESTRICTED;
}

uint16_t derive_stream_level_plev(const StreamLevelParams* params, uint32_t verbose) {
    uint16_t level_bits = JXS_PLEV_LEVEL_UNRESTRICTED;
    if (params->fps_numerator == 0 || params->fps_denominator == 0) {
        if (verbose >= VERBOSE_WARNINGS) {
            SVT_WARN(
                "Warning: frame rate unknown (fps_numerator/fps_denominator not set); an ISO/IEC 21122-2 level bounds "
                "the sample rate, so the Unrestricted level is declared. Set the frame rate or use "
                "level_plev_override to declare a level.\n");
        }
    }
    else {
        level_bits = derive_level_bits(params->width, params->height, params->fps_numerator, params->fps_denominator);
        if (level_bits == JXS_PLEV_LEVEL_UNRESTRICTED && verbose >= VERBOSE_WARNINGS) {
            SVT_WARN("Warning: %ux%u at %u/%u fps exceeds every ISO/IEC 21122-2 level; declaring the Unrestricted level.\n",
                     params->width,
                     params->height,
                     params->fps_numerator,
                     params->fps_denominator);
        }
    }

    uint16_t sublevel_bits = JXS_PLEV_SUBLEVEL_UNRESTRICTED;
    if (!params->lossless && params->frame_bytes != 0) {
        sublevel_bits = derive_sublevel_bits(params->frame_bytes, params->width, params->height);
    }
    return (uint16_t)(level_bits | sublevel_bits);
}

uint32_t check_stream_level_plev(uint16_t plev, uint16_t ppih, const StreamLevelParams* params, uint32_t verbose) {
    char reasons[512];
    size_t used = 0;
    uint32_t count = 0;
    reasons[0] = '\0';

    const uint16_t level_bits = plev & PLEV_LEVEL_MASK;
    if (level_bits != JXS_PLEV_LEVEL_UNRESTRICTED) {
        const LevelBucket* bucket = NULL;
        for (uint32_t i = 0; i < sizeof(levels_ascending) / sizeof(levels_ascending[0]); ++i) {
            if (levels_ascending[i].level_bits == level_bits) {
                bucket = &levels_ascending[i];
            }
        }
        if (bucket == NULL) {
            append_reason(reasons, sizeof(reasons), &used, "level code 0x%04X is not an ISO/IEC 21122-2 level", level_bits);
            count++;
        }
        else if (!level_bounds_picture(bucket, params->width, params->height)) {
            append_reason(reasons,
                          sizeof(reasons),
                          &used,
                          "%ux%u exceeds level %s (at most %ux%u and %llu pixels)",
                          params->width,
                          params->height,
                          bucket->name,
                          bucket->max_width,
                          bucket->max_height,
                          (unsigned long long)bucket->max_pixels);
            count++;
        }
        else if (params->fps_numerator != 0 && params->fps_denominator != 0 &&
                 !level_bounds_rate(bucket, params->width, params->height, params->fps_numerator, params->fps_denominator)) {
            append_reason(reasons,
                          sizeof(reasons),
                          &used,
                          "%ux%u at %u/%u fps exceeds the pixel rate of level %s (%llu per second)",
                          params->width,
                          params->height,
                          params->fps_numerator,
                          params->fps_denominator,
                          bucket->name,
                          (unsigned long long)bucket->max_pixel_rate);
            count++;
        }
    }

    if (plev & PLEV_FBBLEVEL_MASK) {
        append_reason(
            reasons, sizeof(reasons), &used, "frame buffer bandwidth level bits are set, but only TDC profiles use them");
        count++;
    }

    const uint16_t sublevel_bits = plev & PLEV_SUBLEVEL_MASK;
    if (sublevel_bits != JXS_PLEV_SUBLEVEL_UNRESTRICTED) {
        uint32_t nominal_bpp = 0;
        if (sublevel_bits == JXS_PLEV_SUBLEVEL_FULL) {
            const ProfileRule* rule = find_profile_rule(ppih);
            if (ppih == JXS_PPIH_UNRESTRICTED) {
                append_reason(reasons, sizeof(reasons), &used, "the Full sublevel requires a named profile (Table A.9)");
                count++;
            }
            else if (rule != NULL) {
                nominal_bpp = rule->max_decoded_bpp;
            }
        }
        else {
            for (uint32_t i = 0; i < sizeof(sublevels_ascending) / sizeof(sublevels_ascending[0]); ++i) {
                if (sublevels_ascending[i].sublevel_bits == sublevel_bits) {
                    nominal_bpp = sublevels_ascending[i].nominal_bpp;
                }
            }
            if (nominal_bpp == 0) {
                append_reason(
                    reasons, sizeof(reasons), &used, "sublevel code 0x%02X is not an ISO/IEC 21122-2 sublevel", sublevel_bits);
                count++;
            }
        }
        if (nominal_bpp != 0) {
            if (params->lossless) {
                append_reason(reasons,
                              sizeof(reasons),
                              &used,
                              "lossless output size depends on the content, so no sublevel bound is guaranteed");
                count++;
            }
            else if (params->frame_bytes != 0 &&
                     !frame_fits_nominal_bpp(params->frame_bytes, params->width, params->height, nominal_bpp)) {
                append_reason(reasons,
                              sizeof(reasons),
                              &used,
                              "%llu bytes per frame exceed the sublevel bound of %u bpp",
                              (unsigned long long)params->frame_bytes,
                              nominal_bpp);
                count++;
            }
        }
    }

    if (count && verbose >= VERBOSE_SYSTEM_INFO) {
        SVT_WARN("Warning: level_plev_override 0x%04X does not match the stream: %s.\n", plev, reasons);
    }
    return count;
}
