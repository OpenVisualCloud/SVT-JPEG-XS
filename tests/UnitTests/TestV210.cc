/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <cstring>
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"
#include "common_dsp_rtcd.h"
#include "V210.h"

#ifdef ARCH_X86_64
#include "V210_avx2.h"
#endif /* ARCH_X86_64 */

namespace {

typedef void (*unpack_fn)(const void* in_v210, void* out_y, void* out_cb, void* out_cr, uint32_t width);
typedef void (*pack_fn)(const uint16_t* in_y, const uint16_t* in_cb, const uint16_t* in_cr, void* out_v210, uint32_t width);

/* Reference V210, written from the order of the 12 samples of a group: Cb0 Y0 Cr0 Y1 Cb1 Y2 Cr1 Y3 Cb2 Y4 Cr2 Y5,
 * 3 samples per little-endian 32-bit word from bit 0 up. */
uint32_t ref_read_sample(const uint8_t* line, uint32_t group, uint32_t index) {
    const uint8_t* word = line + 16 * group + 4 * (index / 3);
    const uint32_t value = (uint32_t)word[0] | ((uint32_t)word[1] << 8) | ((uint32_t)word[2] << 16) | ((uint32_t)word[3] << 24);
    return (value >> (10 * (index % 3))) & 0x3FF;
}

void ref_write_sample(uint8_t* line, uint32_t group, uint32_t index, uint32_t sample) {
    uint8_t* word = line + 16 * group + 4 * (index / 3);
    uint32_t value = (uint32_t)word[0] | ((uint32_t)word[1] << 8) | ((uint32_t)word[2] << 16) | ((uint32_t)word[3] << 24);
    value |= (sample & 0x3FF) << (10 * (index % 3));
    word[0] = (uint8_t)value;
    word[1] = (uint8_t)(value >> 8);
    word[2] = (uint8_t)(value >> 16);
    word[3] = (uint8_t)(value >> 24);
}

void ref_unpack(const uint8_t* line, uint32_t width, uint16_t* y, uint16_t* cb, uint16_t* cr) {
    for (uint32_t x = 0; x < width; x += 2) {
        const uint32_t group = x / 6;
        const uint32_t pair = (x % 6) / 2;
        cb[x / 2] = (uint16_t)ref_read_sample(line, group, 4 * pair);
        y[x] = (uint16_t)ref_read_sample(line, group, 4 * pair + 1);
        cr[x / 2] = (uint16_t)ref_read_sample(line, group, 4 * pair + 2);
        y[x + 1] = (uint16_t)ref_read_sample(line, group, 4 * pair + 3);
    }
}

/* Writes the whole groups of the line, with 0 in the samples after the end of the line and in bits 30-31. */
void ref_pack(const uint16_t* y, const uint16_t* cb, const uint16_t* cr, uint32_t width, uint8_t* line) {
    memset(line, 0, v210_line_words(width) * sizeof(uint32_t));
    for (uint32_t x = 0; x < width; x += 2) {
        const uint32_t group = x / 6;
        const uint32_t pair = (x % 6) / 2;
        ref_write_sample(line, group, 4 * pair, cb[x / 2]);
        ref_write_sample(line, group, 4 * pair + 1, y[x]);
        ref_write_sample(line, group, 4 * pair + 2, cr[x / 2]);
        ref_write_sample(line, group, 4 * pair + 3, y[x + 1]);
    }
}

std::vector<uint32_t> line_widths() {
    std::vector<uint32_t> widths;
    for (uint32_t w = 2; w <= 64; w += 2) {
        widths.push_back(w);
    }
    for (uint32_t w : {94u, 96u, 98u, 100u, 718u, 720u, 1278u, 1280u, 1918u, 1920u, 3840u}) {
        widths.push_back(w);
    }
    return widths;
}

/* The buffers have exactly the size of the line, so that an address sanitizer reports any access after it. The
 * offset moves the V210 line off the 32-byte alignment of the allocation. */
void test_unpack(unpack_fn fn) {
    std::mt19937 rnd(210);
    for (uint32_t width : line_widths()) {
        for (uint32_t offset : {0u, 1u, 4u}) {
            const size_t line_bytes = v210_line_words(width) * sizeof(uint32_t);
            std::vector<uint8_t> storage(line_bytes + offset);
            uint8_t* line = storage.data() + offset;
            // Every bit random: bits 30-31 and the samples after the end of the line must be ignored.
            for (size_t i = 0; i < line_bytes; i++) {
                line[i] = (uint8_t)rnd();
            }
            std::vector<uint16_t> ref_y(width), ref_cb(width / 2), ref_cr(width / 2);
            std::vector<uint16_t> y(width), cb(width / 2), cr(width / 2);
            ref_unpack(line, width, ref_y.data(), ref_cb.data(), ref_cr.data());
            fn(line, y.data(), cb.data(), cr.data(), width);
            ASSERT_EQ(ref_y, y) << "width " << width << " offset " << offset;
            ASSERT_EQ(ref_cb, cb) << "width " << width << " offset " << offset;
            ASSERT_EQ(ref_cr, cr) << "width " << width << " offset " << offset;
        }
    }
}

void test_pack(pack_fn fn) {
    std::mt19937 rnd(422);
    for (uint32_t width : line_widths()) {
        for (uint32_t offset : {0u, 1u, 4u}) {
            // 16 random bits per sample: only the low 10 bits may be used.
            std::vector<uint16_t> y(width), cb(width / 2), cr(width / 2);
            for (auto* plane : {&y, &cb, &cr}) {
                for (uint16_t& sample : *plane) {
                    sample = (uint16_t)rnd();
                }
            }
            const size_t line_bytes = v210_line_words(width) * sizeof(uint32_t);
            std::vector<uint8_t> ref(line_bytes);
            ref_pack(y.data(), cb.data(), cr.data(), width, ref.data());
            std::vector<uint8_t> storage(line_bytes + offset, 0xA5);
            uint8_t* line = storage.data() + offset;
            fn(y.data(), cb.data(), cr.data(), line, width);
            ASSERT_EQ(0, memcmp(ref.data(), line, line_bytes)) << "width " << width << " offset " << offset;
        }
    }
}

} // namespace

