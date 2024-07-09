/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include "SvtJpegxsImageBufferTools.h"
#include "Threads/SystemResourceManager.h"

struct svt_jpeg_xs_frame_pool {
    uint8_t use_image_buffer;
    svt_jpeg_xs_image_config_t image_config;
    uint32_t bitstream_size;
    SystemResource_t* pool_image_buffer_resource_ptr;
    uint8_t use_bitstream;
    Fifo_t* pool_image_buffer_fifo_ptr;
    SystemResource_t* pool_bitstream_resource_ptr;
    Fifo_t* pool_bitstream_fifo_ptr;
};

PREFIX_API svt_jpeg_xs_image_buffer_t* svt_jpeg_xs_image_buffer_alloc(svt_jpeg_xs_image_config_t* image_config) {
    svt_jpeg_xs_image_buffer_t* image_buffer;
    SVT_NO_THROW_CALLOC(image_buffer, 1, sizeof(svt_jpeg_xs_image_buffer_t));
    if (!image_buffer) {
        return NULL;
    }

    for (int32_t c = 0; c < MAX_COMPONENTS_NUM; c++) {
        image_buffer->data_yuv[c] = NULL;
    }

    uint32_t pixel_size = image_config->bit_depth <= 8 ? sizeof(uint8_t) : sizeof(uint16_t);
    if (image_config->format == COLOUR_FORMAT_PACKED_YUV444_OR_RGB) {
        image_buffer->stride[0] = image_config->components[0].width * 3;
        image_buffer->alloc_size[0] = image_buffer->stride[0] * image_config->components[0].height * pixel_size;
        assert(image_buffer->alloc_size[0] == image_config->components[0].byte_size);
        SVT_NO_THROW_MALLOC(image_buffer->data_yuv[0], image_buffer->alloc_size[0]);
        if (!image_buffer->data_yuv[0]) {
            svt_jpeg_xs_image_buffer_free(image_buffer);
            return NULL;
        }
    }
    else {
        for (uint32_t c = 0; c < image_config->components_num; c++) {
            image_buffer->stride[c] = image_config->components[c].width;
            image_buffer->alloc_size[c] = image_buffer->stride[c] * image_config->components[c].height * pixel_size;
            assert(image_buffer->alloc_size[c] >= image_config->components[c].byte_size);
            SVT_NO_THROW_MALLOC(image_buffer->data_yuv[c], image_buffer->alloc_size[c]);
            if (!image_buffer->data_yuv[c]) {
                svt_jpeg_xs_image_buffer_free(image_buffer);
                return NULL;
            }
        }
    }

    image_buffer->release_ctx_ptr = NULL;
    return image_buffer;
}

PREFIX_API void svt_jpeg_xs_image_buffer_free(svt_jpeg_xs_image_buffer_t* image_buffer) {
    if (image_buffer) {
        for (int32_t c = 0; c < MAX_COMPONENTS_NUM; c++) {
            if (image_buffer->data_yuv[c]) {
                SVT_FREE(image_buffer->data_yuv[c]);
            }
            image_buffer->data_yuv[c] = NULL;
        }
        SVT_FREE(image_buffer);
    }
}

PREFIX_API svt_jpeg_xs_bitstream_buffer_t* svt_jpeg_xs_bitstream_alloc(uint32_t bitstream_size) {
    svt_jpeg_xs_bitstream_buffer_t* bitstream;
    SVT_NO_THROW_CALLOC(bitstream, 1, sizeof(svt_jpeg_xs_bitstream_buffer_t));
    if (!bitstream) {
        return NULL;
    }
    bitstream->allocation_size = bitstream_size;
    bitstream->used_size = 0;
    bitstream->release_ctx_ptr = NULL;
    bitstream->buffer = NULL;

    SVT_NO_THROW_MALLOC(bitstream->buffer, bitstream->allocation_size);
    if (!bitstream->buffer) {
        svt_jpeg_xs_bitstream_free(bitstream);
        return NULL;
    }
    return bitstream;
}

PREFIX_API void svt_jpeg_xs_bitstream_free(svt_jpeg_xs_bitstream_buffer_t* bitstream) {
    if (bitstream) {
        if (bitstream->buffer) {
            SVT_FREE(bitstream->buffer);
        }
        bitstream->buffer = NULL;
        SVT_FREE(bitstream);
    }
}

