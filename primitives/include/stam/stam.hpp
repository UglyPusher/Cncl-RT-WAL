#pragma once

//------------------------------------------------------------------------------
// STAM - root namespace control
//
// Goals:
//  - today: plain namespace stam
//  - later: versioned ABI namespace (stam::v1)
//  - no user-code rewrites
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// 1. ABI version configuration
//
// If this is ever needed:
//   #define STAM_ABI_VERSION v1
// before including library headers.
//------------------------------------------------------------------------------

#ifndef STAM_ABI_VERSION
    #define STAM_ABI_VERSION /* empty */
#endif

//------------------------------------------------------------------------------
// 2. Helper macros for inline namespace
//------------------------------------------------------------------------------

#define STAM_DETAIL_NS_CONCAT(a, b) a::b
#define STAM_DETAIL_NS_EVAL(a, b)   STAM_DETAIL_NS_CONCAT(a, b)

//------------------------------------------------------------------------------
// 3. Open / close root namespace
//------------------------------------------------------------------------------

#ifdef STAM_ABI_VERSION

namespace stam {
inline namespace STAM_ABI_VERSION {

#else

namespace stam {

#endif

//------------------------------------------------------------------------------
// 4. Wrapper macros (for codebase consistency)
//------------------------------------------------------------------------------

#define STAM_BEGIN_NAMESPACE \
    namespace stam {         \
    STAM_DETAIL_ABI_NS_BEGIN

#define STAM_END_NAMESPACE   \
    STAM_DETAIL_ABI_NS_END   \
    }


//------------------------------------------------------------------------------
// 5. Namespace closing
//------------------------------------------------------------------------------

#ifdef STAM_ABI_VERSION

} // inline namespace STAM_ABI_VERSION
} // namespace stam

#else

} // namespace stam

#endif
