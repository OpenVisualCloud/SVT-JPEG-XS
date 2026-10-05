/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "st22_svt_jpeg_xs_plugin.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/log.h"
#include "../include/plugin_platform.h"

static void wait_pending(pthread_mutex_t* mutex, pthread_cond_t* cond,
                         volatile bool* pending, volatile bool* stop) {
  st_pthread_mutex_lock(mutex);
  while (!*pending && !*stop) st_pthread_cond_wait(cond, mutex);
  *pending = false;
  st_pthread_mutex_unlock(mutex);
}

static void signal_pending(pthread_mutex_t* mutex, pthread_cond_t* cond,
                           volatile bool* pending) {
  st_pthread_mutex_lock(mutex);
  *pending = true;
  st_pthread_cond_signal(cond);
  st_pthread_mutex_unlock(mutex);
}

static void stop_thread(const char* name, int idx, pthread_t* thread, pthread_mutex_t* mutex,
                        pthread_cond_t* cond, volatile bool* stop, volatile bool* pending) {
  if (!*thread) return;
  info("%s(%d), trying stop %s\n", __func__, idx, name);
  st_pthread_mutex_lock(mutex);
  *stop = true;
  *pending = true;
  st_pthread_cond_signal(cond);
  st_pthread_mutex_unlock(mutex);
  pthread_join(*thread, NULL);
  *thread = 0;
}

static bool err_log_due(int* cnt) {
  return (*cnt)++ % ERR_LOG_INTERVAL == 0;
}

static uint32_t codec_threads_num(uint32_t width, uint32_t codec_thread_cnt) {
  if (width >= 3840) return 10;
  if (width >= 1920) return 5;
  return codec_thread_cnt;
}

static int encoder_uinit_session(struct st22_encoder_session* session) {
  int idx = session->idx;

  stop_thread("encode_thread_send", idx, &session->encode_thread_send,
              &session->wake_mutex_send, &session->wake_cond_send, &session->stop_send,
              &session->pending_send);
  stop_thread("encode_thread_get", idx, &session->encode_thread_get, &session->wake_mutex_get,
              &session->wake_cond_get, &session->stop_get, &session->pending_get);

  if (session->codec_ctx) {
    svt_jpeg_xs_encoder_close(session->codec_ctx);
    free(session->codec_ctx);
    session->codec_ctx = NULL;
  }

  st_pthread_mutex_destroy(&session->wake_mutex_send);
  st_pthread_cond_destroy(&session->wake_cond_send);

  st_pthread_mutex_destroy(&session->wake_mutex_get);
  st_pthread_cond_destroy(&session->wake_cond_get);
  return 0;
}

// Thread for sending data/yuv to encoder
static void* encode_thread_send(void* arg) {
  struct st22_encoder_session* s = arg;
  st22p_encode_session session_p = s->session_p;
  struct st22_encode_frame_meta* frame = NULL;

  SvtJxsErrorType_t ret = SvtJxsErrorNone;

  info("%s(%d), start\n", __func__, s->idx);
  while (!s->stop_send) {
    frame = st22_encoder_get_frame(session_p);
    if (!frame) { /* no frame */
      wait_pending(&s->wake_mutex_send, &s->wake_cond_send, &s->pending_send, &s->stop_send);
      continue;
    }

    if (s->session_failed) {
      st22_encoder_put_frame(session_p, frame, -EIO);
      continue;
    }

    svt_jpeg_xs_frame_t enc_input;
    uint8_t planes = st_frame_fmt_planes(frame->src->fmt);
    for (uint8_t plane = 0; plane < planes; plane++) {
      enc_input.image.data_yuv[plane] = frame->src->addr[plane];

      // svt-jpegxs require stride in pixel's not in bytes, this means that for 10 bit-depth stride is half the linesize
      enc_input.image.stride[plane] = frame->src->linesize[plane];
      if (s->codec_ctx->input_bit_depth == 10) {
        enc_input.image.stride[plane] /=2;
      }

      size_t plane_sz = st_frame_plane_size(frame->src, plane);
      enc_input.image.alloc_size[plane] = plane_sz;
    }

    enc_input.bitstream.buffer = frame->dst->addr[0];
    enc_input.bitstream.allocation_size = frame->dst->buffer_size;
    //enc_input.bitstream.used_size = frame->dst->addr[0];

    enc_input.user_prv_ctx_ptr = frame;

    ret = svt_jpeg_xs_encoder_send_picture(s->codec_ctx, &enc_input, 1 /*blocking*/);
    if (ret != SvtJxsErrorNone) {
      err("%s(%d), svt_jpeg_xs_encoder_send_picture err = %d\n", __func__, s->idx, ret);
      s->session_failed = true;
      st22_encoder_put_frame(session_p, frame, -EIO);
      continue;
    }
  }
  info("%s(%d), stop\n", __func__, s->idx);

  return NULL;
}

