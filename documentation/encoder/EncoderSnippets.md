# Encoder Snippets

## Encoder configuration parameters overview (svt_jpeg_xs_encoder_api_t)

param | description | mandatory/optional | default | Accepted values
-- | -- | -- | -- | --
source_width | Width of the image in sample grid positions | mandatory | N/A | <64; 65 535>
source_height | Height of the image in sample   grid positions | mandatory | N/A | <64; 65 535>
input_bit_depth | Specifies the bit depth of input video. | mandatory | N/A | 8(8 bit), 10(10 bit)
input_bit_depth_msb_aligned | Non-standard, host-side only (not signalled in bitstream): when set, 10/12-bit input samples are MSB-aligned in each 16-bit word (`v << (16-input_bit_depth)`) instead of LSB-aligned (`v`). Must match the decoder's output_bit_depth_msb_aligned setting. | optional | 0 | 0(LSB-aligned), 1(MSB-aligned)
colour_format | Specifies the format of input video,   please refer to ColourFormat_t enum | mandatory | N/A | Tested: (COLOUR_FORMAT_PLANAR_YUV420, COLOUR_FORMAT_PLANAR_YUV422, COLOUR_FORMAT_PLANAR_YUV444_OR_RGB),   experimental: (COLOUR_FORMAT_YUV400)
bpp_numerator | Bitrate: bits per pixel numerator,   BPP=(bpp_numerator/bpp_denominator), Per frame bitrate is equal to (width \* height \* bpp_numerator / bpp_denominator) | mandatory | N/A | <1;N/A>
bpp_denominator | Bitrate: bits per pixel denominator, required if non-integer   BPP is required | optional | 1 | <1; N/A>
use_cpu_flags | Performance: limit assembly instruction set used by encoder,   please refer to CPU_FLAGS | optional | CPU_FLAGS_ALL | CPU_FLAGS_C, CPU_FLAGS_MMX, CPU_FLAGS_SSE ,CPU_FLAGS_SSE2 ,CPU_FLAGS_SSE3 ,CPU_FLAGS_SSSE3 ,CPU_FLAGS_SSE4_1 ,CPU_FLAGS_SSE4_2 ,CPU_FLAGS_AVX ,CPU_FLAGS_AVX2 ,CPU_FLAGS_ALL (avx512)
threads_num | Performance: Number of thread encoder can create, 0 mean   minimum number of threads is created | optional | 0 | <0;N/A>
cpu_profile | Performance: Encoder internal threading model | optional | 0 (Low latency) | 0(Low latency), 1(Low CPU usage)
print_bands_info | Print to console band information(for debugging purposes) | optional | 0 (disable) | 0(disable), 1(enable)
verbose | Limit number of logs in console, please refer to VerboseMessages enum | optional | VERBOSE_SYSTEM_INFO | VERBOSE_NONE, VERBOSE_ERRORS, VERBOSE_SYSTEM_INFO, VERBOSE_SYSTEM_INFO_ALL, VERBOSE_WARNINGS, VERBOSE_INFO_MULTITHREADING,VERBOSE_INFO_FULL
ndecomp_v | Coding feature: specify number of Vertical decompositions | optional | 2 | 0, 1, 2
ndecomp_h | Coding feature: specify number of Horizontal decompositions | optional | 5 | 0, 1, 2, 3, 4, 5
quantization | Coding feature: specify quantization method | optional | 0 | 0(deadzone), 1(uniform)
slice_height | Coding feature: specify slice height  in units of picture luma pixels | optional | 16 | Any value in range <1;source_height>, also it have to be multiple of 2^(decomp_v))
coding_signs_handling | Coding feature: Sign handling strategy | optional | 0 (disable) | 0(disable), 1(fast), 2(full)
coding_significance | Coding feature: Signification coding | optional | 1 (enable) | 0(disable), 1(enable)
coding_vertical_prediction_mode | Coding feature: vertical prediction | optional | 0 (disable) | 0(disable), 1(zero prediction residuals), 2(zero   coefficients)
rate_control_mode | Rate control type | optional | 0 | 0(CBR: budget per precinct), 1(CBR: budget per precinct with padding movement), 2(CBR: budget per slice), 3(CBR: budget per slice with nax size RATE)
slice_packetization_mode | Specify how encoded stream is returned | optional | 0 | 1(multiple packets per frame), 0(single packet per frame)
coding_raw_disable | Disable packet-based raw-mode coding (disable for legacy-decoder compatibility) | optional | 0 (raw-mode enabled) | 0(raw-mode enabled), 1(raw-mode disabled)
cap_compat | Emit an empty CAP marker for legacy-decoder compatibility when no capability bit is required | optional | 0 (full CAP marker) | 0(full CAP marker), 1(empty CAP marker when no capability bit is set)
profile_ppih_override | Stream profile (Ppih) override. 0 auto-derives the closest ISO/IEC 21122-2 Annex A "Main" profile from colour_format/input_bit_depth (0x0000 is never a valid Ppih, so it safely doubles as the auto-derive sentinel); any other value is used verbatim | optional | 0 (auto-derive) | 0 (auto), or any Annex A Ppih codeword, e.g. 0x3540 (Main 422.10)
level_plev_override | Stream level (Plev) override. 0xFFFF auto-derives the tightest ISO/IEC 21122-2 Annex A level/sublevel bounding source_width/source_height/bpp_numerator/bpp_denominator (falling back to "Unrestricted" when nothing defined fits); any other value (including 0x0000, the valid "Unrestricted" codeword) is used verbatim | optional | 0xFFFF (auto-derive) | 0xFFFF (auto), or any Annex A Plev codeword
callback_send_data_available | Callback called when the next input element is taken from the queue | optional | NULL | function pointer
callback_send_data_available_context | Context pointer passed to callback_send_data_available | optional | NULL | pointer
callback_get_data_available | Callback called when a new encoded frame is ready to get | optional | NULL | function pointer
callback_get_data_available_context | Context pointer passed to callback_get_data_available | optional | NULL | pointer

