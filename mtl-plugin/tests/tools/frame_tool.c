/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Frame helper for the mtl-plugin loopback tests.
 *
 * frame_tool gen <fmt> <w> <h> <out> [phase] [source [field]]
 *   Write one w x h picture in MTL plane order (GBR formats: G, B, R). Without a source the
 *   content is synthetic: smooth gradients with block edges, different per plane and per phase,
 *   so it compresses well losslessly and a swapped plane is visible. With a source (first
 *   YUV422PLANAR10LE frame of the file, w x h; with "field" w x 2h, phase 0 takes the even lines
 *   and 1 the odd ones) every format is derived from it: 8-bit = >> 2, 12-bit = << 2,
 *   16LE = << 6, 4:2:0 = even chroma lines, 4:4:4 = chroma repeated, GBR = (Y, U, V) as
 *   (G, B, R). Fails when two planes of the same size are equal.
 * frame_tool size <fmt> <w> <h>
 *   Print the frame size in bytes, then the size of each plane.
 * frame_tool shift-cmp <a> <b> <bytes> <shift>
 *   Compare the first <bytes> bytes of two files of 16-bit little-endian samples: every sample
 *   of <a> shifted right by <shift> must equal the one of <b>, and the low <shift> bits of <a>
 *   must be 0.
 *
 * Exit status: 0 success, 1 check failed, 2 usage or I/O error.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fmt_desc {
  const char* name;
  int bytes;  /* per sample */
  int depth;  /* significant bits */
  int shift;  /* MSB-aligned samples are stored << shift */
  int cw_div; /* chroma width divisor */
  int ch_div; /* chroma height divisor */
  int gbr;
};

static const struct fmt_desc fmts[] = {
    {"YUV422PLANAR8", 1, 8, 0, 2, 1, 0},     {"YUV422PLANAR10LE", 2, 10, 0, 2, 1, 0},
    {"YUV422PLANAR12LE", 2, 12, 0, 2, 1, 0}, {"YUV422PLANAR16LE", 2, 10, 6, 2, 1, 0},
    {"YUV420PLANAR8", 1, 8, 0, 2, 2, 0},     {"YUV444PLANAR10LE", 2, 10, 0, 1, 1, 0},
    {"YUV444PLANAR12LE", 2, 12, 0, 1, 1, 0}, {"GBRPLANAR10LE", 2, 10, 0, 1, 1, 1},
    {"GBRPLANAR12LE", 2, 12, 0, 1, 1, 1},
};

static const struct fmt_desc* find_fmt(const char* name) {
  for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++)
    if (!strcmp(fmts[i].name, name)) return &fmts[i];
  fprintf(stderr, "unknown format %s\n", name);
  return NULL;
}

static void plane_dims(const struct fmt_desc* f, int w, int h, int plane, int* pw, int* ph) {
  *pw = plane ? w / f->cw_div : w;
  *ph = plane ? h / f->ch_div : h;
}

static size_t plane_bytes(const struct fmt_desc* f, int w, int h, int plane) {
  int pw, ph;
  plane_dims(f, w, h, plane, &pw, &ph);
  return (size_t)pw * ph * f->bytes;
}

/* synthetic 16-bit value in [0, 1 << depth) for plane p at (x, y) of a w x h plane */
static uint32_t synth(int p, int x, int y, int w, int h, int phase, int depth) {
  const uint32_t max = (1u << depth) - 1;
  uint32_t v;
  switch (p) {
    case 0: /* diagonal gradient */
      v = (uint32_t)((uint64_t)(3 * x + 2 * y) * max / (3 * w + 2 * h));
      break;
    case 1: /* horizontal gradient, falling */
      v = (uint32_t)((uint64_t)(w - 1 - x) * max / w);
      break;
    default: /* vertical gradient */
      v = (uint32_t)((uint64_t)y * max / h);
      break;
  }
  /* block edges, shifted by plane and phase, keep the planes and fields distinct */
  if ((((x + 16 * p + 8 * phase) / 64) + ((y + 24 * phase) / 64)) % 2) v = v / 2 + max / 4;
  return v;
}

