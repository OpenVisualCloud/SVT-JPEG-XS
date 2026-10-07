/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Harness for the plugin config loader (src/st22_svt_jpeg_xs_config.c), built with the same
 * flags as the plugin. Loads the file named by SVT_JXS_MTL_PLUGIN_CONFIG like the plugin does
 * at start, then applies it to library default encoder/decoder parameters with lp 5 (the
 * plugin's 1080p thread count) and prints the result as key=value lines:
 *
 *   load=<return value> log_level=<0..3>
 *   enc.<field>=<value> ...
 *   dec.<field>=<value> ...
 *
 * Exit status: 0 config loaded and applied, 1 load or apply failed.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "st22_svt_jpeg_xs_config.h"

static void print_enc(const svt_jpeg_xs_encoder_api_t* e) {
  printf("enc.threads_num=%u\n", e->threads_num);
  printf("enc.use_cpu_flags=%#" PRIx64 "\n", (uint64_t)e->use_cpu_flags);
  printf("enc.cpu_profile=%u\n", e->cpu_profile);
  printf("enc.ndecomp_v=%u\n", e->ndecomp_v);
  printf("enc.ndecomp_h=%u\n", e->ndecomp_h);
  printf("enc.quantization=%u\n", e->quantization);
  printf("enc.slice_height=%u\n", e->slice_height);
  printf("enc.coding_signs_handling=%u\n", e->coding_signs_handling);
  printf("enc.coding_significance=%u\n", e->coding_significance);
  printf("enc.coding_vertical_prediction_mode=%u\n", e->coding_vertical_prediction_mode);
  printf("enc.rate_control_mode=%u\n", e->rate_control_mode);
#ifdef HAVE_ENC_LOSSLESS_ENABLE
  printf("enc.lossless_enable=%u\n", e->lossless_enable);
#endif
#ifdef HAVE_ENC_CODING_RAW_DISABLE
  printf("enc.coding_raw_disable=%u\n", e->coding_raw_disable);
#endif
#ifdef HAVE_ENC_CAP_COMPAT
  printf("enc.cap_compat=%u\n", e->cap_compat);
#endif
#ifdef HAVE_ENC_ENABLE_COLOR_TRANSFORM
  printf("enc.enable_color_transform=%u\n", e->enable_color_transform);
#endif
#ifdef HAVE_ENC_PROFILE_PPIH_OVERRIDE
  printf("enc.profile_ppih_override=%#x\n", e->profile_ppih_override);
#endif
#ifdef HAVE_ENC_LEVEL_PLEV_OVERRIDE
  printf("enc.level_plev_override=%#x\n", e->level_plev_override);
#endif
  printf("enc.verbose=%u\n", e->verbose);
}

static void print_dec(const svt_jpeg_xs_decoder_api_t* d) {
  printf("dec.threads_num=%u\n", d->threads_num);
  printf("dec.use_cpu_flags=%#" PRIx64 "\n", (uint64_t)d->use_cpu_flags);
  printf("dec.verbose=%u\n", d->verbose);
}

int main(void) {
  struct st22_svt_jpeg_xs_config cfg;
  const int load = st22_config_load(&cfg);
  printf("load=%d log_level=%d\n", load, pl_log_level);
  if (load < 0) return 1;

  svt_jpeg_xs_encoder_api_t enc;
  memset(&enc, 0, sizeof(enc));
  svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                                              &enc);
  enc.threads_num = 5;
  const int enc_ret = st22_config_apply_encoder(&cfg, &enc);

  svt_jpeg_xs_decoder_api_t dec;
  memset(&dec, 0, sizeof(dec));
  dec.threads_num = 5;
  dec.use_cpu_flags = CPU_FLAGS_ALL;
  const int dec_ret = st22_config_apply_decoder(&cfg, &dec);

  printf("apply_encoder=%d apply_decoder=%d\n", enc_ret, dec_ret);
  print_enc(&enc);
  print_dec(&dec);
  st22_config_free(&cfg);
  return enc_ret < 0 || dec_ret < 0 ? 1 : 0;
}
