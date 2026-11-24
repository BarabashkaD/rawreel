#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include "core/overlay.h"

using rawreel::applyOverlayStyle;
using rawreel::GRID_LEVELS;
using rawreel::GRID_STEP_VALUES;
using rawreel::GridConfig;
using rawreel::GridLevel;
using rawreel::GridLevelConfig;
using rawreel::gridLevelNote;
using rawreel::labelFits;
using rawreel::labelWidth;
using rawreel::levelHiddenNote;
using rawreel::minimumSpacingBacking;
using rawreel::OverlayStyle;
using rawreel::overlayStyleName;
using rawreel::overlayStyleOf;
using rawreel::Rgb8;
using rawreel::RulerConfig;
using rawreel::rulerOffNote;
using rawreel::rulerTicksShown;
using rawreel::stepsValid;
using rawreel::validSteps;

TEST_SUITE("core") {
    TEST_CASE("overlay: defaults") {
        const GridConfig g;
        CHECK(g.levels[0] == GridLevelConfig{false, 32, 85, {}, 2, false, false, 2});
        CHECK(g.levels[1] == GridLevelConfig{true, 8, 50, {}, 1, false, false, 4});
        CHECK(g.levels[2] == GridLevelConfig{false, 2, 25, {}, 1, false, false, 3});
        CHECK(RulerConfig{} == RulerConfig{.show = false, .numbers = true});
        CHECK(overlayStyleOf(g, RulerConfig{}) == OverlayStyle::Plain);
    }

    TEST_CASE("overlay: the step rules over all 8 x 8 x 8 combinations") {
        for (const auto major : GRID_STEP_VALUES) {
            for (const auto minor : GRID_STEP_VALUES) {
                for (const auto second : GRID_STEP_VALUES) {
                    const bool valid = major >= 2 * minor && minor >= 2 * second;
                    CHECK(stepsValid({major, minor, second}) == valid);
                    // Every listed value is valid and every valid value is listed.
                    for (std::size_t l = 0; l < GRID_LEVELS; ++l) {
                        const auto list =
                            validSteps(static_cast<GridLevel>(l), {major, minor, second});
                        for (const auto v : GRID_STEP_VALUES) {
                            auto s = std::array{major, minor, second};
                            s[l] = v;
                            CHECK((std::ranges::find(list, v) != list.end()) == stepsValid(s));
                        }
                    }
                }
            }
        }
        CHECK(validSteps(GridLevel::Minor, {32, 8, 8}) == std::vector<std::uint32_t>{16});
        CHECK_FALSE(stepsValid({24, 8, 2}));
        CHECK_FALSE(stepsValid({512, 8, 2}));
        CHECK_FALSE(stepsValid({32, 2, 1}));
    }

    TEST_CASE("overlay: styles set colours, opacities, adaptive and halo only") {
        RulerConfig r{.show = true, .numbers = false};
        GridConfig g;
        g.levels[0].thickness = 3;
        g.levels[1].thickness = 2;
        g.levels[0].step = 64;
        g.levels[1].step = 16;
        g.levels[2].step = 4;
        g.levels[0].show = true;
        g.levels[2].minimumSpacing = 7;
        applyOverlayStyle(g, r, OverlayStyle::Halo);
        CHECK(g.levels[0].show);
        CHECK(g.levels[1].show);
        CHECK_FALSE(g.levels[2].show);
        CHECK(g.levels[0].minimumSpacing == 2);
        CHECK(g.levels[2].minimumSpacing == 7);
        CHECK(g.levels[0].thickness == 3);
        CHECK(g.levels[1].thickness == 2);
        CHECK(g.levels[2].thickness == 1);
        CHECK(g.levels[0].step == 64);
        CHECK(g.levels[2].step == 4);
        CHECK(g.levels[0].opacity == 95);
        CHECK(g.levels[1].opacity == 75);
        CHECK(g.levels[2].opacity == 30);
        CHECK(g.levels[0].halo);
        CHECK(g.levels[1].halo);
        CHECK_FALSE(g.levels[2].halo);
        CHECK(overlayStyleOf(g, r) == OverlayStyle::Halo);
        applyOverlayStyle(g, r, OverlayStyle::Coloured);
        CHECK(g.levels[0].colour == Rgb8{255, 159, 26});
        CHECK(g.levels[1].colour == Rgb8{43, 210, 255});
        g.levels[1].opacity = 74;
        CHECK(overlayStyleOf(g, r) == OverlayStyle::Custom);
        applyOverlayStyle(g, r, OverlayStyle::Custom);
        CHECK(g.levels[1].opacity == 74);
        CHECK(r.show);
        CHECK_FALSE(r.numbers);
    }

    TEST_CASE("overlay: styles set the cursor colour") {
        const Rgb8 red{0xff, 0x3b, 0x30};
        const Rgb8 magenta{0xff, 0x2d, 0xd2};
        CHECK(RulerConfig{}.cursorColour == red);
        for (const auto& [style, colour] :
             {std::pair{OverlayStyle::Plain, red}, std::pair{OverlayStyle::Adaptive, red},
              std::pair{OverlayStyle::Coloured, magenta}, std::pair{OverlayStyle::Halo, red}}) {
            CAPTURE(overlayStyleName(style));
            GridConfig g;
            RulerConfig r{.cursorColour = Rgb8{1, 2, 3}};
            applyOverlayStyle(g, r, style);
            CHECK(r.cursorColour == colour);
            CHECK(overlayStyleOf(g, r) == style);
        }
        GridConfig g;
        RulerConfig r{.cursorColour = Rgb8{1, 2, 3}};
        applyOverlayStyle(g, r, OverlayStyle::Custom);
        CHECK(r.cursorColour == Rgb8{1, 2, 3});
    }

    TEST_CASE("overlay: a cursor colour off the style is Custom") {
        const GridConfig g;
        CHECK(overlayStyleOf(g, RulerConfig{.cursorColour = Rgb8{0x34, 0xc7, 0x59}}) ==
              OverlayStyle::Custom);
        // The Plain grid with the Coloured cursor colour is no style.
        CHECK(overlayStyleOf(g, RulerConfig{.cursorColour = Rgb8{0xff, 0x2d, 0xd2}}) ==
              OverlayStyle::Custom);
    }

    TEST_CASE("overlay: every style is recognised after it is applied") {
        for (const OverlayStyle style : {OverlayStyle::Plain, OverlayStyle::Adaptive,
                                         OverlayStyle::Coloured, OverlayStyle::Halo}) {
            GridConfig g;
            RulerConfig r;
            applyOverlayStyle(g, r, style);
            CHECK(overlayStyleOf(g, r) == style);
        }
        GridConfig adaptive;
        RulerConfig r;
        applyOverlayStyle(adaptive, r, OverlayStyle::Adaptive);
        CHECK(adaptive.levels[1].opacity == 55);
        CHECK(std::ranges::all_of(adaptive.levels,
                                  [](const GridLevelConfig& level) { return level.adaptive; }));
        CHECK(overlayStyleName(OverlayStyle::Plain) == "Plain");
        CHECK(overlayStyleName(OverlayStyle::Adaptive) == "Adaptive");
        CHECK(overlayStyleName(OverlayStyle::Coloured) == "Coloured");
        CHECK(overlayStyleName(OverlayStyle::Halo) == "Halo");
        CHECK(overlayStyleName(OverlayStyle::Custom) == "Custom");
    }

    TEST_CASE("overlay: notes") {
        CHECK(levelHiddenNote(2, 1.0, 2.0, 3) ==
              "hidden at this zoom (2 px at 100 % = 1 pt, needs 3 pt)");
        CHECK(levelHiddenNote(8, 1.0, 2.0, 4) == "");
        CHECK(levelHiddenNote(2, 0.5, 2.0, 1) ==
              "hidden at this zoom (2 px at 50 % = 0.5 pt, needs 1 pt)");
        // 32 x 74.9 % = 11.98 pt is hidden, so it never reads as the 12 pt it needs.
        CHECK(levelHiddenNote(32, 0.749, 2.0, 12) ==
              "hidden at this zoom (32 px at 74.9 % = 11.9 pt, needs 12 pt)");
        // A level's line: its own note, else, below a major hidden by its spacing, the major's.
        GridConfig grid;
        CHECK(gridLevelNote(grid, GridLevel::SecondMinor, 1.0, 2.0) ==
              levelHiddenNote(2, 1.0, 2.0, 3));
        CHECK(gridLevelNote(grid, GridLevel::Minor, 1.0, 2.0) == "");
        CHECK(gridLevelNote(grid, GridLevel::Major, 1.0, 2.0) == "");
        grid.levels[0].minimumSpacing = 100;  // 32 < 200: the major is hidden, the minor is not
        CHECK(gridLevelNote(grid, GridLevel::Major, 1.0, 2.0) ==
              levelHiddenNote(32, 1.0, 2.0, 100));
        CHECK(gridLevelNote(grid, GridLevel::Minor, 1.0, 2.0) ==
              "hidden at this zoom (the major lines are hidden)");
        CHECK(gridLevelNote(grid, GridLevel::SecondMinor, 1.0, 2.0) ==
              levelHiddenNote(2, 1.0, 2.0, 3));
        CHECK(minimumSpacingBacking(12, 2.0) == 24.0F);
        CHECK(minimumSpacingBacking(3, 1.25) == 3.75F);
        CHECK(labelWidth(3840, 13.2F) == doctest::Approx(52.8));
        CHECK(labelWidth(0, 10.0F) == 10.0F);
        CHECK(labelFits(26.4F, 13.2F));
        CHECK_FALSE(labelFits(26.5F, 13.2F));
        // 52.8 > 2 x 25.6
        CHECK_FALSE(rulerTicksShown(
            {.majorStepLog2 = 5, .zoom = 0.8F, .minimumSpacing = 24.0F, .labelWidth = 52.8F}));
        CHECK(rulerTicksShown(
            {.majorStepLog2 = 5, .zoom = 0.8F, .minimumSpacing = 24.0F, .labelWidth = 39.6F}));
        // Numbers off: no label width plays a part.
        CHECK(rulerTicksShown({.majorStepLog2 = 5,
                               .zoom = 0.8F,
                               .minimumSpacing = 24.0F,
                               .labelWidth = std::nullopt}));
        // 8 < 24
        CHECK_FALSE(rulerTicksShown({.majorStepLog2 = 5,
                                     .zoom = 0.25F,
                                     .minimumSpacing = 24.0F,
                                     .labelWidth = std::nullopt}));
        const GridConfig g;
        const RulerConfig plain{.show = true, .numbers = false};
        const RulerConfig numbered{.show = true, .numbers = true};
        // 32 px at 6.25 % = 2 px, closer than 2 pt x 2 = 4 px; at 12.5 % the 4 px reach it.
        CHECK(rulerOffNote(g, plain, 0.0625, 2.0, {0, 0}) ==
              "Rulers off at this zoom: major ticks closer than 2 pt");
        CHECK(rulerOffNote(g, numbered, 0.0625, 2.0, {80, 80}) ==
              "Rulers off at this zoom: major ticks closer than 2 pt");
        CHECK(rulerOffNote(g, plain, 0.125, 2.0, {0, 0}) == "");
        CHECK(rulerOffNote(g, numbered, 1.0, 2.0, {60, 60}) == "");  // 60 <= 2 x 32
        CHECK(rulerOffNote(g, numbered, 1.0, 2.0, {80, 60}) ==
              "Top ruler off at this zoom: the numbers do not fit");
        CHECK(rulerOffNote(g, numbered, 1.0, 2.0, {60, 80}) ==
              "Left ruler off at this zoom: the numbers do not fit");
        CHECK(rulerOffNote(g, numbered, 1.0, 2.0, {80, 80}) ==
              "Rulers off at this zoom: the numbers do not fit");
        CHECK(rulerOffNote(g, plain, 1.0, 2.0, {80, 80}) == "");
        CHECK(rulerOffNote(g, {.show = false}, 0.25, 2.0, {0, 0}) == "");
    }
}
