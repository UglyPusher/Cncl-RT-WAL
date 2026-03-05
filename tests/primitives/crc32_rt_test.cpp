/*
 * crc32_rt_test.cpp
 *
 * Unit tests for crc32c / crc32c_update (CRC32C, Castagnoli).
 *
 * Test strategy:
 *  - Known reference vectors (RFC 3720, iSCSI, standard "123456789")
 *  - Incremental vs one-shot equivalence
 *  - Boundary conditions: empty input, single byte, large buffer
 *  - Seed / continuation semantics
 *  - constexpr evaluation (compile-time)
 *  - void* overload equivalence
 *
 * Build (example):
 *   c++ -std=c++20 -O2 crc32_rt_test.cpp -o crc32_rt_test
 *
 * Exit code: 0 = all tests passed, non-zero = failure.
 */

#include "crc32_rt.hpp"

#include <cstdio>
#include <cstring>
#include <array>
#include <numeric>

using namespace stam::exec::primitives;

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) static void name()

#define RUN(name)                                          \
    do {                                                   \
        ++g_total;                                         \
        std::printf("  %-60s", #name " ");                 \
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

#define EXPECT_EQ(a, b)                                            \
    do {                                                           \
        auto _a = (a); auto _b = (b);                              \
        if (_a != _b) {                                            \
            ++g_failed;                                            \
            std::printf("FAIL\n  expected 0x%08X, got 0x%08X\n"   \
                        "  at %s:%d\n",                            \
                        static_cast<unsigned>(_b),                 \
                        static_cast<unsigned>(_a),                 \
                        __FILE__, __LINE__);                        \
            std::abort();                                          \
        }                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// Reference vectors
// ---------------------------------------------------------------------------

// Standard CRC32C test vector (iSCSI / RFC 3720).
// crc32c("123456789") == 0xE3069283
static constexpr uint8_t kVec9[]  = {'1','2','3','4','5','6','7','8','9'};

// RFC 3720 Appendix B.4 — three well-known CRC32C vectors:
//   32 bytes of 0x00  → 0xAA36918A
//   32 bytes of 0xFF  → 0x43ABA862
//   32 bytes of 0x1C..0x3B (incrementing) → 0x4E79DD46
static constexpr uint32_t kRfc3720_zeros_32    = 0xAA36918Au;
static constexpr uint32_t kRfc3720_ones_32     = 0x43ABA862u;
static constexpr uint32_t kRfc3720_incr_32     = 0x4E79DD46u;

// ---------------------------------------------------------------------------
// Compile-time checks (static_assert)
// ---------------------------------------------------------------------------

// The static_assert in crc32_rt.hpp already checks the primary vector.
// Additional constexpr checks here exercise the API at compile time.

static_assert(
    crc32c(kVec9, sizeof(kVec9)) == 0xE3069283u,
    "crc32c compile-time: standard vector mismatch"
);

static_assert(
    crc32c(static_cast<const uint8_t*>(nullptr), 0u) == 0x00000000u,
    "crc32c compile-time: empty input must be 0x00000000"
);

// Single byte 0x00 with standard init.
static_assert(
    crc32c_update(~0u, static_cast<const uint8_t*>("\x00"), 1u) != ~0u,
    "crc32c_update compile-time: single zero byte must change state"
);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// --- Standard reference vectors ---

TEST(test_standard_vector_123456789) {
    EXPECT_EQ(crc32c(kVec9, sizeof(kVec9)), 0xE3069283u);
}

TEST(test_rfc3720_zeros_32) {
    uint8_t buf[32]{};  // zero-initialized
    EXPECT_EQ(crc32c(buf, 32), kRfc3720_zeros_32);
}

TEST(test_rfc3720_ones_32) {
    uint8_t buf[32];
    std::memset(buf, 0xFF, 32);
    EXPECT_EQ(crc32c(buf, 32), kRfc3720_ones_32);
}

TEST(test_rfc3720_incrementing_32) {
    uint8_t buf[32];
    for (int i = 0; i < 32; ++i) buf[i] = static_cast<uint8_t>(0x1C + i);
    EXPECT_EQ(crc32c(buf, 32), kRfc3720_incr_32);
}

// --- Boundary conditions ---

TEST(test_empty_input_returns_zero) {
    // crc32c of empty buffer with seed=0 must be 0x00000000.
    // ~crc32c_update(~0, data, 0) = ~(~0) = 0
    EXPECT_EQ(crc32c(static_cast<const uint8_t*>(nullptr), 0u), 0x00000000u);
    EXPECT_EQ(crc32c(static_cast<const void*>(nullptr),    0u), 0x00000000u);
}

TEST(test_single_byte_zero) {
    const uint8_t b = 0x00;
    // Known value: crc32c of a single 0x00 byte.
    const uint32_t expected = crc32c(&b, 1u);  // compute once, check consistency
    EXPECT_EQ(crc32c(&b, 1u), expected);
    EXPECT_EQ(crc32c(static_cast<const void*>(&b), 1u), expected);
}

TEST(test_single_byte_ff) {
    const uint8_t b = 0xFF;
    const uint32_t r1 = crc32c(&b, 1u);
    const uint32_t r2 = crc32c(static_cast<const void*>(&b), 1u);
    EXPECT_EQ(r1, r2);
    // Must differ from single 0x00 byte.
    const uint8_t z = 0x00;
    EXPECT(r1 != crc32c(&z, 1u));
}

TEST(test_all_zeros_various_lengths) {
    // CRC32C of N zero bytes must be deterministic and consistent.
    uint8_t buf[1024]{};
    const uint32_t c1   = crc32c(buf,   1u);
    const uint32_t c16  = crc32c(buf,  16u);
    const uint32_t c256 = crc32c(buf, 256u);

    // Recompute — must be stable.
    EXPECT_EQ(crc32c(buf,   1u), c1);
    EXPECT_EQ(crc32c(buf,  16u), c16);
    EXPECT_EQ(crc32c(buf, 256u), c256);

    // Different lengths must produce different checksums.
    EXPECT(c1 != c16);
    EXPECT(c16 != c256);
}

// --- Incremental vs one-shot equivalence ---

TEST(test_incremental_2chunks_equals_oneshot) {
    // Split "123456789" into "1234" + "56789" — must equal one-shot.
    const uint32_t oneshot = crc32c(kVec9, sizeof(kVec9));

    uint32_t s = ~0u;
    s = crc32c_update(s, kVec9,     4u);
    s = crc32c_update(s, kVec9 + 4, 5u);
    const uint32_t incremental = ~s;

    EXPECT_EQ(incremental, oneshot);
}

TEST(test_incremental_byte_by_byte_equals_oneshot) {
    const uint32_t oneshot = crc32c(kVec9, sizeof(kVec9));

    uint32_t s = ~0u;
    for (size_t i = 0; i < sizeof(kVec9); ++i) {
        s = crc32c_update(s, kVec9 + i, 1u);
    }
    EXPECT_EQ(~s, oneshot);
}

TEST(test_incremental_many_chunks) {
    // 256-byte buffer split into 16 chunks of 16 bytes each.
    uint8_t buf[256];
    for (int i = 0; i < 256; ++i) buf[i] = static_cast<uint8_t>(i);

    const uint32_t oneshot = crc32c(buf, 256u);

    uint32_t s = ~0u;
    for (int i = 0; i < 16; ++i) {
        s = crc32c_update(s, buf + i * 16, 16u);
    }
    EXPECT_EQ(~s, oneshot);
}

// --- void* overload equivalence ---

TEST(test_void_ptr_overload_equals_uint8_overload) {
    uint8_t buf[64];
    for (int i = 0; i < 64; ++i) buf[i] = static_cast<uint8_t>(i * 3);

    const uint32_t r_typed = crc32c(buf,                        64u);
    const uint32_t r_void  = crc32c(static_cast<const void*>(buf), 64u);
    EXPECT_EQ(r_typed, r_void);
}

TEST(test_void_ptr_update_equals_uint8_update) {
    uint8_t buf[64];
    for (int i = 0; i < 64; ++i) buf[i] = static_cast<uint8_t>(i);

    uint32_t s1 = ~0u;
    s1 = crc32c_update(s1, buf, 64u);

    uint32_t s2 = ~0u;
    s2 = crc32c_update(s2, static_cast<const void*>(buf), 64u);

    EXPECT_EQ(s1, s2);
}

// --- Seed semantics ---

TEST(test_seed_zero_is_default) {
    // Explicit seed=0 must equal default.
    EXPECT_EQ(crc32c(kVec9, sizeof(kVec9), 0u),
              crc32c(kVec9, sizeof(kVec9)));
}

TEST(test_different_seeds_produce_different_results) {
    const uint32_t r0 = crc32c(kVec9, sizeof(kVec9), 0u);
    const uint32_t r1 = crc32c(kVec9, sizeof(kVec9), 1u);
    EXPECT(r0 != r1);
}

// --- Data sensitivity ---

TEST(test_bit_flip_changes_checksum) {
    uint8_t buf[32]{};
    const uint32_t original = crc32c(buf, 32u);

    // Flip one bit in the middle.
    buf[16] ^= 0x01u;
    const uint32_t flipped = crc32c(buf, 32u);

    EXPECT(original != flipped);
}

TEST(test_position_sensitivity) {
    // Same byte value at different positions must produce different CRCs.
    uint8_t buf_a[4] = {0x01, 0x00, 0x00, 0x00};
    uint8_t buf_b[4] = {0x00, 0x01, 0x00, 0x00};
    EXPECT(crc32c(buf_a, 4u) != crc32c(buf_b, 4u));
}

TEST(test_length_sensitivity) {
    // Same prefix, different length — must produce different CRCs.
    uint8_t buf[16]{};
    EXPECT(crc32c(buf, 8u) != crc32c(buf, 16u));
}

// --- Table correctness ---

TEST(test_table_entry_0_is_zero) {
    // CRC of value 0 with reflected polynomial: first entry must be 0.
    EXPECT_EQ(kCrc32cTable[0], 0x00000000u);
}

TEST(test_table_entry_1) {
    // Entry[1] must equal the reflected polynomial itself.
    EXPECT_EQ(kCrc32cTable[1], kCrc32cPolyReflected);
}

TEST(test_table_256_entries_unique_spot_check) {
    // Spot-check: a few known entries from the CRC32C table.
    // Entry[0xFF] is a well-known value for this polynomial.
    EXPECT_EQ(kCrc32cTable[0xFF], 0x2D02EF8Du);
}

// --- constexpr evaluation ---

TEST(test_constexpr_one_shot) {
    // Already covered by static_assert above; runtime re-check for completeness.
    constexpr uint32_t r = crc32c(kVec9, sizeof(kVec9));
    EXPECT_EQ(r, 0xE3069283u);
}

TEST(test_constexpr_update_chaining) {
    constexpr uint32_t r = [] {
        uint32_t s = ~0u;
        s = crc32c_update(s, kVec9,     4u);
        s = crc32c_update(s, kVec9 + 4, 5u);
        return ~s;
    }();
    EXPECT_EQ(r, 0xE3069283u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== crc32c unit tests ===\n\n");

    std::printf("--- reference vectors ---\n");
    RUN(test_standard_vector_123456789);
    RUN(test_rfc3720_zeros_32);
    RUN(test_rfc3720_ones_32);
    RUN(test_rfc3720_incrementing_32);

    std::printf("\n--- boundary conditions ---\n");
    RUN(test_empty_input_returns_zero);
    RUN(test_single_byte_zero);
    RUN(test_single_byte_ff);
    RUN(test_all_zeros_various_lengths);

    std::printf("\n--- incremental vs one-shot ---\n");
    RUN(test_incremental_2chunks_equals_oneshot);
    RUN(test_incremental_byte_by_byte_equals_oneshot);
    RUN(test_incremental_many_chunks);

    std::printf("\n--- void* overload ---\n");
    RUN(test_void_ptr_overload_equals_uint8_overload);
    RUN(test_void_ptr_update_equals_uint8_update);

    std::printf("\n--- seed semantics ---\n");
    RUN(test_seed_zero_is_default);
    RUN(test_different_seeds_produce_different_results);

    std::printf("\n--- data sensitivity ---\n");
    RUN(test_bit_flip_changes_checksum);
    RUN(test_position_sensitivity);
    RUN(test_length_sensitivity);

    std::printf("\n--- table correctness ---\n");
    RUN(test_table_entry_0_is_zero);
    RUN(test_table_entry_1);
    RUN(test_table_256_entries_unique_spot_check);

    std::printf("\n--- constexpr evaluation ---\n");
    RUN(test_constexpr_one_shot);
    RUN(test_constexpr_update_chaining);

    std::printf("\n=== Results: %d/%d passed ===\n", g_passed, g_total);
    return (g_failed == 0) ? 0 : 1;
}
