# Component Testing Guidelines

This document defines **mandatory testing requirements** for each framework component type.

Tests are not optional. They are part of the component's contract.

---

## 1. General Principles

### 1.1 Test Categories

Every component must have:

1. **Contract tests** - verify invariants and preconditions
2. **Unit tests** - verify isolated behavior
3. **Integration tests** - verify interaction with other components (where applicable)

### 1.2 RT Components — Additional Requirements

RT components must additionally verify:

- **No allocation** - instrument allocator, fail if malloc/new called
- **Bounded time** - worst-case execution time (WCET) measurement
- **No syscalls** - static analysis or runtime interception
- **Memory ordering** - race detection (TSan or similar)

### 1.3 Test Failure = Contract Violation

If a test fails, the component contract is broken.

Tests must be run before every commit.

---

## 2. Component-Specific Testing Requirements

### 2.1 `rt/transport/` — Lock-Free Primitives

**Components**: `spsc_ring.hpp`, `double_buffer.hpp`

**Contract Tests** (property-based):
```cpp
// tests/contracts/test_spsc_invariants.cpp

TEST(SPSCRing, single_producer_never_blocks) {
    // Property: producer can always advance (even if consumer stalls)
}

TEST(SPSCRing, single_consumer_never_corrupts) {
    // Property: consumer never sees partial writes
}

TEST(SPSCRing, capacity_invariant) {
    // Property: available_write + available_read + in_flight == capacity
}

TEST(SPSCRing, monotonic_sequence) {
    // Property: sequence numbers never decrease
}

TEST(DoubleBuffer, snapshot_consistency) {
    // Property: reader always sees complete snapshot
}

TEST(DoubleBuffer, writer_never_blocks) {
    // Property: writer completes in bounded time
}
```

**Unit Tests**:
```cpp
// tests/unit/test_spsc_ring.cpp

TEST(SPSCRing, empty_ring_reports_zero_available) { ... }
TEST(SPSCRing, full_ring_rejects_write) { ... }
TEST(SPSCRing, wrapping_preserves_data) { ... }
TEST(SPSCRing, concurrent_producer_consumer) { ... }
```

**RT Requirements**:
- No allocation after construction
- WCET measurement for push/pop
- TSan clean under concurrent access

---

### 2.2 `rt/control/` — Controllers

**Components**: `pid.hpp`, `bangbang.hpp`

**Contract Tests**:
```cpp
// tests/contracts/test_pid_invariants.cpp

TEST(PIDController, output_bounded) {
    // Property: output always within [min_output, max_output]
}

TEST(PIDController, integral_windup_prevented) {
    // Property: integral term clamped to prevent windup
}

TEST(PIDController, no_derivative_kick) {
    // Property: setpoint change doesn't cause derivative spike
}

TEST(BangBang, hysteresis_prevents_chattering) {
    // Property: output doesn't oscillate within deadband
}
```

**Unit Tests**:
```cpp
TEST(PIDController, proportional_term_correct) { ... }
TEST(PIDController, zero_gains_produce_zero_output) { ... }
TEST(PIDController, step_response) { ... }
```

**RT Requirements**:
- No allocation (coefficients passed at construction)
- Fixed point arithmetic verification (if used)
- WCET measurement for update()

---

### 2.3 `rt/fsm/` — Finite State Machines

**Components**: `fsm.hpp`, `safety_fsm.hpp`

**Contract Tests**:
```cpp
// tests/contracts/test_safety_fsm_invariants.cpp

TEST(SafetyFSM, valid_transitions_only) {
    // Property: only allowed state transitions occur
}

TEST(SafetyFSM, escalation_monotonic) {
    // Property: WARN → LIMIT → SHED → PANIC, never reverses without explicit recovery
}

TEST(SafetyFSM, panic_is_terminal) {
    // Property: PANIC state cannot transition without reset
}

TEST(SafetyFSM, state_entry_actions_idempotent) {
    // Property: entering same state twice has same effect as once
}
```

**Unit Tests**:
```cpp
TEST(SafetyFSM, initial_state_is_NORMAL) { ... }
TEST(SafetyFSM, fault_triggers_WARN) { ... }
TEST(SafetyFSM, recovery_clears_WARN) { ... }
TEST(SafetyFSM, all_states_reachable) { ... }
```

**RT Requirements**:
- No allocation
- Transition handlers execute in bounded time
- State table in ROM (const)

---

### 2.4 `rt/logging/` — RT Publisher

**Components**: `record.hpp`, `publisher.hpp`

