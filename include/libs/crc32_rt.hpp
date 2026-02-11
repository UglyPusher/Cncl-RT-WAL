#pragma once

// Requires C++17 (constexpr functions are implicitly inline; inline constexpr variables).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wal {

// CRC32C (Castagnoli) reflected polynomial.
static constexpr uint32_t kCrc32cPolyReflected = 0x82F63B78u;

constexpr uint32_t crc32c_table_entry(uint32_t idx) noexcept {
    uint32_t r = idx;
    for (int k = 0; k < 8; ++k) {
        r = (r & 1u) ? (kCrc32cPolyReflected ^ (r >> 1)) : (r >> 1);
    }
    return r;
}

constexpr std::array<uint32_t, 256> make_crc32c_table() noexcept {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) t[i] = crc32c_table_entry(i);
    return t;
}

// Compile-time table — no runtime init, no allocation.
inline constexpr auto kCrc32cTable = make_crc32c_table();

// ---------------------------------------------------------------------------
// crc32c_update — incremental (streaming) interface
//
// Processes [data, data+len) into the running CRC state.
//
// `state` must be a pre-inverted value:
//   - Start:    uint32_t state = ~seed;   (seed = 0 gives standard 0xFFFFFFFF init)
//   - Continue: pass the value returned by the previous crc32c_update() call.
//   - Finish:   result = ~state;
//
// Example (multi-chunk):
//   uint32_t s = ~0u;
//   s = crc32c_update(s, buf1, len1);
//   s = crc32c_update(s, buf2, len2);
//   uint32_t result = ~s;
//
// Two overloads:
//   - const uint8_t*  — constexpr-compatible (usable in static_assert / consteval).
//   - const void*     — runtime convenience; void* cast is not allowed in constexpr.
// ---------------------------------------------------------------------------
inline constexpr uint32_t crc32c_update(uint32_t state, const uint8_t* data, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        state = kCrc32cTable[(state ^ data[i]) & 0xFFu] ^ (state >> 8);
    }
    return state;
}

inline uint32_t crc32c_update(uint32_t state, const void* data, std::size_t len) noexcept {
    return crc32c_update(state, static_cast<const uint8_t*>(data), len);
}

// ---------------------------------------------------------------------------
// crc32c — one-shot interface
//
// `seed`: initial value BEFORE pre-inversion.
//   seed = 0  →  standard CRC32C (Ethernet-style, init = 0xFFFFFFFF).
//   seed = previous_crc32c_result  →  continue over a previous result
//                                      (non-standard; prefer crc32c_update for chaining).
//
// Returns the final CRC32C checksum.
//
// The const uint8_t* overload is constexpr-compatible.
// The const void*    overload is for runtime use only.
// ---------------------------------------------------------------------------
inline constexpr uint32_t crc32c(const uint8_t* data, std::size_t len, uint32_t seed = 0u) noexcept {
    return ~crc32c_update(~seed, data, len);
}

inline uint32_t crc32c(const void* data, std::size_t len, uint32_t seed = 0u) noexcept {
    return ~crc32c_update(~seed, static_cast<const uint8_t*>(data), len);
}

// ---------------------------------------------------------------------------
// Sanity check: CRC32C("123456789") == 0xE3069283  (standard test vector)
//
// string_view::data() returns const char*, which is pointer-interconvertible
// with const uint8_t* and safe to use in constexpr context via std::bit_cast.
// ---------------------------------------------------------------------------
namespace detail {
    constexpr uint32_t kCrc32cTestVector = [] {
        constexpr std::string_view kInput = "123456789";
        uint32_t state = ~0u;
        for (char c : kInput) {
            state = kCrc32cTable[(state ^ static_cast<uint8_t>(c)) & 0xFFu] ^ (state >> 8);
        }
        return ~state;
    }();
} // namespace detail

static_assert(
    detail::kCrc32cTestVector == 0xE3069283u,
    "CRC32C table or algorithm mismatch — check polynomial and reflection"
);

} // namespace wal