## Encoder simplified usage

Code listed below is also kept in [Source/App/SampleEncoder/main.c](../../Source/App/SampleEncoder/main.c)

```c
#include <SvtJpegxsEnc.h>

int32_t main(int32_t argc, char* argv[]) {
    if (argc < 2) {
        printf("Not set input file!\n");
        return -1;
    }
    const char *input_file_name = argv[1];
    FILE *input_file = NULL;
#ifdef _WIN32
    fopen_s(&input_file, input_file_name, "rb");
#else
    input_file = fopen(input_file_name, "rb");
#endif
    if (input_file == NULL) {
        printf("Can not open input file: %s!\n", input_file_name);
        return -1;
    }

    svt_jpeg_xs_encoder_api_t enc;
    SvtJxsErrorType_t err = svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);

    if (err != SvtJxsErrorNone) {
        return err;
    }

    enc.source_width = 1920;
    enc.source_height = 1080;
    enc.input_bit_depth = 8;
    enc.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    enc.bpp_numerator = 3;

    err = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
    if (err != SvtJxsErrorNone) {
        return err;
    }

    //8bit YUV422 frame size:
    //luma:      width * height * 1 /*bytes per pixel*/
    //chroma cb: width * height * 1 /*bytes per pixel*/ * 0.5 /*chroma is half the luma size*/
    //chroma cr: width * height * 1 /*bytes per pixel*/ * 0.5 /*chroma is half the luma size*/

    uint32_t pixel_size = enc.input_bit_depth <= 8 ? 1 : 2;
    svt_jpeg_xs_image_buffer_t in_buf;
    in_buf.stride[0] = enc.source_width;
    in_buf.stride[1] = enc.source_width/2;
    in_buf.stride[2] = enc.source_width/2;
    for (uint8_t i = 0; i < 3; ++i) {
        in_buf.alloc_size[i] = in_buf.stride[i] * enc.source_height * pixel_size;
        in_buf.data_yuv[i] = malloc(in_buf.alloc_size[i]);
        if (!in_buf.data_yuv[i]) {
            return SvtJxsErrorInsufficientResources;
        }
    }

    svt_jpeg_xs_bitstream_buffer_t out_buf;
    uint32_t bitstream_size =
        (uint32_t)(((uint64_t)enc.source_width * enc.source_height * enc.bpp_numerator / enc.bpp_denominator + 7) / +8);
    out_buf.allocation_size = bitstream_size;
    out_buf.used_size = 0;
    out_buf.buffer = malloc(out_buf.allocation_size);
    if (!out_buf.buffer) {
        return SvtJxsErrorInsufficientResources;
    }

    { //loop over multiple frames
        //Get YUV 8bit from file or any other location
        size_t read_size = 0;
        read_size += fread(in_buf.data_yuv[0], 1, in_buf.alloc_size[0], input_file); //luma
        read_size += fread(in_buf.data_yuv[1], 1, in_buf.alloc_size[1], input_file); //chroma cb
        read_size += fread(in_buf.data_yuv[2], 1, in_buf.alloc_size[2], input_file); //chroma cr
        printf("Read file %s size: %lu!\n", input_file_name, (unsigned long)read_size);
        fclose(input_file);

        svt_jpeg_xs_frame_t enc_input;
        enc_input.bitstream = out_buf;
        enc_input.image = in_buf;
        enc_input.user_prv_ctx_ptr = NULL;

        err = svt_jpeg_xs_encoder_send_picture(&enc, &enc_input, 1 /*blocking*/);
        if (err != SvtJxsErrorNone) {
            return err;
        }

        svt_jpeg_xs_frame_t enc_output;
        err = svt_jpeg_xs_encoder_get_packet(&enc, &enc_output, 1 /*blocking*/);
        if (err != SvtJxsErrorNone) {
            return err;
        }
        //Store bitstream to file
        printf("bitstream output: pointer: %p bitstream size: %u\n",
               enc_output.bitstream.buffer,
               enc_output.bitstream.used_size);
        //fwrite(enc_output.bitstream.buffer, 1, enc_output.bitstream.used_size, out_file);
    }

    svt_jpeg_xs_encoder_close(&enc);

    free(out_buf.buffer);
    free(in_buf.data_yuv[0]);
    free(in_buf.data_yuv[1]);
    free(in_buf.data_yuv[2]);
    return 0;
}
```

## Notes

The information in this document was compiled at **v0.10** of the code and may not
reflect the latest status of the design. For the most up-to-date
settings and implementation, it's recommended to visit the specific section of the code.