TEST(V210, UnpackC) {
    test_unpack(convert_packed_to_planar_v210_c);
}

TEST(V210, PackC) {
    test_pack(convert_planar_to_packed_v210_c);
}

#ifdef ARCH_X86_64
TEST(V210, UnpackAVX2) {
    if (!(get_cpu_flags() & CPU_FLAGS_AVX2)) {
        GTEST_SKIP() << "no AVX2";
    }
    test_unpack(convert_packed_to_planar_v210_avx2);
}

TEST(V210, PackAVX2) {
    if (!(get_cpu_flags() & CPU_FLAGS_AVX2)) {
        GTEST_SKIP() << "no AVX2";
    }
    test_pack(convert_planar_to_packed_v210_avx2);
}
#endif /* ARCH_X86_64 */

namespace {

struct Picture422 {
    uint32_t width;
    uint32_t height;
    std::vector<uint16_t> y, cb, cr; // tightly packed planes
};

/* Smooth gradients with noise, so that the wavelet bands are not empty. */
Picture422 make_picture(uint32_t width, uint32_t height, uint32_t seed) {
    std::mt19937 rnd(seed);
    Picture422 picture{width, height, {}, {}, {}};
    picture.y.resize((size_t)width * height);
    picture.cb.resize((size_t)width / 2 * height);
    picture.cr.resize((size_t)width / 2 * height);
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t x = 0; x < width; x++) {
            picture.y[(size_t)row * width + x] = (uint16_t)((64 + x * 3 + row * 5 + rnd() % 64) % 1024);
        }
        for (uint32_t x = 0; x < width / 2; x++) {
            picture.cb[(size_t)row * width / 2 + x] = (uint16_t)((512 + x * 2 - row + rnd() % 32) % 1024);
            picture.cr[(size_t)row * width / 2 + x] = (uint16_t)((300 + x + row * 3 + rnd() % 32) % 1024);
        }
    }
    return picture;
}