static SvtJxsErrorType_t out_image_buffer_create_ctor(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    svt_jpeg_xs_frame_pool_t* frame_pool = (svt_jpeg_xs_frame_pool_t*)object_init_data_ptr;
    assert(frame_pool->use_image_buffer);
    svt_jpeg_xs_image_buffer_t* image_buffer = svt_jpeg_xs_image_buffer_alloc(&frame_pool->image_config);
    if (!image_buffer) {
        return SvtJxsErrorInsufficientResources;
    }

    *object_dbl_ptr = image_buffer;
    return SvtJxsErrorNone;
}

static void out_image_buffer_destroy_ctor(void_ptr p) {
    svt_jpeg_xs_image_buffer_t* image_buffer = (svt_jpeg_xs_image_buffer_t*)p;
    svt_jpeg_xs_image_buffer_free(image_buffer);
}

static SvtJxsErrorType_t out_bitstream_buffer_create_ctor(void_ptr* object_dbl_ptr, void_ptr object_init_data_ptr) {
    svt_jpeg_xs_frame_pool_t* bitstream_buffer_pool = (svt_jpeg_xs_frame_pool_t*)object_init_data_ptr;
    assert(bitstream_buffer_pool->use_bitstream);
    svt_jpeg_xs_bitstream_buffer_t* bitstream_buffer = svt_jpeg_xs_bitstream_alloc(bitstream_buffer_pool->bitstream_size);
    if (!bitstream_buffer) {
        return SvtJxsErrorInsufficientResources;
    }

    *object_dbl_ptr = bitstream_buffer;
    return SvtJxsErrorNone;
}

static void out_bitstream_buffer_destroy_ctor(void_ptr p) {
    svt_jpeg_xs_bitstream_buffer_t* bitstream_buffer = (svt_jpeg_xs_bitstream_buffer_t*)p;
    svt_jpeg_xs_bitstream_free(bitstream_buffer);
}

PREFIX_API svt_jpeg_xs_frame_pool_t* svt_jpeg_xs_frame_pool_alloc(const svt_jpeg_xs_image_config_t* image_config,
                                                                  uint32_t bitstream_size, uint32_t count) {
    svt_jpeg_xs_frame_pool_t* frame_pool = NULL;

    if (count == 0 || (image_config == NULL && bitstream_size == 0)) {
        return NULL;
    }

    SVT_NO_THROW_MALLOC(frame_pool, sizeof(svt_jpeg_xs_frame_pool_t));
    if (frame_pool) {
        svt_jxs_increase_component_count();
        frame_pool->pool_image_buffer_resource_ptr = NULL;
        frame_pool->pool_image_buffer_fifo_ptr = NULL;
        frame_pool->pool_bitstream_resource_ptr = NULL;
        frame_pool->pool_image_buffer_fifo_ptr = NULL;

        if (image_config == NULL) {
            frame_pool->use_image_buffer = 0;
        }
        else {
            frame_pool->use_image_buffer = 1;
            frame_pool->image_config = *image_config; /*Copy structure with configuration.*/

            SVT_NO_THROW_NEW(frame_pool->pool_image_buffer_resource_ptr,
                             svt_jxs_system_resource_ctor,
                             count,
                             1,
                             0,
                             out_image_buffer_create_ctor,
                             frame_pool,
                             out_image_buffer_destroy_ctor);
            if (frame_pool->pool_image_buffer_resource_ptr) {
                frame_pool->pool_image_buffer_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(
                    frame_pool->pool_image_buffer_resource_ptr, 0);
            }
            else {
                svt_jpeg_xs_frame_pool_free(frame_pool);
                frame_pool = NULL;
            }
        }

        if (frame_pool) {
            if (bitstream_size == 0) {
                frame_pool->use_bitstream = 0;
            }
            else {
                frame_pool->use_bitstream = 1;
                frame_pool->bitstream_size = bitstream_size;
                SVT_NO_THROW_NEW(frame_pool->pool_bitstream_resource_ptr,
                                 svt_jxs_system_resource_ctor,
                                 count,
                                 1,
                                 0,
                                 out_bitstream_buffer_create_ctor,
                                 frame_pool,
                                 out_bitstream_buffer_destroy_ctor);
                if (frame_pool->pool_bitstream_resource_ptr) {
                    frame_pool->pool_bitstream_fifo_ptr = svt_jxs_system_resource_get_producer_fifo(
                        frame_pool->pool_bitstream_resource_ptr, 0);
                }
                else {
                    svt_jpeg_xs_frame_pool_free(frame_pool);
                    frame_pool = NULL;
                }
            }
        }
    }
    return frame_pool;
}

