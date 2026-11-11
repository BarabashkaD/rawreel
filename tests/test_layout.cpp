#include <doctest/doctest.h>

#include <array>
#include <vector>

#include "core/layout.h"

using rawreel::centreMinimum;
using rawreel::DockFit;
using rawreel::DockLimit;
using rawreel::fitDocks;
using rawreel::infoWindowPlace;
using rawreel::LayoutSize;
using rawreel::minimumClient;
using rawreel::ScreenRect;

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

namespace {

bool inside(const ScreenRect& rect, const ScreenRect& area) {
    return rect.x >= area.x && rect.y >= area.y && rect.x + rect.width <= area.x + area.width &&
           rect.y + rect.height <= area.y + area.height;
}

}  // namespace

TEST_CASE("info window place: right of the main window, else inside its right edge") {
    constexpr LayoutSize SIZE{240, 260};
    constexpr int GAP = 8;
    struct Case {
        ScreenRect frame, client, work, expected;
    };
    const std::array<Case, 6> cases{{
        // Room on the right: level with the frame's top, a gap right of its right edge.
        {{100, 100, 800, 600}, {100, 128, 800, 572}, {0, 0, 1920, 1080}, {908, 100, 240, 260}},
        // No room: inside the frame's right edge, a gap below the client top.
        {{100, 100, 800, 600}, {100, 128, 800, 572}, {0, 0, 1000, 1080}, {652, 136, 240, 260}},
        // Maximised: the frame is the work area.
        {{0, 25, 1440, 875}, {0, 53, 1440, 847}, {0, 25, 1440, 875}, {1192, 61, 240, 260}},
        // A display left of the primary (negative coordinates), with room and without.
        {{-1500, 100, 800, 600},
         {-1500, 128, 800, 572},
         {-1920, 0, 1920, 1080},
         {-692, 100, 240, 260}},
        {{-1000, 100, 800, 600},
         {-1000, 128, 800, 572},
         {-1920, 0, 1920, 1080},
         {-448, 136, 240, 260}},
        // A frame partly below and right of the work area: clamped into it.
        {{1500, 900, 800, 600}, {1500, 928, 800, 572}, {0, 0, 1920, 1080}, {1672, 820, 240, 260}},
    }};
    for (const Case& c : cases) {
        CAPTURE(c.frame.x);
        CAPTURE(c.frame.y);
        const ScreenRect place = infoWindowPlace(c.frame, c.client, SIZE, c.work, GAP);
        CHECK(place == c.expected);
        CHECK(inside(place, c.work));
    }
}