/* V210 of the picture with stride_words per line, in a buffer of exactly the size the API requires. The unused
 * bits (30-31, the samples after the end of the line in the last group, the words after the groups) are random. */
std::vector<uint8_t> make_v210(const Picture422& picture, uint32_t stride_words, uint32_t seed) {
    std::mt19937 rnd(seed);
    const uint32_t line_words = v210_line_words(picture.width);
    const size_t size = ((size_t)stride_words * (picture.height - 1) + line_words) * sizeof(uint32_t);
    std::vector<uint8_t> v210(size);
    for (uint8_t& byte : v210) {
        byte = (uint8_t)rnd();
    }
    std::vector<uint8_t> line(line_words * sizeof(uint32_t));
    for (uint32_t row = 0; row < picture.height; row++) {
        ref_pack(&picture.y[(size_t)row * picture.width],
                 &picture.cb[(size_t)row * picture.width / 2],
                 &picture.cr[(size_t)row * picture.width / 2],
                 picture.width,
                 line.data());
        uint8_t* out = &v210[(size_t)row * stride_words * sizeof(uint32_t)];
        for (size_t i = 0; i < line.size(); i++) {
            // Keep the random unused bits: bits 30-31 of each word, and the samples after the end of the line.
            out[i] = line[i];
        }
        for (uint32_t word = 0; word < line_words; word++) {
            out[4 * word + 3] |= (uint8_t)(rnd() & 0xC0);
        }
        const uint32_t pixels_in_last_group = picture.width % 6;
        if (pixels_in_last_group) {
            uint8_t* last = out + (line_words - 4) * sizeof(uint32_t);
            // The samples of pixels 2 to 5 (4 pixels) or 4 to 5 (2 pixels): stream order indices 4.. or 8..
            for (uint32_t index = pixels_in_last_group * 2; index < 12; index++) {
                ref_write_sample(last, 0, index, rnd() & 0x3FF);
            }
        }
    }
    return v210;
}

struct EncodeSettings {
    uint32_t ndecomp_v;
    uint32_t slice_height;
    uint8_t lossless;
};

/* The kernels of each instruction set: C only, and all that the host has. */
const uint64_t cpu_flags_list[] = {CPU_FLAGS_C, CPU_FLAGS_ALL};

