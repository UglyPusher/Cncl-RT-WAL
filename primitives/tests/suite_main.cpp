#include "test_filter.hpp"

#ifndef STAM_SUITE_FN
#error "STAM_SUITE_FN must be defined (e.g. -DSTAM_SUITE_FN=crc32_tests)"
#endif

#define STAM_SUITE_DECL(fn) int fn()
STAM_SUITE_DECL(STAM_SUITE_FN);
#undef STAM_SUITE_DECL

int main(int argc, char** argv) {
    stam::tests::configure_from_cli(argc, argv);
    return STAM_SUITE_FN();
}