void callback_jpeg_xs_encoder_frame_ready(svt_jpeg_xs_encoder_api_t* encoder,
                                          void* priv) {
  (void)encoder;

  struct st22_encoder_session* s = priv;

  signal_pending(&s->wake_mutex_get, &s->wake_cond_get, &s->pending_get);
}

// Thread for getting data/bitstream from encoder
static void* encode_thread_get(void* arg) {
  struct st22_encoder_session* s = arg;
  st22p_encode_session session_p = s->session_p;

  SvtJxsErrorType_t ret = SvtJxsErrorNone;

  info("%s(%d), start\n", __func__, s->idx);
  while (!s->stop_get) {
    svt_jpeg_xs_frame_t enc_output;
    /* not written by the codec on parameter errors */
    enc_output.user_prv_ctx_ptr = NULL;
    ret = svt_jpeg_xs_encoder_get_packet(s->codec_ctx, &enc_output, 0 /*non-blocking*/);
    if (ret == SvtJxsErrorNoErrorEmptyQueue) {
      wait_pending(&s->wake_mutex_get, &s->wake_cond_get, &s->pending_get, &s->stop_get);
      continue;
    }

    if (ret != SvtJxsErrorNone) {
      if (err_log_due(&s->get_err_cnt))
        err("%s(%d), svt_jpeg_xs_encoder_get_packet err = %d, %d errors so far\n", __func__,
            s->idx, ret, s->get_err_cnt);
      if (enc_output.user_prv_ctx_ptr != NULL) {
        struct st22_encode_frame_meta* frame =
            (struct st22_encode_frame_meta*)enc_output.user_prv_ctx_ptr;
        st22_encoder_put_frame(session_p, frame, -EIO);
      } else {
        /* error not tied to a frame: retrying at once would spin */
        wait_pending(&s->wake_mutex_get, &s->wake_cond_get, &s->pending_get, &s->stop_get);
      }
      continue;
    }

    if (enc_output.user_prv_ctx_ptr != NULL) {
      struct st22_encode_frame_meta* frame =
          (struct st22_encode_frame_meta*)enc_output.user_prv_ctx_ptr;

      frame->dst->data_size = enc_output.bitstream.used_size;

      st22_encoder_put_frame(session_p, frame, 0);
      s->frame_cnt++;
    }
  }
  info("%s(%d), stop\n", __func__, s->idx);

  return NULL;
}

