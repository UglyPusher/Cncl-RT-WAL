#pragma once
#include <cstdint>


namespace stam::model {

struct PortName {
    uint32_t value = 0;

    constexpr explicit PortName(const char (&s)[5]) noexcept
        : value((uint32_t(s[0]) << 24) |
                (uint32_t(s[1]) << 16) |
                (uint32_t(s[2]) << 8)  |
                uint32_t(s[3]))
    {}

    constexpr bool operator==(PortName rhs) const noexcept {
        return value == rhs.value;
    }
};

enum class BindResult : uint8_t {
    ok,
    payload_has_no_ports,
    unknown_port,
    type_mismatch,
    already_bound,
    reader_limit_exceeded,
};

} // namespace stam::model
