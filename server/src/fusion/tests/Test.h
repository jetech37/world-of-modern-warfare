/*
 * Azeroth Warfare: the fusion rules' test harness. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_TEST_H
#define FUSION_TEST_H

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// A minimal harness: TEST(name) registers a case, CHECK and CHECK_NEAR record failures, main runs
// every case and exits non-zero if any failed. No dependencies, so it builds anywhere the server does.
namespace FusionTest
{
    struct Case
    {
        char const* name;
        std::function<void()> body;
    };

    inline std::vector<Case>& Cases()
    {
        static std::vector<Case> cases;
        return cases;
    }

    inline int& Failures()
    {
        static int failures = 0;
        return failures;
    }

    struct Registrar
    {
        Registrar(char const* name, std::function<void()> body) { Cases().push_back({name, std::move(body)}); }
    };

    inline void Fail(char const* file, int line, std::string const& what)
    {
        ++Failures();
        std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
    }
}

#define FT_CAT2(a, b) a##b
#define FT_CAT(a, b) FT_CAT2(a, b)
#define TEST(name)                                                                              \
    static void FT_CAT(test_, name)();                                                          \
    static FusionTest::Registrar FT_CAT(reg_, name)(#name, FT_CAT(test_, name));                \
    static void FT_CAT(test_, name)()

#define CHECK(cond)                                                                             \
    do { if (!(cond)) FusionTest::Fail(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_NEAR(a, b, eps)                                                                   \
    do {                                                                                        \
        double fa_ = double(a), fb_ = double(b);                                                \
        if (!(std::fabs(fa_ - fb_) <= double(eps)))                                             \
            FusionTest::Fail(__FILE__, __LINE__, std::string(#a " ~= " #b ": ") +               \
                std::to_string(fa_) + " vs " + std::to_string(fb_));                            \
    } while (0)

#endif