static int encoder_init_session(struct st22_encoder_session* session,
                                struct st22_encoder_create_req* req) {
  int idx = session->idx;

  st_pthread_mutex_init(&session->wake_mutex_send, NULL);
  st_pthread_cond_init(&session->wake_cond_send, NULL);

  st_pthread_mutex_init(&session->wake_mutex_get, NULL);
  st_pthread_cond_init(&session->wake_cond_get, NULL);

  req->max_codestream_size = req->codestream_size;
  session->req = *req;

  session->codec_ctx = malloc(sizeof(svt_jpeg_xs_encoder_api_t));
  if (!session->codec_ctx) {
    err("%s(%d), codec_ctx malloc fail\n", __func__, idx);
    encoder_uinit_session(session);
    return -ENOMEM;
  }

  SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_load_default_parameters(
      SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, session->codec_ctx);

  if (ret != SvtJxsErrorNone) {
    err("%s(%d), svt_jpeg_xs_encoder_load_default_parameters err = %d\n", __func__, idx, ret);
    encoder_uinit_session(session);
    return -EIO;
  }

  session->codec_ctx->source_width = req->width;
  session->codec_ctx->source_height = req->height;
  session->codec_ctx->use_cpu_flags = CPU_FLAGS_ALL;
  session->codec_ctx->threads_num = codec_threads_num(req->width, req->codec_thread_cnt);

  if (req->input_fmt == ST_FRAME_FMT_YUV422PLANAR10LE) {
    session->codec_ctx->input_bit_depth = 10;
    session->codec_ctx->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
  }
  else if (req->input_fmt == ST_FRAME_FMT_YUV422PLANAR8) {
    session->codec_ctx->input_bit_depth = 8;
    session->codec_ctx->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
  } else {
  //TODO: add YUV420 support
    err("%s(%d), unsupported colour format %u\n", __func__, idx, req->input_fmt);
    encoder_uinit_session(session);
    return -EIO;
  }

  if (req->output_fmt != ST_FRAME_FMT_JPEGXS_CODESTREAM) {
    err("%s(%d), unexpected output format\n", __func__, idx);
    encoder_uinit_session(session);
    return -EIO;
  }

  if (req->quality == ST22_QUALITY_MODE_SPEED){
    session->codec_ctx->ndecomp_v = 0;  // default 2
  }
  else if (req->quality == ST22_QUALITY_MODE_QUALITY) {
    session->codec_ctx->quantization = 1;
    session->codec_ctx->slice_height = 64;
  }
  else {
    err("%s(%d), unsupported quality mode %u\n", __func__, idx, req->quality);
    encoder_uinit_session(session);
    return -EIO;
  }

  if (req->codestream_size > UINT32_MAX / 8) {
    err("%s(%d), codestream_size %zu too large for bpp calculation\n", __func__, idx,
        req->codestream_size);
    encoder_uinit_session(session);
    return -EINVAL;
  }
  session->codec_ctx->bpp_numerator = (uint32_t)(req->codestream_size * 8);
  session->codec_ctx->bpp_denominator = req->width * req->height;

  session->codec_ctx->callback_send_data_available = NULL;
  session->codec_ctx->callback_send_data_available_context = NULL;
  session->codec_ctx->callback_get_data_available = callback_jpeg_xs_encoder_frame_ready;
  session->codec_ctx->callback_get_data_available_context = (void *)session;

  ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, session->codec_ctx);
  if (ret != SvtJxsErrorNone) {
    err("%s(%d), svt_jpeg_xs_encoder_init err = %d\n", __func__, idx, ret);
    encoder_uinit_session(session);
    return -EIO;
  }

  int ret2 = pthread_create(&session->encode_thread_send, NULL, encode_thread_send, session);
  if (ret2 != 0) {
    err("%s(%d), encode_thread_send thread create fail %d\n", __func__, idx, ret2);
    encoder_uinit_session(session);
    return -ret2;
  }

  ret2 = pthread_create(&session->encode_thread_get, NULL, encode_thread_get, session);
  if (ret2 != 0) {
    err("%s(%d), encode_thread_get thread create fail %d\n", __func__, idx, ret2);
    encoder_uinit_session(session);
    return -ret2;
  }

  return 0;
}

static st22_encode_priv encoder_create_session(void* priv, st22p_encode_session session_p,
                                               struct st22_encoder_create_req* req) {
  struct st22_svt_jpeg_xs_ctx* ctx = priv;
  struct st22_encoder_session* session = NULL;
  int ret;

  for (int i = 0; i < MAX_ST22_ENCODER_SESSIONS; i++) {
    if (ctx->encoder_sessions[i]) continue;

    session = malloc(sizeof(*session));
    if (!session) return NULL;
    memset(session, 0, sizeof(*session));
    session->idx = i;
    session->session_p = session_p;

    ret = encoder_init_session(session, req);
    if (ret < 0) {
      err("%s(%d), init session fail %d\n", __func__, i, ret);
      free(session);
      return NULL;
    }

    ctx->encoder_sessions[i] = session;
    info("%s(%d), input fmt: %s, output fmt: %s\n", __func__, i,
         st_frame_fmt_name(req->input_fmt), st_frame_fmt_name(req->output_fmt));
    info("%s(%d), max_codestream_size %ld\n", __func__, i,
         session->req.max_codestream_size);
    return session;
  }

  err("%s, all session slot are used\n", __func__);
  return NULL;
}

static int encoder_free_session(void* priv, st22_encode_priv session) {
  struct st22_svt_jpeg_xs_ctx* ctx = priv;
  struct st22_encoder_session* encoder_session = session;
  int idx = encoder_session->idx;

  encoder_uinit_session(encoder_session);

  info("%s(%d), total %d encode frames\n", __func__, idx, encoder_session->frame_cnt);
  free(encoder_session);
  ctx->encoder_sessions[idx] = NULL;
  return 0;
}

