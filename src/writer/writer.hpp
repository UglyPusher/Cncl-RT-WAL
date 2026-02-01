#pragma once

namespace wal::internal {

class Backend;

class Writer {
public:
    explicit Writer(Backend&) noexcept;
    bool push(/* raw record */) noexcept;
};
 
} // namespace wal::internal