std::vector<uint8_t> encode(const Picture422& picture, const EncodeSettings& settings, bool as_v210, uint32_t v210_extra_words,
                            uint64_t cpu_flags = CPU_FLAGS_ALL) {
    svt_jpeg_xs_encoder_api_t enc;
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.use_cpu_flags = cpu_flags;
    enc.source_width = picture.width;
    enc.source_height = picture.height;
    enc.input_bit_depth = 10;
    enc.colour_format = as_v210 ? COLOUR_FORMAT_PACKED_V210 : COLOUR_FORMAT_PLANAR_YUV422;
    enc.ndecomp_v = settings.ndecomp_v;
    enc.slice_height = settings.slice_height;
    enc.bpp_numerator = 3;
    enc.bpp_denominator = 1;
    enc.lossless_enable = settings.lossless;
    enc.threads_num = 4;

    svt_jpeg_xs_image_config_t config;
    uint32_t bytes_per_frame = 0;
    EXPECT_EQ(
        svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &config, &bytes_per_frame),
        SvtJxsErrorNone);
    EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);

    svt_jpeg_xs_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    std::vector<uint8_t> v210;
    if (as_v210) {
        const uint32_t stride = v210_line_words(picture.width) + v210_extra_words;
        v210 = make_v210(picture, stride, picture.width * 7 + picture.height);
        frame.image.data_yuv[0] = v210.data();
        frame.image.stride[0] = stride;
        frame.image.alloc_size[0] = (uint32_t)v210.size();
    }
    else {
        const std::vector<uint16_t>* planes[3] = {&picture.y, &picture.cb, &picture.cr};
        for (uint32_t c = 0; c < 3; c++) {
            frame.image.data_yuv[c] = (void*)planes[c]->data();
            frame.image.stride[c] = c ? picture.width / 2 : picture.width;
            frame.image.alloc_size[c] = (uint32_t)(planes[c]->size() * sizeof(uint16_t));
        }
    }
    std::vector<uint8_t> bitstream(bytes_per_frame + 4096);
    frame.bitstream.buffer = bitstream.data();
    frame.bitstream.allocation_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &frame, 1), SvtJxsErrorNone);
    svt_jpeg_xs_frame_t out;
    memset(&out, 0, sizeof(out));
    EXPECT_EQ(svt_jpeg_xs_encoder_get_packet(&enc, &out, 1), SvtJxsErrorNone);
    std::vector<uint8_t> result(out.bitstream.buffer, out.bitstream.buffer + out.bitstream.used_size);
    svt_jpeg_xs_encoder_close(&enc);
    return result;
}

/* Decodes to planar output, returns the planes tightly packed. */
Picture422 decode_planar(const std::vector<uint8_t>& bitstream, proxy_mode_t proxy_mode, uint32_t threads) {
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_NONE;
    dec.use_cpu_flags = CPU_FLAGS_ALL;
    dec.proxy_mode = proxy_mode;
    dec.threads_num = threads;
    svt_jpeg_xs_image_config_t config;
    EXPECT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.data(), bitstream.size(), &config),
              SvtJxsErrorNone);
    EXPECT_EQ(config.format, COLOUR_FORMAT_PLANAR_YUV422);
    Picture422 picture{config.width, config.height, {}, {}, {}};
    picture.y.resize((size_t)config.width * config.height);
    picture.cb.resize((size_t)config.width / 2 * config.height);
    picture.cr.resize((size_t)config.width / 2 * config.height);
    svt_jpeg_xs_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    std::vector<uint16_t>* planes[3] = {&picture.y, &picture.cb, &picture.cr};
    for (uint32_t c = 0; c < 3; c++) {
        frame.image.data_yuv[c] = planes[c]->data();
        frame.image.stride[c] = config.components[c].width;
        frame.image.alloc_size[c] = (uint32_t)(planes[c]->size() * sizeof(uint16_t));
    }
    frame.bitstream.buffer = (uint8_t*)bitstream.data();
    frame.bitstream.used_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &frame, 1), SvtJxsErrorNone);
    svt_jpeg_xs_frame_t out;
    EXPECT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &out, 1), SvtJxsErrorNone);
    svt_jpeg_xs_decoder_close(&dec);
    return picture;
}

/* Decodes to V210 with stride_words per line, into a buffer of exactly the size the API requires; the bytes
 * that are not part of a line are 0xA5 before the decode. */
