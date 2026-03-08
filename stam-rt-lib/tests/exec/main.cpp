#include <cstdio>

void taskwrapper_tests();
void task_registry_tests();

int main()
{
    std::printf("=== STAM exec tests ===\n");

    taskwrapper_tests();
    task_registry_tests();

    std::printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