static void put_sample(uint8_t* buf, size_t idx, int bytes, uint32_t v) {
  if (bytes == 1) {
    buf[idx] = (uint8_t)v;
  } else {
    buf[2 * idx] = (uint8_t)(v & 0xFF);
    buf[2 * idx + 1] = (uint8_t)(v >> 8);
  }
}

static int read_file(const char* path, uint8_t* buf, size_t len) {
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    perror(path);
    return -1;
  }
  const size_t got = fread(buf, 1, len, fp);
  fclose(fp);
  if (got != len) {
    fprintf(stderr, "%s: need %zu bytes, got %zu\n", path, len, got);
    return -1;
  }
  return 0;
}

/* 10-bit source sample of plane p (0 Y, 1 U, 2 V) at output (x, y), already scaled to the
 * output depth and chroma layout */
static uint32_t source_sample(const uint16_t* src, int sw, int sh, int field, int phase,
                              const struct fmt_desc* f, int p, int x, int y) {
  const int scw = sw / 2;
  /* rows of the output plane map to source frame rows (fields: every second row) */
  int sy;
  int sx;
  const uint16_t* plane;
  if (p == 0) {
    plane = src;
    sx = x;
    sy = field ? 2 * y + phase : y;
  } else {
    plane = src + (size_t)sw * sh + (size_t)(p - 1) * scw * sh;
    /* 4:4:4 output repeats each 4:2:2 chroma sample, 4:2:0 output takes even chroma rows */
    sx = f->cw_div == 1 ? x / 2 : x;
    const int out_row = f->ch_div == 2 ? 2 * y : y;
    sy = field ? 2 * out_row + phase : out_row;
  }
  const int stride = p == 0 ? sw : scw;
  const uint32_t v = plane[(size_t)sy * stride + sx] & 0x3FF;
  return f->depth >= 10 ? v << (f->depth - 10) : v >> (10 - f->depth);
}

static int planes_differ(const struct fmt_desc* f, int w, int h, uint8_t* const planes[3]) {
  /* the chroma planes always have the same size, 4:4:4 also luma */
  if (!memcmp(planes[1], planes[2], plane_bytes(f, w, h, 1))) return 0;
  if (f->cw_div == 1 && f->ch_div == 1 &&
      (!memcmp(planes[0], planes[1], plane_bytes(f, w, h, 0)) ||
       !memcmp(planes[0], planes[2], plane_bytes(f, w, h, 0))))
    return 0;
  return 1;
}