std::vector<uint8_t> decode_v210(const std::vector<uint8_t>& bitstream, proxy_mode_t proxy_mode, uint32_t threads,
                                 uint32_t extra_words, uint32_t* stride_words, svt_jpeg_xs_image_config_t* config,
                                 uint64_t cpu_flags = CPU_FLAGS_ALL) {
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_NONE;
    dec.use_cpu_flags = cpu_flags;
    dec.proxy_mode = proxy_mode;
    dec.threads_num = threads;
    dec.output_packed_format = COLOUR_FORMAT_PACKED_V210;
    EXPECT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.data(), bitstream.size(), config),
              SvtJxsErrorNone);
    EXPECT_EQ(config->format, COLOUR_FORMAT_PACKED_V210);
    EXPECT_EQ(config->components_num, 1);
    EXPECT_EQ(config->components[0].byte_size, v210_aligned_line_words(config->width) * 4 * config->height);
    *stride_words = v210_line_words(config->width) + extra_words;
    std::vector<uint8_t> v210(((size_t)*stride_words * (config->height - 1) + v210_line_words(config->width)) * 4, 0xA5);
    svt_jpeg_xs_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.image.data_yuv[0] = v210.data();
    frame.image.stride[0] = *stride_words;
    frame.image.alloc_size[0] = (uint32_t)v210.size();
    frame.bitstream.buffer = (uint8_t*)bitstream.data();
    frame.bitstream.used_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &frame, 1), SvtJxsErrorNone);
    svt_jpeg_xs_frame_t out;
    EXPECT_EQ(svt_jpeg_xs_decoder_get_frame(&dec, &out, 1), SvtJxsErrorNone);
    svt_jpeg_xs_decoder_close(&dec);
    return v210;
}

struct Case {
    uint32_t width;
    uint32_t height;
    EncodeSettings settings;
};

std::vector<Case> cases() {
    std::vector<Case> list;
    // 64 and 1920: whole groups; 100 and 130: the last group has 4 pixels; 194: it has 2 pixels.
    for (uint32_t width : {64u, 100u, 130u, 194u, 1920u}) {
        for (uint32_t ndecomp_v = 0; ndecomp_v <= 2; ndecomp_v++) {
            list.push_back({width, width == 1920 ? 40u : 72u, {ndecomp_v, 16, 0}});
        }
    }
    list.push_back({130, 70, {2, 8, 0}});
    list.push_back({100, 48, {1, 16, 1}});
    return list;
}

} // namespace

/* The codestream of a V210 picture is the codestream of the same picture given as planar 4:2:2. */
TEST(V210, EncodeSameAsPlanar) {
    for (const Case& c : cases()) {
        const Picture422 picture = make_picture(c.width, c.height, c.width + c.settings.ndecomp_v);
        const std::vector<uint8_t> planar = encode(picture, c.settings, false, 0);
        ASSERT_GT(planar.size(), 0u);
        for (uint64_t cpu_flags : cpu_flags_list) {
            for (uint32_t extra_words : {0u, 3u}) {
                const std::vector<uint8_t> v210 = encode(picture, c.settings, true, extra_words, cpu_flags);
                ASSERT_EQ(planar, v210) << c.width << "x" << c.height << " ndecomp_v " << c.settings.ndecomp_v << " slice height "
                                        << c.settings.slice_height << " lossless " << (int)c.settings.lossless << " extra words "
                                        << extra_words << " cpu flags " << cpu_flags;
            }
        }
    }
}

