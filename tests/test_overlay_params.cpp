#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/layout.h"
#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "dvv/media/view_lines.h"
#include "gui/overlay_params.h"
#include "render/view_state.h"

using rawreel::GridConfig;
using rawreel::GridLevelParams;
using rawreel::OverlayConversion;
using rawreel::OverlayParams;
using rawreel::OverlayScale;
using rawreel::RulerConfig;

TEST_CASE("overlay params: the defaults at a scale of 2") {
    const OverlayParams params =
        rawreel::convertOverlay(GridConfig{}, RulerConfig{},
                                OverlayScale{.display = {2, 2}, .fontDip = 11}, false)
            .params;
    CHECK(params.levels[0] == GridLevelParams{5, 4.0F, 4, 0xD9FFFFFF, false, false, false});
    CHECK(params.levels[1] == GridLevelParams{3, 8.0F, 2, 0x80FFFFFF, true, false, false});
    CHECK(params.levels[2].minimumSpacing == 6.0F);
    CHECK(params.levels[2].stepLog2 == 1);
    CHECK(params.tickWidth == 2);  // 0.75 pt x 2 = 1.5, half up
    CHECK(params.tickLength == std::array<std::uint32_t, 3>{18, 10, 6});
    CHECK(params.markerDepth == 6);
    CHECK(params.markSize == 12);  // the padding marks: 6 and 1 DIP
    CHECK(params.markRim == 2);
    CHECK(params.numbers);
    CHECK(params.strips == dvv::media::ViewStrips{});  // the rulers are hidden
    CHECK(params.tickBand == 0);
    CHECK_FALSE(params.dark);
    CHECK(rawreel::convertOverlay(GridConfig{}, RulerConfig{}, {.display = {2, 2}}, true)
              .params.dark);
    const OverlayParams one =
        rawreel::convertOverlay(GridConfig{}, RulerConfig{}, {.display = {1, 1}}, false).params;
    CHECK(one.markSize == 6);
    CHECK(one.markRim == 1);
}

TEST_CASE("overlay params: the rulers shown at a Windows scale of 125 %") {
    const OverlayScale scale{.display = {.backingPerDip = 1.25, .contentScale = 1}, .fontDip = 9};
    const RulerConfig rulers{.show = true, .numbers = false};
    const OverlayConversion overlay = rawreel::convertOverlay(GridConfig{}, rulers, scale, false);
    const OverlayParams& params = overlay.params;
    // 10 pt x 1.25 = 12.5 window units, rounded up to whole ones.
    CHECK(params.strips == dvv::media::ViewStrips{.top = 13, .left = 13});
    CHECK(params.tickBand == 13);
    CHECK(params.tickWidth == 1);  // 0.9375 -> 1
    CHECK(params.levels[0].minimumSpacing == rawreel::minimumSpacingBacking(2, 1.25));
    CHECK(params.levels[0].thickness == 3);                              // 2.5, half up
    CHECK(params.tickLength == std::array<std::uint32_t, 3>{11, 6, 4});  // 11.25, 6.25, 3.75
    CHECK(params.markerDepth == 4);                                      // 3.75
    CHECK_FALSE(params.numbers);
    // The one strip: in window units for the GUI, its backing pixels in the params.
    CHECK(overlay.strip == rawreel::rulerStrip(rulers, 9, scale.display));
    CHECK(overlay.strip.windowUnits == 13);
    CHECK(params.strips.top == overlay.strip.backing);
    CHECK(params.tickBand == overlay.strip.tickBand);
}

TEST_CASE("overlay params: the lit tick reaches 4 pt past the tick band, within the strip") {
    const RulerConfig numbers{.show = true, .numbers = true};
    const OverlayScale retina{.display = {2, 2}, .fontDip = 11};
    // 10 + 1.2 x 11 = 23.2 pt: a strip of 24 window units, 48 px, its tick band 20 px.
    const OverlayParams on = rawreel::convertOverlay(GridConfig{}, numbers, retina, false).params;
    CHECK(on.strips.top == 48);
    CHECK(on.tickBand == 20);
    CHECK(on.litDepth == 28);  // 20 + 4 pt x 2
    // A label band thinner than 4 pt: 10 + 1.2 x 1 = 11.2 pt, a strip of 12 window units,
    // 24 px; its label band 24 - 20 = 4 px is less than 4 pt x 2 = 8 px, so the lit tick fills
    // the strip.
    const OverlayParams thin =
        rawreel::convertOverlay(GridConfig{}, numbers, {.display = {2, 2}, .fontDip = 1}, false)
            .params;
    CHECK(thin.strips.top == 24);
    CHECK(thin.litDepth == 24);
    // Numbers off: the strip is the tick band, so the lit tick fills the strip, longer than the
    // major tick.
    for (const OverlayScale& scale :
         {retina, OverlayScale{.display = {.backingPerDip = 1.25, .contentScale = 1}}}) {
        CAPTURE(scale.display.backingPerDip);
        const OverlayParams off =
            rawreel::convertOverlay(GridConfig{}, RulerConfig{.show = true, .numbers = false},
                                    scale, false)
                .params;
        CHECK(off.strips.top == off.tickBand);
        CHECK(off.litDepth == off.strips.top);
        CHECK(off.litDepth > off.tickLength[0]);
    }
    // Rulers hidden: no strip, no lit depth.
    CHECK(rawreel::convertOverlay(GridConfig{}, RulerConfig{}, retina, false).params.litDepth == 0);
}

