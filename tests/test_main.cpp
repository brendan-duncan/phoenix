// Lightweight test runner for Phoenix's non-Qt core (data model, commands,
// serialization). Kept free of Qt so it can build with just a C++ compiler.
#include "test_util.h"

#include <cstdio>
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

int main()
{
    int failed = 0;
    for (const TestCase& test : registry())
    {
        const int before = TestRegistrar::failures;
        test.fn();
        const bool ok = TestRegistrar::failures == before;
        std::printf("%s %s\n", ok ? "[ pass ]" : "[ FAIL ]", test.name);
        if (!ok)
            ++failed;
    }

    std::printf("\n%d/%d tests passed, %d checks\n",
        static_cast<int>(registry().size()) - failed,
        static_cast<int>(registry().size()),
        TestRegistrar::checks);

    return failed == 0 ? 0 : 1;
}