/* V210 output is the V210 packing of the planar output, and the decoder writes only the groups of each line. */
TEST(V210, DecodeSameAsPlanar) {
    for (const Case& c : cases()) {
        const Picture422 picture = make_picture(c.width, c.height, c.width + c.settings.ndecomp_v);
        const std::vector<uint8_t> bitstream = encode(picture, c.settings, false, 0);
        std::vector<proxy_mode_t> proxy_modes = {proxy_mode_full};
        if (c.settings.ndecomp_v >= 1 && c.width % 4 == 0) {
            proxy_modes.push_back(proxy_mode_half);
        }
        if (c.settings.ndecomp_v >= 2 && c.width % 8 == 0) {
            proxy_modes.push_back(proxy_mode_quarter);
        }
        for (proxy_mode_t proxy_mode : proxy_modes) {
            for (uint32_t threads : {0u, 4u}) {
                const Picture422 planar = decode_planar(bitstream, proxy_mode, threads);
                for (uint64_t cpu_flags : cpu_flags_list) {
                    uint32_t stride_words = 0;
                    svt_jpeg_xs_image_config_t config;
                    const std::vector<uint8_t> v210 = decode_v210(
                        bitstream, proxy_mode, threads, 3, &stride_words, &config, cpu_flags);
                    ASSERT_EQ(config.width, planar.width);
                    ASSERT_EQ(config.height, planar.height);
                    const uint32_t line_bytes = v210_line_words(planar.width) * 4;
                    std::vector<uint8_t> expected(line_bytes);
                    for (uint32_t row = 0; row < planar.height; row++) {
                        ref_pack(&planar.y[(size_t)row * planar.width],
                                 &planar.cb[(size_t)row * planar.width / 2],
                                 &planar.cr[(size_t)row * planar.width / 2],
                                 planar.width,
                                 expected.data());
                        const uint8_t* line = &v210[(size_t)row * stride_words * 4];
                        ASSERT_EQ(0, memcmp(expected.data(), line, line_bytes))
                            << c.width << "x" << c.height << " ndecomp_v " << c.settings.ndecomp_v << " proxy " << proxy_mode
                            << " threads " << threads << " cpu flags " << cpu_flags << " row " << row;
                        if (row + 1 < planar.height) {
                            for (uint32_t i = line_bytes; i < stride_words * 4; i++) {
                                ASSERT_EQ(line[i], 0xA5) << "byte after the line written, row " << row;
                            }
                        }
                    }
                }
            }
        }
    }
}

/* Lossless coding gives back the V210 input, without its unused bits. */
TEST(V210, LosslessRoundTrip) {
    for (uint64_t cpu_flags : cpu_flags_list) {
        const Picture422 picture = make_picture(130, 48, 5);
        const EncodeSettings settings = {2, 16, 1};
        const std::vector<uint8_t> bitstream = encode(picture, settings, true, 1, cpu_flags);
        uint32_t stride_words = 0;
        svt_jpeg_xs_image_config_t config;
        const std::vector<uint8_t> v210 = decode_v210(bitstream, proxy_mode_full, 0, 0, &stride_words, &config, cpu_flags);
        std::vector<uint8_t> expected(v210_line_words(picture.width) * 4);
        for (uint32_t row = 0; row < picture.height; row++) {
            ref_pack(&picture.y[(size_t)row * picture.width],
                     &picture.cb[(size_t)row * picture.width / 2],
                     &picture.cr[(size_t)row * picture.width / 2],
                     picture.width,
                     expected.data());
            ASSERT_EQ(0, memcmp(expected.data(), &v210[(size_t)row * stride_words * 4], expected.size()))
                << "row " << row << " cpu flags " << cpu_flags;
        }
    }
}

namespace {

SvtJxsErrorType_t init_encoder(ColourFormat_t format, uint32_t width, uint8_t bit_depth, uint8_t msb_aligned,
                               uint8_t cpu_profile) {
    svt_jpeg_xs_encoder_api_t enc;
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = width;
    enc.source_height = 32;
    enc.input_bit_depth = bit_depth;
    enc.input_bit_depth_msb_aligned = msb_aligned;
    enc.cpu_profile = cpu_profile;
    enc.colour_format = format;
    enc.bpp_numerator = 3;
    enc.bpp_denominator = 1;
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
    svt_jpeg_xs_encoder_close(&enc);
    return ret;
}

std::vector<uint8_t> encode_format(ColourFormat_t format, uint8_t bit_depth) {
    svt_jpeg_xs_encoder_api_t enc;
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 64;
    enc.source_height = 32;
    enc.input_bit_depth = bit_depth;
    enc.colour_format = format;
    enc.bpp_numerator = 3;
    enc.bpp_denominator = 1;
    svt_jpeg_xs_image_config_t config;
    uint32_t bytes_per_frame = 0;
    EXPECT_EQ(
        svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &config, &bytes_per_frame),
        SvtJxsErrorNone);
    EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);
    svt_jpeg_xs_image_buffer_t* image = svt_jpeg_xs_image_buffer_alloc(&config);
    for (uint32_t c = 0; c < config.components_num; c++) {
        memset(image->data_yuv[c], 0, image->alloc_size[c]);
    }
    std::vector<uint8_t> bitstream(bytes_per_frame + 4096);
    svt_jpeg_xs_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.image = *image;
    frame.bitstream.buffer = bitstream.data();
    frame.bitstream.allocation_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &frame, 1), SvtJxsErrorNone);
    svt_jpeg_xs_frame_t out;
    memset(&out, 0, sizeof(out));
    EXPECT_EQ(svt_jpeg_xs_encoder_get_packet(&enc, &out, 1), SvtJxsErrorNone);
    bitstream.resize(out.bitstream.used_size);
    svt_jpeg_xs_encoder_close(&enc);
    svt_jpeg_xs_image_buffer_free(image);
    return bitstream;
}