static int encoder_frame_available(void* priv) {
  struct st22_encoder_session* s = priv;

  dbg("%s(%d)\n", __func__, s->idx);
  signal_pending(&s->wake_mutex_send, &s->wake_cond_send, &s->pending_send);

  return 0;
}

static int decoder_uinit_session(struct st22_decoder_session* session) {
  int idx = session->idx;

  stop_thread("decode_thread_send", idx, &session->decode_thread_send,
              &session->wake_mutex_send, &session->wake_cond_send, &session->stop_send,
              &session->pending_send);
  stop_thread("decode_thread_get", idx, &session->decode_thread_get, &session->wake_mutex_get,
              &session->wake_cond_get, &session->stop_get, &session->pending_get);

  if (session->codec_ctx) {
    svt_jpeg_xs_decoder_close(session->codec_ctx);
    free(session->codec_ctx);
    session->codec_ctx = NULL;
  }

  st_pthread_mutex_destroy(&session->codec_mutex);
  st_pthread_mutex_destroy(&session->wake_mutex_send);
  st_pthread_cond_destroy(&session->wake_cond_send);

  st_pthread_mutex_destroy(&session->wake_mutex_get);
  st_pthread_cond_destroy(&session->wake_cond_get);
  return 0;
}

//Thread for sending data/bitstream to decoder
static void* decode_thread_send(void* arg) {
  struct st22_decoder_session* s = arg;
  st22p_decode_session session_p = s->session_p;
  struct st22_decode_frame_meta* frame = NULL;
  SvtJxsErrorType_t ret = SvtJxsErrorNone;

  bool decoder_initialized = 0;
  bool reinit = false;

  info("%s(%d), start\n", __func__, s->idx);
  while (!s->stop_send) {
    frame = st22_decoder_get_frame(session_p);
    if (!frame) { /* no frame */
      wait_pending(&s->wake_mutex_send, &s->wake_cond_send, &s->pending_send, &s->stop_send);
      continue;
    }

    if (s->session_failed) {
      st22_decoder_put_frame(session_p, frame, -EIO);
      continue;
    }

    if (s->needs_reinit) {
      /* closing the decoder drops queued frames, let the get thread return them first */
      st_pthread_mutex_lock(&s->wake_mutex_send);
      while (__atomic_load_n(&s->inflight, __ATOMIC_ACQUIRE) > 0 && !s->stop_send)
        st_pthread_cond_wait(&s->wake_cond_send, &s->wake_mutex_send);
      st_pthread_mutex_unlock(&s->wake_mutex_send);
      if (s->stop_send) {
        st22_decoder_put_frame(session_p, frame, -EIO);
        break;
      }
      decoder_initialized = 0;
      reinit = true;
      s->needs_reinit = false;
    }

    if (!decoder_initialized) {
      if (s->init_fail_cnt == 0)
        info("%s(%d), svt-jpeg-xs %s decoder\n", __func__, s->idx,
             reinit ? "re-initialize" : "initialize");
      st_pthread_mutex_lock(&s->codec_mutex);
      svt_jpeg_xs_decoder_close(s->codec_ctx); /* no-op if not initialized */
      ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                     SVT_JPEGXS_API_VER_MINOR,
                                     s->codec_ctx,
                                     frame->src->addr[0],
                                     frame->src->data_size,
                                     &(s->image_config));
      st_pthread_mutex_unlock(&s->codec_mutex);

      bool stream_ok = false;
      if (ret != SvtJxsErrorNone) {
        if (s->init_fail_cnt == 0)
          err("%s(%d), svt_jpeg_xs_decoder_init err = %d\n", __func__, s->idx, ret);
      } else {
        bool depth_ok = (s->req.output_fmt == ST_FRAME_FMT_YUV422PLANAR10LE &&
                         s->image_config.bit_depth == 10) ||
                        (s->req.output_fmt == ST_FRAME_FMT_YUV422PLANAR8 &&
                         s->image_config.bit_depth == 8);
        stream_ok = depth_ok && s->image_config.format == COLOUR_FORMAT_PLANAR_YUV422 &&
                    s->image_config.width == s->req.width &&
                    s->image_config.height == s->req.height;
        if (!stream_ok && s->init_fail_cnt == 0)
          err("%s(%d), stream mismatch: %ux%u bit_depth %u format %u, expect %ux%u %s\n",
              __func__, s->idx, s->image_config.width, s->image_config.height,
              s->image_config.bit_depth, s->image_config.format, s->req.width,
              s->req.height, st_frame_fmt_name(s->req.output_fmt));
      }

      if (!stream_ok) {
        /* the decoder is closed and re-initialized again with the next frame */
        if (++s->init_fail_cnt >= DECODER_INIT_MAX_RETRY) {
          err("%s(%d), decoder init failed on %d consecutive frames, giving up\n", __func__,
              s->idx, s->init_fail_cnt);
          s->session_failed = true;
        }
        st22_decoder_put_frame(session_p, frame, -EIO);
        continue;
      }
      s->init_fail_cnt = 0;
      reinit = false;
      decoder_initialized = 1;
    }

    svt_jpeg_xs_frame_t dec_input;
    dec_input.bitstream.buffer = frame->src->addr[0];
    dec_input.bitstream.allocation_size = frame->src->buffer_size;
    dec_input.bitstream.used_size = frame->src->data_size;

    uint8_t planes = st_frame_fmt_planes(frame->dst->fmt);
    for (uint8_t plane = 0; plane < planes; plane++) {
      dec_input.image.data_yuv[plane] = frame->dst->addr[plane];
      dec_input.image.stride[plane] = frame->dst->linesize[plane];
      if (s->image_config.bit_depth > 8) {
        dec_input.image.stride[plane] /= 2;
      }
      size_t plane_sz = st_frame_plane_size(frame->dst, plane);
      dec_input.image.alloc_size[plane] = plane_sz;
    }
    dec_input.user_prv_ctx_ptr = frame;

    /* counted before sending: the get thread may return the frame before send_frame returns */
    __atomic_add_fetch(&s->inflight, 1, __ATOMIC_RELEASE);
    ret = svt_jpeg_xs_decoder_send_frame(s->codec_ctx, &dec_input, 1 /*blocking*/);
    if (ret != SvtJxsErrorNone) {
      __atomic_sub_fetch(&s->inflight, 1, __ATOMIC_RELEASE);
      err("%s(%d), svt_jpeg_xs_decoder_send_frame err = %d\n", __func__, s->idx, ret);
      s->session_failed = true;
      st22_decoder_put_frame(session_p, frame, -EIO);
      continue;
    }
  }
  info("%s(%d), stop\n", __func__, s->idx);

  return NULL;
}