PREFIX_API void svt_jpeg_xs_frame_pool_free(svt_jpeg_xs_frame_pool_t* frame_pool) {
    if (frame_pool) {
        if (frame_pool->pool_image_buffer_resource_ptr) {
            SVT_DELETE(frame_pool->pool_image_buffer_resource_ptr);
        }
        if (frame_pool->pool_bitstream_resource_ptr) {
            SVT_DELETE(frame_pool->pool_bitstream_resource_ptr);
        }
        SVT_FREE(frame_pool);
        svt_jxs_decrease_component_count();
    }
}

PREFIX_API SvtJxsErrorType_t svt_jpeg_xs_frame_pool_get(svt_jpeg_xs_frame_pool_t* frame_pool, svt_jpeg_xs_frame_t* frame,
                                                        uint8_t blocking_flag) {
    SvtJxsErrorType_t ret = SvtJxsErrorBadParameter;
    if (frame_pool && frame) {
        ObjectWrapper_t* wrapper_ptr_image_ctx = NULL;
        ObjectWrapper_t* wrapper_ptr_bitstream_ctx = NULL;
        if (blocking_flag) {
            if (frame_pool->use_image_buffer) {
                svt_jxs_get_empty_object(frame_pool->pool_image_buffer_fifo_ptr, &wrapper_ptr_image_ctx);
            }
            if (frame_pool->use_bitstream) {
                svt_jxs_get_empty_object(frame_pool->pool_bitstream_fifo_ptr, &wrapper_ptr_bitstream_ctx);
            }
        }
        else {
            if (frame_pool->use_image_buffer) {
                svt_jxs_get_empty_object_non_blocking(frame_pool->pool_image_buffer_fifo_ptr, &wrapper_ptr_image_ctx);
                if (wrapper_ptr_image_ctx == NULL) {
                    return SvtJxsErrorNoErrorEmptyQueue;
                }
            }
            if (frame_pool->use_bitstream) {
                svt_jxs_get_empty_object_non_blocking(frame_pool->pool_bitstream_fifo_ptr, &wrapper_ptr_bitstream_ctx);
                if (wrapper_ptr_bitstream_ctx == NULL) {
                    if (wrapper_ptr_image_ctx) {
                        svt_jxs_release_object(wrapper_ptr_image_ctx);
                    }
                    return SvtJxsErrorNoErrorEmptyQueue;
                }
            }
        }
        ret = SvtJxsErrorNone;
        if (wrapper_ptr_image_ctx) {
            frame->image = *(svt_jpeg_xs_image_buffer_t*)wrapper_ptr_image_ctx->object_ptr;
            frame->image.release_ctx_ptr = wrapper_ptr_image_ctx;
        }
        if (wrapper_ptr_bitstream_ctx) {
            frame->bitstream = *(svt_jpeg_xs_bitstream_buffer_t*)wrapper_ptr_bitstream_ctx->object_ptr;
            frame->bitstream.release_ctx_ptr = wrapper_ptr_bitstream_ctx;
            frame->bitstream.used_size = 0;
        }
    }
    return ret;
}

PREFIX_API void svt_jpeg_xs_frame_pool_release(svt_jpeg_xs_frame_pool_t* frame_pool, svt_jpeg_xs_frame_t* frame) {
    if (frame_pool && frame) {
        if (frame_pool->use_image_buffer && frame->image.release_ctx_ptr && frame->image.ready_to_release) {
            svt_jxs_release_object(frame->image.release_ctx_ptr);
            frame->image.release_ctx_ptr = NULL;
        }
        if (frame_pool->use_bitstream && frame->bitstream.release_ctx_ptr && frame->bitstream.ready_to_release) {
            svt_jxs_release_object(frame->bitstream.release_ctx_ptr);
            frame->bitstream.release_ctx_ptr = NULL;
        }
    }
}