SvtJxsErrorType_t init_decoder(const std::vector<uint8_t>& bitstream, uint8_t output_packed_format, uint8_t msb_aligned) {
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_NONE;
    dec.output_packed_format = output_packed_format;
    dec.output_bit_depth_msb_aligned = msb_aligned;
    svt_jpeg_xs_image_config_t config;
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.data(), bitstream.size(), &config);
    svt_jpeg_xs_decoder_close(&dec);
    return ret;
}

} // namespace

TEST(V210, EncoderParameters) {
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 64, 10, 0, 0 /*Low latency*/), SvtJxsErrorNone);
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 64, 8, 0, 0 /*Low latency*/), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 64, 12, 0, 0 /*Low latency*/), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 66, 10, 0, 0 /*Low latency*/), SvtJxsErrorNone);
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 65, 10, 0, 0 /*Low latency*/), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 64, 10, 1, 0 /*Low latency*/), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_encoder(COLOUR_FORMAT_PACKED_V210, 64, 10, 0, 1 /*Low CPU usage*/), SvtJxsErrorBadParameter);
}

TEST(V210, EncoderImageConfigAndBuffer) {
    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 1280; // 26.7 groups of 48 pixels: lines of 27 * 128 bytes
    enc.source_height = 20;
    enc.input_bit_depth = 10;
    enc.colour_format = COLOUR_FORMAT_PACKED_V210;
    enc.bpp_numerator = 3;
    enc.bpp_denominator = 1;
    svt_jpeg_xs_image_config_t config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(
        svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &config, &bytes_per_frame),
        SvtJxsErrorNone);
    EXPECT_EQ(config.format, COLOUR_FORMAT_PACKED_V210);
    EXPECT_EQ(config.components_num, 1);
    EXPECT_EQ(config.components[0].width, 1280u);
    EXPECT_EQ(config.components[0].height, 20u);
    EXPECT_EQ(config.components[0].byte_size, 27u * 128 * 20);
    svt_jpeg_xs_image_buffer_t* image = svt_jpeg_xs_image_buffer_alloc(&config);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->stride[0], 27u * 32);
    EXPECT_EQ(image->alloc_size[0], 27u * 128 * 20);
    svt_jpeg_xs_image_buffer_free(image);
}

