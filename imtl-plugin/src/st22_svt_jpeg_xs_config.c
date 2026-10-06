/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "st22_svt_jpeg_xs_config.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#ifdef DEBUG
int pl_log_level = PL_LOG_DEBUG;
#else
int pl_log_level = PL_LOG_INFO;
#endif

struct name_value {
  const char* name;
  uint64_t value;
};

enum key_kind {
  KEY_UINT, /* JSON integer in [min, max] */
  KEY_BOOL, /* JSON boolean */
  KEY_NAME, /* one of names; with numeric also an integer or numeric string in [min, max] */
};

/* maps a JSON key onto a field of the library's encoder/decoder parameter struct */
struct key_desc {
  const char* name;
  enum key_kind kind;
  size_t offset;
  size_t size;
  uint64_t min;
  uint64_t max;
  const struct name_value* names; /* NULL name terminated */
  bool numeric;
  bool invert;      /* KEY_BOOL: the field is the negation, e.g. coding_raw_disable */
  bool zero_keeps;  /* KEY_UINT: 0 leaves the field unchanged */
  bool unsupported; /* the installed library has no such field */
};

#define ENC(f) \
  .offset = offsetof(svt_jpeg_xs_encoder_api_t, f), .size = sizeof(((svt_jpeg_xs_encoder_api_t*)0)->f)
#define DEC(f) \
  .offset = offsetof(svt_jpeg_xs_decoder_api_t, f), .size = sizeof(((svt_jpeg_xs_decoder_api_t*)0)->f)

static const struct name_value log_level_names[] = {
    {"error", PL_LOG_ERROR}, {"warning", PL_LOG_WARNING}, {"info", PL_LOG_INFO},
    {"debug", PL_LOG_DEBUG}, {NULL, 0},
};

/* same names as SvtJpegxsEncApp / SvtJpegxsDecApp --asm */
static const struct name_value asm_names[] = {
    {"c", CPU_FLAGS_C},
    {"mmx", (CPU_FLAGS_MMX << 1) - 1},
    {"sse", (CPU_FLAGS_SSE << 1) - 1},
    {"sse2", (CPU_FLAGS_SSE2 << 1) - 1},
    {"sse3", (CPU_FLAGS_SSE3 << 1) - 1},
    {"ssse3", (CPU_FLAGS_SSSE3 << 1) - 1},
    {"sse4_1", (CPU_FLAGS_SSE4_1 << 1) - 1},
    {"sse4_2", (CPU_FLAGS_SSE4_2 << 1) - 1},
    {"avx", (CPU_FLAGS_AVX << 1) - 1},
    {"avx2", (CPU_FLAGS_AVX2 << 1) - 1},
    {"avx512", CPU_FLAGS_ALL},
    {"max", CPU_FLAGS_ALL},
    {NULL, 0},
};

static const struct name_value cpu_profile_names[] = {
    {"latency", 0}, {"cpu", 1}, {NULL, 0},
};

static const struct name_value quantization_names[] = {
    {"deadzone", 0}, {"uniform", 1}, {NULL, 0},
};

static const struct name_value coding_signs_names[] = {
    {"disable", 0}, {"fast", 1}, {"full", 2}, {NULL, 0},
};

static const struct name_value coding_vpred_names[] = {
    {"disable", 0}, {"zero_residuals", 1}, {"zero_coefficients", 2}, {NULL, 0},
};

static const struct name_value rc_names[] = {
    {"precinct", 0}, {"precinct_padding", 1}, {"slice", 2}, {"slice_max_rate", 3}, {NULL, 0},
};

/* Ppih per ISO/IEC 21122-2 Annex A, same names as SvtJpegxsEncApp --stream-profile */
static const struct name_value stream_profile_names[] = {
    {"auto", 0x0000},     {"light422", 0x1500}, {"light444", 0x1A00}, {"lightsubline422", 0x2500},
    {"main420", 0x3240},  {"main422", 0x3540},  {"main444", 0x3A40},  {"main4444", 0x3E40},
    {"high420", 0x4240},  {"high444", 0x4A40},  {"high4444", 0x4E40}, {NULL, 0},
};

/* Plev level part (bits 15:10) with unrestricted sublevel, same names as
 * SvtJpegxsEncApp --stream-level; a raw value also selects the sublevel */
static const struct name_value stream_level_names[] = {
    {"auto", 0xFFFF},        {"unrestricted", 0x0000}, {"1k-1", 0x0001 << 10},
    {"2k-1", 0x0004 << 10},  {"4k-1", 0x0008 << 10},   {"4k-2", 0x0009 << 10},
    {"4k-3", 0x000A << 10},  {"5k-1", 0x000B << 10},   {"8k-1", 0x000C << 10},
    {"8k-2", 0x000D << 10},  {"8k-3", 0x000E << 10},   {"10k-1", 0x0010 << 10},
    {NULL, 0},
};

