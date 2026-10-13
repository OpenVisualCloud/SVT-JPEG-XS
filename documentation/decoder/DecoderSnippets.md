# Decoder configuration parameters overview (svt_jpeg_xs_decoder_api_t)

param | description | mandatory/optional | default | Accepted values
-- | -- | -- | -- | --
use_cpu_flags | Performance: limit assembly instruction set used by decoder,   please refer to CPU_FLAGS | optional | CPU_FLAGS_C (0) | CPU_FLAGS_C, CPU_FLAGS_MMX, CPU_FLAGS_SSE ,CPU_FLAGS_SSE2 ,CPU_FLAGS_SSE3 ,CPU_FLAGS_SSSE3 ,CPU_FLAGS_SSE4_1 ,CPU_FLAGS_SSE4_2 ,CPU_FLAGS_AVX ,CPU_FLAGS_AVX2 ,CPU_FLAGS_NEON ,CPU_FLAGS_ALL (all flags, including avx512, POPCNT, BMI2 and NEON)
threads_num | Performance: Number of thread decoder can create, 0 mean   minimum number of threads is created | optional | 0 | <0;N/A>
verbose | Limit number of logs in console, please refer to VerboseMessages enum | optional | VERBOSE_NONE (0) | VERBOSE_NONE, VERBOSE_ERRORS, VERBOSE_SYSTEM_INFO, VERBOSE_SYSTEM_INFO_ALL, VERBOSE_WARNINGS, VERBOSE_INFO_MULTITHREADING,VERBOSE_INFO_FULL
packetization_mode | Specify how bitstream is passed to decoder. 1 is rejected by `svt_jpeg_xs_decoder_init()` (SvtJxsErrorBadParameter) for variable bitrate (Lcod=0) and lossless (Fq=0) streams | optional | 0 | 1(multiple packets per frame, `svt_jpeg_xs_decoder_send_packet()`), 0(single packet per frame, `svt_jpeg_xs_decoder_send_frame()`)
proxy_mode | Decode the stream at reduced resolution, please refer to proxy_mode_t enum | optional | proxy_mode_full (0) | proxy_mode_full (full resolution), proxy_mode_half (1/2 width and height), proxy_mode_quarter (1/4 width and height). Half needs every component's decomposition >= 1, quarter >= 2, otherwise `svt_jpeg_xs_decoder_init()` returns SvtJxsErrorBadParameter: quarter never works for 4:2:0, and neither works with Sd>0. `svt_jpeg_xs_decoder_get_single_frame_size_with_proxy()` does not check this
output_bit_depth_msb_aligned | Non-standard, host-side only (not signalled in bitstream): when set, 10/12-bit output samples are MSB-aligned in each 16-bit word (`v << (16-output_bit_depth)`) instead of LSB-aligned (`v`). Must match the encoder's input_bit_depth_msb_aligned setting. Not applied to streams whose colour transform is done on the whole frame (for example Star-Tetrix, Cpih=3), which are always output LSB-aligned. | optional | 0 | 0(LSB-aligned), 1(MSB-aligned)
callback_send_data_available | Callback invoked when the decoder can accept new input data (after init and per frame taken from the input queue). Never called with packetization_mode=1 | optional | NULL | function pointer
callback_send_data_available_context | User context pointer passed to callback_send_data_available | optional | NULL | N/A
callback_get_data_available | Callback invoked when a decoded frame is available | optional | NULL | function pointer
callback_get_data_available_context | User context pointer passed to callback_get_data_available | optional | NULL | N/A

There is no function that loads decoder defaults; the defaults above are the values of a zero-initialised structure
(`svt_jpeg_xs_decoder_api_t dec = {0};`).

`svt_jpeg_xs_decoder_init()` only parses the headers of the passed bitstream; that first frame must still be
sent for decoding. `out_image_config` may be NULL, but then the check for an unsupported output format is skipped.

## Decoder simplified usage

Code listed below is also kept in [Source/App/SampleDecoder/main.c](../../Source/App/SampleDecoder/main.c)

