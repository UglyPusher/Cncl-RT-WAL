#pragma once
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include "model/port.hpp"


namespace stam::model {

namespace detail {

template <class Primitive>
concept HasMaxReadersValue =
    requires {
        { Primitive::max_readers } -> std::convertible_to<size_t>;
    };

template <class Primitive>
concept HasMaxReadersFn =
    requires {
        { Primitive::max_readers() } -> std::convertible_to<size_t>;
    };

template <class Primitive>
consteval size_t max_readers_of() {
    if constexpr (HasMaxReadersValue<Primitive>) {
        return static_cast<size_t>(Primitive::max_readers);
    } else if constexpr (HasMaxReadersFn<Primitive>) {
        return static_cast<size_t>(Primitive::max_readers());
    } else {
        return 1u;
    }
}

template <class Primitive>
concept ChannelPrimitive =
    requires(Primitive& p) {
        p.writer();
        p.reader();
    };

} // namespace detail

template <detail::ChannelPrimitive Primitive>
class ChannelWrapper final {
public:
    using primitive_t = Primitive;
    using writer_t = decltype(std::declval<primitive_t&>().writer());
    using reader_t = decltype(std::declval<primitive_t&>().reader());
    static constexpr size_t max_readers = detail::max_readers_of<primitive_t>();
    static_assert(max_readers > 0, "Channel primitive max_readers must be greater than zero.");

    ChannelWrapper() noexcept
        : writer_obj_(primitive_.writer())
    {}

    ChannelWrapper(const ChannelWrapper&) = delete;
    ChannelWrapper& operator=(const ChannelWrapper&) = delete;

    template <class Payload>
    [[nodiscard]] BindResult bind_writer(Payload& payload, PortName name) noexcept {
        if (writer_bound_) {
            fail_fast_bind_error("bind_writer", BindResult::already_bound);
        }

        const BindResult r = payload.bind_port(name, std::move(writer_obj_));
        if (r == BindResult::ok) {
            writer_bound_ = true;
        } else {
            fail_fast_bind_error("bind_writer", r);
        }
        return r;
    }

    template <class Payload>
    [[nodiscard]] BindResult bind_reader(Payload& payload, PortName name) noexcept {
        if (next_reader_idx_ >= max_readers) {
            fail_fast_bind_error("bind_reader", BindResult::reader_limit_exceeded);
        }

        auto reader = primitive_.reader();
        const BindResult r = payload.bind_port(name, std::move(reader));
        if (r == BindResult::ok) {
            ++next_reader_idx_;
        } else {
            fail_fast_bind_error("bind_reader", r);
        }
        return r;
    }

    [[nodiscard]] bool is_fully_bound() const noexcept {
        return writer_bound_ && next_reader_idx_ == max_readers;
    }

private:
    [[noreturn]] static void fail_fast_bind_error(const char* where, BindResult result) noexcept {
        switch (result) {
            case BindResult::payload_has_no_ports:
                assert(false && "ChannelWrapper bind failure: payload has no ports.");
                break;
            case BindResult::unknown_port:
                assert(false && "ChannelWrapper bind failure: unknown port.");
                break;
            case BindResult::type_mismatch:
                assert(false && "ChannelWrapper bind failure: type mismatch.");
                break;
            case BindResult::already_bound:
                assert(false && "ChannelWrapper bind failure: port already bound.");
                break;
            case BindResult::reader_limit_exceeded:
                assert(false && "ChannelWrapper bind failure: reader limit exceeded.");
                break;
            case BindResult::ok:
                assert(false && "ChannelWrapper internal error: fail_fast called with BindResult::ok.");
                break;
        }
        (void)where;
        std::abort();
    }

    primitive_t                         primitive_{};
    writer_t                            writer_obj_;
    size_t                              next_reader_idx_ = 0;
    bool                                writer_bound_ = false;
};

template <detail::ChannelPrimitive Primitive>
using Channel = ChannelWrapper<Primitive>;

} // namespace stam::model
