/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#ifndef _SVT_JPEG_XS_API_IMAGE_BUFFER_TOOLS_H_
#define _SVT_JPEG_XS_API_IMAGE_BUFFER_TOOLS_H_
#include "SvtJpegxs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*Allocate simple image YUV buffer
 * Parameters:
 * @ image_config - Image config how much memory allocate.
 * Return:
 *  Pointer on allocated structure with buffers, or NULL - when allocation fail
 **/
PREFIX_API svt_jpeg_xs_image_buffer_t* svt_jpeg_xs_image_buffer_alloc(svt_jpeg_xs_image_config_t* image_config);

/*Free simple image YUV buffer
 * Parameters:
 * @ image_buffer - Pointer on allocated image buffer, have to be that same pointer as allocated by svt_jpeg_xs_image_buffer_alloc()
 **/
PREFIX_API void svt_jpeg_xs_image_buffer_free(svt_jpeg_xs_image_buffer_t* image_buffer);

/*Allocate simple bitstream buffer
 * Parameters:
 * @ bitstream_size - Bitstream buffer size
 * Return:
 *  Pointer on allocated structure with buffers, or NULL - when allocation fail
 **/
PREFIX_API svt_jpeg_xs_bitstream_buffer_t* svt_jpeg_xs_bitstream_alloc(uint32_t bitstream_size);

/*Free simple bitstream buffer
 * Parameters:
 * @ bitstream - Pointer on allocated bitstream, have to be that same pointer as allocated by svt_jpeg_xs_bitstream_alloc()
 **/
PREFIX_API void svt_jpeg_xs_bitstream_free(svt_jpeg_xs_bitstream_buffer_t* bitstream);

/*
 * Management pools of image YUV buffers and bitstream buffers.
 */
typedef struct svt_jpeg_xs_frame_pool svt_jpeg_xs_frame_pool_t;

/*Allocate pool of image YUV and bitstream buffers
 * Parameters:
 * @ image_config - Image config how much memory allocate. If NULL, then image buffer will not be allocated for frames.
 * @ bitstream_size - Bitstream buffers size. If 0, then bitstream buffer will not be allocated for frames.
 * @ count - number of buffers to allocate
 * Return:
 *  Pointer to allocated structure with buffers, or NULL - when allocation fail
 **/
PREFIX_API svt_jpeg_xs_frame_pool_t* svt_jpeg_xs_frame_pool_alloc(const svt_jpeg_xs_image_config_t* image_config,
                                                                  uint32_t bitstream_size, uint32_t count);

/*Free pool of image YUV and bitstream buffers
 * Parameters:
 * @ image_buffer_pool - Pointer on allocated pool of frames, have to be that same pointer as allocated by svt_jpeg_xs_frame_pool_alloc()
 **/
PREFIX_API void svt_jpeg_xs_frame_pool_free(svt_jpeg_xs_frame_pool_t* frame_pool);

/* Get frame buffers from pool. Pointer svt_jpeg_xs_frame_pool_t::svt_jpeg_xs_image_buffer_t::release_ctx_ptr
                                        svt_jpeg_xs_frame_pool_t::svt_jpeg_xs_bitstream_buffer_t::release_ctx_ptr
                                        can not be override because it will be used to release.
  * Parameters:
  * @ frame_pool - Frame pool
  * @ frame - Frame structure to fill by image buffer, and bitstream buffer from pool.
  *           If pool not allocate image buffer (when init pool with image_config == NULL) then svt_jpeg_xs_frame_t::image will not be modified.
  *           If pool not allocate bitstream buffer (when init pool with bitstream_size == 0) then svt_jpeg_xs_frame_t::bitstream will not be modified.
  *           To free frame buffers need return with that same svt_jpeg_xs_frame_pool_t::*::release_ctx_ptr pointers.
  * @ blocking_flag - If set to 1, then function is blocked until buffer will be available (different thread release used buffer), otherwise "buffer not ready try again later" error code can be returned.
 * Return non-fatal:
  *  SvtJxsErrorNone - on success,
  *  SvtJxsErrorNoErrorEmptyQueue - (warning), Pool is empty, try again later after some release frame by svt_jpeg_xs_image_buffer_pool_release() happen only when blocking_flag == 0
  * Return fatal:
  *  SvtJxsErrorBadParameter - Invalid NULL parameters
  **/
PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_frame_pool_get(svt_jpeg_xs_frame_pool_t* frame_pool, svt_jpeg_xs_frame_t* frame,
                                                        uint8_t blocking_flag);

/* Return image YUV buffer to pool. Pointer svt_jpeg_xs_image_buffer_pool_t::release_ctx_ptr can not be overwritten because it will be used to release
  * Parameters:
  * @ frame_pool - Frame pool
  * @ image_buffer - Image buffer to release
  **/
PREFIX_API void svt_jpeg_xs_frame_pool_release(svt_jpeg_xs_frame_pool_t* frame_pool, svt_jpeg_xs_frame_t* frame);

#ifdef __cplusplus
}
#endif

#endif /* _SVT_JPEG_XS_API_IMAGE_BUFFER_TOOLS_H_ */
