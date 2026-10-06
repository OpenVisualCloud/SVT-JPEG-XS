/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "SvtJpegxs.h"
#include "SvtLog.h"

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct CallbackResult {
    int called;
    SvtLogLevel level;
    char tag[32];
    char message[256];
};

void test_log_callback(void* context, SvtLogLevel level, const char* tag, const char* fmt, va_list args) {
    CallbackResult* result = static_cast<CallbackResult*>(context);
    result->called = 1;
    result->level = level;
    if (tag) {
        snprintf(result->tag, sizeof(result->tag), "%s", tag);
    }
    else {
        result->tag[0] = '\0';
    }
    vsnprintf(result->message, sizeof(result->message), fmt, args);
}

} // namespace

/*
 * The logger is initialized exactly once per process (lazily, on first log call), so a custom
 * callback registered via svt_jpeg_xs_set_log_callback() only takes effect if no log call has
 * happened yet in this process. fork() alone isn't enough: the child inherits whatever logger
 * state the parent already had, so if another test in this binary logged first, the once-guard
 * would already be fired in the child too. Run in a forked child and explicitly reset the
 * logger there before registering the callback, so the outcome doesn't depend on test order.
 */
TEST(LogCallback, CustomCallbackInterceptsMessages) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // Child: fork() alone only copies the parent's memory, so if some other test already
        // triggered logger init in this process, the child would inherit that already-fired
        // state too. Force a virgin logger before staging our callback.
        close(pipefd[0]);
        svt_jxs_log_reset_for_testing();
        CallbackResult result;
        memset(&result, 0, sizeof(result));
        svt_jpeg_xs_set_log_callback(test_log_callback, &result);
        svt_jxs_log(SVT_LOG_ERROR, "TestTag", "hello %d\n", 42);
        ssize_t written = write(pipefd[1], &result, sizeof(result));
        (void)written;
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    CallbackResult result;
    memset(&result, 0, sizeof(result));
    ssize_t bytes_read = read(pipefd[0], &result, sizeof(result));
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);

    ASSERT_EQ(bytes_read, static_cast<ssize_t>(sizeof(result)));
    EXPECT_TRUE(result.called);
    EXPECT_EQ(result.level, SVT_LOG_ERROR);
    EXPECT_STREQ(result.tag, "TestTag");
    EXPECT_STREQ(result.message, "hello 42\n");
}

/*
 * Sanity check: without a callback registered, the default logger must remain usable (no crash) -
 * exercised in a fresh subprocess for the same isolation reason as above.
 */
TEST(LogCallback, DefaultLoggerStillWorksWithoutCallback) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        svt_jxs_log(SVT_LOG_ERROR, "TestTag", "default logger message\n");
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

/*
 * The default logger writes the "tag[level]: " prefix and the message in separate stdio calls. Several
 * threads logging at once (e.g. decoder slice threads) must still produce whole lines, not a prefix
 * followed by another thread's prefix. Logs from many threads into SVT_LOG_FILE in a forked child and
 * checks every line in the file.
 */
TEST(LogDefault, ConcurrentMessagesStayWholeLines) {
    const int threads_num = 8;
    const int messages_num = 2000;
    char path[] = "/tmp/svt_jxs_log_test_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    //Removes the file on every exit from the test, also a failed ASSERT; the child leaves with _exit()
    struct RemoveFile {
        const char* path;
        ~RemoveFile() {
            unlink(path);
        }
    } remove_file{path};

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        setenv("SVT_LOG_FILE", path, 1);
        setenv("SVT_LOG", "4", 1);
        svt_jxs_log_reset_for_testing();
        std::vector<std::thread> threads;
        for (int t = 0; t < threads_num; t++) {
            threads.emplace_back([t]() {
                for (int m = 0; m < messages_num; m++) {
                    svt_jxs_log(SVT_LOG_WARN, "Svt", "thread %d message %d\n", t, m);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    FILE* f = fopen(path, "r");
    ASSERT_NE(f, nullptr);
    std::vector<int> next_message(threads_num, 0);
    char line[256];
    int lines = 0;
    int bad_lines = 0;
    while (fgets(line, sizeof(line), f)) {
        int t = -1, m = -1;
        char tail = 0;
        lines++;
        if (sscanf(line, "Svt[warn]: thread %d message %d%c", &t, &m, &tail) != 3 || tail != '\n' || t < 0 ||
            t >= threads_num || m != next_message[t]) {
            if (bad_lines++ < 5) {
                ADD_FAILURE() << "broken log line " << lines << ": " << line;
            }
            continue;
        }
        next_message[t]++;
    }
    fclose(f);

    EXPECT_EQ(bad_lines, 0);
    EXPECT_EQ(lines, threads_num * messages_num);
    for (int t = 0; t < threads_num; t++) {
        EXPECT_EQ(next_message[t], messages_num) << "thread " << t;
    }
}

#endif // _WIN32