static int cmd_gen(int argc, char** argv) {
  if (argc < 6) return -1;
  const struct fmt_desc* f = find_fmt(argv[2]);
  const int w = atoi(argv[3]);
  const int h = atoi(argv[4]);
  const char* out = argv[5];
  const int phase = argc > 6 ? atoi(argv[6]) : 0;
  const char* source = argc > 7 ? argv[7] : NULL;
  if (!f || w <= 0 || h <= 0 || w % 2 || h % 2 || phase < 0 || phase > 1) return -1;

  uint16_t* src = NULL;
  /* "field": the output is one field of a source frame of twice the height */
  const int field = argc > 8 && !strcmp(argv[8], "field");
  const int sh = field ? 2 * h : h;
  if (source) {
    /* first frame of the file, 4:2:2 10-bit: 2 samples of 2 bytes per pixel */
    const size_t need = (size_t)w * sh * 2 * 2;
    src = malloc(need);
    if (!src || read_file(source, (uint8_t*)src, need) < 0) {
      free(src);
      return 2;
    }
  }

  uint8_t* planes[3];
  for (int p = 0; p < 3; p++) {
    planes[p] = malloc(plane_bytes(f, w, h, p));
    if (!planes[p]) return 2;
  }

  for (int p = 0; p < 3; p++) {
    int pw, ph;
    plane_dims(f, w, h, p, &pw, &ph);
    /* GBR planes are G, B, R: the YUV-like content Y, U, V goes to G, B, R */
    for (int y = 0; y < ph; y++) {
      for (int x = 0; x < pw; x++) {
        const uint32_t v = src ? source_sample(src, w, sh, field, phase, f, p, x, y)
                         : synth(p, x, y, pw, ph, phase, f->depth);
        put_sample(planes[p], (size_t)y * pw + x, f->bytes, v << f->shift);
      }
    }
  }
  free(src);

  if (!planes_differ(f, w, h, planes)) {
    fprintf(stderr, "gen %s: planes are not distinct, a plane swap would go unnoticed\n",
            f->name);
    return 1;
  }

  FILE* fp = fopen(out, "wb");
  if (!fp) {
    perror(out);
    return 2;
  }
  int ret = 0;
  for (int p = 0; p < 3; p++) {
    const size_t len = plane_bytes(f, w, h, p);
    if (fwrite(planes[p], 1, len, fp) != len) ret = 2;
    free(planes[p]);
  }
  if (fclose(fp) || ret) {
    fprintf(stderr, "%s: write failed\n", out);
    return 2;
  }
  return 0;
}

static int cmd_size(int argc, char** argv) {
  if (argc < 5) return -1;
  const struct fmt_desc* f = find_fmt(argv[2]);
  const int w = atoi(argv[3]);
  const int h = atoi(argv[4]);
  if (!f || w <= 0 || h <= 0) return -1;
  const size_t p0 = plane_bytes(f, w, h, 0);
  const size_t p1 = plane_bytes(f, w, h, 1);
  const size_t p2 = plane_bytes(f, w, h, 2);
  printf("%zu %zu %zu %zu\n", p0 + p1 + p2, p0, p1, p2);
  return 0;
}

static int cmd_shift_cmp(int argc, char** argv) {
  if (argc < 6) return -1;
  const size_t len = strtoull(argv[4], NULL, 10);
  const int shift = atoi(argv[5]);
  if (!len || len % 2 || shift < 0 || shift > 15) return -1;

  uint8_t* a = malloc(len);
  uint8_t* b = malloc(len);
  if (!a || !b || read_file(argv[2], a, len) < 0 || read_file(argv[3], b, len) < 0) {
    free(a);
    free(b);
    return 2;
  }

  const uint32_t low_mask = (1u << shift) - 1;
  size_t bad = 0;
  for (size_t i = 0; i < len / 2; i++) {
    const uint32_t va = a[2 * i] | (uint32_t)a[2 * i + 1] << 8;
    const uint32_t vb = b[2 * i] | (uint32_t)b[2 * i + 1] << 8;
    if ((va >> shift) != vb || (va & low_mask)) {
      if (!bad)
        printf("first mismatch at sample %zu: %#x >> %d != %#x\n", i, va, shift, vb);
      bad++;
    }
  }
  free(a);
  free(b);
  printf("%zu of %zu samples differ\n", bad, len / 2);
  return bad ? 1 : 0;
}

int main(int argc, char** argv) {
  int ret = -1;
  if (argc > 1 && !strcmp(argv[1], "gen"))
    ret = cmd_gen(argc, argv);
  else if (argc > 1 && !strcmp(argv[1], "size"))
    ret = cmd_size(argc, argv);
  else if (argc > 1 && !strcmp(argv[1], "shift-cmp"))
    ret = cmd_shift_cmp(argc, argv);
  if (ret < 0) {
    fprintf(stderr,
            "usage: %s gen <fmt> <w> <h> <out> [phase] [source [field]]\n"
            "       %s size <fmt> <w> <h>\n"
            "       %s shift-cmp <a> <b> <bytes> <shift>\n",
            argv[0], argv[0], argv[0]);
    return 2;
  }
  return ret;
}
