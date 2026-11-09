/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "gtest/gtest.h"
#include "SvtJpegxsEnc.h"
#include "ProfileLevel.h"
static svt_jpeg_xs_encoder_api_t profile_config(ColourFormat_t format, uint8_t depth,
                                                uint8_t vertical, bool lossless, bool raw) {
    svt_jpeg_xs_encoder_api_t enc = {};
    EXPECT_EQ(svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc), SvtJxsErrorNone);
    enc.source_width = 128; enc.source_height = 48;
    enc.input_bit_depth = depth; enc.colour_format = format;
    enc.ndecomp_v = vertical; enc.ndecomp_h = 5; enc.slice_height = 16;
    enc.lossless_enable = lossless; enc.coding_raw_disable = !raw;
    enc.bpp_numerator = 8; enc.bpp_denominator = 1;
    enc.threads_num = 1; enc.use_cpu_flags = 0; enc.verbose = VERBOSE_NONE;
    return enc;
}


#include <vector>
#include <fstream>
#include <cstdlib>
#include "SvtJpegxsImageBufferTools.h"
#include "DecoderSimple.h"

static std::vector<uint8_t> profile_stream(svt_jpeg_xs_encoder_api_t enc) {
    svt_jpeg_xs_image_config_t cfg = {};
    uint32_t capacity = 0;
    EXPECT_EQ(svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR,SVT_JPEGXS_API_VER_MINOR,
                                                &enc,&cfg,&capacity),SvtJxsErrorNone);
    auto *image = svt_jpeg_xs_image_buffer_alloc(&cfg);
    if (!image) { ADD_FAILURE() << "image allocation"; return {}; }
    for (int32_t c=0;c<cfg.components_num;++c) {
        const uint32_t count=image->alloc_size[c]/(enc.input_bit_depth>8 ? 2 : 1);
        for (uint32_t i=0;i<count;++i) {
            const uint16_t value=(i*73+i*i*19+c*31+17)&((1u<<enc.input_bit_depth)-1);
            if(enc.input_bit_depth>8) static_cast<uint16_t *>(image->data_yuv[c])[i]=value;
            else static_cast<uint8_t *>(image->data_yuv[c])[i]=(uint8_t)value;
        }
    }
    std::vector<uint8_t> output(capacity*2+4096);
    EXPECT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR,SVT_JPEGXS_API_VER_MINOR,&enc),SvtJxsErrorNone);
    svt_jpeg_xs_frame_t frame = {};
    frame.image=*image; frame.bitstream.buffer=output.data(); frame.bitstream.allocation_size=static_cast<uint32_t>(output.size());
    EXPECT_EQ(svt_jpeg_xs_encoder_send_picture(&enc,&frame,1),SvtJxsErrorNone);
    svt_jpeg_xs_frame_t packet = {};
    EXPECT_EQ(svt_jpeg_xs_encoder_get_packet(&enc,&packet,1),SvtJxsErrorNone);
    output.resize(packet.bitstream.used_size);
    svt_jpeg_xs_encoder_close(&enc); svt_jpeg_xs_image_buffer_free(image);
    return output;
}

static std::vector<uint8_t> profile_pixels(const std::vector<uint8_t>& stream) {
    DecoderSimple_t decoder = {};
    decoder_simple_init_rtcd(0);
    EXPECT_EQ(decoder_simple_alloc(&decoder,stream.data(),stream.size()),SvtJxsErrorNone);
    EXPECT_GE(decoder_simple_get_frame(&decoder,stream.data(),stream.size()),0);
    std::vector<uint8_t> pixels;
    const uint32_t bytes=decoder.image_config.bit_depth>8 ? 2 : 1;
    for(int32_t c=0;c<decoder.image_config.components_num;++c) {
        const auto& comp=decoder.image_config.components[c];
        for(uint32_t y=0;y<comp.height;++y) {
            const uint8_t *row=static_cast<uint8_t *>(decoder.image_out->data_yuv[c])+y*decoder.image_out->stride[c]*bytes;
            pixels.insert(pixels.end(),row,row+comp.width*bytes);
        }
    }
    decoder_simple_free(&decoder);
    return pixels;
}

