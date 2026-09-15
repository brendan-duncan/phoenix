#pragma once

#include <cmath>
#include <cstdio>

struct TestCase
{
    const char* name;
    void (*fn)();
};

struct TestRegistrar
{
    static int failures;
    static int checks;

    static void add(const char* name, void (*fn)());

    TestRegistrar(const char* name, void (*fn)()) { add(name, fn); }
};

#define TEST(name)                                                   \
    static void name();                                              \
    static TestRegistrar registrar_##name(#name, name);              \
    static void name()

#define CHECK(cond)                                                  \
    do {                                                             \
        ++TestRegistrar::checks;                                     \
        if (!(cond)) {                                               \
            ++TestRegistrar::failures;                               \
            std::printf("    %s:%d: CHECK failed: %s\n",             \
                __FILE__, __LINE__, #cond);                          \
        }                                                            \
    } while (0)

#define CHECK_NEAR(a, b)                                             \
    do {                                                             \
        ++TestRegistrar::checks;                                     \
        const double va = (a), vb = (b);                             \
        if (std::fabs(va - vb) > 1e-9) {                             \
            ++TestRegistrar::failures;                               \
            std::printf("    %s:%d: CHECK_NEAR failed: %s (%g) != %s (%g)\n", \
                __FILE__, __LINE__, #a, va, #b, vb);                 \
        }                                                            \
    } while (0)