TEST(V210, EncoderInputBufferChecks) {
    svt_jpeg_xs_encoder_api_t enc;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc),
              SvtJxsErrorNone);
    enc.verbose = VERBOSE_NONE;
    enc.source_width = 100;
    enc.source_height = 16;
    enc.input_bit_depth = 10;
    enc.colour_format = COLOUR_FORMAT_PACKED_V210;
    enc.bpp_numerator = 3;
    enc.bpp_denominator = 1;
    svt_jpeg_xs_image_config_t config;
    uint32_t bytes_per_frame = 0;
    ASSERT_EQ(
        svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &config, &bytes_per_frame),
        SvtJxsErrorNone);
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);
    const uint32_t line_words = v210_line_words(100); // 17 groups
    ASSERT_EQ(line_words, 68u);
    std::vector<uint8_t> v210((size_t)line_words * 4 * 16);
    std::vector<uint8_t> bitstream(bytes_per_frame + 4096);
    svt_jpeg_xs_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.image.data_yuv[0] = v210.data();
    frame.image.stride[0] = line_words - 1;
    frame.image.alloc_size[0] = (uint32_t)v210.size();
    frame.bitstream.buffer = bitstream.data();
    frame.bitstream.allocation_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &frame, 1), SvtJxsErrorBadParameter);
    frame.image.stride[0] = line_words;
    frame.image.alloc_size[0] = (uint32_t)v210.size() - 1;
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &frame, 1), SvtJxsErrorBadParameter);
    frame.image.alloc_size[0] = (uint32_t)v210.size();
    frame.image.data_yuv[0] = nullptr;
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc, &frame, 1), SvtJxsErrorBadParameter);
    svt_jpeg_xs_encoder_close(&enc);
}

TEST(V210, DecoderParameters) {
    const std::vector<uint8_t> yuv422_10 = encode_format(COLOUR_FORMAT_PLANAR_YUV422, 10);
    EXPECT_EQ(init_decoder(yuv422_10, 0, 0), SvtJxsErrorNone);
    EXPECT_EQ(init_decoder(yuv422_10, COLOUR_FORMAT_PACKED_V210, 0), SvtJxsErrorNone);
    EXPECT_EQ(init_decoder(yuv422_10, COLOUR_FORMAT_PACKED_V210, 1), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_decoder(yuv422_10, COLOUR_FORMAT_PACKED_YUV444_OR_RGB, 0), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_decoder(yuv422_10, COLOUR_FORMAT_PLANAR_YUV422, 0), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_decoder(encode_format(COLOUR_FORMAT_PLANAR_YUV422, 8), COLOUR_FORMAT_PACKED_V210, 0), SvtJxsErrorBadParameter);
    EXPECT_EQ(init_decoder(encode_format(COLOUR_FORMAT_PLANAR_YUV422, 12), COLOUR_FORMAT_PACKED_V210, 0),
              SvtJxsErrorBadParameter);
    EXPECT_EQ(init_decoder(encode_format(COLOUR_FORMAT_PLANAR_YUV444, 10), COLOUR_FORMAT_PACKED_V210, 0),
              SvtJxsErrorBadParameter);
    EXPECT_EQ(init_decoder(encode_format(COLOUR_FORMAT_PLANAR_YUV420, 10), COLOUR_FORMAT_PACKED_V210, 0),
              SvtJxsErrorBadParameter);
}

TEST(V210, DecoderOutputBufferChecks) {
    const std::vector<uint8_t> bitstream = encode_format(COLOUR_FORMAT_PLANAR_YUV422, 10);
    svt_jpeg_xs_decoder_api_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.verbose = VERBOSE_NONE;
    dec.output_packed_format = COLOUR_FORMAT_PACKED_V210;
    svt_jpeg_xs_image_config_t config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(
                  SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.data(), bitstream.size(), &config),
              SvtJxsErrorNone);
    const uint32_t line_words = v210_line_words(64);
    std::vector<uint8_t> v210((size_t)line_words * 4 * 32);
    svt_jpeg_xs_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.image.data_yuv[0] = v210.data();
    frame.image.stride[0] = line_words - 1;
    frame.image.alloc_size[0] = (uint32_t)v210.size();
    frame.bitstream.buffer = (uint8_t*)bitstream.data();
    frame.bitstream.used_size = (uint32_t)bitstream.size();
    EXPECT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &frame, 1), SvtJxsErrorBadParameter);
    frame.image.stride[0] = line_words;
    frame.image.alloc_size[0] = (uint32_t)v210.size() - 1;
    EXPECT_EQ(svt_jpeg_xs_decoder_send_frame(&dec, &frame, 1), SvtJxsErrorBadParameter);
    svt_jpeg_xs_decoder_close(&dec);
}
