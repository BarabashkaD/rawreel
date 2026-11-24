#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "core/layout.h"

using rawreel::centreMinimum;
using rawreel::DockFit;
using rawreel::DockLimit;
using rawreel::fitDocks;
using rawreel::floatingWindowPlace;
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

bool overlap(const ScreenRect& a, const ScreenRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

}  // namespace

TEST_CASE("layout: floatingWindowPlace alone") {
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
        const ScreenRect place =
            floatingWindowPlace(c.frame, c.client, SIZE, c.work, std::nullopt, GAP);
        CHECK(place == c.expected);
        CHECK(inside(place, c.work));
    }
}

TEST_CASE("layout: floatingWindowPlace beside another window") {
    const ScreenRect work{0, 0, 1920, 1080}, frame{400, 100, 800, 600}, client{400, 128, 800, 572};
    const LayoutSize size{240, 260};
    const ScreenRect info{1208, 100, 240, 260};
    CHECK(floatingWindowPlace(frame, client, size, work, info, 8) ==
          ScreenRect{1208, 368, 240, 260});  // below
    const ScreenRect low{1208, 900, 240, 260};
    CHECK(floatingWindowPlace(frame, client, size, work, low, 8) ==
          ScreenRect{152, 100, 240, 260});    // left of the frame
    const ScreenRect wide{0, 0, 1920, 1080};  // maximised player, other inside its right edge,
                                              // no room below
    CHECK(floatingWindowPlace(wide, {0, 28, 1920, 1052}, size, work,
                              ScreenRect{1672, 836, 240, 260}, 8) == ScreenRect{8, 36, 240, 260});
}

TEST_CASE("layout: floatingWindowPlace in either order") {
    constexpr int GAP = 8;
    constexpr LayoutSize INFO{240, 260};
    constexpr LayoutSize GRID{300, 320};
    struct Case {
        const char* name;
        ScreenRect frame, client, work;
        ScreenRect info, gridAfterInfo;  // Info first, then the Grid and Rulers window
        ScreenRect grid, infoAfterGrid;  // the other order
    };
    const std::array<Case, 5> cases{{
        {"room right of the player: below the first window",
         {400, 100, 800, 600},
         {400, 128, 800, 572},
         {0, 0, 1920, 1080},
         {1208, 100, 240, 260},
         {1208, 368, 300, 320},
         {1208, 100, 300, 320},
         {1208, 428, 240, 260}},
        {"no room right: the first inside the player's right edge, the second below it",
         {1000, 100, 800, 600},
         {1000, 128, 800, 572},
         {0, 0, 1920, 1080},
         {1552, 136, 240, 260},
         {1552, 404, 300, 320},
         {1492, 136, 300, 320},
         {1492, 464, 240, 260}},
        {"no room below: left of the player",
         {1000, 100, 800, 600},
         {1000, 128, 800, 572},
         {0, 0, 1920, 700},
         {1552, 136, 240, 260},
         {692, 100, 300, 320},
         {1492, 136, 300, 320},
         {752, 100, 240, 260}},
        // The wider Grid window does not fit below the Info window at the right edge, so it goes
        // inside the left edge; the narrower Info window fits below the Grid window.
        {"maximised player",
         {0, 0, 1920, 1080},
         {0, 28, 1920, 1052},
         {0, 0, 1920, 1080},
         {1672, 36, 240, 260},
         {8, 36, 300, 320},
         {1612, 36, 300, 320},
         {1612, 364, 240, 260}},
        {"a display left of the primary",
         {-1000, 100, 800, 600},
         {-1000, 128, 800, 572},
         {-1920, 0, 1920, 1080},
         {-448, 136, 240, 260},
         {-448, 404, 300, 320},
         {-508, 136, 300, 320},
         {-508, 464, 240, 260}},
    }};
    for (const Case& c : cases) {
        CAPTURE(c.name);
        const ScreenRect info = floatingWindowPlace(c.frame, c.client, INFO, c.work, {}, GAP);
        CHECK(info == c.info);
        const ScreenRect grid_after =
            floatingWindowPlace(c.frame, c.client, GRID, c.work, info, GAP);
        CHECK(grid_after == c.gridAfterInfo);
        const ScreenRect grid = floatingWindowPlace(c.frame, c.client, GRID, c.work, {}, GAP);
        CHECK(grid == c.grid);
        const ScreenRect info_after =
            floatingWindowPlace(c.frame, c.client, INFO, c.work, grid, GAP);
        CHECK(info_after == c.infoAfterGrid);
        for (const ScreenRect& place : {info, grid_after, grid, info_after}) {
            CHECK(inside(place, c.work));
        }
        CHECK_FALSE(overlap(info, grid_after));
        CHECK_FALSE(overlap(grid, info_after));
    }
}

