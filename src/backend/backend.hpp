#pragma once

namespace wal::internal {

class Backend {
public:
    virtual ~Backend() = default;
    virtual void write(/* batch */) noexcept = 0;
};

} // namespace wal::internal