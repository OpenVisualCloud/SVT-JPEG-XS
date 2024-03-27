/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "NltDec.h"
#include "Pi.h"

static INLINE int32_t nlt_clamp(int32_t val, int32_t clamp_val) {
    return (val > clamp_val ? clamp_val : val < 0 ? 0 : val);
}

static INLINE int32_t nlt_clamp64(int64_t val, int32_t clamp_val) {
    return (int32_t)(val > clamp_val ? clamp_val : val < 0 ? 0 : val);
}

void linear_output_scaling_8bit_c(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, uint32_t depth,
                                  svt_jpeg_xs_image_buffer_t* out) {
    int32_t dzeta = bw - depth;
    int32_t m = (1 << depth) - 1;
    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        int32_t h = pi->components[i].height;
        uint8_t* out_buf = (uint8_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                int32_t v = comps[i][y * w + x];
                v += (1 << bw) >> 1;
                v = (v + ((1 << dzeta) >> 1)) >> dzeta;
                out_buf[y * out_stride + x] = (uint8_t)nlt_clamp(v, m);
            }
        }
    }
}

void linear_output_scaling_16bit_c(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, uint32_t depth,
                                   svt_jpeg_xs_image_buffer_t* out) {
    int32_t dzeta = bw - depth;
    int32_t m = (1 << depth) - 1;
    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        int32_t h = pi->components[i].height;
        uint16_t* out_buf = (uint16_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                int32_t v = comps[i][y * w + x];
                v += (1 << bw) >> 1;
                v = (v + ((1 << dzeta) >> 1)) >> dzeta;
                out_buf[y * out_stride + x] = (uint16_t)nlt_clamp(v, m);
            }
        }
    }
}

// NOTE The inverse gamma correction step can produce intermediate results that can exceed 32 bit precision.
// For a value of Bw = 18, v can obtain values as large as 2^36
void quadratic_output_scaling_8bit(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, int32_t dco,
                                   uint32_t depth, svt_jpeg_xs_image_buffer_t* out) {
    int32_t dzeta = 2 * bw - depth;
    int32_t m = (1 << depth) - 1;
    int32_t clamp_val = (1 << bw) - 1;
    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        int32_t h = pi->components[i].height;
        uint8_t* out_buf = (uint8_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                int32_t v = comps[i][y * w + x];
                v += (1 << bw) >> 1;
                v = nlt_clamp(v, clamp_val);
                int64_t v_64 = (int64_t)v * (int64_t)v;
                v_64 = (v_64 + ((1 << dzeta) >> 1)) >> dzeta;
                v_64 += dco;
                out_buf[y * out_stride + x] = (uint8_t)nlt_clamp64(v_64, m);
            }
        }
    }
}

void quadratic_output_scaling_16bit(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint32_t bw, int32_t dco,
                                    uint32_t depth, svt_jpeg_xs_image_buffer_t* out) {
    int32_t dzeta = 2 * bw - depth;
    int32_t m = (1 << depth) - 1;
    int32_t clamp_val = (1 << bw) - 1;
    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        int32_t h = pi->components[i].height;
        uint16_t* out_buf = (uint16_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                int32_t v = comps[i][y * w + x];
                v += (1 << bw) >> 1;
                v = nlt_clamp(v, clamp_val);
                int64_t v_64 = (int64_t)v * (int64_t)v;
                v_64 = (v_64 + ((1 << dzeta) >> 1)) >> dzeta;
                v_64 += dco;
                out_buf[y * out_stride + x] = (uint16_t)nlt_clamp64(v_64, m);
            }
        }
    }
}

void extended_output_scaling_8bit(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint8_t bw, int32_t t1, int32_t t2,
                                  uint8_t e, uint8_t depth, svt_jpeg_xs_image_buffer_t* out) {
    int32_t b1 = t1 + (1 << (bw - e - 1));
    int64_t b2 = (int64_t)t1 * t1;
    int64_t b3 = t2 - ((int64_t)1 << (bw - e - 1));
    int64_t a1 = b2 + ((int64_t)t1 << (bw - e)) + ((int64_t)1 << (2 * bw - 2 - 2 * e));
    int64_t a3 = b2 + ((int64_t)t2 << (bw - e)) - ((int64_t)1 << (2 * bw - 2 - 2 * e));

    int32_t eps = bw - e;
    int32_t dzeta = 2 * bw - depth;
    int32_t clamp_val = (1 << bw) - 1;
    int32_t m = (1 << depth) - 1;

    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        int32_t h = pi->components[i].height;
        uint8_t* out_buf = (uint8_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                int64_t v = comps[i][y * w + x];
                v += ((int64_t)1 << bw) >> 1;
                if (v < t1) {
                    v = b1 - v;
                    v = nlt_clamp64(v, clamp_val);
                    v = a1 - (v * v);
                }
                else if (v < t2) {
                    v = (v << eps) + b2;
                }
                else {
                    v -= b3;
                    v = nlt_clamp64(v, clamp_val);
                    v = a3 + (v * v);
                }
                v = (v + ((1 << dzeta) >> 1)) >> dzeta;
                out_buf[y * out_stride + x] = (uint8_t)nlt_clamp64(v, m);
            }
        }
    }
}

