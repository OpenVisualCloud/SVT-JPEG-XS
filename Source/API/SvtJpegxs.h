/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_JPEG_XS_API_H_
#define _SVT_JPEG_XS_API_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdint.h>

/* API Version */
#define SVT_JPEGXS_API_VER_MAJOR (0)
#define SVT_JPEGXS_API_VER_MINOR (9)

/*Define PREFIX_API*/
#ifdef DEF_DLL
#ifdef DEF_BUILDING_SHARED_LIBS
#if defined(_WIN32)
#define PREFIX_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define PREFIX_API __attribute__((visibility("default")))
#else /*other compiler*/
#define PREFIX_API
#endif
#else /*DEF_BUILDING_SHARED_LIBS*/
#if defined(_WIN32)
#define PREFIX_API __declspec(dllimport)
#else /*other compiler*/
#define PREFIX_API
#endif
#endif /*DEF_BUILDING_SHARED_LIBS*/
#else  /*DEF_DLL*/
#define PREFIX_API
#endif /*DEF_DLL*/

/********************************
 * Defines
 ********************************/

#define MAX_COMPONENTS_NUM (4)

/* jpegxs Chroma Format */
typedef enum ColourFormat {
    COLOUR_FORMAT_INVALID = 0,
    COLOUR_FORMAT_PLANAR_YUV400 = 1,
    COLOUR_FORMAT_PLANAR_YUV420 = 2,        //planar: yuv420p, yuv420p10le etc.
    COLOUR_FORMAT_PLANAR_YUV422 = 3,        //planar: yuv422p, yuv422p10le etc.
    COLOUR_FORMAT_PLANAR_YUV444_OR_RGB = 4, //planar: yuv444p, rgbp, gbrp, yuv444p10le, gbrp10le etc.
    COLOUR_FORMAT_PLANAR_4_COMPONENTS = 5,  // planar 4 components
    COLOUR_FORMAT_GRAY = 6,
    COLOUR_FORMAT_PLANAR_MAX,

    COLOUR_FORMAT_PACKED_MIN = 20,
    COLOUR_FORMAT_PACKED_YUV444_OR_RGB, //packed rgb/bgr, 8:8:8, 24bpp, RGBRGB... / BGRBGR...
    COLOUR_FORMAT_PACKED_MAX
} ColourFormat_t;

enum VerboseMessages {
    VERBOSE_NONE = 0,
    VERBOSE_ERRORS = 1,
    VERBOSE_SYSTEM_INFO = 2,
    VERBOSE_SYSTEM_INFO_ALL = 3,
    VERBOSE_WARNINGS = 4,
    VERBOSE_INFO_MULTITHREADING = 5,
    VERBOSE_INFO_FULL = 6
};

typedef struct svt_jpeg_xs_image_config {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    ColourFormat_t format;
    uint8_t components_num;
    struct {
        uint32_t width;
        uint32_t height;
        uint32_t byte_size;
    } components[MAX_COMPONENTS_NUM];
} svt_jpeg_xs_image_config_t;

typedef struct svt_jpeg_xs_image_buffer {
    void *data_yuv[MAX_COMPONENTS_NUM];      /*Allocated Data buffer for components*/
    uint32_t stride[MAX_COMPONENTS_NUM];     /*Greater than or equal to width. Not supported in decoder yet.*/
    uint32_t alloc_size[MAX_COMPONENTS_NUM]; /*Used by Encoder and Decoder to validate that the buffer size is enough.*/
    void *release_ctx_ptr;                   /*Used by pool allocator, for different allocation free to use.*/
    /*Set by encoder/decoder,
    1 indicates that buffer can be released by user
    */
    uint8_t ready_to_release;
} svt_jpeg_xs_image_buffer_t;

typedef struct svt_jpeg_xs_bitstream_buffer {
    /* Bitstream buffer,
    this pointer can be changed during encoding process,
    for deallocation please use *release_ctx_ptr
    */
    uint8_t *buffer;
    uint32_t allocation_size; /* Bitstream buffer size */
    uint32_t used_size;       /* Bitstream buffer used size */
    /*
    Encoder/decoder does not modify this pointer,
    Used by pool allocator, for different allocation free to use.
    */
    void *release_ctx_ptr;
    /*
    Set by encoder/decoder,
    1 indicates that buffer can be released by user
    */
    uint8_t ready_to_release;
    /*
    Set by encoder, unused in decoder,
    1 - indicate that this is last packetization unit within frame
    */
    uint8_t last_packet_in_frame;
} svt_jpeg_xs_bitstream_buffer_t;

