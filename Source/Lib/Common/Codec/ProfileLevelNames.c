/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "ProfileLevelNames.h"
#include <stdio.h>

typedef struct CodewordName {
    uint16_t value;
    const char* name;
} CodewordName;

static const CodewordName profile_names[] = {
    {0x1500, "Light 422.10"},
    {0x1A00, "Light 444.12"},
    {0x2500, "Light-Subline 422.10"},
    {0x3240, "Main 420.12"},
    {0x3540, "Main 422.10"},
    {0x3A40, "Main 444.12"},
    {0x3E40, "Main 4444.12"},
    {0x4240, "High 420.12"},
    {0x4A40, "High 444.12"},
    {0x4E40, "High 4444.12"},
    {0x6EC0, "MLS.12"},
};

/* Level occupies Plev bits [15:10]. */
#define PLEV_LEVEL_MASK 0xFC00u
static const CodewordName level_names[] = {
    {0x0000, "Unrestricted"},
    {0x0001 << 10, "1k-1"},
    {0x0004 << 10, "2k-1"},
    {0x0008 << 10, "4k-1"},
    {0x0009 << 10, "4k-2"},
    {0x000A << 10, "4k-3"},
    {0x000B << 10, "5k-1"},
    {0x000C << 10, "8k-1"},
    {0x000D << 10, "8k-2"},
    {0x000E << 10, "8k-3"},
    {0x0010 << 10, "10k-1"},
};

/* Sublevel occupies the remaining low bits; FBB-level bits, if set, are not decoded here and make
 * the whole codeword fall back to "unknown". */
static const CodewordName sublevel_names[] = {
    {0x00, "Sublevel Unrestricted"},
    {0x10, "Sublev12bpp"},
    {0x0C, "Sublev9bpp"},
    {0x08, "Sublev6bpp"},
    {0x06, "Sublev4bpp"},
    {0x04, "Sublev3bpp"},
    {0x03, "Sublev2bpp"},
};

static const char* lookup(const CodewordName* table, size_t entries_num, uint16_t value) {
    for (size_t i = 0; i < entries_num; ++i) {
        if (table[i].value == value) {
            return table[i].name;
        }
    }
    return NULL;
}

const char* jxs_profile_name(uint16_t ppih) {
    return lookup(profile_names, sizeof(profile_names) / sizeof(profile_names[0]), ppih);
}

const char* jxs_level_name(uint16_t plev, char* buf, size_t buf_size) {
    const char* level = lookup(level_names, sizeof(level_names) / sizeof(level_names[0]), plev & PLEV_LEVEL_MASK);
    const char* sublevel = lookup(
        sublevel_names, sizeof(sublevel_names) / sizeof(sublevel_names[0]), (uint16_t)(plev & ~PLEV_LEVEL_MASK));
    if (level == NULL || sublevel == NULL || buf == NULL || buf_size == 0) {
        return NULL;
    }
    snprintf(buf, buf_size, "%s, %s", level, sublevel);
    return buf;
}

const char* jxs_profile_level_str(uint16_t ppih, uint16_t plev, char* buf, size_t buf_size) {
    char level_buf[64];
    const char* profile = jxs_profile_name(ppih);
    const char* level = jxs_level_name(plev, level_buf, sizeof(level_buf));
    int len = snprintf(buf, buf_size, "0x%04X", ppih);
    if (profile && len >= 0 && (size_t)len < buf_size) {
        len += snprintf(buf + len, buf_size - len, " (%s)", profile);
    }
    if (len >= 0 && (size_t)len < buf_size) {
        len += snprintf(buf + len, buf_size - len, " / 0x%04X", plev);
    }
    if (level && len >= 0 && (size_t)len < buf_size) {
        snprintf(buf + len, buf_size - len, " (%s)", level);
    }
    return buf;
}
