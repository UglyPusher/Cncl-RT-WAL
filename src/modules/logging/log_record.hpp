#pragma once
#include <cstdint>
#include <type_traits>

namespace wal {

struct LogRecordV2 final {
  uint32_t crc32;        // [0..3]   CRC over bytes [4..63]

  uint8_t  version;      // [4]      format version (start with 2)
  uint8_t  event_type;   // [5]
  uint8_t  flags;        // [6]
  uint8_t  producer_id;  // [7]

  uint64_t global_seq;   // [8..15]  total WAL order

  uint64_t commit_ts;    // [16..23] 100 µs ticks, coordinator time
  uint64_t event_ts;     // [24..31] 100 µs ticks, producer time
  uint64_t producer_seq; // [32..39] local producer order

  uint8_t  reserved[10];  // [40..49]
  uint8_t  payload[14];  // [50..63]
};

static_assert(sizeof(LogRecordV2) == 64);
static_assert(std::is_trivially_copyable_v<LogRecordV2>);
static_assert(alignof(LogRecordV2) >= 8);

} // namespace wal
