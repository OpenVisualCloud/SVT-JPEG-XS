/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_JPEG_XS_API_DEC_H_
#define _SVT_JPEG_XS_API_DEC_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include <stdint.h>
#include <stddef.h>
#include "SvtJpegxs.h"

typedef struct svt_jpeg_xs_decoder_api {
    /* CPU FLAGS to limit assembly instruction set used by decoder, 0 = C only, CPU_FLAGS_ALL = all.
     * svt_jpeg_xs_decoder_init() masks it in this struct with the flags detected on the CPU. */
    uint64_t use_cpu_flags;
    uint32_t verbose; //VerboseMessages
    uint32_t threads_num; /* Number of threads decoder can create, 0 = minimum number of threads. */
    /*
    * Mode how bitstream is passed and processed in decoder
    * 0 - frame based - In this mode, a single buffer/packet that contains the entire JPEG XS picture segment is passed to the decoder and used
    *       throughout the entire decoder pipeline, making it more memory efficient at the cost of higher latency.
    *       This mode requires the use of the svt_jpeg_xs_decoder_send_frame() API function.
    * 1 - packet based -In this mode, multiple buffers/packets per frame are used. The bitstream is immediately copied into an internal decoder memory location,
    *       allowing for lower latency at the cost of higher memory usage and bandwidth.
    *       This mode requires the use of the svt_jpeg_xs_decoder_send_packet() API function.
    */
    uint8_t packetization_mode;

    /* Decode at full, 1/2 or 1/4 width and height. Half needs every component's vertical and horizontal
     * decomposition >= 1, quarter >= 2, otherwise svt_jpeg_xs_decoder_init() returns BadParameter:
     * quarter never works for 4:2:0, and neither works with Sd > 0 (components without decomposition).
     * svt_jpeg_xs_decoder_get_single_frame_size_with_proxy() does not check this. */
    proxy_mode_t proxy_mode;

    /* Callback: Call when available place in queue to send data.
     * Call when finish init decoder.
     * Call every time when the next element from the input queue is taken.
     * Never called with packetization_mode = 1.
     * Only one callback will be triggered at a time. Always from that same thread.
     * No synchronization required.
     * decoder - Pointer passed on initialization.*/
    void (*callback_send_data_available)(struct svt_jpeg_xs_decoder_api* decoder, void* context);
    void* callback_send_data_available_context;

    /* Callback: Call when new frame is ready to get.
     * Only one callback will be triggered at a time.
     * No synchronization required.
     * decoder - Pointer passed on initialization.*/
    void (*callback_get_data_available)(struct svt_jpeg_xs_decoder_api* decoder, void* context);
    void* callback_get_data_available_context;

    void* private_ptr; /*Private decoder pointer, do not touch!!! */

    /* Non-standard, host-side buffer layout convention only - not signalled in the bitstream.
     * Both encoder and decoder must be configured consistently by the caller.
     * 0 = LSB-aligned (default, current behavior): 10/12-bit samples occupy the low bits of each
     *     16-bit output word (value `v`), high bits are set to zero.
     * 1 = MSB-aligned: 10/12-bit samples occupy the high bits of each 16-bit output word
     *     (value `v << (16 - output_bit_depth)`), low bits are set to zero.
     *     Not applied when the colour transform is done on the whole frame (e.g. Star-Tetrix, Cpih=3);
     *     such output is always LSB-aligned.
     * Optional, default 0 */
    uint8_t output_bit_depth_msb_aligned;

    /* This padding is used to avoid changing the size of the public configuration struct
     * when new parameters are added in the future please follow these steps:
     * 1. Insert the new parameter as a member of this structure before the padding array.
     * 2. Decrease the size of the padding array by the size of the new parameter to keep the struct size unchanged.
     */
    uint8_t padding[63];
} svt_jpeg_xs_decoder_api_t;

