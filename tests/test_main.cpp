// Lightweight test runner for Phoenix's non-Qt core (data model, commands,
// serialization). Kept free of Qt so it can build with just a C++ compiler.
#include "test_util.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}
} // namespace

int TestRegistrar::failures = 0;
int TestRegistrar::checks = 0;

void TestRegistrar::add(const char* name, void (*fn)())
{
    registry().push_back({name, fn});
}

int main(int argc, char** argv)
{
    // An argument runs only the tests whose names contain it.
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int failed = 0;
    int run = 0;
    for (const TestCase& test : registry())
    {
        if (filter && !std::strstr(test.name, filter))
            continue;
        ++run;
        const int before = TestRegistrar::failures;
        test.fn();
        const bool ok = TestRegistrar::failures == before;
        std::printf("%s %s\n", ok ? "[ pass ]" : "[ FAIL ]", test.name);
        if (!ok)
            ++failed;
    }

    std::printf("\n%d/%d tests passed, %d checks\n", run - failed, run, TestRegistrar::checks);

    return failed == 0 ? 0 : 1;
}
