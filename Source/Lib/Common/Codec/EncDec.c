/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "EncDec.h"

uint8_t compute_truncation(uint8_t gain, uint8_t priority, uint8_t quantization, uint8_t refinement) {
    uint8_t pump_up = 0;
    if (priority < refinement) {
        pump_up = 1;
    }
    if (quantization < (gain + pump_up)) {
        return 0;
    }
    uint8_t truncation = quantization - gain - pump_up;
    if (truncation > TRUNCATION_MAX) {
        truncation = TRUNCATION_MAX;
    }
    return truncation;
}

void precinct_compute_truncation_light(const pi_t* pi, precinct_t* precinct, uint8_t quantization, uint8_t refinement) {
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
            precinct->bands[c][b].gtli = compute_truncation(
                pi->components[c].bands[b].gain, pi->components[c].bands[b].priority, quantization, refinement);
        }
    }
}

/* precinct_compute_truncation()
 *Return:
 * 0 - Find correct values to truncation
 * 1 - Incorrect parameters, truncation will zero-out all coefficient for all bands in quantization
 */
uint8_t precinct_compute_truncation(const pi_t* pi, precinct_t* precinct, uint8_t quantization, uint8_t refinement) {
    uint8_t ret = 1;
    for (uint32_t c = 0; c < pi->comps_num; c++) {
        for (uint32_t b = 0; b < pi->components[c].bands_num; b++) {
            precinct_band_t* band = &precinct->bands[c][b];
            band->gtli = compute_truncation(
                pi->components[c].bands[b].gain, pi->components[c].bands[b].priority, quantization, refinement);
            if (band->gtli != TRUNCATION_MAX) {
                ret = 0;
            }
        }
    }
    return ret;
}

const char* get_asm_level_name_str(CPU_FLAGS cpu_flags) {
    const struct {
        const char* name;
        CPU_FLAGS flags;
    } param_maps[] = {{"c", CPU_FLAGS_C},
                      {"mmx", CPU_FLAGS_MMX},
                      {"sse", CPU_FLAGS_SSE},
                      {"sse2", CPU_FLAGS_SSE2},
                      {"sse3", CPU_FLAGS_SSE3},
                      {"ssse3", CPU_FLAGS_SSSE3},
                      {"sse4_1", CPU_FLAGS_SSE4_1},
                      {"sse4_2", CPU_FLAGS_SSE4_2},
                      {"avx", CPU_FLAGS_AVX},
                      {"avx2", CPU_FLAGS_AVX2},
                      {"avx512", CPU_FLAGS_AVX512F}};
    const uint32_t para_map_size = sizeof(param_maps) / sizeof(param_maps[0]);
    int32_t i;

    for (i = para_map_size - 1; i >= 0; --i) {
        if (param_maps[i].flags & cpu_flags) {
            return param_maps[i].name;
        }
    }
    return "c";
}

const char* svt_jpeg_xs_get_format_name(ColourFormat_t format) {
    if (COLOUR_FORMAT_PLANAR_YUV400 == format) {
        return "PLANAR YUV400";
    }
    else if (COLOUR_FORMAT_PLANAR_YUV420 == format) {
        return "PLANAR YUV420";
    }
    else if (COLOUR_FORMAT_PLANAR_YUV422 == format) {
        return "PLANAR YUV422";
    }
    else if (COLOUR_FORMAT_PLANAR_YUV444_OR_RGB == format) {
        return "PLANAR YUV444 OR RGB";
    }
    else if (COLOUR_FORMAT_PLANAR_4_COMPONENTS == format) {
        return "UNKNOWN FORMAT COMPONENTS 4";
    }
    else if (COLOUR_FORMAT_GRAY == format) {
        return "UNKNOWN GRAY";
    }
    else if (COLOUR_FORMAT_PACKED_YUV444_OR_RGB == format) {
        return "PACKED YUV444 OR RGB";
    }
    else {
        return "UNKNOWN FORMAT";
    }
}