/* Init decoder and fill out_image_config from the first frame.
 * Only the headers are parsed; the first frame must still be sent with send_frame()/send_packet().
 * out_image_config may be NULL, but then the check for an unsupported output format is skipped.
 * Return:
 *  SvtJxsErrorNone - on success,
 *  SvtJxsErrorInvalidApiVersion - unsupported API version,
 *  SvtJxsErrorDecoderInvalidPointer - dec_api or bitstream_buf NULL, or codestream_size 0,
 *  SvtJxsErrorBadParameter - invalid dec_api field, proxy_mode not supported by the stream, or
 *                            packetization_mode = 1 for a variable bitrate (Lcod=0) or lossless (Fq=0) stream,
 *  or a bitstream parse error (e.g. SvtJxsErrorDecoderInvalidBitstream, SvtJxsErrorDecoderBitstreamTooShort).
 * out_image_config->format for 4:4:4 streams:
 *  - Cpih=1 (colour transform): COLOUR_FORMAT_PLANAR_RGB (3 components) or COLOUR_FORMAT_PLANAR_RGBA
 *    (4 components), output components in R, G, B(, A) order.
 *  - Cpih=0: COLOUR_FORMAT_PLANAR_YUV444 or COLOUR_FORMAT_PLANAR_4_COMPONENTS. The stream does not
 *    signal whether it holds YUV or RGB, so this must come from the container or the user.
 *  - Cpih=3 (Star-Tetrix, 4 components): COLOUR_FORMAT_PLANAR_4_COMPONENTS. */
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_init(uint64_t version_api_major, uint64_t version_api_minor,
                                                      svt_jpeg_xs_decoder_api_t* dec_api, const uint8_t* bitstream_buf,
                                                      size_t codestream_size, svt_jpeg_xs_image_config_t* out_image_config);
PREFIX_API void svt_jpeg_xs_decoder_close(svt_jpeg_xs_decoder_api_t* dec_api);

