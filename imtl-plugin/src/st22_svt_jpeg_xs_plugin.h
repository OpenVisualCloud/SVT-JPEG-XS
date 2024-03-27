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

#define MAX_ST22_ENCODER_SESSIONS (8)
#define MAX_ST22_DECODER_SESSIONS (8)

struct st22_encoder_session {
  int idx;

  struct st22_encoder_create_req req;
  st22p_encode_session session_p;

  volatile bool   stop_send;
  pthread_t       encode_thread_send;
  pthread_cond_t  wake_cond_send;
  pthread_mutex_t wake_mutex_send;

  volatile bool   stop_get;
  pthread_t       encode_thread_get;
  pthread_cond_t  wake_cond_get;
  pthread_mutex_t wake_mutex_get;

  int frame_cnt;
  int frame_idx;

  svt_jpeg_xs_encoder_api_t* codec_ctx;
};

struct st22_decoder_session {
  int idx;

  struct st22_decoder_create_req req;
  st22p_decode_session session_p;

  volatile bool   stop_send;
  pthread_t       decode_thread_send;
  pthread_cond_t  wake_cond_send;
  pthread_mutex_t wake_mutex_send;

  volatile bool   stop_get;
  pthread_t       decode_thread_get;
  pthread_cond_t  wake_cond_get;
  pthread_mutex_t wake_mutex_get;

  int frame_cnt;
  int frame_idx;

  svt_jpeg_xs_decoder_api_t* codec_ctx;
  svt_jpeg_xs_image_config_t image_config;
};

struct st22_svt_jpeg_xs_ctx {
  st22_encoder_dev_handle encoder_dev_handle;
  st22_decoder_dev_handle decoder_dev_handle;
  struct st22_encoder_session* encoder_sessions[MAX_ST22_ENCODER_SESSIONS];
  struct st22_decoder_session* decoder_sessions[MAX_ST22_DECODER_SESSIONS];
};

/* the APIs for plugin */
int st_plugin_get_meta(struct st_plugin_meta* meta);
st_plugin_priv st_plugin_create(mtl_handle st);
int st_plugin_free(st_plugin_priv handle);

#endif