TEST_CASE("layout: floatingWindowPlace falls back inside the left edge, overlapping") {
    // A player narrower than both windows on a narrow display: nothing outside the player fits, so
    // the second window goes inside its left edge over the first; both stay in the work area.
    const ScreenRect work{0, 0, 400, 700}, frame{0, 100, 300, 500}, client{0, 128, 300, 472};
    const ScreenRect info = floatingWindowPlace(frame, client, {240, 260}, work, {}, 8);
    CHECK(info == ScreenRect{52, 136, 240, 260});  // inside the right edge
    const ScreenRect grid = floatingWindowPlace(frame, client, {300, 320}, work, info, 8);
    CHECK(grid == ScreenRect{8, 136, 300, 320});  // inside the left edge
    CHECK(overlap(info, grid));
    CHECK(inside(info, work));
    CHECK(inside(grid, work));
}

TEST_CASE("layout: floatingWindowPlace clamps the last choice into the work area") {
    // A small maximised player: neither below the other window nor left of the player fits, and
    // the window is wider than the work area, so its left edge stays inside it.
    const ScreenRect work{0, 0, 600, 500};
    const ScreenRect frame = work, client{0, 28, 600, 472};
    const ScreenRect info{352, 36, 240, 260};
    CHECK(floatingWindowPlace(frame, client, {240, 260}, work, {}, 8) == info);
    CHECK(floatingWindowPlace(frame, client, {700, 320}, work, info, 8) ==
          ScreenRect{0, 36, 700, 320});
    // A player partly off the work area's left edge: inside the work area's left edge.
    CHECK(floatingWindowPlace({-100, 100, 600, 380}, {-100, 128, 600, 352}, {240, 260}, work,
                              ScreenRect{252, 100, 240, 260}, 8) == ScreenRect{8, 136, 240, 260});
}

TEST_CASE("layout: rulerStrip") {
    using rawreel::RulerStrip;
    using rawreel::rulerStrip;
    CHECK(rulerStrip({.show = false}, 11, {2, 2}) == RulerStrip{});
    CHECK(rulerStrip({.show = true, .numbers = false}, 11, {2, 2}) == RulerStrip{10, 20, 20});
    CHECK(rulerStrip({.show = true, .numbers = true}, 11, {2, 2}) ==
          RulerStrip{37, 74, 20});  // 36.4 pt
    CHECK(rulerStrip({.show = true, .numbers = true}, 12, {1.25, 1}) ==
          RulerStrip{49, 49, 13});  // 38.8 pt x 1.25
    CHECK(rulerStrip({.show = true, .numbers = false}, 12, {1.5, 1}) == RulerStrip{15, 15, 15});
    CHECK(rulerStrip({.show = true, .numbers = false}, 12, {1, 1}) == RulerStrip{10, 10, 10});
    // Windows 125 % and 150 % with a 9 pt font: 31.6 pt.
    CHECK(rulerStrip({.show = true, .numbers = true}, 9, {1.25, 1}) == RulerStrip{40, 40, 13});
    CHECK(rulerStrip({.show = true, .numbers = true}, 9, {1.5, 1}) == RulerStrip{48, 48, 15});
    // A scale that is not positive counts as 1, a font size that is not positive as 0.
    for (const rawreel::DisplayScale scale :
         {rawreel::DisplayScale{0, 0}, rawreel::DisplayScale{-1, std::nan("")},
          rawreel::DisplayScale{std::numeric_limits<double>::infinity(), 1}}) {
        CHECK(rulerStrip({.show = true, .numbers = false}, 12, scale) == RulerStrip{10, 10, 10});
    }
    CHECK(rulerStrip({.show = true, .numbers = true}, -3, {1, 1}) == RulerStrip{10, 10, 10});
    CHECK(rulerStrip({.show = true, .numbers = true}, std::nan(""), {1, 1}) ==
          RulerStrip{10, 10, 10});
}

