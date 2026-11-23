#include "testFramework.h"

int main()
{
    for (const TestCase &test : testRegistry()) {
        int before = testFailures();
        test.body();
        printf("%s %s\n", testFailures() == before ? "[ ok ]" : "[FAIL]", test.name);
    }
    printf("\n%zu tests, %d failed checks\n", testRegistry().size(), testFailures());
    return testFailures() == 0 ? 0 : 1;
}