/* Get single frame size from bitstream
  * Parameters:
  * @ *bitstream_buf - pointer to bitstream
  * @ bitstream_buf_size - size of bitstream in bytes
  * @ *out_image_config - optional parameter, can be null, pointer to basic frame information to be filled
  * @ *frame_size - output parameter that will be filled on success
  * @ fast_search - If enabled, then search only bitstream headers for size without further validation.
  *                 If disabled or variable bitrate coding is used, then function parse whole stream.
  * Return non-fatal:
  *  SvtJxsErrorNone on success, set frame_size with proper value. Additionally if out_image_config is not null, then
  *   out_image_config is filled with basic image information,
  * Return fatal:
  * SvtJxsErrorDecoderInvalidPointer - when bitstream_buf or frame_size are null,
  * SvtJxsErrorDecoderBitstreamTooShort - when bitstream is to short, please call again with more bytes in buffer.
  * SvtJxsErrorDecoderInvalidBitstream   - when bitstream is invalid and can not be analyzed
  * svt_jpeg_xs_decoder_get_single_frame_size() stays for compatibility with 0.9 version of API
  * svt_jpeg_xs_decoder_get_single_frame_size_with_proxy() shall be used instead, as it allows to specify proxy mode.
  * out_image_config->format is the same as from svt_jpeg_xs_decoder_init().
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_get_single_frame_size(const uint8_t* bitstream_buf, size_t bitstream_buf_size,
                                                                       svt_jpeg_xs_image_config_t* out_image_config,
                                                                       uint32_t* frame_size, uint32_t fast_search);
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(const uint8_t* bitstream_buf,
                                                                                  size_t bitstream_buf_size,
                                                                                  svt_jpeg_xs_image_config_t* out_image_config,
                                                                                  uint32_t* frame_size, uint32_t fast_search,
                                                                                  proxy_mode_t proxy_mode);

/*Start decode bitstream
  * Parameters:
  * @ *dec_api - Decoder handle.
  * @ *dec_input - Structure with frame info: pointers on yuv and bitstream, frame context. Structure will be copied internally and received by svt_jpeg_xs_decoder_get_frame().
  *                 The bitstream and image buffers must stay valid until returned by svt_jpeg_xs_decoder_get_frame().
  * @ blocking_flag - If set to 1, then function is blocked until frame is sent to decoder, otherwise "frame not ready try again later" error code can be returned.
  * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  *  SvtJxsErrorNoErrorEmptyQueue - (warning), Frame not ready, try again later
  * Return fatal:
  *  SvtJxsErrorDecoderInvalidPointer - when dec_input or decoder handle is null or decoder is not initialized
  *  SvtJxsErrorBadParameter - when image.data_yuv[c] is null or image.alloc_size[c] is too small for stride and height
  *  SvtJxsErrorUndefined - when decoder is initialized with packetization_mode = 1
  * or any other from SvtJxsErrorType_t enum
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_frame(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_input,
                                                            uint8_t blocking_flag);

/*Start decode bitstream, only with packetization_mode = 1. Always blocking.
  * Parameters:
  * @ *dec_api - Decoder handle.
  * @ *dec_input - Structure with frame info: pointers on yuv and bitstream, frame context. Structure will be copied internally and received by svt_jpeg_xs_decoder_get_frame().
  *                 bitstream.used_size bytes are copied internally. The image buffer and user_prv_ctx_ptr are taken
  *                 only from the first packet of a frame; the image buffer size is not checked.
  *                 bitstream.ready_to_release and image.ready_to_release are written into this structure.
  * @ *bytes_used - Bytes consumed: min(bytes still missing in current frame, bitstream.used_size).
  *                 The caller must send the remaining bytes again (they belong to the next frame).
  * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  * SvtJxsErrorDecoderBitstreamTooShort - data was consumed but not enough to decode the entire image
  * Return fatal:
  *  SvtJxsErrorDecoderInvalidPointer - when dec_input, bytes_used or decoder handle is null or decoder is not initialized
  *  SvtJxsErrorUndefined - when decoder is initialized with packetization_mode = 0 (bytes_used is set to 0)
  * or an internal queue error.
  * Header and slice errors are not returned here; they are reported for the frame by svt_jpeg_xs_decoder_get_frame().
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_packet(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_input,
                                                             uint32_t* bytes_used);

/*Get frame
  * Parameters:
  * @ *dec_api - Decoder handle.
  * @ *dec_output - Fill output structure with decoder results, and context user pointer.
  * @ blocking_flag - If set to 1, then function is blocked until frame is received from decoder, otherwise "frame not ready try again later" error code can be returned.
  * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  *  SvtJxsErrorNoErrorEmptyQueue - (warning), Frame not ready, try again later, and dec_output.user_prv_ctx_ptr set to NULL
  *  SvtJxsDecoderEndOfCodestream - indicate that there is no more frames processed in pipeline, can be returned only if svt_jpeg_xs_decoder_send_eoc() is called
  * Return fatal:
  *  SvtJxsErrorDecoderInvalidBitstream - Invalid bitstream, can not decode
  *  SvtJxsErrorDecoderConfigChange - Invalid decoder parameters, different resolution or output format. Init decoder again to decode frame,
  * or any other from SvtJxsErrorType_t enum
  * On frame error dec_output is still filled with the frame buffers (ready_to_release = 1), which must be released.
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_get_frame(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_output,
                                                           uint8_t blocking_flag);

/* Optional API function to tell decoder that there in no more frames to process aka. End Of Codestream
*   If used, then svt_jpeg_xs_decoder_get_frame() will return SvtJxsDecoderEndOfCodestream
  * Parameters:
  * @ *dec_api - Decoder handle.
  * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  * Return fatal:
  *  SvtJxsErrorDecoderInvalidPointer - when decoder handle in null or is not initialized
  * or any other from SvtJxsErrorType_t enum
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_eoc(svt_jpeg_xs_decoder_api_t* dec_api);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif /*_SVT_JPEG_XS_API_DEC_H_*/
