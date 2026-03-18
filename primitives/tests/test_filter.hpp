#pragma once

namespace stam::tests {

void configure_from_cli(int argc, char** argv);

bool should_run_suite(const char* suite_name) noexcept;
bool should_run_test(const char* suite_name, const char* test_name) noexcept;
bool should_run_diagnostic_stress() noexcept;

} // namespace stam::tests