static const struct key_desc log_level_key = {
    .name = "log_level", .kind = KEY_NAME, .names = log_level_names};

static const struct key_desc enc_keys[] = {
    {.name = "lp", .kind = KEY_UINT, ENC(threads_num), .max = 1024, .zero_keeps = true},
    {.name = "asm", .kind = KEY_NAME, ENC(use_cpu_flags), .names = asm_names},
    {.name = "cpu_profile", .kind = KEY_NAME, ENC(cpu_profile), .names = cpu_profile_names},
    {.name = "decomp_v", .kind = KEY_UINT, ENC(ndecomp_v), .max = 2},
    {.name = "decomp_h", .kind = KEY_UINT, ENC(ndecomp_h), .min = 1, .max = 5},
    {.name = "quantization", .kind = KEY_NAME, ENC(quantization), .names = quantization_names,
     .numeric = true, .max = 1},
    {.name = "slice_height", .kind = KEY_UINT, ENC(slice_height), .min = 1, .max = 65535},
    {.name = "coding_signs", .kind = KEY_NAME, ENC(coding_signs_handling),
     .names = coding_signs_names, .numeric = true, .max = 2},
    {.name = "coding_sigf", .kind = KEY_BOOL, ENC(coding_significance)},
    {.name = "coding_vpred", .kind = KEY_NAME, ENC(coding_vertical_prediction_mode),
     .names = coding_vpred_names, .numeric = true, .max = 2},
    {.name = "rc", .kind = KEY_NAME, ENC(rate_control_mode), .names = rc_names, .numeric = true,
     .max = 3},
#ifdef HAVE_ENC_LOSSLESS_ENABLE
    {.name = "lossless", .kind = KEY_BOOL, ENC(lossless_enable)},
#else
    {.name = "lossless", .unsupported = true},
#endif
#ifdef HAVE_ENC_CODING_RAW_DISABLE
    {.name = "coding_raw", .kind = KEY_BOOL, ENC(coding_raw_disable), .invert = true},
#else
    {.name = "coding_raw", .unsupported = true},
#endif
#ifdef HAVE_ENC_CAP_COMPAT
    {.name = "cap_compat", .kind = KEY_BOOL, ENC(cap_compat)},
#else
    {.name = "cap_compat", .unsupported = true},
#endif
#ifdef HAVE_ENC_ENABLE_COLOR_TRANSFORM
    {.name = "rct", .kind = KEY_BOOL, ENC(enable_color_transform)},
#else
    {.name = "rct", .unsupported = true},
#endif
#ifdef HAVE_ENC_PROFILE_PPIH_OVERRIDE
    {.name = "stream_profile", .kind = KEY_NAME, ENC(profile_ppih_override),
     .names = stream_profile_names, .numeric = true, .max = 0xFFFF},
#else
    {.name = "stream_profile", .unsupported = true},
#endif
#ifdef HAVE_ENC_LEVEL_PLEV_OVERRIDE
    {.name = "stream_level", .kind = KEY_NAME, ENC(level_plev_override),
     .names = stream_level_names, .numeric = true, .max = 0xFFFF},
#else
    {.name = "stream_level", .unsupported = true},
#endif
};

static const struct key_desc dec_keys[] = {
    {.name = "lp", .kind = KEY_UINT, DEC(threads_num), .max = 1024, .zero_keeps = true},
    {.name = "asm", .kind = KEY_NAME, DEC(use_cpu_flags), .names = asm_names},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static bool parse_uint(const struct key_desc* k, struct json_object* v, uint64_t* out) {
  if (json_object_get_type(v) == json_type_int) {
    int64_t n = json_object_get_int64(v);
    if (n < 0) return false;
    *out = (uint64_t)n;
  } else if (json_object_get_type(v) == json_type_string) {
    const char* s = json_object_get_string(v);
    char* end = NULL;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
      s += 2;
      base = 16;
    }
    /* decimal or 0x hex only, no sign, leading space or octal */
    if (!isalnum((unsigned char)s[0])) return false;
    errno = 0;
    *out = strtoull(s, &end, base);
    if (end == s || *end != '\0' || errno) return false;
  } else {
    return false;
  }
  return *out >= k->min && *out <= k->max;
}