void extended_output_scaling_16bit(const pi_t* const pi, int32_t* comps[MAX_COMPONENTS_NUM], uint8_t bw, int32_t t1, int32_t t2,
                                   uint8_t e, uint8_t depth, svt_jpeg_xs_image_buffer_t* out) {
    int32_t b1 = t1 + (1 << (bw - e - 1));
    int64_t b2 = (int64_t)t1 * t1;
    int64_t b3 = t2 - ((int64_t)1 << (bw - e - 1));
    int64_t a1 = b2 + ((int64_t)t1 << (bw - e)) + ((int64_t)1 << (2 * bw - 2 - 2 * e));
    int64_t a3 = b2 + ((int64_t)t2 << (bw - e)) - ((int64_t)1 << (2 * bw - 2 - 2 * e));

    int32_t eps = bw - e;
    int32_t dzeta = 2 * bw - depth;
    int32_t clamp_val = (1 << bw) - 1;
    int32_t m = (1 << depth) - 1;

    for (uint32_t i = 0; i < pi->comps_num; i++) {
        int32_t w = pi->components[i].width;
        int32_t h = pi->components[i].height;
        uint16_t* out_buf = (uint16_t*)(out->data_yuv[i]);
        const uint32_t out_stride = out->stride[i];
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                int64_t v = comps[i][y * w + x];
                v += ((int64_t)1 << bw) >> 1;
                if (v < t1) {
                    v = b1 - v;
                    v = nlt_clamp64(v, clamp_val);
                    v = a1 - (v * v);
                }
                else if (v < t2) {
                    v = (v << eps) + b2;
                }
                else {
                    v -= b3;
                    v = nlt_clamp64(v, clamp_val);
                    v = a3 + (v * v);
                }
                v = (v + ((1 << dzeta) >> 1)) >> dzeta;
                out_buf[y * out_stride + x] = (uint16_t)nlt_clamp64(v, m);
            }
        }
    }
}

void nlt_inverse_transform(int32_t* comps[MAX_COMPONENTS_NUM], const pi_t* pi, const picture_header_const_t* picture_header_const,
                           const picture_header_dynamic_t* picture_header_dynamic, svt_jpeg_xs_image_buffer_t* out) {
    if (picture_header_const->hdr_bit_depth[0] <= 8) {
        if (picture_header_dynamic->hdr_Tnlt == 0) {
            linear_output_scaling_8bit(pi, comps, picture_header_dynamic->hdr_Bw, picture_header_const->hdr_bit_depth[0], out);
        }
        else if (picture_header_dynamic->hdr_Tnlt == 1) {
            int32_t dco = (int32_t)picture_header_dynamic->hdr_Tnlt_alpha -
                ((int32_t)picture_header_dynamic->hdr_Tnlt_sigma * (1 << 15));
            quadratic_output_scaling_8bit(
                pi, comps, picture_header_dynamic->hdr_Bw, dco, picture_header_const->hdr_bit_depth[0], out);
        }
        else if (picture_header_dynamic->hdr_Tnlt == 2) {
            int32_t t1 = picture_header_dynamic->hdr_Tnlt_t1;
            int32_t t2 = picture_header_dynamic->hdr_Tnlt_t2;
            uint8_t e = picture_header_dynamic->hdr_Tnlt_e;
            extended_output_scaling_8bit(
                pi, comps, picture_header_dynamic->hdr_Bw, t1, t2, e, picture_header_const->hdr_bit_depth[0], out);
        }
        else {
            assert(0);
        }
    }
    else {
        if (picture_header_dynamic->hdr_Tnlt == 0) {
            linear_output_scaling_16bit(pi, comps, picture_header_dynamic->hdr_Bw, picture_header_const->hdr_bit_depth[0], out);
        }
        else if (picture_header_dynamic->hdr_Tnlt == 1) {
            int32_t dco = (int32_t)picture_header_dynamic->hdr_Tnlt_alpha -
                ((int32_t)picture_header_dynamic->hdr_Tnlt_sigma * (1 << 15));
            quadratic_output_scaling_16bit(
                pi, comps, picture_header_dynamic->hdr_Bw, dco, picture_header_const->hdr_bit_depth[0], out);
        }
        else if (picture_header_dynamic->hdr_Tnlt == 2) {
            int32_t t1 = picture_header_dynamic->hdr_Tnlt_t1;
            int32_t t2 = picture_header_dynamic->hdr_Tnlt_t2;
            uint8_t e = picture_header_dynamic->hdr_Tnlt_e;
            extended_output_scaling_16bit(
                pi, comps, picture_header_dynamic->hdr_Bw, t1, t2, e, picture_header_const->hdr_bit_depth[0], out);
        }
        else {
            assert(0);
        }
    }
}