**Contract Tests**:
```cpp
// tests/contracts/test_publisher_invariants.cpp

TEST(Publisher, publish_never_blocks) {
    // Property: publish returns immediately even if ring full
}

TEST(Publisher, lossy_when_full) {
    // Property: overflow drops new records, preserves old
}

TEST(Publisher, record_integrity) {
    // Property: CRC32 matches record contents
}

TEST(Publisher, timestamp_monotonic) {
    // Property: timestamps never decrease
}
```

**Unit Tests**:
```cpp
TEST(Publisher, encode_decode_roundtrip) { ... }
TEST(Publisher, overflow_handling) { ... }
TEST(Publisher, priority_levels_respected) { ... }
```

**RT Requirements**:
- No allocation (pre-allocated ring)
- CRC computation in bounded time
- Record encoding O(1)

---

### 2.5 `nonrt/drain/` — Ring Drain

**Components**: `ring_drain.hpp`

**Contract Tests**:
```cpp
// tests/contracts/test_drain_invariants.cpp

TEST(RingDrain, never_corrupts_rt_side) {
    // Property: consumer side never interferes with producer
}

TEST(RingDrain, eventual_progress) {
    // Property: if producer stops, drain eventually empties ring
}

TEST(RingDrain, backpressure_bounded) {
    // Property: slow drain doesn't block RT indefinitely
}
```

**Unit Tests**:
```cpp
TEST(RingDrain, drains_all_records) { ... }
TEST(RingDrain, handles_empty_ring) { ... }
TEST(RingDrain, respects_batch_size) { ... }
```

---

### 2.6 `nonrt/backend/` — Persistence Backends

**Components**: `file_backend.hpp`, `memory_backend.hpp`

**Contract Tests**:
```cpp
// tests/contracts/test_file_backend_invariants.cpp

TEST(FileBackend, durability_guarantee) {
    // Property: after fsync, data survives process crash
}

TEST(FileBackend, corruption_detected) {
    // Property: CRC mismatch detected on read
}

TEST(FileBackend, append_only) {
    // Property: old records never modified
}
```

**Unit Tests**:
```cpp
TEST(FileBackend, write_and_read_roundtrip) { ... }
TEST(FileBackend, handles_disk_full) { ... }
TEST(FileBackend, rotation_policy) { ... }

TEST(MemoryBackend, capacity_limit_enforced) { ... }
TEST(MemoryBackend, eviction_policy) { ... }
```

**Integration Tests**:
```cpp
// tests/integration/test_drain_to_backend.cpp

TEST(Integration, rt_to_file_pipeline) {
    // RT publisher → ring → drain → file backend
}

TEST(Integration, backpressure_propagation) {
    // Slow backend → full ring → RT drops records
}
```

---

### 2.7 `exec/` — Task Wrapper

**Components**: `task.hpp`, `exec_policy_rt.hpp`, `exec_policy_nonrt.hpp`

**Contract Tests**:
```cpp
// tests/contracts/test_exec_policy_invariants.cpp

TEST(ExecPolicyRT, deterministic_scheduling) {
    // Property: same inputs → same schedule
}

TEST(ExecPolicyRT, deadline_honored) {
    // Property: task completes within deadline or signals overrun
}

TEST(ExecPolicyNonRT, fairness) {
    // Property: no task starves indefinitely
}
```

**Unit Tests**:
```cpp
TEST(TaskWrapper, executes_payload) { ... }
TEST(TaskWrapper, handles_exceptions) { ... }
TEST(TaskWrapper, policy_switchable) { ... }
```

---

### 2.8 `hal/` — Hardware Abstraction

**Components**: `tick.hpp`, `gpio.hpp`, `adc.hpp`, `watchdog.hpp`

**Unit Tests** (with mocks):
```cpp
// tests/unit/test_hal_mock.cpp

TEST(HAL_Tick, monotonic_increment) { ... }
TEST(HAL_GPIO, set_get_roundtrip) { ... }
TEST(HAL_ADC, read_within_range) { ... }
TEST(HAL_Watchdog, kick_resets_timer) { ... }
```

**Integration Tests** (platform-specific):
```cpp
// tests/integration/linux/test_hal_linux.cpp

TEST(HAL_Linux, tick_matches_CLOCK_MONOTONIC) { ... }

// tests/integration/stm32/test_hal_stm32.cpp

TEST(HAL_STM32, gpio_hardware_toggle) { ... }
```

---

### 2.9 `sys/` — Portability Layer

**Unit Tests**:
```cpp
// tests/unit/test_sys_fence.cpp

TEST(SysFence, acquire_release_ordering) {
    // Validate fence semantics match C++ memory model
}

TEST(SysFence, cache_coherence) {
    // Platform-specific cache maintenance
}
```