static int key_value(const char* section, const struct key_desc* k, struct json_object* v,
                     uint64_t* out) {
  enum json_type t = json_object_get_type(v);

  switch (k->kind) {
    case KEY_BOOL:
      if (t == json_type_boolean) {
        *out = json_object_get_boolean(v) ? !k->invert : k->invert;
        return 0;
      }
      break;
    case KEY_UINT:
      if (t == json_type_int && parse_uint(k, v, out)) return 0;
      break;
    case KEY_NAME:
      if (t == json_type_string) {
        const char* s = json_object_get_string(v);
        for (const struct name_value* n = k->names; n->name; n++) {
          if (!strcmp(s, n->name)) {
            *out = n->value;
            return 0;
          }
        }
      }
      if (k->numeric && parse_uint(k, v, out)) return 0;
      break;
  }

  char expect[256];
  int len = 0;
  if (k->kind == KEY_BOOL) {
    len = snprintf(expect, sizeof(expect), "true or false");
  } else if (k->kind == KEY_UINT) {
    len = snprintf(expect, sizeof(expect), "integer %" PRIu64 "-%" PRIu64, k->min, k->max);
  } else {
    for (const struct name_value* n = k->names; n->name && len < (int)sizeof(expect); n++)
      len += snprintf(expect + len, sizeof(expect) - len, "%s\"%s\"", len ? ", " : "", n->name);
    if (k->numeric && len < (int)sizeof(expect) && k->max > 0xFF)
      snprintf(expect + len, sizeof(expect) - len, " or number 0x%" PRIX64 "-0x%" PRIX64, k->min,
               k->max);
    else if (k->numeric && len < (int)sizeof(expect))
      snprintf(expect + len, sizeof(expect) - len, " or number %" PRIu64 "-%" PRIu64, k->min,
               k->max);
  }
  err("%s, %s%s%s: invalid value %s, expected %s\n", __func__, section ? section : "",
      section ? "." : "", k->name, json_object_to_json_string(v), expect);
  return -EINVAL;
}

static void store_field(void* base, const struct key_desc* k, uint64_t v) {
  uint8_t* p = (uint8_t*)base + k->offset;

  switch (k->size) {
    case 1:
      *(uint8_t*)p = (uint8_t)v;
      break;
    case 2:
      *(uint16_t*)p = (uint16_t)v;
      break;
    case 4:
      *(uint32_t*)p = (uint32_t)v;
      break;
    case 8:
      *(uint64_t*)p = v;
      break;
  }
}

/* load: called once to validate the file, rejects unknown keys and logs every
 * accepted value; otherwise called per session silently */
static int apply_section(const char* section, struct json_object* obj, const struct key_desc* keys,
                         size_t keys_num, void* base, bool load) {
  json_object_object_foreach(obj, name, val) {
    const struct key_desc* k = NULL;
    for (size_t i = 0; i < keys_num; i++) {
      if (!strcmp(name, keys[i].name)) {
        k = &keys[i];
        break;
      }
    }
    if (!k) {
      /* a typo must not silently fall back to the default */
      err("%s, unknown key %s.%s\n", __func__, section, name);
      return -EINVAL;
    }
    if (k->unsupported) {
      err("%s, %s.%s is not supported by the installed SVT-JPEG-XS library\n", __func__,
          section, name);
      return -ENOTSUP;
    }

    uint64_t v;
    int ret = key_value(section, k, val, &v);
    if (ret < 0) return ret;
    if (load)
      info("%s, %s.%s = %s\n", __func__, section, name, json_object_to_json_string(val));
    if (k->kind == KEY_UINT && k->zero_keeps && v == 0) continue;
    store_field(base, k, v);
  }
  return 0;
}

/* library verbosity for the configured log level; warning and info keep the
 * library default, VERBOSE_WARNINGS makes the decoder warn on every slice */
static uint32_t lib_verbose(void) {
  switch (pl_log_level) {
    case PL_LOG_ERROR:
      return VERBOSE_ERRORS;
    case PL_LOG_WARNING:
    case PL_LOG_INFO:
      return VERBOSE_SYSTEM_INFO;
    default:
      return VERBOSE_INFO_MULTITHREADING;
  }
}

/* json-c skips comments inside the top level value only, not after it */
static bool only_space_or_comments(const char* s) {
  for (;;) {
    s += strspn(s, " \t\r\n");
    if (s[0] == '/' && s[1] == '/') {
      s = strchr(s, '\n');
      if (!s) return true;
    } else if (s[0] == '/' && s[1] == '*') {
      s = strstr(s + 2, "*/");
      if (!s) return false;
      s += 2;
    } else {
      return s[0] == '\0';
    }
  }
}

/* far above any real config, keeps the length within json-c's int */
#define CONFIG_MAX_SIZE (1 << 20)

