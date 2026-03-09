#include "test_filter.hpp"

#include <cstring>
#include <cstdlib>

namespace stam::tests {
namespace {

const char* g_suite_filter = nullptr;
const char* g_test_filter = nullptr;
bool g_diag_stress_enabled = false;

bool str_eq(const char* a, const char* b) noexcept {
    return std::strcmp(a, b) == 0;
}

bool str_contains(const char* haystack, const char* needle) noexcept {
    return std::strstr(haystack, needle) != nullptr;
}

} // namespace

void configure_from_cli(int argc, char** argv) {
    if (const char* env = std::getenv("STAM_TEST_DIAG_STRESS")) {
        if (std::strcmp(env, "1") == 0 || std::strcmp(env, "true") == 0 || std::strcmp(env, "TRUE") == 0) {
            g_diag_stress_enabled = true;
        }
    }

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strncmp(arg, "--suite=", 8) == 0) {
            g_suite_filter = arg + 8;
        } else if (std::strncmp(arg, "--test=", 7) == 0) {
            g_test_filter = arg + 7;
        } else if (std::strcmp(arg, "--diag-stress") == 0 ||
                   std::strcmp(arg, "--enable-diagnostic-stress") == 0) {
            g_diag_stress_enabled = true;
        }
    }
}

bool should_run_suite(const char* suite_name) noexcept {
    if (g_suite_filter == nullptr || g_suite_filter[0] == '\0') {
        return true;
    }
    return str_eq(suite_name, g_suite_filter);
}

bool should_run_test(const char* suite_name, const char* test_name) noexcept {
    if (!should_run_suite(suite_name)) {
        return false;
    }
    if (g_test_filter == nullptr || g_test_filter[0] == '\0') {
        return true;
    }
    return str_contains(test_name, g_test_filter);
}

bool should_run_diagnostic_stress() noexcept {
    return g_diag_stress_enabled;
}

} // namespace stam::tests