**Compile Tests**:
```cpp
// tests/compile/test_sys_config.cpp

// Verify correct platform detection
static_assert(RTFW_PLATFORM_LINUX || RTFW_PLATFORM_STM32 || ...);

// Verify cache line size reasonable
static_assert(RTFW_CACHE_LINE_SIZE >= 32 && RTFW_CACHE_LINE_SIZE <= 256);
```

---

## 3. Test Infrastructure Requirements

### 3.1 RT Test Harness

All RT tests must run with:

```cpp
// tests/rt_test_harness.hpp

class RTTestHarness {
public:
    RTTestHarness() {
        install_alloc_hooks();   // Fail on malloc/new
        install_syscall_hooks(); // Fail on syscalls
        start_wcet_measurement();
    }
    
    ~RTTestHarness() {
        report_wcet();
        check_no_leaks();
    }
};
```

### 3.2 Property-Based Testing

Contract tests use property-based testing where applicable:

```cpp
// Use RapidCheck or similar
RC_GTEST_PROP(SPSCRing, capacity_invariant, (uint32_t capacity)) {
    RC_PRE(capacity > 0 && capacity < 1000000);
    
    SPSCRing<int> ring(capacity);
    
    // Property: available_write + available_read + in_flight == capacity
    RC_ASSERT(ring.available_write() + ring.available_read() <= capacity);
}
```

### 3.3 Sanitizers

All tests must pass under:

- **AddressSanitizer** (ASan) - memory errors
- **ThreadSanitizer** (TSan) - data races
- **UndefinedBehaviorSanitizer** (UBSan) - undefined behavior

```cmake
# CMakeLists.txt
option(RTFW_ENABLE_ASAN "Enable AddressSanitizer" ON)
option(RTFW_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(RTFW_ENABLE_UBSAN "Enable UBSanitizer" ON)
```

---

## 4. Test Coverage Requirements

### Minimum Coverage:

- **Contract tests**: 100% of documented invariants
- **Unit tests**: 80% line coverage
- **RT components**: 100% branch coverage for safety-critical paths

### Reporting:

```bash
# Generate coverage report
cmake --build build --target coverage
```

---

## 5. Continuous Integration

All tests must pass on:

- Linux x86_64 (primary CI)
- Windows x86_64 (secondary)
- ARM cross-compile (smoke test)

```yaml
# .github/workflows/ci.yml
- name: Run contract tests
  run: ctest -R "^contract_"
  
- name: Run unit tests
  run: ctest -R "^unit_"
  
- name: Run integration tests
  run: ctest -R "^integration_"
```

---

## 6. Test Naming Convention

```
tests/
  contracts/
    test_<component>_invariants.cpp
  unit/
    test_<component>.cpp
  integration/
    test_<scenario>.cpp
```

Example:
```
tests/contracts/test_spsc_ring_invariants.cpp
tests/unit/test_spsc_ring.cpp
tests/integration/test_rt_to_file_pipeline.cpp
```

---

## 7. Documentation Requirements

Each test file must have a header comment:

```cpp
/**
 * @file test_spsc_ring_invariants.cpp
 * @brief Contract tests for SPSCRing
 * 
 * Tests the following invariants:
 * - INV-1: Single producer never blocks
 * - INV-2: Single consumer never sees torn writes
 * - INV-3: Capacity equation holds
 * 
 * @see docs/contracts/spsc_ring.md
 */
```

---

## 8. Failure Triage Process

When a test fails:

1. **Identify violated invariant** - which contract is broken?
2. **Minimal reproduction** - simplify test case
3. **Root cause** - implementation bug or contract ambiguity?
4. **Fix** - code or contract or both
5. **Regression test** - add test to prevent recurrence

---

## 9. Test-First Development

For new components:

1. Write contract document (`docs/contracts/<component>.md`)
2. Write contract tests (`tests/contracts/test_<component>_invariants.cpp`)
3. Write failing unit tests (`tests/unit/test_<component>.cpp`)
4. Implement component until tests pass
5. Measure RT properties (WCET, allocation)

---

## 10. Summary: Test Checklist

Before merging a component:

- [ ] Contract document exists
- [ ] Contract tests cover all invariants
- [ ] Unit tests achieve 80%+ coverage
- [ ] Integration tests (if applicable)
- [ ] RT requirements verified (if RT component)
- [ ] ASan/TSan/UBSan clean
- [ ] CI passes on all platforms
- [ ] Test documentation complete

Without these, the component is not production-ready.