TEST(AutomaticProfileConstraints, DeclarationOnlyPreservesPayloadAndPixels) {
    const ColourFormat_t formats[]={COLOUR_FORMAT_PLANAR_YUV420,COLOUR_FORMAT_PLANAR_YUV422,
        COLOUR_FORMAT_PLANAR_YUV444_OR_RGB,COLOUR_FORMAT_PLANAR_4_COMPONENTS,
        COLOUR_FORMAT_PLANAR_YUV422_ALPHA,COLOUR_FORMAT_PACKED_YUV444_OR_RGB};
    unsigned completed=0;
    for(auto f:formats) for(uint8_t depth=8;depth<=14;++depth) for(uint8_t v=0;v<=2;++v)
    for(bool lossless:{false,true}) for(bool raw:{false,true}) {
        if(f==COLOUR_FORMAT_PLANAR_YUV420 && v==0) continue; // Explicitly invalid layout/transform combination.
        auto enc=profile_config(f,depth,v,lossless,raw);
        auto automatic=profile_stream(enc);
        enc.profile_ppih_override=derive_stream_profile_ppih(f,depth,lossless,VERBOSE_NONE);
        auto legacy=profile_stream(enc);
        ASSERT_FALSE(automatic.empty()); ASSERT_EQ(automatic.size(),legacy.size());
        const auto pixels=profile_pixels(automatic);
        ASSERT_EQ(pixels,profile_pixels(legacy));
        // Ppih is the only field permitted to change; locate PIH through the marker parser.
        ASSERT_EQ(automatic[0],0xff); ASSERT_EQ(automatic[1],0x10);
        // PIH follows the variable-length CAP marker.
        size_t offset=2;
        ASSERT_EQ(automatic[offset],0xff); ASSERT_EQ(automatic[offset+1],0x50);
        offset+=2+((automatic[offset+2]<<8)|automatic[offset+3]);
        ASSERT_EQ(automatic[offset],0xff); ASSERT_EQ(automatic[offset+1],0x12);
        offset+=8; // PIH marker, Lpih, Lcod, then Ppih.
        if(const char *dir=std::getenv("JPEGXS_PROFILE_ARTIFACT_DIR")) {
            const std::string stem=std::string(dir)+"/f"+std::to_string(f)+"-b"+std::to_string(depth)+"-v"+
                std::to_string(v)+"-l"+std::to_string(lossless)+"-r"+std::to_string(raw);
            std::ofstream a(stem+"-auto.jxs",std::ios::binary), b(stem+"-legacy.jxs",std::ios::binary);
            ASSERT_TRUE(a.good()); ASSERT_TRUE(b.good());
            std::ofstream decoded(stem+"-pixels.bin",std::ios::binary);
            ASSERT_TRUE(decoded.good());
            decoded.write(reinterpret_cast<const char *>(pixels.data()),pixels.size());
            a.write(reinterpret_cast<const char *>(automatic.data()),automatic.size());
            b.write(reinterpret_cast<const char *>(legacy.data()),legacy.size());
        }
        automatic[offset]=legacy[offset]; automatic[offset+1]=legacy[offset+1];
        EXPECT_EQ(automatic,legacy);
        ++completed;
    }
    EXPECT_EQ(completed,476u);
}

TEST(AutomaticProfileConstraints, MainAndHighAreWrittenForCompatibleConfigurations) {
    for(uint8_t vertical:{1,2}) {
        auto stream=profile_stream(profile_config(COLOUR_FORMAT_PLANAR_YUV422,8,vertical,false,false));
        ASSERT_GT(stream.size(),32u);
        size_t pih=4+((stream[4]<<8)|stream[5]);
        ASSERT_EQ(stream[pih],0xff); ASSERT_EQ(stream[pih+1],0x12);
        uint16_t ppih=(stream[pih+8]<<8)|stream[pih+9];
        EXPECT_EQ(ppih,vertical==1?JXS_PPIH_MAIN_422_10:JXS_PPIH_HIGH_444_12);
    }
}
