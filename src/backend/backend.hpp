#pragma once

#include <string>
#include <span>
#include <cstddef>

namespace wal::internal::backend {

    enum BackendMode{
        File,
        RawDevice
    };

    struct BackendConfig {
    std::string target;          // file path / device
    BackendMode mode;             // File / RawDevice / Socket / etc
    size_t      max_batch_bytes;  // batching policy
    bool        fsync_on_commit;  // durability level
};

struct Record;   

class WalBackend {
public:
    virtual ~WalBackend() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    // non-RT only
    virtual void submit_batch(Span<const Record>) = 0;
    virtual void set_degrade(void) = 0;
};