void callback_jpeg_xs_decoder_frame_ready(struct svt_jpeg_xs_decoder_api* decoder, void* priv) {
  (void)decoder;
  struct st22_decoder_session* s = priv;

  signal_pending(&s->wake_mutex_get, &s->wake_cond_get, &s->pending_get);
}

// Thread for getting data/yuv from decoder
static void* decode_thread_get(void* arg) {
  struct st22_decoder_session* s = arg;
  st22p_decode_session session_p = s->session_p;

  SvtJxsErrorType_t ret = SvtJxsErrorNone;

  info("%s(%d), start\n", __func__, s->idx);
  while (!s->stop_get) {
    svt_jpeg_xs_frame_t dec_output;
    /* not written by the codec when it is not initialized (before first frame, during re-init) */
    dec_output.user_prv_ctx_ptr = NULL;
    st_pthread_mutex_lock(&s->codec_mutex);
    ret = svt_jpeg_xs_decoder_get_frame(s->codec_ctx, &dec_output, 0 /*non-blocking*/);
    st_pthread_mutex_unlock(&s->codec_mutex);

    struct st22_decode_frame_meta* frame = dec_output.user_prv_ctx_ptr;
    if (!frame) {
      if (ret != SvtJxsErrorNoErrorEmptyQueue && ret != SvtJxsErrorDecoderInvalidPointer &&
          err_log_due(&s->get_err_cnt))
        err("%s(%d), svt_jpeg_xs_decoder_get_frame err = %d, %d errors so far\n", __func__,
            s->idx, ret, s->get_err_cnt);
      wait_pending(&s->wake_mutex_get, &s->wake_cond_get, &s->pending_get, &s->stop_get);
      continue;
    }

    if (ret == SvtJxsErrorNone) {
      st22_decoder_put_frame(session_p, frame, 0);
      s->frame_cnt++;
    } else {
      if (ret == SvtJxsErrorDecoderConfigChange) {
        if (!s->needs_reinit)
          warn("%s(%d), decoder config change, re-init on next frame\n", __func__, s->idx);
        s->needs_reinit = true;
      } else if (err_log_due(&s->get_err_cnt)) {
        err("%s(%d), svt_jpeg_xs_decoder_get_frame err = %d, %d errors so far\n", __func__,
            s->idx, ret, s->get_err_cnt);
      }
      st22_decoder_put_frame(session_p, frame, -EIO);
    }

    if (__atomic_sub_fetch(&s->inflight, 1, __ATOMIC_ACQ_REL) == 0 && s->needs_reinit)
      signal_pending(&s->wake_mutex_send, &s->wake_cond_send, &s->pending_send);
  }
  info("%s(%d), stop\n", __func__, s->idx);

  return NULL;
}

