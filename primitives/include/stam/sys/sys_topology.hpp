#pragma once

#include <cstdint>

namespace stam::sys {

#if defined(STAM_SYSTEM_TOPOLOGY_UP) && defined(STAM_SYSTEM_TOPOLOGY_SMP)
#  error "STAM topology config conflict: define only one of STAM_SYSTEM_TOPOLOGY_UP or STAM_SYSTEM_TOPOLOGY_SMP."
#endif

// Compatibility-first default:
// unless SMP is explicitly configured, keep UP/same-core mode.
inline constexpr bool kSystemTopologyIsSmp =
#if defined(STAM_SYSTEM_TOPOLOGY_SMP)
    true;
#else
    false;
#endif

} // namespace stam::sys
