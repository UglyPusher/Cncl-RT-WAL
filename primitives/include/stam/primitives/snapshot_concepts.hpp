#pragma once

#include <concepts>

namespace stam::primitives {

// SnapshotWriter<W, T>:
// W is a writer-side handle for snapshot channels carrying values of type T.
template<typename W, typename T>
concept SnapshotWriter = requires(W w, const T& v) {
    { w.write(v) } noexcept -> std::same_as<void>;
};

// SnapshotReader<R, T>:
// R is a reader-side handle for snapshot channels carrying values of type T.
template<typename R, typename T>
concept SnapshotReader = requires(R r, T& out) {
    { r.try_read(out) } noexcept -> std::same_as<bool>;
};

} // namespace stam::primitives
