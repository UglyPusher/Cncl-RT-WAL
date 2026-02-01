#pragma once

#include <cstddef>
#include <cstdint>

namespace wal {

struct RecordView;   // forward-only, POD-like
struct SubmitResult; // enum / status, без исключений

class WritersDispatcher final {
public:
    explicit WritersDispatcher(/* config */) noexcept;

    // RT-safe: non-blocking, no allocation, no IO
    bool submit(const RecordView& rec) noexcept;

    // lifecycle
    void flush() noexcept;   // non-RT domain
};

} // namespace wal