static int decoder_init_session(struct st22_decoder_session* session,
                                struct st22_decoder_create_req* req) {
  int idx = session->idx;
  int ret;

  st_pthread_mutex_init(&session->codec_mutex, NULL);
  st_pthread_mutex_init(&session->wake_mutex_send, NULL);
  st_pthread_cond_init(&session->wake_cond_send, NULL);

  st_pthread_mutex_init(&session->wake_mutex_get, NULL);
  st_pthread_cond_init(&session->wake_cond_get, NULL);

  session->req = *req;

  session->codec_ctx = malloc(sizeof(svt_jpeg_xs_decoder_api_t));
  if (!session->codec_ctx) {
    err("%s(%d), codec_ctx malloc fail\n", __func__, idx);
    decoder_uinit_session(session);
    return -ENOMEM;
  }
  memset(session->codec_ctx, 0, sizeof(svt_jpeg_xs_decoder_api_t));

  session->codec_ctx->use_cpu_flags = CPU_FLAGS_ALL;
  session->codec_ctx->verbose = VERBOSE_SYSTEM_INFO;
  session->codec_ctx->proxy_mode = proxy_mode_full;
  session->codec_ctx->callback_send_data_available = NULL;
  session->codec_ctx->callback_send_data_available_context = NULL;
  session->codec_ctx->callback_get_data_available = callback_jpeg_xs_decoder_frame_ready;
  session->codec_ctx->callback_get_data_available_context = (void *)session;
  session->codec_ctx->threads_num = codec_threads_num(req->width, req->codec_thread_cnt);

  if (req->input_fmt != ST_FRAME_FMT_JPEGXS_CODESTREAM) {
    err("%s(%d), unexpected input format\n", __func__, idx);
    decoder_uinit_session(session);
    return -EIO;
  }

  if (req->output_fmt != ST_FRAME_FMT_YUV422PLANAR10LE &&
      req->output_fmt != ST_FRAME_FMT_YUV422PLANAR8) {
    err("%s(%d), unexpected output format\n", __func__, idx);
    decoder_uinit_session(session);
    return -EIO;
  }

  ret = pthread_create(&session->decode_thread_send, NULL, decode_thread_send, session);
  if (ret != 0) {
    err("%s(%d), thread create fail %d\n", __func__, idx, ret);
    decoder_uinit_session(session);
    return -ret;
  }
  ret = pthread_create(&session->decode_thread_get, NULL, decode_thread_get, session);
  if (ret != 0) {
    err("%s(%d), thread create fail %d\n", __func__, idx, ret);
    decoder_uinit_session(session);
    return -ret;
  }

  return 0;
}

static st22_decode_priv decoder_create_session(void* priv, st22p_decode_session session_p,
                                               struct st22_decoder_create_req* req) {
  struct st22_svt_jpeg_xs_ctx* ctx = priv;
  struct st22_decoder_session* session = NULL;
  int ret;

  for (int i = 0; i < MAX_ST22_DECODER_SESSIONS; i++) {
    if (ctx->decoder_sessions[i]) continue;

    session = malloc(sizeof(*session));
    if (!session) return NULL;
    memset(session, 0, sizeof(*session));
    session->idx = i;
    session->session_p = session_p;

    ret = decoder_init_session(session, req);
    if (ret < 0) {
      err("%s(%d), init session fail %d\n", __func__, i, ret);
      free(session);
      return NULL;
    }

    ctx->decoder_sessions[i] = session;
    info("%s(%d), input fmt: %s, output fmt: %s\n", __func__, i,
         st_frame_fmt_name(req->input_fmt), st_frame_fmt_name(req->output_fmt));
    return session;
  }

  err("%s, all session slot are used\n", __func__);
  return NULL;
}

