/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _JPEGXS_PROFILE_LEVEL_NAMES_H_
#define _JPEGXS_PROFILE_LEVEL_NAMES_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable names for ISO/IEC 21122-2 Annex A Ppih/Plev codewords, used by the encoder and
 * decoder config printouts. Codeword values mirror Source/Lib/Encoder/Codec/ProfileLevel.h. */

/* Returns the profile name (e.g. "Main 422.10", "MLS.12"), or NULL if ppih is not a known codeword. */
const char* jxs_profile_name(uint16_t ppih);

/* Writes "<level>, <sublevel>" (e.g. "2k-1, Sublev3bpp") into buf and returns buf, or returns NULL
 * (buf left untouched) if any part of plev is not a known codeword. */
const char* jxs_level_name(uint16_t plev, char* buf, size_t buf_size);

/* Writes "0x6EC0 (MLS.12) / 0x1000 (2k-1, Sublevel Unrestricted)" into buf; a name is omitted for
 * any codeword that is not known. buf_size of JXS_PROFILE_LEVEL_STR_SIZE is always sufficient. */
#define JXS_PROFILE_LEVEL_STR_SIZE 128
const char* jxs_profile_level_str(uint16_t ppih, uint16_t plev, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /*_JPEGXS_PROFILE_LEVEL_NAMES_H_*/