TEST_CASE("overlay params: colour and opacity pack into one word") {
    GridConfig grid;
    grid.levels[0].colour = {.r = 255, .g = 159, .b = 26};
    grid.levels[0].opacity = 90;  // 229.5, half up
    grid.levels[1].opacity = 0;
    grid.levels[2].opacity = 100;
    const OverlayParams params = rawreel::convertOverlay(grid, RulerConfig{}, {}, false).params;
    CHECK(params.levels[0].colour == 0xE61A9FFF);
    CHECK(params.levels[1].colour == 0x00FFFFFF);
    CHECK(params.levels[2].colour == 0xFFFFFFFF);
}

TEST_CASE("overlay params: the label spacing, the last value and the tag text") {
    // 2, 3, 1 and 2 pt, each half up and at least 1.
    const RulerConfig rulers{.show = true, .lastValue = rawreel::LastValue::Edge};
    const OverlayParams retina =
        rawreel::convertOverlay(GridConfig{}, rulers, {.display = {2, 2}, .fontDip = 11}, false)
            .params;
    CHECK(retina.labelSpacing ==
          rawreel::LabelSpacing{.numberGap = 4, .tagPad = 6, .tagInset = 2, .tagMargin = 4});
    CHECK(retina.lastValue == rawreel::LastValue::Edge);
    CHECK(retina.glyphs == nullptr);  // the GUI adds the strip
    const OverlayParams windows =
        rawreel::convertOverlay(GridConfig{}, RulerConfig{}, {.display = {1.25, 1}, .fontDip = 9},
                                false)
            .params;
    CHECK(windows.labelSpacing ==
          rawreel::LabelSpacing{.numberGap = 3, .tagPad = 4, .tagInset = 1, .tagMargin = 3});
    CHECK(windows.lastValue == rawreel::LastValue::Pixel);
    // The default red cursor takes white text, a light yellow one black.
    CHECK_FALSE(windows.cursorTagTextBlack);
    const RulerConfig yellow{.cursorColour = {.r = 0xff, .g = 0xd6, .b = 0x0a}};
    CHECK(rawreel::convertOverlay(GridConfig{}, yellow, {.display = {1, 1}}, false)
              .params.cursorTagTextBlack);
}

TEST_CASE("convertOverlay packs the cursor colour") {
    const GridConfig grid;
    CHECK(rawreel::convertOverlay(grid, RulerConfig{}, {}, false).params.cursorColour == 0xff3b30U);
    const RulerConfig green{.cursorColour = {.r = 0x34, .g = 0xc7, .b = 0x59}};
    CHECK(rawreel::convertOverlay(grid, green, {}, false).params.cursorColour == 0x34c759U);
}

TEST_CASE("overlay params: every size is at least one backing pixel") {
    // A scale below 1 (or none at all) never rounds a thickness, a tick or the marker to 0.
    for (const double per_dip : {0.0, 0.25, 0.5}) {
        CAPTURE(per_dip);
        const OverlayParams params =
            rawreel::convertOverlay(GridConfig{}, RulerConfig{.show = true},
                                    {.display = {per_dip, 1}}, false)
                .params;
        CHECK(params.tickWidth >= 1);
        CHECK(params.markerDepth >= 1);
        CHECK(params.litDepth >= 1);
        for (std::size_t level = 0; level < params.levels.size(); ++level) {
            CHECK(params.levels[level].thickness >= 1);
            CHECK(params.tickLength[level] >= 1);
        }
        CHECK(params.checkerSize >= 1);
    }
}

TEST_CASE("overlay params: the checker square is 8 DIP in backing pixels") {
    // It follows a DPI or backing-scale change as the grid's thickness does.
    const std::array<std::pair<double, std::uint32_t>, 4> cases{
        {{1.0, 8}, {1.25, 10}, {1.5, 12}, {2.0, 16}}};
    for (const auto& [per_dip, square] : cases) {
        CAPTURE(per_dip);
        CHECK(rawreel::convertOverlay(GridConfig{}, RulerConfig{}, {.display = {per_dip, 1}}, false)
                  .params.checkerSize == square);
    }
    CHECK(OverlayParams{}.checkerSize == 0);
}
