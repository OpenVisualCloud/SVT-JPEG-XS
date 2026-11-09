/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _JPEGXS_ENCODER_PROFILE_LEVEL_H_
#define _JPEGXS_ENCODER_PROFILE_LEVEL_H_

#include <stdint.h>
#include "SvtJpegxs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ISO/IEC 21122-2 Annex A profile identifiers (Ppih).
 * EncHandle.c validates the finalized configuration before selecting a named
 * profile. Unrestricted (0) is used when these named profiles do not apply.
 * The format/depth helper below is retained as a candidate selector only. */
typedef enum StreamProfilePpih {
    JXS_PPIH_LIGHT_422_10 = 0x1500,
    JXS_PPIH_LIGHT_444_12 = 0x1A00,
    JXS_PPIH_LIGHT_SUBLINE_422_10 = 0x2500,
    JXS_PPIH_MAIN_420_12 = 0x3240,
    JXS_PPIH_MAIN_422_10 = 0x3540,
    JXS_PPIH_MAIN_444_12 = 0x3A40,
    JXS_PPIH_MAIN_4444_12 = 0x3E40,
    JXS_PPIH_HIGH_420_12 = 0x4240,
    JXS_PPIH_HIGH_444_12 = 0x4A40,
    JXS_PPIH_HIGH_4444_12 = 0x4E40,
    JXS_PPIH_MLS_12 = 0x6EC0, /* MLS.12; the encoder uses Br=4 and does not claim MLS.16. */
} StreamProfilePpih;

/* ISO/IEC 21122-2 Annex A level identifiers, pre-shifted into their Plev bit position (bits [15:10]). */
typedef enum StreamLevelPlev {
    JXS_PLEV_LEVEL_UNRESTRICTED = 0x0000,
    JXS_PLEV_LEVEL_1K_1 = 0x0001 << 10,
    JXS_PLEV_LEVEL_2K_1 = 0x0004 << 10,
    JXS_PLEV_LEVEL_4K_1 = 0x0008 << 10,
    JXS_PLEV_LEVEL_4K_2 = 0x0009 << 10,
    JXS_PLEV_LEVEL_4K_3 = 0x000A << 10,
    JXS_PLEV_LEVEL_5K_1 = 0x000B << 10,
    JXS_PLEV_LEVEL_8K_1 = 0x000C << 10,
    JXS_PLEV_LEVEL_8K_2 = 0x000D << 10,
    JXS_PLEV_LEVEL_8K_3 = 0x000E << 10,
    JXS_PLEV_LEVEL_10K_1 = 0x0010 << 10,
} StreamLevelPlev;

/* Sublevel (nominal bpp bound) bit patterns, already positioned within the Plev field.
 * FBB-level bits are always left at 0 (Unrestricted): this encoder never emits TDC profiles, which
 * are the only profiles that require a frame-buffer bandwidth bound. */
typedef enum StreamSublevelPlev {
    JXS_PLEV_SUBLEVEL_UNRESTRICTED = 0x00,
    JXS_PLEV_SUBLEVEL_12BPP = 0x10,
    JXS_PLEV_SUBLEVEL_9BPP = 0x0C,
    JXS_PLEV_SUBLEVEL_6BPP = 0x08,
    JXS_PLEV_SUBLEVEL_4BPP = 0x06,
    JXS_PLEV_SUBLEVEL_3BPP = 0x04,
    JXS_PLEV_SUBLEVEL_2BPP = 0x03,
} StreamSublevelPlev;

/* Legacy format/depth candidate selector; this does not validate a complete
 * profile declaration. encoder_init_configuration() additionally checks precision,
 * vertical levels, raw mode, slice height and long-header enforcement. */
uint16_t derive_stream_profile_ppih(ColourFormat_t colour_format, uint8_t bit_depth, uint32_t lossless_enable,
                                    uint32_t verbose);

/* Derive the Plev (level + sublevel) codeword from picture resolution and target bits-per-pixel.
 * Always returns a validly-defined Plev codeword: falls back to the "Unrestricted" level and/or
 * sublevel when resolution or bpp exceed every specifically defined bucket. When several defined
 * levels bound the same resolution, the variant with the highest declared pixel-rate (Rs,max) is
 * selected, since the encoder has no explicit frame-rate input to disambiguate and over-declaring
 * throughput capability is always a conformant (if less specific) choice; under-declaring would not
 * be. bpp_numerator/bpp_denominator are ignored (sublevel forced Unrestricted) when lossless_enable
 * is set, since lossless output size is data-dependent and not bounded by a nominal bpp target
 * (Table A.9 NOTE 3). */
uint16_t derive_stream_level_plev(uint32_t width, uint32_t height, uint32_t bpp_numerator, uint32_t bpp_denominator,
                                  uint32_t lossless_enable);

#ifdef __cplusplus
}
#endif

#endif /*_JPEGXS_ENCODER_PROFILE_LEVEL_H_*/
