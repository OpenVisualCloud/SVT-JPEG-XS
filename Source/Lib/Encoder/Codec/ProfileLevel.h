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

/* ISO/IEC 21122-2 Annex A profile identifiers (Ppih), Table A.7.
 * derive_stream_profile_ppih() below only selects the Main, High, CHigh and MLS.12 profiles, and
 * Unrestricted when none of them applies. The Light family and other values are only reachable via
 * the profile_ppih_override API field. Bayer and TDC profiles are out of scope: this encoder never
 * signals Star-Tetrix/CFA, non-linear transforms or temporal differential coding. */
typedef enum StreamProfilePpih {
    JXS_PPIH_UNRESTRICTED = 0x0000, /* Full Part 1 syntax; valid, but not a conformance point. */
    JXS_PPIH_LIGHT_422_10 = 0x1500,
    JXS_PPIH_LIGHT_444_12 = 0x1A00,
    JXS_PPIH_LIGHT_SUBLINE_422_10 = 0x2500,
    JXS_PPIH_MAIN_420_12 = 0x3240,
    JXS_PPIH_MAIN_422_10 = 0x3540,
    JXS_PPIH_MAIN_444_12 = 0x3A40,
    JXS_PPIH_MAIN_4444_12 = 0x3E40,
    JXS_PPIH_HIGH_420_12 = 0x4240,
    JXS_PPIH_HIGH_444_12 = 0x4A40,
    JXS_PPIH_CHIGH_444_12 = 0x4A44, /* 3rd edition: High 444.12 with raw-mode selection per packet (Rl=1). */
    JXS_PPIH_HIGH_4444_12 = 0x4E40,
    JXS_PPIH_MLS_12 = 0x6EC0,
} StreamProfilePpih;

/* Finalized codestream parameters that decide which profile a stream conforms to. Parameters this
 * encoder always writes with values every selectable profile allows (Cw=0, Sd=0, NL,x in 1..5,
 * Fq=8/Bw=20 for lossy and Fq=0/Bw=B for lossless coding) are not listed. */
typedef struct StreamProfileParams {
    ColourFormat_t colour_format;
    uint32_t width;              /* Wf; the encoder writes Cw=0, so the column width is Wf. */
    uint8_t bit_depth;           /* B[i], identical for all components. */
    uint8_t ndecomp_v;           /* NL,y. */
    uint32_t slice_height_rows;  /* Hsl multiplied by the precinct height (2^NL,y rows). */
    uint8_t raw_mode_per_packet; /* Rl. */
    uint8_t long_header;         /* Lh. */
    uint8_t lossless;            /* Fq=0, Bw=B[i]. */
    uint8_t quantization;        /* Qpih: 0 deadzone, 1 uniform. */
    uint8_t colour_transform;    /* Cpih=1 (RCT). */
} StreamProfileParams;

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

/* Derive the Ppih (profile) codeword from the finalized codestream parameters.
 * Returns the first profile, in the order MLS.12, Main 420.12, Main 422.10, Main 444.12,
 * Main 4444.12, High 420.12, High 444.12, High 4444.12, CHigh 444.12, whose restrictions
 * (ISO/IEC 21122-2 Tables A.1, A.3 and A.4, and CHigh 444.12 of the 3rd edition) the stream meets.
 * All of them require B[i] in {8, 10, 12}, a slice height of 16 rows and Lh=0; MLS.12, Main and
 * High require Rl=0, CHigh requires Rl=1. Light profiles are never selected: their smaller decoder
 * smoothing buffer (Nsbu=4) is not guaranteed by the rate control. When no profile applies, returns
 * JXS_PPIH_UNRESTRICTED and, when verbose >= VERBOSE_WARNINGS, prints a warning that names the
 * failed restrictions. */
uint16_t derive_stream_profile_ppih(const StreamProfileParams* params, uint32_t verbose);

/* Check a caller-declared Ppih (profile_ppih_override) against the stream parameters. Prints one
 * warning per violated restriction when verbose >= VERBOSE_SYSTEM_INFO, and one for a codepoint
 * that is not a profile this encoder can produce. Returns the number of problems found (0 = the
 * declaration holds). Unrestricted (0x0000) always holds. */
uint32_t check_stream_profile_ppih(uint16_t ppih, const StreamProfileParams* params, uint32_t verbose);

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
