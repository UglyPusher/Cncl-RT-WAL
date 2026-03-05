#include <cstdio>

void taskwrapper_tests();

int main()
{
    std::printf("=== STAM exec tests ===\n");

    taskwrapper_tests();

    std::printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