```c
#include <SvtJpegxsDec.h>
#include <stdio.h>
#include <stdlib.h>

int32_t main(int32_t argc, char* argv[]) {
    if (argc < 2) {
        printf("Not set input file!\n");
        return -1;
    }
    const char* input_file_name = argv[1];
    FILE* input_file = NULL;
#ifdef _WIN32
    fopen_s(&input_file, input_file_name, "rb");
#else
    input_file = fopen(input_file_name, "rb");
#endif
    if (input_file == NULL) {
        printf("Can not open input file: %s!\n", input_file_name);
        return -1;
    }

    svt_jpeg_xs_decoder_api_t dec = {0};
    dec.verbose = VERBOSE_SYSTEM_INFO;
    dec.threads_num = 10;
    dec.use_cpu_flags = CPU_FLAGS_ALL;
    dec.proxy_mode = proxy_mode_full;

    svt_jpeg_xs_bitstream_buffer_t bitstream;
    // 1000 bytes is enough to obtain single encoded frame size when fast_search option is enabled
    bitstream.allocation_size = 1000;
    bitstream.buffer = malloc(bitstream.allocation_size);
    if (!bitstream.buffer) {
        return SvtJxsErrorInsufficientResources;
    }
    size_t read_size = fread(bitstream.buffer, 1, bitstream.allocation_size, input_file);
    printf("Read file %s size: %lu!\n", input_file_name, (unsigned long)read_size);
    //Reset the pointer to the start of the file
    fseek(input_file, 0, SEEK_SET);

    uint32_t frame_size = 0;
    SvtJxsErrorType_t err = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        (uint8_t*)bitstream.buffer, (uint32_t)read_size, NULL, &frame_size, 1, dec.proxy_mode);
    if (err) {
        return err;
    }
    if (frame_size == 0) {
        return SvtJxsErrorDecoderInvalidBitstream;
    }

    //Allocate buffer with sufficient size to store single frame
    bitstream.allocation_size = frame_size;
    bitstream.buffer = realloc(bitstream.buffer, bitstream.allocation_size);
    if (!bitstream.buffer) {
        return SvtJxsErrorInsufficientResources;
    }
    read_size = fread(bitstream.buffer, 1, bitstream.allocation_size, input_file);
    if (read_size != bitstream.allocation_size) {
        printf("Provided file does not have full frame\n");
        return SvtJxsErrorDecoderInvalidBitstream;
    }
    bitstream.used_size = read_size;
    fclose(input_file);

    svt_jpeg_xs_image_config_t image_config;
    err = svt_jpeg_xs_decoder_init(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, bitstream.buffer, read_size, &image_config);
    if (err) {
        return err;
    }

    svt_jpeg_xs_image_buffer_t image_buffer;
    uint32_t pixel_size = image_config.bit_depth <= 8 ? 1 : 2;
    for (uint8_t i = 0; i < image_config.components_num; ++i) {
        image_buffer.stride[i] = image_config.components[i].width;
        image_buffer.alloc_size[i] = image_buffer.stride[i] * image_config.components[i].height * pixel_size;
        image_buffer.data_yuv[i] = malloc(image_buffer.alloc_size[i]);
        if (!image_buffer.data_yuv[i]) {
            return SvtJxsErrorInsufficientResources;
        }
    }

    { ////loop over multiple frames
        svt_jpeg_xs_frame_t dec_input;
        dec_input.bitstream = bitstream;
        dec_input.image = image_buffer;
        err = svt_jpeg_xs_decoder_send_frame(&dec, &dec_input, 1 /*blocking*/);
        if (err) {
            return err;
        }
        svt_jpeg_xs_frame_t dec_output;
        err = svt_jpeg_xs_decoder_get_frame(&dec, &dec_output, 1 /*blocking*/);
        if (err) {
            return err;
        }

        //write YUV to file
        uint32_t pixel_size = image_config.bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
        for (int32_t c = 0; c < image_config.components_num; c++) {
            uint32_t size = image_config.components[c].width * image_config.components[c].height * pixel_size;
            if (size != image_config.components[c].byte_size) {
                printf("Invalid image config structure!!!\n");
                return -1;
            }
            printf("YUV Component %u resolution: %ux%u bitdepth: %u pointer: %p data_size: %u\n",
                   c,
                   image_config.components[c].width,
                   image_config.components[c].height,
                   image_config.bit_depth,
                   dec_output.image.data_yuv[c],
                   image_config.components[c].byte_size);
            //fwrite(dec_output.image.data_yuv[c], 1, image_config.components[c].byte_size, out_file);
        }

        //Get next bitstream/frame from file
        //fread(bitstream.buffer, 1, bitstream.allocation_size, input_file);
    }

    free(bitstream.buffer);
    for (uint8_t i = 0; i < image_config.components_num; ++i) {
        free(image_buffer.data_yuv[i]);
    }
    svt_jpeg_xs_decoder_close(&dec);
    return 0;
}
```

## Output colour format

`out_image_config->format` from `svt_jpeg_xs_decoder_init()` tells how the components are to be
interpreted. For 4:4:4 streams:

- Streams with the colour transform (Cpih=1) are reported as `COLOUR_FORMAT_PLANAR_RGB`
  (`COLOUR_FORMAT_PLANAR_RGBA` for 4 components), with the components in R, G, B(, A) order.
- Streams without it (Cpih=0) are reported as `COLOUR_FORMAT_PLANAR_YUV444`
  (`COLOUR_FORMAT_PLANAR_4_COMPONENTS`). Such a stream does not signal whether it holds YUV or RGB,
  so this must come from the container or the user.
- Star-Tetrix streams (Cpih=3, 4 components) are reported as `COLOUR_FORMAT_PLANAR_4_COMPONENTS`.
- `svt_jpeg_xs_decoder_get_single_frame_size()` and `_with_proxy()` report the same format as
  `svt_jpeg_xs_decoder_init()`.
- Earlier versions reported `COLOUR_FORMAT_PLANAR_YUV444_OR_RGB` (`COLOUR_FORMAT_PLANAR_4_COMPONENTS`)
  for Cpih=1 streams too; callers that switch on the format need a `COLOUR_FORMAT_PLANAR_RGB`
  (`COLOUR_FORMAT_PLANAR_RGBA`) case.

## Notes

The information in this document was compiled at `v0.10` of the code and may not
reflect the latest status of the design. For the most up-to-date
settings and implementation, it's recommended to visit the specific section of the code.
