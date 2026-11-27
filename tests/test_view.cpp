#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <optional>

#include "core/frame_set.h"
#include "core/view.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

using dvv::media::PointD;
using dvv::media::ViewMode;
using dvv::media::ViewRequest;
using dvv::media::zoomAround;
using dvv::platform::PixelSize;
using rawreel::MAX_STREAMS;
using rawreel::nextZoomIn;
using rawreel::nextZoomOut;
using rawreel::physicalPixelSize;
using rawreel::stepZoom;
using rawreel::takeWheelNotches;
using rawreel::wheelPanDip;
using rawreel::ZOOM_MENU_STEPS;
using rawreel::ZOOM_STEPS;
using rawreel::ZoomLimits;
using rawreel::zoomLimits;
using rawreel::ZoomMenuCheck;
using rawreel::zoomMenuChecked;
using rawreel::ZoomMenuEntry;
using rawreel::ZoomStep;
using rawreel::zoomText;

static_assert(MAX_STREAMS == 1);

namespace {

const PixelSize VP{200, 100};
const PixelSize IMG{400, 300};

}  // namespace

TEST_SUITE("core") {
    // macOS: a view's size in points times its layer's scale, rounded to nearest.
    TEST_CASE("view: physicalPixelSize") {
        CHECK(physicalPixelSize(400, 300, 2.0) == PixelSize{800, 600});
        CHECK(physicalPixelSize(400.5, 300, 2.0) == PixelSize{801, 600});
        CHECK(physicalPixelSize(1, 1, 1.5) == PixelSize{2, 2});
        CHECK(physicalPixelSize(0, 300, 2.0).empty());
        // At least one pixel on each axis of a non-empty view.
        CHECK(physicalPixelSize(0.2, 0.2, 1.0) == PixelSize{1, 1});
        CHECK(physicalPixelSize(-4, 300, 2.0).empty());
        CHECK(physicalPixelSize(400, 300, 0.0).empty());
    }

    TEST_CASE("takeWheelNotches") {
        double acc = 0;
        CHECK(takeWheelNotches(acc, 120, 120) == 1);
        CHECK(acc == 0);
        CHECK(takeWheelNotches(acc, 60, 120) == 0);
        CHECK(acc == 60);
        CHECK(takeWheelNotches(acc, 60, 120) == 1);
        CHECK(acc == 0);
        CHECK(takeWheelNotches(acc, -240, 120) == -2);
        CHECK(acc == 0);
        acc = 60;
        CHECK(takeWheelNotches(acc, -30, 120) == 0);
        CHECK(acc == -30);
        acc = 0;
        CHECK(takeWheelNotches(acc, 120, 0) == 0);
    }

    TEST_CASE("wheelPanDip") {
        CHECK(wheelPanDip(120, 120, 3, 16, 500) == doctest::Approx(48));
        CHECK(wheelPanDip(-240, 120, 3, 16, 500) == doctest::Approx(-96));
        CHECK(wheelPanDip(60, 120, 3, 16, 500) == doctest::Approx(24));
        CHECK(wheelPanDip(120, 120, -1, 16, 500) == doctest::Approx(500));
        CHECK(wheelPanDip(10, 10, 1, 10, 500) == doctest::Approx(10));
        CHECK(wheelPanDip(120, 0, 3, 16, 500) == 0);
    }

    TEST_CASE("zoom steps: every step") {
        for (std::size_t i = 0; i + 1 < ZOOM_STEPS.size(); ++i) {
            CHECK(nextZoomIn(ZOOM_STEPS[i], 0.01) == ZoomStep{ZOOM_STEPS[i + 1], false});
            CHECK(nextZoomOut(ZOOM_STEPS[i + 1], 0.01) == ZoomStep{ZOOM_STEPS[i], false});
        }
    }

    TEST_CASE("zoom steps: between steps") {
        CHECK(nextZoomIn(0.47, 0.2) == ZoomStep{0.5, false});
        CHECK(nextZoomOut(0.47, 0.2) == ZoomStep{0.25, false});
        CHECK(nextZoomIn(1.0000001, 0.3) == ZoomStep{2, false});
    }

    TEST_CASE("zoom steps: the fit stop") {
        CHECK(nextZoomOut(1, 0.63) == ZoomStep{0.63, true});
        CHECK(nextZoomOut(0.63, 0.63) == ZoomStep{0.5, false});
        CHECK(nextZoomIn(0.5, 0.63) == ZoomStep{0.63, true});
        CHECK(nextZoomIn(0.63, 0.63) == ZoomStep{1, false});
    }

    TEST_CASE("zoom steps: a fit within 1 % of a step is that step") {
        const double f = 1.0005;
        CHECK(nextZoomIn(1, f) == ZoomStep{2, false});
        CHECK(nextZoomIn(f, f) == ZoomStep{2, false});
        CHECK(nextZoomOut(f, f) == ZoomStep{0.5, false});
        CHECK(nextZoomIn(0.5, f) == ZoomStep{1, false});
        CHECK(nextZoomOut(2, 0.995) == ZoomStep{1, false});
    }

    TEST_CASE("zoom steps: the limits") {
        CHECK(nextZoomIn(32, 0.5) == std::nullopt);
        CHECK(nextZoomOut(0.125, 0.5) == std::nullopt);
        CHECK(nextZoomOut(0.125, 0.01) == ZoomStep{0.01, true});
        CHECK(nextZoomIn(32, 45) == ZoomStep{45, true});
        CHECK(nextZoomIn(45, 45) == std::nullopt);
        CHECK(nextZoomOut(45, 45) == ZoomStep{32, false});
    }

    TEST_CASE("stepZoom") {
        // VP and IMG: the fit is 1/3, a stop between 0.25 and 0.5.
        const PointD q{50, 25};
        const ViewRequest one{ViewMode::Zoom, 1, {200, 150}, IMG};
        CHECK(stepZoom(one, VP, IMG, true, q) == zoomAround(one, VP, IMG, 2, q));
        CHECK(stepZoom(one, VP, IMG, false, q) == zoomAround(one, VP, IMG, 0.5, q));
        const ViewRequest half{ViewMode::Zoom, 0.5, {200, 150}, IMG};
        CHECK(stepZoom(half, VP, IMG, false, q) == ViewRequest{});
        CHECK(stepZoom(ViewRequest{}, VP, IMG, true, q) ==
              zoomAround(ViewRequest{}, VP, IMG, 0.5, q));
        CHECK(stepZoom(ViewRequest{}, VP, IMG, false, q) ==
              zoomAround(ViewRequest{}, VP, IMG, 0.25, q));
        // A request for another frame size resolves as Fit.
        const ViewRequest other{ViewMode::Zoom, 4, {10, 10}, {401, 300}};
        CHECK(stepZoom(other, VP, IMG, true, q) == zoomAround(ViewRequest{}, VP, IMG, 0.5, q));
        CHECK(stepZoom({ViewMode::Zoom, 32, {200, 150}, IMG}, VP, IMG, true, q) == std::nullopt);
        CHECK(stepZoom({ViewMode::Zoom, 0.125, {200, 150}, IMG}, VP, IMG, false, q) ==
              std::nullopt);
    }

    TEST_CASE("zoom limits") {
        CHECK(zoomLimits(0.3) == ZoomLimits{0.125, 32});
        CHECK(zoomLimits(45) == ZoomLimits{0.125, 45});
        CHECK(zoomLimits(0.01) == ZoomLimits{0.01, 32});
    }

    TEST_CASE("view: zoom menu check") {
        for (const double zoom : {0.83, 1.0, 2.0}) {
            CHECK(zoomMenuChecked(ViewMode::Fit, zoom) == ZoomMenuEntry{ZoomMenuCheck::Fit, 0});
        }
        CHECK(zoomMenuChecked(ViewMode::Zoom, 1) == ZoomMenuEntry{ZoomMenuCheck::ActualSize, 0});
        CHECK(zoomMenuChecked(ViewMode::Zoom, 1 + 1e-9) ==
              ZoomMenuEntry{ZoomMenuCheck::ActualSize, 0});
        CHECK(zoomMenuChecked(ViewMode::Zoom, 0.25) == ZoomMenuEntry{ZoomMenuCheck::Step, 0});
        CHECK(zoomMenuChecked(ViewMode::Zoom, 4) == ZoomMenuEntry{ZoomMenuCheck::Step, 3});
        CHECK(zoomMenuChecked(ViewMode::Zoom, 16) == ZoomMenuEntry{ZoomMenuCheck::Step, 5});
        for (const double zoom : {0.125, 32.0, 1.37, 4.001}) {
            CHECK(zoomMenuChecked(ViewMode::Zoom, zoom) == ZoomMenuEntry{});
        }
        CHECK(ZOOM_MENU_STEPS.back() == 16);
        for (const double step : ZOOM_MENU_STEPS) {
            CHECK(step != 1);
            CHECK(std::ranges::find(ZOOM_STEPS, step) != ZOOM_STEPS.end());
        }
    }

    TEST_CASE("zoomText") {
        CHECK(zoomText(ViewMode::Fit, 0.473) == "Fit (47.3 %)");
        CHECK(zoomText(ViewMode::Fit, 0.4996) == "Fit (50 %)");
        CHECK(zoomText(ViewMode::Zoom, 1) == "100 %");
        CHECK(zoomText(ViewMode::Zoom, 2) == "200 %");
        CHECK(zoomText(ViewMode::Zoom, 0.125) == "12.5 %");
        CHECK(zoomText(ViewMode::Zoom, 1.0 / 3) == "33.3 %");
        CHECK(zoomText(ViewMode::Zoom, 1.5) == "150 %");
        CHECK(zoomText(ViewMode::Zoom, 32) == "3200 %");
    }
}