static int decoder_free_session(void* priv, st22_decode_priv session) {
  struct st22_svt_jpeg_xs_ctx* ctx = priv;
  struct st22_decoder_session* decoder_session = session;
  int idx = decoder_session->idx;

  decoder_uinit_session(decoder_session);

  info("%s(%d), total %d decode frames\n", __func__, idx, decoder_session->frame_cnt);
  free(decoder_session);
  ctx->decoder_sessions[idx] = NULL;
  return 0;
}

static int decoder_frame_available(void* priv) {
  struct st22_decoder_session* s = priv;

  dbg("%s(%d)\n", __func__, s->idx);
  signal_pending(&s->wake_mutex_send, &s->wake_cond_send, &s->pending_send);

  return 0;
}

st_plugin_priv st_plugin_create(mtl_handle st) {
  struct st22_svt_jpeg_xs_ctx* ctx;

  ctx = malloc(sizeof(*ctx));
  if (!ctx) return NULL;
  memset(ctx, 0, sizeof(*ctx));

  struct st22_decoder_dev d_dev;
  memset(&d_dev, 0, sizeof(d_dev));
  d_dev.name = "st22_svt_jpeg_xs_plugin_decoder";
  d_dev.priv = ctx;
  d_dev.target_device = ST_PLUGIN_DEVICE_CPU;
  d_dev.input_fmt_caps = ST_FMT_CAP_JPEGXS_CODESTREAM;
  d_dev.output_fmt_caps = ST_FMT_CAP_YUV422PLANAR10LE | ST_FMT_CAP_YUV422PLANAR8;
  d_dev.create_session = decoder_create_session;
  d_dev.free_session = decoder_free_session;
  d_dev.notify_frame_available = decoder_frame_available;
  ctx->decoder_dev_handle = st22_decoder_register(st, &d_dev);
  if (!ctx->decoder_dev_handle) {
    info("%s, decoder register fail\n", __func__);
    free(ctx);
    return NULL;
  }

  struct st22_encoder_dev e_dev;
  memset(&e_dev, 0, sizeof(e_dev));
  e_dev.name = "st22_svt_jpeg_xs_plugin_encoder";
  e_dev.priv = ctx;
  e_dev.target_device = ST_PLUGIN_DEVICE_CPU;
  e_dev.input_fmt_caps = ST_FMT_CAP_YUV422PLANAR10LE | ST_FMT_CAP_YUV422PLANAR8;
  e_dev.output_fmt_caps = ST_FMT_CAP_JPEGXS_CODESTREAM;
  e_dev.create_session = encoder_create_session;
  e_dev.free_session = encoder_free_session;
  e_dev.notify_frame_available = encoder_frame_available;
  ctx->encoder_dev_handle = st22_encoder_register(st, &e_dev);
  if (!ctx->encoder_dev_handle) {
    info("%s, encoder register fail\n", __func__);
    st22_decoder_unregister(ctx->decoder_dev_handle);
    free(ctx);
    return NULL;
  }

  info("%s, succ with st22 svt jpeg xs plugin\n", __func__);
  return ctx;
}

int st_plugin_free(st_plugin_priv handle) {
  struct st22_svt_jpeg_xs_ctx* ctx = handle;

  for (int i = 0; i < MAX_ST22_DECODER_SESSIONS; i++) {
    if (ctx->decoder_sessions[i]) {
      decoder_uinit_session(ctx->decoder_sessions[i]);
      free(ctx->decoder_sessions[i]);
    }
  }
  for (int i = 0; i < MAX_ST22_ENCODER_SESSIONS; i++) {
    if (ctx->encoder_sessions[i]) {
      encoder_uinit_session(ctx->encoder_sessions[i]);
      free(ctx->encoder_sessions[i]);
    }
  }
  if (ctx->decoder_dev_handle) {
    st22_decoder_unregister(ctx->decoder_dev_handle);
    ctx->decoder_dev_handle = NULL;
  }
  if (ctx->encoder_dev_handle) {
    st22_encoder_unregister(ctx->encoder_dev_handle);
    ctx->encoder_dev_handle = NULL;
  }
  free(ctx);

  info("%s, succ with st22 svt jpeg xs plugin\n", __func__);
  return 0;
}

int st_plugin_get_meta(struct st_plugin_meta* meta) {
  meta->version = ST_PLUGIN_VERSION_V1;
  meta->magic = ST_PLUGIN_VERSION_V1_MAGIC;
  return 0;
}
