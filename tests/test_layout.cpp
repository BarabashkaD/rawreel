#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <optional>
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

namespace {

const rawreel::WindowNow NOW{.outer = {100, 100, 1000, 800},
                             .client = {1000, 772},
                             .viewport = {800, 500},
                             .minimumClient = {950, 500}};
const rawreel::ScreenRect WORK{0, 0, 1920, 1080};

}  // namespace

TEST_CASE("window: exact") {
    const auto r = rawreel::windowForViewport(NOW, {900, 600}, WORK, rawreel::WindowSizing::Exact);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{100, 100, 1100, 900}));
}

TEST_CASE("window: below the minimum") {
    const auto r = rawreel::windowForViewport(NOW, {64, 48}, WORK, rawreel::WindowSizing::Exact);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{100, 100, 950, 528}));
}

TEST_CASE("window: too large for the work area") {
    CHECK_FALSE(rawreel::windowForViewport(NOW, {3000, 2000}, WORK, rawreel::WindowSizing::Exact));
    const auto r =
        rawreel::windowForViewport(NOW, {3000, 2000}, WORK, rawreel::WindowSizing::Clamped);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{0, 0, 1920, 1080}));
}

TEST_CASE("window: moved back inside") {
    rawreel::WindowNow now = NOW;
    now.outer = {1000, 500, 1000, 800};
    const auto r = rawreel::windowForViewport(now, {900, 600}, WORK, rawreel::WindowSizing::Exact);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{820, 180, 1100, 900}));
}

TEST_CASE("window: a display left of the primary") {
    rawreel::WindowNow now = NOW;
    now.outer = {-1500, 100, 1000, 800};
    const auto r = rawreel::windowForViewport(now, {900, 600}, {-1920, 0, 1920, 1080},
                                              rawreel::WindowSizing::Exact);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{-1500, 100, 1100, 900}));
}

TEST_CASE("window: keep the clip's shape") {
    const auto r =
        rawreel::windowForViewport(NOW, {3840, 2160}, WORK, rawreel::WindowSizing::KeepShape);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{167, 0, 1586, 1080}));
}

TEST_CASE("window: keep the shape of a portrait clip, never below the minimum") {
    const auto r =
        rawreel::windowForViewport(NOW, {1080, 1920}, WORK, rawreel::WindowSizing::KeepShape);
    CHECK(r == std::optional<rawreel::ScreenRect>(rawreel::ScreenRect{485, 0, 950, 1080}));
}

TEST_CASE("window: nothing for an empty size or an empty work area") {
    for (const rawreel::WindowSizing sizing :
         {rawreel::WindowSizing::Exact, rawreel::WindowSizing::Clamped,
          rawreel::WindowSizing::KeepShape}) {
        CHECK_FALSE(rawreel::windowForViewport(NOW, {0, 600}, WORK, sizing));
        CHECK_FALSE(rawreel::windowForViewport(NOW, {900, 0}, WORK, sizing));
        CHECK_FALSE(rawreel::windowForViewport(NOW, {900, 600}, {0, 0, 0, 0}, sizing));
        CHECK_FALSE(rawreel::windowForViewport(NOW, {900, 600}, {0, 0, 1920, 0}, sizing));
    }
}

TEST_CASE("window: keep the shape when the frame leaves no room") {
    // NOW's window adds 200 x 300 window units around the viewport.
    CHECK_FALSE(rawreel::windowForViewport(NOW, {3840, 2160}, {0, 0, 200, 1080},
                                           rawreel::WindowSizing::KeepShape));
    CHECK_FALSE(rawreel::windowForViewport(NOW, {3840, 2160}, {0, 0, 1920, 300},
                                           rawreel::WindowSizing::KeepShape));
}

TEST_CASE("viewport for a clip: a scale <= 0 counts as 1") {
    for (const double scale : {0.0, -2.0, std::numeric_limits<double>::quiet_NaN()}) {
        CAPTURE(scale);
        CHECK(rawreel::viewportForClip({1920, 1080}, scale) == rawreel::LayoutSize{1920, 1080});
    }
}

TEST_CASE("viewport for a clip rounds up") {
    CHECK(rawreel::viewportForClip({1921, 1081}, 2.0) == rawreel::LayoutSize{961, 541});
    CHECK(rawreel::viewportForClip({1920, 1080}, 2.0) == rawreel::LayoutSize{960, 540});
    CHECK(rawreel::viewportForClip({1920, 1080}, 1.0) == rawreel::LayoutSize{1920, 1080});
}