void linear_output_scaling_8bit_line_c(int32_t* in, uint32_t bw, uint32_t depth, uint8_t* out, uint32_t w) {
    int32_t dzeta = bw - depth;
    int32_t m = (1 << depth) - 1;

    for (uint32_t x = 0; x < w; x++) {
        int32_t v = in[x];
        v += (1 << bw) >> 1;
        v = (v + ((1 << dzeta) >> 1)) >> dzeta;
        out[x] = (uint8_t)nlt_clamp(v, m);
    }
}

void linear_output_scaling_16bit_line_c(int32_t* in, uint32_t bw, uint32_t depth, uint16_t* out, uint32_t w) {
    int32_t dzeta = bw - depth;
    int32_t m = (1 << depth) - 1;

    for (uint32_t x = 0; x < w; x++) {
        int32_t v = in[x];
        v += (1 << bw) >> 1;
        v = (v + ((1 << dzeta) >> 1)) >> dzeta;
        out[x] = (uint16_t)nlt_clamp(v, m);
    }
}

void quadratic_output_scaling_8bit_line(int32_t* in, uint32_t bw, int32_t dco, uint32_t depth, uint8_t* out, uint32_t w) {
    int32_t dzeta = 2 * bw - depth;
    int32_t m = (1 << depth) - 1;
    int32_t clamp_val = (1 << bw) - 1;

    for (uint32_t x = 0; x < w; x++) {
        int32_t v = in[x];
        v += (1 << bw) >> 1;
        v = nlt_clamp(v, clamp_val);
        int64_t v_64 = (int64_t)v * (int64_t)v;
        v_64 = (v_64 + ((1 << dzeta) >> 1)) >> dzeta;
        v_64 += dco;
        out[x] = (uint8_t)nlt_clamp64(v_64, m);
    }
}

void quadratic_output_scaling_16bit_line(int32_t* in, uint32_t bw, int32_t dco, uint32_t depth, uint16_t* out, uint32_t w) {
    int32_t dzeta = 2 * bw - depth;
    int32_t m = (1 << depth) - 1;
    int32_t clamp_val = (1 << bw) - 1;

    for (uint32_t x = 0; x < w; x++) {
        int32_t v = in[x];
        v += (1 << bw) >> 1;
        v = nlt_clamp(v, clamp_val);
        int64_t v_64 = (int64_t)v * (int64_t)v;
        v_64 = (v_64 + ((1 << dzeta) >> 1)) >> dzeta;
        v_64 += dco;
        out[x] = (uint16_t)nlt_clamp64(v_64, m);
    }
}

void extended_output_scaling_8bit_line(int32_t* in, uint8_t bw, int32_t t1, int32_t t2, uint8_t e, uint8_t depth, uint8_t* out,
                                       uint32_t w) {
    int32_t b1 = t1 + (1 << (bw - e - 1));
    int64_t b2 = (int64_t)t1 * t1;
    int64_t b3 = t2 - ((int64_t)1 << (bw - e - 1));
    int64_t a1 = b2 + ((int64_t)t1 << (bw - e)) + ((int64_t)1 << (2 * bw - 2 - 2 * e));
    int64_t a3 = b2 + ((int64_t)t2 << (bw - e)) - ((int64_t)1 << (2 * bw - 2 - 2 * e));

    int32_t eps = bw - e;
    int32_t dzeta = 2 * bw - depth;
    int32_t clamp_val = (1 << bw) - 1;
    int32_t m = (1 << depth) - 1;

    for (uint32_t x = 0; x < w; x++) {
        int64_t v = in[x];
        v += ((int64_t)1 << bw) >> 1;
        if (v < t1) {
            v = b1 - v;
            v = nlt_clamp64(v, clamp_val);
            v = a1 - (v * v);
        }
        else if (v < t2) {
            v = (v << eps) + b2;
        }
        else {
            v -= b3;
            v = nlt_clamp64(v, clamp_val);
            v = a3 + (v * v);
        }
        v = (v + ((1 << dzeta) >> 1)) >> dzeta;
        out[x] = (uint8_t)nlt_clamp64(v, m);
    }
}

