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
    uint64_t use_cpu_flags;
    uint32_t verbose; //VerboseMessages
    uint32_t threads_num;
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

    /* Callback: Call when available place in queue to send data.
     * Call when finish init decoder.
     * Call every time when the next element from the input queue is taken.
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

    void* private_ptr;
} svt_jpeg_xs_decoder_api_t;

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
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_get_single_frame_size(const uint8_t* bitstream_buf, size_t bitstream_buf_size,
                                                                       svt_jpeg_xs_image_config_t* out_image_config,
                                                                       uint32_t* frame_size, uint32_t fast_search);

/*Start decode bitstream
  * Parameters:
  * @ *dec_api - Decoder handle.
  * @ *dec_input - Structure with frame info: pointers on yuv and bitstream, frame context. Structure will be copied internally and received by svt_jpeg_xs_decoder_get_frame().
  * @ blocking_flag - If set to 1, then function is blocked until frame is sent to decoder, otherwise "frame not ready try again later" error code can be returned.
  * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  *  SvtJxsErrorNoErrorEmptyQueue - (warning), Frame not ready, try again later
  * Return fatal:
  *  SvtJxsErrorDecoderInvalidPointer - when bitstream is null or decoder handle in null or is not initialized
  * or any other from SvtJxsErrorType_t enum
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_decoder_send_frame(svt_jpeg_xs_decoder_api_t* dec_api, svt_jpeg_xs_frame_t* dec_input,
                                                            uint8_t blocking_flag);

/*Start decode bitstream
  * Parameters:
  * @ *dec_api - Decoder handle.
  * @ *dec_input - Structure with frame info: pointers on yuv and bitstream, frame context. Structure will be copied internally and received by svt_jpeg_xs_decoder_get_frame().
  * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  * SvtJxsErrorDecoderBitstreamTooShort - data was consumed but not enough to decode the entire image
  * Return fatal:
  *  SvtJxsErrorDecoderInvalidPointer - when bitstream is null or decoder handle in null or is not initialized
  *  SvtJxsErrorDecoderInvalidBitstream - when quick stream parsing indicates a corrupted bitstream
  * or any other from SvtJxsErrorType_t enum
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
