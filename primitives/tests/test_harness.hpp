#pragma once

#include "test_filter.hpp"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>

// Minimal test harness macros shared by all test suites.

#define TEST(name) static void name(); static void name##_announce() { std::printf("[RUN] %s\n", #name); } static void name()

#define RUN(name)                                          \
    do {                                                   \
        if (!stam::tests::should_run_test(kSuiteName, #name)) {\
            std::printf("  %-55sSKIP\n", #name " ");\
            break;\
        }\
        ++g_total;                                         \
        std::printf("  %-55s", #name " ");                 \
        name##_announce();                                 \
        name();                                            \
        ++g_passed;                                        \
        std::printf("PASS\n");                             \
    } while (0)

#define EXPECT(cond)                                               \
    do {                                                           \
        if (!(cond)) {                                             \
            ++g_failed;                                            \
            std::printf("FAIL\n  assertion failed: %s\n"          \
                        "  at %s:%d\n", #cond, __FILE__, __LINE__);\
            std::abort();                                          \
        }                                                          \
    } while (0)

namespace stam::tests {

template <class Fn>
inline bool expect_child_abort(Fn&& fn) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        fn();
        std::fflush(stdout);
        _Exit(0);
    }
    if (pid < 0) {
        return false;
    }

    constexpr long kTimeoutMs = 2000;

    const auto now_ms = []() -> long {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<long>(ts.tv_sec) * 1000L +
               static_cast<long>(ts.tv_nsec / 1000000L);
    };

    const long deadline = now_ms() + kTimeoutMs;
    int status = 0;

    for (;;) {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (now_ms() >= deadline) {
            break;
        }
        ::usleep(1000); // 1ms backoff
    }

    // Timeout: force-kill and reap.
    (void)::kill(pid, SIGKILL);
    (void)::waitpid(pid, &status, 0);
    return false;
}

template <class IssueFn>
inline bool expect_double_issue_abort(IssueFn&& issue) {
    return expect_child_abort([&] {
        issue();
        issue();
    });
}

template <class IssueFn>
inline bool expect_issue_limit_abort(uint32_t limit, IssueFn&& issue) {
    return expect_child_abort([&] {
        for (uint32_t i = 0; i < limit + 1; ++i) {
            issue();
        }
    });
}

} // namespace stam::tests