/* json_object_from_file() reports no position for syntax errors */
static struct json_object* parse_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    err("%s, fail to open %s: %s\n", __func__, path, strerror(errno));
    return NULL;
  }

  char* buf = NULL;
  long len = -1;
  if (fseek(f, 0, SEEK_END) == 0) len = ftell(f);
  if (len > CONFIG_MAX_SIZE) {
    err("%s, %s larger than %d bytes\n", __func__, path, CONFIG_MAX_SIZE);
    fclose(f);
    return NULL;
  }
  if (len >= 0 && fseek(f, 0, SEEK_SET) == 0) buf = malloc(len + 1);
  if (!buf || fread(buf, 1, len, f) != (size_t)len) {
    err("%s, fail to read %s\n", __func__, path);
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  buf[len] = '\0';

  struct json_object* obj = NULL;
  struct json_tokener* tok = json_tokener_new();
  if (tok) {
    obj = json_tokener_parse_ex(tok, buf, (int)len);
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    size_t end = json_tokener_get_parse_end(tok);
    const char* reason = NULL;

    if (!obj)
      reason = jerr == json_tokener_continue ? "unexpected end of file"
                                             : json_tokener_error_desc(jerr);
    else if (end < (size_t)len && !only_space_or_comments(buf + end))
      reason = "unexpected data after the top level value";
    if (reason) {
      int line = 1;
      for (size_t i = 0; i < end && i < (size_t)len; i++)
        if (buf[i] == '\n') line++;
      err("%s, %s:%d: %s\n", __func__, path, line, reason);
      if (obj) json_object_put(obj);
      obj = NULL;
    }
    json_tokener_free(tok);
  }
  free(buf);
  return obj;
}

int st22_config_load(struct st22_svt_jpeg_xs_config* cfg) {
  int ret = -EINVAL;
  int log_level = pl_log_level;

  memset(cfg, 0, sizeof(*cfg));

  const char* path = getenv(ST22_SVT_JXS_CONFIG_ENV);
  if (!path || !*path) {
    info("%s, %s not set, use built-in defaults\n", __func__, ST22_SVT_JXS_CONFIG_ENV);
    return 0;
  }

  cfg->root = parse_file(path);
  if (!cfg->root) return -EINVAL;
  if (!json_object_is_type(cfg->root, json_type_object)) {
    err("%s, %s: top level is not a JSON object\n", __func__, path);
    goto fail;
  }

  /* first, so the messages about the rest of the file already follow it */
  struct json_object* level_obj;
  if (json_object_object_get_ex(cfg->root, "log_level", &level_obj)) {
    uint64_t v;
    if (key_value(NULL, &log_level_key, level_obj, &v) < 0) goto fail;
    pl_log_level = (int)v;
    cfg->log_level_set = true;
  }
  info("%s, loaded %s\n", __func__, path);
  if (cfg->log_level_set) info("%s, log_level = %s\n", __func__, log_level_names[pl_log_level].name);

  json_object_object_foreach(cfg->root, name, val) {
    if (!strcmp(name, "log_level")) {
      continue;
    } else if (!strcmp(name, "encoder") || !strcmp(name, "decoder")) {
      if (!json_object_is_type(val, json_type_object)) {
        err("%s, %s: \"%s\" is not a JSON object\n", __func__, path, name);
        goto fail;
      }
      if (name[0] == 'e')
        cfg->enc = val;
      else
        cfg->dec = val;
    } else {
      err("%s, unknown key %s\n", __func__, name);
      goto fail;
    }
  }

  /* validate the values once here on scratch parameters, so a bad file fails
   * plugin start instead of every session */
  if (cfg->enc) {
    svt_jpeg_xs_encoder_api_t enc;
    memset(&enc, 0, sizeof(enc));
    ret = apply_section("encoder", cfg->enc, enc_keys, ARRAY_SIZE(enc_keys), &enc, true);
    if (ret < 0) goto fail;
  }
  if (cfg->dec) {
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    ret = apply_section("decoder", cfg->dec, dec_keys, ARRAY_SIZE(dec_keys), &dec, true);
    if (ret < 0) goto fail;
  }

  return 0;

fail:
  pl_log_level = log_level;
  st22_config_free(cfg);
  return ret;
}

void st22_config_free(struct st22_svt_jpeg_xs_config* cfg) {
  if (cfg->root) json_object_put(cfg->root);
  memset(cfg, 0, sizeof(*cfg));
}

int st22_config_apply_encoder(const struct st22_svt_jpeg_xs_config* cfg,
                              svt_jpeg_xs_encoder_api_t* enc) {
  if (cfg->log_level_set) enc->verbose = lib_verbose();
  if (!cfg->enc) return 0;
  return apply_section("encoder", cfg->enc, enc_keys, ARRAY_SIZE(enc_keys), enc, false);
}

int st22_config_apply_decoder(const struct st22_svt_jpeg_xs_config* cfg,
                              svt_jpeg_xs_decoder_api_t* dec) {
  if (cfg->log_level_set) dec->verbose = lib_verbose();
  if (!cfg->dec) return 0;
  return apply_section("decoder", cfg->dec, dec_keys, ARRAY_SIZE(dec_keys), dec, false);
}
