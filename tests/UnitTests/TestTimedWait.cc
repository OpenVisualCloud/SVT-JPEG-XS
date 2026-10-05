/*
* Copyright(c) 2026 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/* Tests for the timed wait before blocking (see SvtThreads.c): the semaphore and condition variable
 * waits, the parsing of the SVT_JXS_TW_* environment variables, and which objects the decoder and
 * the encoder mark for the timed wait. The encoder side is in TestTimedWaitEncoder.cc, since
 * DecHandle.h and EncHandle.h cannot be included together. */

#include <chrono>
#include <thread>
#include "gtest/gtest.h"
#include "SvtJpegxsDec.h"
#include "Threads/SvtThreads.h"
#include "Threads/SystemResourceManager.h"
#include "DecHandle.h"
#include "SampleFramesData.h"
#if defined(__linux__)
#include <semaphore.h>
#if __has_include(<valgrind/valgrind.h>)
#include <valgrind/valgrind.h>
#endif
#ifndef RUNNING_ON_VALGRIND
#define RUNNING_ON_VALGRIND 0
#endif
#endif

namespace {

using Clock = std::chrono::steady_clock;

/* Longer than the default 1000us budget, so the waiter is already in the blocking wait. */
constexpr std::chrono::microseconds after_budget_delay(20000);
/* Inside the default budget, so the waiter is still in the timed slices. */
constexpr std::chrono::microseconds within_budget_delay(200);

/* With after_deadline, waits until the waiter has set its timed-wait deadline, so the delay is
 * counted from there and the post or set always comes after the budget ran out. Otherwise a waiter
 * thread that is descheduled for longer than the delay (e.g. on a loaded machine under valgrind)
 * finds the semaphore already posted or the value already set. Only for waits that do the timed
 * wait, or it never returns. */
void wait_for_deadline(bool after_deadline) {
#if defined(__linux__)
    while (after_deadline && svt_jxs_tw_get_waits_entered_for_testing() == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
#else
    (void)after_deadline;
#endif
}

void post_semaphore_after(Handle_t semaphore, std::chrono::microseconds delay, bool after_deadline) {
    wait_for_deadline(after_deadline);
    std::this_thread::sleep_for(delay);
    EXPECT_EQ(svt_jxs_post_semaphore(semaphore), SvtJxsErrorNone);
}

void wait_semaphore_posted_after(std::chrono::microseconds delay, bool after_deadline) {
    Handle_t semaphore = svt_jxs_create_semaphore(0, 1);
    ASSERT_NE(semaphore, nullptr);
    const auto start = Clock::now();
    std::thread poster(post_semaphore_after, semaphore, delay, after_deadline);
    EXPECT_EQ(svt_jxs_block_on_semaphore_timed_wait(semaphore), SvtJxsErrorNone);
    const auto elapsed = Clock::now() - start;
    poster.join();
    EXPECT_GE(elapsed, delay);
#if defined(__linux__)
    // The wait took exactly one count.
    int value = -1;
    EXPECT_EQ(sem_getvalue((sem_t *)semaphore, &value), 0);
    EXPECT_EQ(value, 0);
#endif
    EXPECT_EQ(svt_jxs_destroy_semaphore(semaphore), SvtJxsErrorNone);
}

void set_cond_var_after(CondVar *cond_var, int32_t value, std::chrono::microseconds delay, bool after_deadline) {
    wait_for_deadline(after_deadline);
    std::this_thread::sleep_for(delay);
    EXPECT_EQ(svt_jxs_set_cond_var(cond_var, value), SvtJxsErrorNone);
}

void wait_cond_var_set_after(uint8_t timed_wait, std::chrono::microseconds delay, bool after_deadline) {
    CondVar cond_var;
    ASSERT_EQ(svt_jxs_create_cond_var(&cond_var), SvtJxsErrorNone);
    cond_var.timed_wait = timed_wait;
    const auto start = Clock::now();
    std::thread setter(set_cond_var_after, &cond_var, 1, delay, after_deadline);
    EXPECT_EQ(svt_jxs_wait_cond_var(&cond_var, 0), SvtJxsErrorNone);
    const auto elapsed = Clock::now() - start;
    setter.join();
    EXPECT_GE(elapsed, delay);
    EXPECT_EQ(cond_var.val, 1);
    EXPECT_EQ(svt_jxs_free_cond_var(&cond_var), SvtJxsErrorNone);
}

#if defined(__linux__)
/* Forces a timed-wait config for one test, so the result does not depend on the SVT_JXS_TW_*
 * environment, and restores the config from the environment afterwards. Also clears the stats of
 * the calling thread. */
class TimedWaitConfigScope {
  public:
    TimedWaitConfigScope(int64_t budget_us, int64_t slice_us) {
        SvtJxsTimedWaitConfig config;
        svt_jxs_tw_parse_config(NULL, NULL, NULL, &config);
        config.budget_ns = budget_us * 1000;
        config.slice_ns  = slice_us * 1000;
        svt_jxs_tw_set_config_for_testing(&config);
        svt_jxs_tw_reset_stats_for_testing();
    }
    ~TimedWaitConfigScope() {
        SvtJxsTimedWaitConfig config;
        svt_jxs_tw_parse_config(getenv("SVT_JXS_TW_US"), getenv("SVT_JXS_TW_SLICE_US"), getenv("SVT_JXS_TW_SLACK_NS"), &config);
        svt_jxs_tw_set_config_for_testing(&config);
    }
};

/* Checks what the timed waits of the calling thread did since the TimedWaitConfigScope. The
 * minimum of slice timeouts is not checked under valgrind: it can deschedule the waiter at any point
 * for longer than the delay, e.g. inside a slice, which then sees the wake-up instead of timing out. */
void expect_tw_stats(uint32_t min_slice_timeouts, uint32_t max_slice_timeouts, uint32_t fallbacks) {
    SvtJxsTimedWaitStats stats;
    svt_jxs_tw_get_stats_for_testing(&stats);
    if (!RUNNING_ON_VALGRIND) {
        EXPECT_GE(stats.slice_timeouts, min_slice_timeouts);
    }
    EXPECT_LE(stats.slice_timeouts, max_slice_timeouts);
    EXPECT_EQ(stats.fallbacks, fallbacks);
}

constexpr uint32_t any_count = UINT32_MAX;
#endif

/* Checks the timed_wait flag on every fifo of a SystemResource. */
void expect_resource_timed_wait(const SystemResource_t *resource, uint8_t expected) {
    ASSERT_NE(resource, nullptr);
    const MuxingQueue_t *queues[2] = {resource->empty_queue, resource->full_queue};
    uint32_t fifos = 0;
    for (const MuxingQueue_t *queue : queues) {
        if (queue == nullptr) {
            continue; // e.g. a pool has no full queue
        }
        fifos += queue->process_total_count;
        for (uint32_t i = 0; i < queue->process_total_count; i++) {
            EXPECT_EQ(queue->process_fifo_ptr_array[i]->timed_wait, expected) << "fifo " << i;
        }
    }
    EXPECT_GT(fifos, 0u);
}

} // namespace

TEST(TimedWaitSemaphore, AlreadyAvailable) {
#if defined(__linux__)
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    Handle_t semaphore = svt_jxs_create_semaphore(2, 2);
    ASSERT_NE(semaphore, nullptr);
    EXPECT_EQ(svt_jxs_block_on_semaphore_timed_wait(semaphore), SvtJxsErrorNone);
    EXPECT_EQ(svt_jxs_block_on_semaphore_timed_wait(semaphore), SvtJxsErrorNone);
#if defined(__linux__)
    int value = -1;
    EXPECT_EQ(sem_getvalue((sem_t *)semaphore, &value), 0);
    EXPECT_EQ(value, 0);
    // Fast path: no timed slice at all.
    expect_tw_stats(0, 0, 0);
#endif
#if defined(__APPLE__)
    // dispatch semaphores must have their initial count restored before release.
    EXPECT_EQ(svt_jxs_post_semaphore(semaphore), SvtJxsErrorNone);
    EXPECT_EQ(svt_jxs_post_semaphore(semaphore), SvtJxsErrorNone);
#endif
    EXPECT_EQ(svt_jxs_destroy_semaphore(semaphore), SvtJxsErrorNone);
}

TEST(TimedWaitSemaphore, PostedWithinBudget) {
#if defined(__linux__)
    // The largest budget, so only a stall of about 1 s could reach the blocking wait.
    TimedWaitConfigScope scope(SVT_JXS_TW_MAX_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    wait_semaphore_posted_after(within_budget_delay, false);
#if defined(__linux__)
    // Woken in a timed slice (or by the fast path if the waiter started late), never the fallback.
    expect_tw_stats(0, any_count, 0);
#endif
}

TEST(TimedWaitSemaphore, PostedAfterBudget) {
#if defined(__linux__)
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    wait_semaphore_posted_after(after_budget_delay, true);
#if defined(__linux__)
    // The post comes long after the budget: the slices time out, then the blocking wait takes over.
    expect_tw_stats(1, any_count, 1);
#endif
}

#if defined(__linux__)
TEST(TimedWaitSemaphore, LastSliceCutToBudget) {
    // A slice far longer than the budget must be cut to the budget, not run to its full length.
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_MAX_US);
    wait_semaphore_posted_after(after_budget_delay, true);
    // The slice timeout is on CLOCK_REALTIME and the budget on CLOCK_MONOTONIC, so a few ns of
    // budget can be left after the first slice, which gives one more very short slice.
    expect_tw_stats(1, 2, 1);
}

TEST(TimedWaitSemaphore, DisabledBlocksAtOnce) {
    TimedWaitConfigScope scope(0, SVT_JXS_TW_DEFAULT_SLICE_US);
    wait_semaphore_posted_after(after_budget_delay, false);
    expect_tw_stats(0, 0, 0);
}
#endif

TEST(TimedWaitSemaphore, ManyHandOffs) {
    // Ping-pong between two threads; every hand-off must be seen exactly once.
#if defined(__linux__)
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    const int rounds = 2000;
    Handle_t ping = svt_jxs_create_semaphore(0, 1);
    Handle_t pong = svt_jxs_create_semaphore(0, 1);
    ASSERT_NE(ping, nullptr);
    ASSERT_NE(pong, nullptr);
    std::thread peer([&]() {
        for (int i = 0; i < rounds; i++) {
            EXPECT_EQ(svt_jxs_block_on_semaphore_timed_wait(ping), SvtJxsErrorNone);
            EXPECT_EQ(svt_jxs_post_semaphore(pong), SvtJxsErrorNone);
        }
    });
    for (int i = 0; i < rounds; i++) {
        EXPECT_EQ(svt_jxs_post_semaphore(ping), SvtJxsErrorNone);
        EXPECT_EQ(svt_jxs_block_on_semaphore_timed_wait(pong), SvtJxsErrorNone);
    }
    peer.join();
    EXPECT_EQ(svt_jxs_destroy_semaphore(ping), SvtJxsErrorNone);
    EXPECT_EQ(svt_jxs_destroy_semaphore(pong), SvtJxsErrorNone);
}

TEST(TimedWaitCondVar, CreateLeavesTimedWaitOff) {
    CondVar cond_var;
    memset(&cond_var, 0xFF, sizeof(cond_var));
    ASSERT_EQ(svt_jxs_create_cond_var(&cond_var), SvtJxsErrorNone);
    EXPECT_EQ(cond_var.timed_wait, 0);
    EXPECT_EQ(cond_var.val, 0);
    EXPECT_EQ(svt_jxs_free_cond_var(&cond_var), SvtJxsErrorNone);
}

TEST(TimedWaitCondVar, AlreadyChanged) {
#if defined(__linux__)
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    CondVar cond_var;
    ASSERT_EQ(svt_jxs_create_cond_var(&cond_var), SvtJxsErrorNone);
    cond_var.timed_wait = 1;
    EXPECT_EQ(svt_jxs_set_cond_var(&cond_var, 5), SvtJxsErrorNone);
    EXPECT_EQ(svt_jxs_wait_cond_var(&cond_var, 0), SvtJxsErrorNone);
    EXPECT_EQ(cond_var.val, 5);
#if defined(__linux__)
    expect_tw_stats(0, 0, 0);
#endif
    EXPECT_EQ(svt_jxs_free_cond_var(&cond_var), SvtJxsErrorNone);
}

TEST(TimedWaitCondVar, SetWithinBudget) {
#if defined(__linux__)
    // The largest budget, so only a stall of about 1 s could reach the blocking wait.
    TimedWaitConfigScope scope(SVT_JXS_TW_MAX_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    wait_cond_var_set_after(1, within_budget_delay, false);
#if defined(__linux__)
    expect_tw_stats(0, any_count, 0);
#endif
}

TEST(TimedWaitCondVar, SetAfterBudget) {
#if defined(__linux__)
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    wait_cond_var_set_after(1, after_budget_delay, true);
#if defined(__linux__)
    expect_tw_stats(1, any_count, 1);
#endif
}

TEST(TimedWaitCondVar, NotMarkedSetAfterDelay) {
#if defined(__linux__)
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_DEFAULT_SLICE_US);
#endif
    wait_cond_var_set_after(0, after_budget_delay, false);
#if defined(__linux__)
    // Not marked: straight to the blocking wait even though the timed wait is enabled.
    expect_tw_stats(0, 0, 0);
#endif
}

#if defined(__linux__)
TEST(TimedWaitCondVar, LastSliceCutToBudget) {
    // A slice far longer than the budget must be cut to the budget. The slices and the budget use
    // the same clock here, so exactly one slice runs.
    TimedWaitConfigScope scope(SVT_JXS_TW_DEFAULT_BUDGET_US, SVT_JXS_TW_MAX_US);
    wait_cond_var_set_after(1, after_budget_delay, true);
    expect_tw_stats(1, 1, 1);
}

TEST(TimedWaitCondVar, DisabledBlocksAtOnce) {
    TimedWaitConfigScope scope(0, SVT_JXS_TW_DEFAULT_SLICE_US);
    wait_cond_var_set_after(1, after_budget_delay, false);
    expect_tw_stats(0, 0, 0);
}
#endif

#if defined(__linux__)
TEST(TimedWaitConfig, Defaults) {
    SvtJxsTimedWaitConfig config;
    svt_jxs_tw_parse_config(NULL, NULL, NULL, &config);
    EXPECT_EQ(config.budget_ns, SVT_JXS_TW_DEFAULT_BUDGET_US * 1000);
    EXPECT_EQ(config.slice_ns, SVT_JXS_TW_DEFAULT_SLICE_US * 1000);
    EXPECT_EQ(config.slack_ns, (unsigned long)SVT_JXS_TW_DEFAULT_SLACK_NS);
}

TEST(TimedWaitConfig, ValidValues) {
    SvtJxsTimedWaitConfig config;
    svt_jxs_tw_parse_config("2000", "20", "500", &config);
    EXPECT_EQ(config.budget_ns, 2000000);
    EXPECT_EQ(config.slice_ns, 20000);
    EXPECT_EQ(config.slack_ns, 500ul);
}

TEST(TimedWaitConfig, BudgetDisabled) {
    const char *disabling[] = {"0", "-5", "abc", "12x", "", " ", " 2000", "+2000", "99999999999999999999999"};
    for (const char *budget : disabling) {
        SvtJxsTimedWaitConfig config;
        svt_jxs_tw_parse_config(budget, NULL, NULL, &config);
        EXPECT_EQ(config.budget_ns, 0) << "SVT_JXS_TW_US=\"" << budget << "\"";
    }
}

TEST(TimedWaitConfig, SliceInvalidKeepsDefault) {
    const char *invalid[] = {"0", "-1", "abc", "20us", "", " 20", "+20", "99999999999999999999999"};
    for (const char *slice : invalid) {
        SvtJxsTimedWaitConfig config;
        svt_jxs_tw_parse_config(NULL, slice, NULL, &config);
        EXPECT_EQ(config.slice_ns, SVT_JXS_TW_DEFAULT_SLICE_US * 1000) << "SVT_JXS_TW_SLICE_US=\"" << slice << "\"";
    }
}

TEST(TimedWaitConfig, SlackZeroMeansOne) {
    SvtJxsTimedWaitConfig config;
    svt_jxs_tw_parse_config(NULL, NULL, "0", &config);
    EXPECT_EQ(config.slack_ns, 1ul);
}

TEST(TimedWaitConfig, SlackInvalidKeepsDefault) {
    const char *invalid[] = {"-1", "abc", "1000ns", "", " 500", "+500", "99999999999999999999999"};
    for (const char *slack : invalid) {
        SvtJxsTimedWaitConfig config;
        svt_jxs_tw_parse_config(NULL, NULL, slack, &config);
        EXPECT_EQ(config.slack_ns, (unsigned long)SVT_JXS_TW_DEFAULT_SLACK_NS) << "SVT_JXS_TW_SLACK_NS=\"" << slack << "\"";
    }
}

TEST(TimedWaitConfig, LargeValuesClamped) {
    SvtJxsTimedWaitConfig config;
    svt_jxs_tw_parse_config("5000000", "5000000", "2000000000", &config);
    EXPECT_EQ(config.budget_ns, (int64_t)SVT_JXS_TW_MAX_US * 1000);
    EXPECT_EQ(config.slice_ns, (int64_t)SVT_JXS_TW_MAX_US * 1000);
    EXPECT_EQ(config.slack_ns, (unsigned long)SVT_JXS_TW_MAX_US * 1000);
}

TEST(TimedWaitConfig, ValuesBeyond32BitsClamped) {
    /* Too big for a 32-bit long, still clamped (not rejected) on every target. */
    SvtJxsTimedWaitConfig config;
    svt_jxs_tw_parse_config("3000000000", "3000000000", "3000000000", &config);
    EXPECT_EQ(config.budget_ns, (int64_t)SVT_JXS_TW_MAX_US * 1000);
    EXPECT_EQ(config.slice_ns, (int64_t)SVT_JXS_TW_MAX_US * 1000);
    EXPECT_EQ(config.slack_ns, (unsigned long)SVT_JXS_TW_MAX_US * 1000);
}
#endif

TEST(TimedWaitScope, DecoderMarksItsObjects) {
    svt_jpeg_xs_decoder_api_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.verbose = VERBOSE_NONE;
    decoder.threads_num = 2;
    svt_jpeg_xs_image_config_t image_config;
    ASSERT_EQ(svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                       SVT_JPEGXS_API_VER_MINOR,
                                       &decoder,
                                       Frame_Sample_1_16x16_8bit_422_bitstream,
                                       Frame_Sample_1_16x16_8bit_422_bitstream_size,
                                       &image_config),
              SvtJxsErrorNone);
    const svt_jpeg_xs_decoder_api_prv_t *prv = (const svt_jpeg_xs_decoder_api_prv_t *)decoder.private_ptr;
    ASSERT_NE(prv, nullptr);
    expect_resource_timed_wait(prv->input_buffer_resource_ptr, 1);
    expect_resource_timed_wait(prv->universal_buffer_resource_ptr, 1);
    expect_resource_timed_wait(prv->final_buffer_resource_ptr, 1);
    expect_resource_timed_wait(prv->output_buffer_resource_ptr, 1);
    expect_resource_timed_wait(prv->internal_pool_decoder_instance_resource_ptr, 1);
    EXPECT_EQ(prv->sync_output_ringbuffer_left.timed_wait, 1);
    svt_jpeg_xs_decoder_close(&decoder);
}
