/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _ST22_SVT_JPEG_XS_HEAD_H_
#define _ST22_SVT_JPEG_XS_HEAD_H_



#include <SvtJpegxsEnc.h>
#include <SvtJpegxsDec.h>
#include <SvtJpegxs.h>
#include <mtl/st_pipeline_api.h>

#include "st22_svt_jpeg_xs_config.h"

#define MAX_ST22_ENCODER_SESSIONS (8)
#define MAX_ST22_DECODER_SESSIONS (8)
/* consecutive frames the decoder may fail to init on before the session gives up */
#define DECODER_INIT_MAX_RETRY (60)
/* per-frame codec errors are logged once every this many occurrences */
#define ERR_LOG_INTERVAL (60)

/* an MTL frame format the plugin encodes from and decodes to */
struct st22_svt_jpeg_xs_fmt {
  enum st_frame_fmt fmt;
  ColourFormat_t colour_format;
  uint8_t bit_depth;
  /* samples in the high bits of 16-bit words, low bits zero */
  bool msb_aligned;
  /* planes stored G, B, R; the codec gets them as components R, G, B */
  bool gbr;
};

struct st22_encoder_session {
  int idx;

  struct st22_encoder_create_req req;
  st22p_encode_session session_p;
  const struct st22_svt_jpeg_xs_fmt* fmt;

  bool            stop_send; /* accessed only with __atomic builtins */
  volatile bool   pending_send;
  pthread_t       encode_thread_send;
  pthread_cond_t  wake_cond_send;
  pthread_mutex_t wake_mutex_send;

  bool            stop_get; /* accessed only with __atomic builtins */
  volatile bool   pending_get;
  pthread_t       encode_thread_get;
  pthread_cond_t  wake_cond_get;
  pthread_mutex_t wake_mutex_get;

  /* set once send_picture fails fatally; subsequent frames are failed
   * immediately instead of being handed to the codec. */
  volatile bool   session_failed;

  int frame_cnt;
  int frame_idx;
  int get_err_cnt;
  /* frames failed because the codec can't address their line layout */
  int layout_err_cnt;

  svt_jpeg_xs_encoder_api_t* codec_ctx;
};

struct st22_decoder_session {
  int idx;

  struct st22_decoder_create_req req;
  st22p_decode_session session_p;
  const struct st22_svt_jpeg_xs_fmt* fmt;

  bool            stop_send; /* accessed only with __atomic builtins */
  volatile bool   pending_send;
  pthread_t       decode_thread_send;
  pthread_cond_t  wake_cond_send;
  pthread_mutex_t wake_mutex_send;

  bool            stop_get; /* accessed only with __atomic builtins */
  volatile bool   pending_get;
  pthread_t       decode_thread_get;
  pthread_cond_t  wake_cond_get;
  pthread_mutex_t wake_mutex_get;

  /* set once send_frame fails, or decoder init / stream format check fails on
   * DECODER_INIT_MAX_RETRY consecutive frames; subsequent frames are failed
   * immediately instead of being handed to the codec. */
  volatile bool   session_failed;
  int             init_fail_cnt;
  /* set by the get thread when the codec reports a mid-stream format/
   * resolution change; the send thread re-inits the decoder on the next
   * frame it sends, once all in-flight frames are drained. Accessed only
   * with __atomic builtins. */
  bool            needs_reinit;
  /* frames handed to the codec and not yet returned by get_frame */
  volatile int    inflight;
  /* serializes get_frame against decoder close/init during re-init */
  pthread_mutex_t codec_mutex;

  int frame_cnt;
  int frame_idx;
  int get_err_cnt;
  /* frames failed because the codec can't address their line layout */
  int layout_err_cnt;

  svt_jpeg_xs_decoder_api_t* codec_ctx;
  svt_jpeg_xs_image_config_t image_config;
};

struct st22_svt_jpeg_xs_ctx {
  st22_encoder_dev_handle encoder_dev_handle;
  st22_decoder_dev_handle decoder_dev_handle;
  struct st22_encoder_session* encoder_sessions[MAX_ST22_ENCODER_SESSIONS];
  struct st22_decoder_session* decoder_sessions[MAX_ST22_DECODER_SESSIONS];
  struct st22_svt_jpeg_xs_config cfg;
};

/* the APIs for plugin, looked up by MTL with dlsym */
#define ST_PLUGIN_API __attribute__((visibility("default")))
ST_PLUGIN_API int st_plugin_get_meta(struct st_plugin_meta* meta);
ST_PLUGIN_API st_plugin_priv st_plugin_create(mtl_handle st);
ST_PLUGIN_API int st_plugin_free(st_plugin_priv handle);

#endif
