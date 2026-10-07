/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _ST22_SVT_JPEG_XS_CONFIG_H_
#define _ST22_SVT_JPEG_XS_CONFIG_H_

#include <stdbool.h>

#include <SvtJpegxsDec.h>
#include <SvtJpegxsEnc.h>

/* env var holding the path of the plugin's JSON config file */
#define ST22_SVT_JXS_CONFIG_ENV "SVT_JXS_MTL_PLUGIN_CONFIG"

struct json_object;

struct st22_svt_jpeg_xs_config {
  struct json_object* root;
  /* "encoder" / "decoder" sections of root, NULL when absent */
  struct json_object* enc;
  struct json_object* dec;
  /* "log_level" present: also drives the library verbosity */
  bool log_level_set;
};

/* Loads the file named by ST22_SVT_JXS_CONFIG_ENV, if set, and validates every
 * key by applying it to scratch encoder/decoder parameters. Sets the plugin log
 * level. Returns 0 on success (also when the env var is not set), negative errno
 * if the file can't be read or contains an unknown key or an invalid value. */
int st22_config_load(struct st22_svt_jpeg_xs_config* cfg);
void st22_config_free(struct st22_svt_jpeg_xs_config* cfg);

/* Apply the "encoder" / "decoder" section on top of the parameters already set
 * for a session (defaults, MTL request, quality mode); keys absent from the file
 * leave the field unchanged. Return 0 or negative errno on an invalid value. */
int st22_config_apply_encoder(const struct st22_svt_jpeg_xs_config* cfg,
                              svt_jpeg_xs_encoder_api_t* enc);
int st22_config_apply_decoder(const struct st22_svt_jpeg_xs_config* cfg,
                              svt_jpeg_xs_decoder_api_t* dec);

#endif
