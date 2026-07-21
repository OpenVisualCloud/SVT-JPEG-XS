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
 * Only the "Main" family is auto-derivable by derive_codestream_profile_ppih() below: this encoder
 * never signals Star-Tetrix/RAW-CFA, non-linear transforms, component-dependent decomposition or
 * lossless coding (see write_capabilities_marker()), so Bayer/TDC/MLS profiles are out of scope, and
 * Light/High family membership additionally implies encoder-complexity guarantees that cannot be
 * safely inferred from configuration alone. Light/High/other values are only reachable via the
 * profile_override_enable/profile_ppih_override API fields. */
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

/* Derive the Ppih (profile) codeword from the encoder configuration.
 * Always returns a validly-defined "Main" family Ppih codeword for the supported bit-depth range
 * (8-12 bit); for the encoder's extended 13-14 bit range (outside every defined ISO/IEC 21122-2 lossy
 * profile) the closest Main profile for the given colour format is returned and a warning is printed
 * when verbose >= VERBOSE_WARNINGS. */
uint16_t derive_stream_profile_ppih(ColourFormat_t colour_format, uint8_t bit_depth, uint32_t verbose);

/* Derive the Plev (level + sublevel) codeword from picture resolution and target bits-per-pixel.
 * Always returns a validly-defined Plev codeword: falls back to the "Unrestricted" level and/or
 * sublevel when resolution or bpp exceed every specifically defined bucket. When several defined
 * levels bound the same resolution, the variant with the highest declared pixel-rate (Rs,max) is
 * selected, since the encoder has no explicit frame-rate input to disambiguate and over-declaring
 * throughput capability is always a conformant (if less specific) choice; under-declaring would not
 * be. */
uint16_t derive_stream_level_plev(uint32_t width, uint32_t height, uint32_t bpp_numerator, uint32_t bpp_denominator);

#ifdef __cplusplus
}
#endif

#endif /*_JPEGXS_ENCODER_PROFILE_LEVEL_H_*/
