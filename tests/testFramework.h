#ifndef TESTFRAMEWORK_H
#define TESTFRAMEWORK_H
// Minimal test harness: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase
{
    const char *name;
    std::function<void()> body;
};

inline std::vector<TestCase> &testRegistry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int &testFailures()
{
    static int failures = 0;
    return failures;
}

struct TestRegistrar
{
    TestRegistrar(const char *name, std::function<void()> body) { testRegistry().push_back({name, std::move(body)}); }
};

#define TEST(name)                                         \
    static void test_##name();                             \
    static TestRegistrar registrar_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            printf("    FAILED %s:%d  %s\n", __FILE__, __LINE__, #condition);         \
            testFailures()++;                                                         \
        }                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        auto a_ = (actual);                                                           \
        auto e_ = (expected);                                                         \
        if (!(a_ == e_)) {                                                            \
            printf("    FAILED %s:%d  %s != %s\n", __FILE__, __LINE__, #actual, #expected); \
            testFailures()++;                                                         \
        }                                                                             \
    } while (0)

#endif // TESTFRAMEWORK_H
