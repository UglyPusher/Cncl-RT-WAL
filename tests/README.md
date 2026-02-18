# RT-WAL Tests

This directory contains the test suite for the RT-WAL framework.

## Test Structure

```
tests/
├── contracts/          # Property-based tests for component invariants
├── unit/               # Unit tests for isolated components
├── integration/        # Integration tests for component interactions
├── rt_test_harness.hpp # RT test utilities (WCET, allocation checks)
└── rt_alloc_hook.cpp   # Allocation hook to detect malloc in RT path
```

## Test Categories

### Contract Tests

Verify architectural invariants and component contracts.

Examples:
- `contracts/test_spsc_ring_invariants.cpp` - SPSC ring properties
- `contracts/test_double_buffer_invariants.cpp` - Double buffer consistency

Run with:
```bash
cmake --build build --target test_contracts
```

### Unit Tests

Test individual components in isolation.

Examples:
- `unit/test_crc32.cpp` - CRC32 algorithm correctness

Run with:
```bash
cmake --build build --target test_unit
```

### Integration Tests

Test interactions between multiple components.

Examples:
- `integration/test_rt_to_file_pipeline.cpp` - Full RT→file logging pipeline

Run with:
```bash
cmake --build build --target test_integration
```

## RT Test Harness

The RT test harness provides:

1. **Allocation detection** - Fails if malloc/new called in RT path
2. **WCET measurement** - Measures worst-case execution time
3. **Sanitizer integration** - ASan/TSan/UBSan support

### Usage Example

```cpp
#include "rt_test_harness.hpp"

TEST(MyRTComponent, no_allocation) {
    RTTestHarness harness;  // Enables allocation hooks
    
    // RT code under test - will fail if it allocates
    my_rt_function();
    
    // Harness destructor checks violations
}

TEST(MyRTComponent, wcet_bounded) {
    WCETMeasurement wcet("my_function");
    
    for (int i = 0; i < 10000; ++i) {
        wcet.start();
        my_rt_function();
        wcet.stop();
    }
    
    wcet.report();
    EXPECT_LT(wcet.get_wcet_ns(), 1000);  // < 1μs
}
```

## Sanitizers

Tests are compiled with sanitizers enabled by default:

- **AddressSanitizer (ASan)** - Memory errors (buffer overflows, use-after-free)
- **UndefinedBehaviorSanitizer (UBSan)** - Undefined behavior
- **ThreadSanitizer (TSan)** - Data races (enable with `-DRTFW_ENABLE_TSAN=ON`)

Note: ASan and TSan cannot be enabled simultaneously.

### Build with Sanitizers

```bash
# Default: ASan + UBSan
cmake -B build
cmake --build build

# Enable TSan instead of ASan
cmake -B build -DRTFW_ENABLE_TSAN=ON -DRTFW_ENABLE_ASAN=OFF
cmake --build build
```

## Running Tests

### All tests
```bash
ctest --test-dir build --output-on-failure
```

### Specific category
```bash
ctest --test-dir build -R "^contract_"
ctest --test-dir build -R "^unit_"
ctest --test-dir build -R "^integration_"
```

### With verbose output
```bash
ctest --test-dir build --verbose
```

## Coverage

Generate coverage report (requires `lcov` and `genhtml`):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Coverage
cmake --build build
cmake --build build --target coverage
# Open build/coverage_html/index.html
```

## Requirements

- **GoogleTest** - Test framework
- **C++20 compiler** - GCC 11+ or Clang 14+
- **lcov** (optional) - For coverage reports
- **ASan/TSan/UBSan** (optional) - Built into modern compilers

## Test Failure Triage

When a test fails:

1. Check which invariant is violated (see test output)
2. Run test under debugger: `gdb --args build/tests/contract_spsc_ring_invariants`
3. Check sanitizer output for memory errors
4. Review component contract: `docs/contracts/<component>.md`
5. File issue or fix implementation

## Adding New Tests

See [docs/testing_guidelines.md](../docs/testing_guidelines.md) for component-specific testing requirements.

### Contract Test Template

```cpp
/**
 * @file test_<component>_invariants.cpp
 * @brief Contract tests for <Component>
 * 
 * Tests the following invariants:
 * - INV-1: <description>
 * - INV-2: <description>
 * 
 * @see docs/contracts/<component>.md
 */

#include <gtest/gtest.h>
#include <path/to/component.hpp>
#include "rt_test_harness.hpp"

namespace rtfw::test {

class ComponentInvariants : public ::testing::Test {};

TEST_F(ComponentInvariants, invariant_1) {
    // Test INV-1
}

TEST_F(ComponentInvariants, invariant_2) {
    // Test INV-2
}

} // namespace rtfw::test
```

## CI Integration

Tests run automatically on:
- Every pull request
- Every commit to main branch
- Nightly builds with extended fuzzing

See `.github/workflows/ci.yml` for CI configuration.
