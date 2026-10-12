// Lightweight test runner for Phoenix's non-Qt core (data model, commands,
// serialization). Kept free of Qt so it can build with just a C++ compiler.
#include "test_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <windows.h>
#endif

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
#ifdef _MSC_VER
    // A failed assertion or abort reports on stderr and ends the run,
    // rather than opening a dialog that waits for someone to click it.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
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
