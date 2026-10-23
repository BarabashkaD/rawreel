#include <doctest/doctest.h>

#include <cstdint>

#include "test_pattern.h"

using rawreel::testPatternPixel;

TEST_CASE("testPatternPixel") {
    SUBCASE("a 1x1 image is all border") {
        CHECK(testPatternPixel(0, 0, 1, 1) == 0xFFFFFFFFU);
    }
    SUBCASE("33x17: the corners are border") {
        CHECK(testPatternPixel(0, 0, 33, 17) == 0xFFFFFFFFU);
        CHECK(testPatternPixel(32, 0, 33, 17) == 0xFFFFFFFFU);
        CHECK(testPatternPixel(0, 16, 33, 17) == 0xFFFFFFFFU);
        CHECK(testPatternPixel(32, 16, 33, 17) == 0xFFFFFFFFU);
    }
    SUBCASE("33x17: colour bars, R in the low byte") {
        CHECK(testPatternPixel(1, 1, 33, 17) == 0xFFBFBFBFU);   // bar 0, white
        CHECK(testPatternPixel(5, 1, 33, 17) == 0xFF00BFBFU);   // bar 1, yellow
        CHECK(testPatternPixel(31, 1, 33, 17) == 0xFFBF0000U);  // bar 6, blue
    }
    SUBCASE("33x17: the grey ramp") {
        CHECK(testPatternPixel(1, 15, 33, 17) == 0xFF070707U);   // 255 / 32 = 7
        CHECK(testPatternPixel(31, 15, 33, 17) == 0xFFF7F7F7U);  // 7905 / 32 = 247
    }
    SUBCASE("640x360") {
        CHECK(testPatternPixel(320, 100, 640, 360) == 0xFF00BF00U);  // bar 3, green
        CHECK(testPatternPixel(639, 359, 640, 360) == 0xFFFFFFFFU);  // border
        CHECK(testPatternPixel(638, 300, 640, 360) == 0xFFFEFEFEU);  // 162690 / 639 = 254
    }
}