typedef struct svt_jpeg_xs_frame {
    /*Common structure to keep input and output for encoder and decoder.
     *Encoder: image - is input and bitstream is output.
     *Decoder: bitstream - is input and image is output.*/
    svt_jpeg_xs_image_buffer_t image;         /* YUV buffer */
    svt_jpeg_xs_bitstream_buffer_t bitstream; /* Bitstream buffer */
    void *user_prv_ctx_ptr;                   /* Input user private context pointer, receive in output */
} svt_jpeg_xs_frame_t;

typedef enum SvtJxsErrorType {
    SvtJxsErrorNone = 0,
    SvtJxsErrorInvalidApiVersion = (int32_t)0x40001001,
    SvtJxsErrorCorrupt_Frame = (int32_t)0x4000100C,
    SvtJxsErrorInsufficientResources = (int32_t)0x80001000,
    SvtJxsErrorUndefined = (int32_t)0x80001001,
    SvtJxsErrorInvalidComponent = (int32_t)0x80001004,
    SvtJxsErrorBadParameter = (int32_t)0x80001005,
    SvtJxsErrorDestroyThreadFailed = (int32_t)0x80002012,
    SvtJxsErrorSemaphoreUnresponsive = (int32_t)0x80002021,
    SvtJxsErrorDestroySemaphoreFailed = (int32_t)0x80002022,
    SvtJxsErrorCreateMutexFailed = (int32_t)0x80002030,
    SvtJxsErrorMutexUnresponsive = (int32_t)0x80002031,
    SvtJxsErrorDestroyMutexFailed = (int32_t)0x80002032,
    SvtJxsErrorNoErrorEmptyQueue = (int32_t)0x80002033,
    SvtJxsErrorNoErrorFifoShutdown = (int32_t)0x80002034,

    //Encoder
    SvtJxsErrorEncodeFrameError = (int32_t)0x80002035,

    //Decoder
    SvtJxsErrorDecoderInvalidPointer = (int32_t)0x80003000,
    SvtJxsErrorDecoderInvalidBitstream = (int32_t)0x80003001,
    SvtJxsErrorDecoderInternal = (int32_t)0x80003002,
    SvtJxsErrorDecoderBitstreamTooShort = (int32_t)0x80003003,
    SvtJxsErrorDecoderConfigChange = (int32_t)0x80003004,
    SvtJxsDecoderEndOfCodestream = (int32_t)0x80003005,

    SvtJxsErrorMax = 0x7FFFFFFF
} SvtJxsErrorType_t;

/**
CPU FLAGS
*/
typedef uint64_t CPU_FLAGS;
#define CPU_FLAGS_C        (0)
#define CPU_FLAGS_MMX      (1 << 0)
#define CPU_FLAGS_SSE      (1 << 1)
#define CPU_FLAGS_SSE2     (1 << 2)
#define CPU_FLAGS_SSE3     (1 << 3)
#define CPU_FLAGS_SSSE3    (1 << 4)
#define CPU_FLAGS_SSE4_1   (1 << 5)
#define CPU_FLAGS_SSE4_2   (1 << 6)
#define CPU_FLAGS_AVX      (1 << 7)
#define CPU_FLAGS_AVX2     (1 << 8)
#define CPU_FLAGS_AVX512F  (1 << 9)
#define CPU_FLAGS_AVX512CD (1 << 10)
#define CPU_FLAGS_AVX512DQ (1 << 11)
#define CPU_FLAGS_AVX512ER (1 << 12)
#define CPU_FLAGS_AVX512PF (1 << 13)
#define CPU_FLAGS_AVX512BW (1 << 14)
#define CPU_FLAGS_AVX512VL (1 << 15)
#define CPU_FLAGS_ALL      ((CPU_FLAGS_AVX512VL << 1) - 1)
#define CPU_FLAGS_INVALID  (1ULL << (sizeof(CPU_FLAGS) * 8ULL - 1ULL))

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /*_SVT_JPEG_XS_API_H_*/
