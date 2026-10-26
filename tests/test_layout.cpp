#include <doctest/doctest.h>

#include <array>
#include <vector>

#include "core/layout.h"

using rawreel::centreMinimum;
using rawreel::DockFit;
using rawreel::DockLimit;
using rawreel::fitDocks;
using rawreel::LayoutSize;
using rawreel::minimumClient;

TEST_CASE("layout: the centre minimum stacks the infobar, the viewport and the transport bar") {
    // A transport bar narrower than the viewport: the viewport sets the width.
    CHECK(centreMinimum({320, 240}, {300, 28}, 40) == LayoutSize{320, 308});
    // A wider transport bar sets it; a negative infobar height counts as none.
    CHECK(centreMinimum({320, 240}, {540, 28}, -1) == LayoutSize{540, 268});
    // 150 % scale: every input is scaled, so is the result.
    CHECK(centreMinimum({480, 360}, {810, 42}, 60) == LayoutSize{810, 462});
}

TEST_CASE("layout: the minimum client adds every dock minimum and its sash") {
    const std::array<DockLimit, 2> docks{DockLimit{.acrossWidth = true, .minimum = 162, .sash = 4},
                                         DockLimit{.acrossWidth = false, .minimum = 80, .sash = 4}};
    CHECK(minimumClient({540, 308}, docks) == LayoutSize{706, 392});
    CHECK(minimumClient({540, 308}, {}) == LayoutSize{540, 308});
}

TEST_CASE("layout: the docks keep the user size while they fit") {
    const std::array<DockFit, 1> one{DockFit{.user = 320, .minimum = 162}};
    CHECK(fitDocks(one, 500) == std::vector<int>{320});
    CHECK(fitDocks(one, 320) == std::vector<int>{320});
}

TEST_CASE("layout: the docks give way down to their minimum and grow back") {
    const std::array<DockFit, 1> one{DockFit{.user = 320, .minimum = 162}};
    CHECK(fitDocks(one, 200) == std::vector<int>{200});
    CHECK(fitDocks(one, 100) == std::vector<int>{162});  // never below the minimum
    CHECK(fitDocks(one, 0) == std::vector<int>{162});
    CHECK(fitDocks(one, -50) == std::vector<int>{162});
    CHECK(fitDocks(one, 900) == std::vector<int>{320});  // back to the user size
    // A user size below the minimum (a stale layout) is raised to it.
    const std::array<DockFit, 1> small{DockFit{.user = 10, .minimum = 80}};
    CHECK(fitDocks(small, 500) == std::vector<int>{80});
}

TEST_CASE("layout: several docks of one direction give way in order") {
    const std::array<DockFit, 2> two{DockFit{.user = 300, .minimum = 100},
                                     DockFit{.user = 200, .minimum = 100}};
    CHECK(fitDocks(two, 500) == std::vector<int>{300, 200});
    CHECK(fitDocks(two, 400) == std::vector<int>{200, 200});
    CHECK(fitDocks(two, 250) == std::vector<int>{100, 150});
    CHECK(fitDocks(two, 100) == std::vector<int>{100, 100});
    CHECK(fitDocks({}, 100).empty());
}
