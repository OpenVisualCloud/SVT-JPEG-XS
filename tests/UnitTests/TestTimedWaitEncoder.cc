/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* The encoder must not use the timed wait (see SvtThreads.c). Kept apart from TestTimedWait.cc,
 * since DecHandle.h and EncHandle.h cannot be included together. */

#include "gtest/gtest.h"
#include "SvtJpegxsEnc.h"
#include "Threads/SystemResourceManager.h"
#include "EncHandle.h"

namespace {

/* Checks that no fifo of a SystemResource is marked for the timed wait. Resources that the
 * configuration does not use are NULL. */
void expect_resource_not_timed_wait(const SystemResource_t *resource) {
    if (resource == nullptr) {
        return;
    }
    const MuxingQueue_t *queues[2] = {resource->empty_queue, resource->full_queue};
    for (const MuxingQueue_t *queue : queues) {
        if (queue == nullptr) {
            continue; // e.g. a pool has no full queue
        }
        for (uint32_t i = 0; i < queue->process_total_count; i++) {
            EXPECT_EQ(queue->process_fifo_ptr_array[i]->timed_wait, 0) << "fifo " << i;
        }
    }
}

} // namespace

TEST(TimedWaitScope, EncoderDoesNotMarkItsObjects) {
    svt_jpeg_xs_encoder_api_t encoder;
    ASSERT_EQ(svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder),
              SvtJxsErrorNone);
    encoder.verbose = VERBOSE_NONE;
    encoder.source_width = 64;
    encoder.source_height = 64;
    encoder.input_bit_depth = 8;
    encoder.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    encoder.bpp_numerator = 3;
    encoder.bpp_denominator = 1;
    encoder.threads_num = 2;
    ASSERT_EQ(svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &encoder), SvtJxsErrorNone);
    const svt_jpeg_xs_encoder_api_prv_t *prv = (const svt_jpeg_xs_encoder_api_prv_t *)encoder.private_ptr;
    ASSERT_NE(prv, nullptr);
    expect_resource_not_timed_wait(prv->input_image_resource_ptr);
    expect_resource_not_timed_wait(prv->dwt_input_resource_ptr);
    expect_resource_not_timed_wait(prv->pack_input_resource_ptr);
    expect_resource_not_timed_wait(prv->pack_output_resource_ptr);
    expect_resource_not_timed_wait(prv->output_queue_resource_ptr);
    expect_resource_not_timed_wait(prv->picture_control_set_pool_ptr);
    EXPECT_EQ(prv->sync_output_ringbuffer_left.timed_wait, 0);
    svt_jpeg_xs_encoder_close(&encoder);
}
