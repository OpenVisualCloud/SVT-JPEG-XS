/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "ProfileLevel.h"

#include <stdio.h>

uint16_t derive_stream_profile_ppih(ColourFormat_t colour_format, uint8_t bit_depth, uint32_t verbose) {
    uint16_t ppih;

    switch (colour_format) {
    case COLOUR_FORMAT_PLANAR_YUV420:
        ppih = JXS_PPIH_MAIN_420_12;
        break;
    case COLOUR_FORMAT_PLANAR_4_COMPONENTS:
        ppih = JXS_PPIH_MAIN_4444_12;
        break;
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
    case COLOUR_FORMAT_PACKED_YUV444_OR_RGB:
        ppih = JXS_PPIH_MAIN_444_12;
        break;
    case COLOUR_FORMAT_PLANAR_YUV422:
    case COLOUR_FORMAT_PLANAR_YUV400:
    default:
        /* Main 422.10 also covers 4:0:0 (grayscale); Main 444.12 is the closest defined profile once
         * bit depth exceeds what Main 422.10 permits (8 or 10 bit). */
        ppih = (bit_depth <= 10) ? JXS_PPIH_MAIN_422_10 : JXS_PPIH_MAIN_444_12;
        break;
    }

    /* Every "Main" profile above tops out at 12-bit; the encoder additionally allows 13/14-bit input
     * (see encoder_init_configuration()), a combination with no defined ISO/IEC 21122-2 lossy profile.
     * Keep declaring the closest Main profile (still a validly-defined codeword, unlike Ppih=0) but
     * make the limitation visible. */
    if (bit_depth > 12 && verbose >= VERBOSE_WARNINGS) {
        fprintf(stderr,
                "Warning: bit depth %u exceeds the maximum (12) defined by any ISO/IEC 21122-2 lossy profile; "
                "declaring the closest Main profile (Ppih=0x%04X). Use profile_override_enable if a different "
                "declaration is required.\n",
                bit_depth,
                ppih);
    }

    return ppih;
}

/* Level bucket, ascending. Matching resolution/pixel-count picks the first (tightest) row; where two
 * rows share identical bounds (4k-2/4k-3, 8k-2/8k-3) only the higher-throughput variant is listed,
 * since it is always a safe (if less specific) choice without an explicit frame-rate input. */
typedef struct LevelBucket {
    uint32_t max_width;
    uint32_t max_height;
    uint64_t max_pixels;
    uint16_t level_bits;
} LevelBucket;

static const LevelBucket levels_ascending[] = {
    {1280, 5120, 2621440ULL, JXS_PLEV_LEVEL_1K_1},
    {2048, 8192, 4194304ULL, JXS_PLEV_LEVEL_2K_1},
    {4096, 16384, 8912896ULL, JXS_PLEV_LEVEL_4K_1},
    {4096, 16384, 16777216ULL, JXS_PLEV_LEVEL_4K_3},
    {5120, 16384, 16777216ULL, JXS_PLEV_LEVEL_5K_1},
    {8192, 32768, 35651584ULL, JXS_PLEV_LEVEL_8K_1},
    {8192, 32768, 67108864ULL, JXS_PLEV_LEVEL_8K_3},
    {10240, 40960, 104857600ULL, JXS_PLEV_LEVEL_10K_1},
};

static uint16_t derive_level_bits(uint32_t width, uint32_t height) {
    const uint64_t pixels = (uint64_t)width * height;
    const uint32_t buckets_num = sizeof(levels_ascending) / sizeof(levels_ascending[0]);

    for (uint32_t i = 0; i < buckets_num; ++i) {
        const LevelBucket* bucket = &levels_ascending[i];
        if (width <= bucket->max_width && height <= bucket->max_height && pixels <= bucket->max_pixels) {
            return bucket->level_bits;
        }
    }
    return JXS_PLEV_LEVEL_UNRESTRICTED;
}

static uint16_t derive_sublevel_bits(uint32_t bpp_numerator, uint32_t bpp_denominator) {
    /* Compare the bpp fraction against each bucket via cross-multiplication to avoid float rounding. */
    if (bpp_numerator <= 2 * bpp_denominator) {
        return JXS_PLEV_SUBLEVEL_2BPP;
    }
    if (bpp_numerator <= 3 * bpp_denominator) {
        return JXS_PLEV_SUBLEVEL_3BPP;
    }
    if (bpp_numerator <= 4 * bpp_denominator) {
        return JXS_PLEV_SUBLEVEL_4BPP;
    }
    if (bpp_numerator <= 6 * bpp_denominator) {
        return JXS_PLEV_SUBLEVEL_6BPP;
    }
    if (bpp_numerator <= 9 * bpp_denominator) {
        return JXS_PLEV_SUBLEVEL_9BPP;
    }
    if (bpp_numerator <= 12 * bpp_denominator) {
        return JXS_PLEV_SUBLEVEL_12BPP;
    }
    return JXS_PLEV_SUBLEVEL_UNRESTRICTED;
}

uint16_t derive_stream_level_plev(uint32_t width, uint32_t height, uint32_t bpp_numerator, uint32_t bpp_denominator) {
    uint16_t level_bits = derive_level_bits(width, height);
    uint16_t sublevel_bits = (bpp_denominator == 0) ? JXS_PLEV_SUBLEVEL_UNRESTRICTED
                                                     : derive_sublevel_bits(bpp_numerator, bpp_denominator);
    return (uint16_t)(level_bits | sublevel_bits);
}