TEST_CASE("layout: rulerStrip keeps a rounding error on the whole unit and caps the strip") {
    using rawreel::RulerStrip;
    using rawreel::rulerStrip;
    // 100 pt at 110 %: the product lands a rounding error above 110 (and above 55 at a content
    // scale of 2), which stays on the whole unit.
    CHECK(rulerStrip({.show = true, .numbers = true}, 37.5, {1.1, 1}) == RulerStrip{110, 110, 11});
    CHECK(rulerStrip({.show = true, .numbers = true}, 37.5, {1.1, 2}) == RulerStrip{55, 110, 11});
    // No strip is wider than 65536 window units, at any font size.
    CHECK(rulerStrip({.show = true, .numbers = true}, 1e6, {1, 1}) == RulerStrip{65536, 65536, 10});
    CHECK(rulerStrip({.show = true, .numbers = true}, 1e6, {2, 2}) ==
          RulerStrip{65536, 131072, 20});
    CHECK(rulerStrip({.show = true, .numbers = true}, 1e308, {3, 3}) ==
          RulerStrip{65536, 196608, 30});
}

TEST_CASE("layout: the ruler strip is whole window units at every scale") {
    using rawreel::DisplayScale;
    // Windows 100 to 200 % (window units are backing pixels), macOS 1x and 2x, GTK 1x to 3x
    // (whole content scales).
    const std::array<DisplayScale, 7> scales{
        {{1, 1}, {1.25, 1}, {1.5, 1}, {1.75, 1}, {2, 1}, {2, 2}, {3, 3}}};
    constexpr LayoutSize CLIP{641, 361};
    for (const DisplayScale scale : scales) {
        for (const bool numbers : {false, true}) {
            for (const double font : {9.0, 11.0, 12.0, 13.0}) {
                CAPTURE(scale.backingPerDip);
                CAPTURE(scale.contentScale);
                CAPTURE(numbers);
                CAPTURE(font);
                const rawreel::RulerStrip strip =
                    rawreel::rulerStrip({.show = true, .numbers = numbers}, font, scale);
                const double wanted_backing =
                    (rawreel::RULER_TICK_BAND_DIP +
                     (numbers ? 2 * rawreel::RULER_LABEL_LINE * font : 0)) *
                    scale.backingPerDip;
                // The fewest whole window units that hold the strip.
                CHECK(strip.windowUnits * scale.contentScale >= wanted_backing - 1e-6);
                CHECK((strip.windowUnits - 1) * scale.contentScale < wanted_backing);
                CHECK(strip.backing == static_cast<std::uint32_t>(
                                           std::lround(strip.windowUnits * scale.contentScale)));
                CHECK(strip.tickBand <= strip.backing);
                CHECK(strip.tickBand ==
                      std::min(strip.backing,
                               static_cast<std::uint32_t>(std::lround(10 * scale.backingPerDip))));
                // A viewport of the clip's size plus the strips leaves the picture region the
                // viewport for the clip alone: P = viewport - strips exactly.
                const LayoutSize viewport = rawreel::viewportForClip(CLIP, scale.contentScale);
                const auto backing_of = [&](int units) {
                    return std::lround(units * scale.contentScale);
                };
                CHECK(backing_of(viewport.width + strip.windowUnits) - strip.backing ==
                      backing_of(viewport.width));
                CHECK(backing_of(viewport.height + strip.windowUnits) - strip.backing ==
                      backing_of(viewport.height));
                if (scale.contentScale == 1) {
                    CHECK(backing_of(viewport.width) == CLIP.width);
                    CHECK(backing_of(viewport.height) == CLIP.height);
                }
            }
        }
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

TEST_CASE("layout: a viewport for a clip beside the ruler strips keeps P at least the clip") {
    using rawreel::RulerConfig;
    using rawreel::RulerStrip;
    // The main window's rule for 100 %: viewportForClip(clip) plus the strips' window units. With
    // a physical size of round(window units x scale), half up, P = round(viewport x scale) - the
    // strip's backing pixels is the clip rounded up to whole window units at an integer scale; at
    // a fractional one (wxGTK may report one) it is at least the clip and less than scale + 1
    // pixels larger, so the clip always shows whole at 100 %.
    for (const double scale : {1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 3.0}) {
        for (const bool numbers : {false, true}) {
            const RulerStrip strip =
                rawreel::rulerStrip(RulerConfig{.show = true, .numbers = numbers}, 11,
                                    {.backingPerDip = scale, .contentScale = scale});
            for (int clip = 1; clip <= 400; ++clip) {
                CAPTURE(scale);
                CAPTURE(clip);
                const int viewport =
                    rawreel::viewportForClip({clip, clip}, scale).width + strip.windowUnits;
                const int picture = static_cast<int>(std::lround(viewport * scale)) -
                                    static_cast<int>(strip.backing);
                CHECK(picture >= clip);
                CHECK(picture < clip + scale + 1);
                if (scale == std::floor(scale)) {
                    CHECK(picture ==
                          ((clip + static_cast<int>(scale) - 1) / static_cast<int>(scale)) *
                              static_cast<int>(scale));
                }
            }
        }
    }
}
