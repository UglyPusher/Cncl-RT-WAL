#pragma once

namespace wal::internal {

class Backend {
public:
    virtual ~Backend() = default;
};

} // namespace wal::internal
