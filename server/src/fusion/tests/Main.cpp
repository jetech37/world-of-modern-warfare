/*
 * Azeroth Warfare: the fusion rules' test runner. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

int main()
{
    int ran = 0;
    for (auto const& c : FusionTest::Cases())
    {
        int before = FusionTest::Failures();
        c.body();
        ++ran;
        std::printf("%s %s\n", FusionTest::Failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", ran, FusionTest::Failures());
    return FusionTest::Failures() == 0 ? 0 : 1;
}