void extended_output_scaling_16bit_line(int32_t* in, uint8_t bw, int32_t t1, int32_t t2, uint8_t e, uint8_t depth, uint16_t* out,
                                        uint32_t w) {
    int32_t b1 = t1 + (1 << (bw - e - 1));
    int64_t b2 = (int64_t)t1 * t1;
    int64_t b3 = t2 - ((int64_t)1 << (bw - e - 1));
    int64_t a1 = b2 + ((int64_t)t1 << (bw - e)) + ((int64_t)1 << (2 * bw - 2 - 2 * e));
    int64_t a3 = b2 + ((int64_t)t2 << (bw - e)) - ((int64_t)1 << (2 * bw - 2 - 2 * e));

    int32_t eps = bw - e;
    int32_t dzeta = 2 * bw - depth;
    int32_t clamp_val = (1 << bw) - 1;
    int32_t m = (1 << depth) - 1;

    for (uint32_t x = 0; x < w; x++) {
        int64_t v = in[x];
        v += ((int64_t)1 << bw) >> 1;
        if (v < t1) {
            v = b1 - v;
            v = nlt_clamp64(v, clamp_val);
            v = a1 - (v * v);
        }
        else if (v < t2) {
            v = (v << eps) + b2;
        }
        else {
            v -= b3;
            v = nlt_clamp64(v, clamp_val);
            v = a3 + (v * v);
        }
        v = (v + ((1 << dzeta) >> 1)) >> dzeta;
        out[x] = (uint16_t)nlt_clamp64(v, m);
    }
}

void nlt_inverse_transform_line_8bit(int32_t* in, uint32_t depth, const picture_header_dynamic_t* picture_header_dynamic,
                                     uint8_t* out, int32_t w) {
    if (picture_header_dynamic->hdr_Tnlt == 0) {
        linear_output_scaling_8bit_line(in, picture_header_dynamic->hdr_Bw, depth, out, w);
    }
    else if (picture_header_dynamic->hdr_Tnlt == 1) {
        int32_t dco = (int32_t)picture_header_dynamic->hdr_Tnlt_alpha -
            ((int32_t)picture_header_dynamic->hdr_Tnlt_sigma * (1 << 15));
        quadratic_output_scaling_8bit_line(in, picture_header_dynamic->hdr_Bw, dco, depth, out, w);
    }
    else if (picture_header_dynamic->hdr_Tnlt == 2) {
        int32_t t1 = picture_header_dynamic->hdr_Tnlt_t1;
        int32_t t2 = picture_header_dynamic->hdr_Tnlt_t2;
        uint8_t e = picture_header_dynamic->hdr_Tnlt_e;
        extended_output_scaling_8bit_line(in, picture_header_dynamic->hdr_Bw, t1, t2, e, depth, out, w);
    }
    else {
        assert(0);
    }
}

void nlt_inverse_transform_line_16bit(int32_t* in, uint32_t depth, const picture_header_dynamic_t* picture_header_dynamic,
                                      uint16_t* out, int32_t w) {
    if (picture_header_dynamic->hdr_Tnlt == 0) {
        linear_output_scaling_16bit_line(in, picture_header_dynamic->hdr_Bw, depth, out, w);
    }
    else if (picture_header_dynamic->hdr_Tnlt == 1) {
        int32_t dco = (int32_t)picture_header_dynamic->hdr_Tnlt_alpha -
            ((int32_t)picture_header_dynamic->hdr_Tnlt_sigma * (1 << 15));
        quadratic_output_scaling_16bit_line(in, picture_header_dynamic->hdr_Bw, dco, depth, out, w);
    }
    else if (picture_header_dynamic->hdr_Tnlt == 2) {
        int32_t t1 = picture_header_dynamic->hdr_Tnlt_t1;
        int32_t t2 = picture_header_dynamic->hdr_Tnlt_t2;
        uint8_t e = picture_header_dynamic->hdr_Tnlt_e;
        extended_output_scaling_16bit_line(in, picture_header_dynamic->hdr_Bw, t1, t2, e, depth, out, w);
    }
    else {
        assert(0);
    }
}
