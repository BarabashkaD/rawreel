#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "core/view.h"

using dvv::platform::PixelSize;
using rawreel::canPan;
using rawreel::imagePointAt;
using rawreel::MAX_STREAMS;
using rawreel::nextZoomIn;
using rawreel::nextZoomOut;
using rawreel::panBy;
using rawreel::physicalPixelSize;
using rawreel::PointD;
using rawreel::resolveView;
using rawreel::stepZoom;
using rawreel::takeWheelNotches;
using rawreel::ViewMode;
using rawreel::ViewRequest;
using rawreel::ViewTransform;
using rawreel::wheelPanDip;
using rawreel::ZOOM_STEPS;
using rawreel::zoomAround;
using rawreel::ZoomLimits;
using rawreel::zoomLimits;
using rawreel::ZoomStep;
using rawreel::zoomText;

static_assert(MAX_STREAMS == 1);

namespace {

double offsetX(const ViewTransform& t) {
    return -t.origin().x * t.zoom();
}
double offsetY(const ViewTransform& t) {
    return -t.origin().y * t.zoom();
}

// The image column and row of an output pixel (the compositor's rule, design §4.4).
double column(const ViewTransform& t, int px) {
    return std::floor(t.origin().x + (px + 0.5) / t.zoom());
}
double row(const ViewTransform& t, int py) {
    return std::floor(t.origin().y + (py + 0.5) / t.zoom());
}

const PixelSize VP{200, 100};
const PixelSize IMG{400, 300};

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("view: pillarbox upscale (37x23 in 211x97)") {
        const auto t = ViewTransform::fit({211, 97}, {37, 23});
        CHECK(t.zoom() == 97.0 / 23.0);
        CHECK(t.origin().x == -27.0 / (97.0 / 23.0));
        CHECK(t.origin().y == 0.0);
        CHECK(std::abs(offsetX(t) - 27.0) <= 1e-9);
        CHECK(offsetY(t) == 0.0);
        CHECK(t.viewport() == PixelSize{211, 97});
        CHECK(t.image() == PixelSize{37, 23});
    }

    TEST_CASE("view: letterbox upscale (64x48 in 400x400)") {
        const auto t = ViewTransform::fit({400, 400}, {64, 48});
        CHECK(t.zoom() == 400.0 / 64.0);
        CHECK(t.origin().x == 0.0);
        CHECK(t.origin().y == -50.0 / (400.0 / 64.0));
        CHECK(std::abs(offsetY(t) - 50.0) <= 1e-9);
    }

    TEST_CASE("view: 1:1 and the tie") {
        const auto t = ViewTransform::fit({37, 23}, {37, 23});
        CHECK(t.zoom() == 1.0);
        CHECK(t.origin() == PointD{0, 0});
    }

    TEST_CASE("view: downscale (1920x1080 in 640x480)") {
        const auto t = ViewTransform::fit({640, 480}, {1920, 1080});
        CHECK(t.zoom() == 640.0 / 1920.0);
        CHECK(t.origin().x == 0.0);
        CHECK(t.origin().y == -60.0 / (640.0 / 1920.0));
        CHECK(std::abs(offsetY(t) - 60.0) <= 1e-9);
    }

    TEST_CASE("view: integer zoom covers whole pixels (37x23 in 74x80)") {
        const auto t = ViewTransform::fit({74, 80}, {37, 23});
        CHECK(t.zoom() == 2.0);
        CHECK(t.origin().x == 0.0);
        CHECK(t.origin().y == -8.5);
        for (int px = 0; px < 74; ++px) {
            CHECK(column(t, px) == px / 2);
        }
        for (int py = 0; py < 80; ++py) {
            if (py < 17 || py >= 63) {
                CHECK((row(t, py) < 0 || row(t, py) >= 23));
            } else {
                CHECK(row(t, py) == (py - 17) / 2);
            }
        }
    }

    TEST_CASE("view: an odd remainder is floored (10x5 in 30x20)") {
        const auto t = ViewTransform::fit({30, 20}, {10, 5});
        CHECK(t.zoom() == 3.0);
        CHECK(t.origin().y == -2.0 / 3.0);
        for (int py = 0; py < 20; ++py) {
            if (py >= 2 && py < 17) {
                CHECK(row(t, py) == (py - 2) / 3);
            } else {
                CHECK((row(t, py) < 0 || row(t, py) >= 5));
            }
        }
        for (int px = 0; px < 30; ++px) {
            CHECK(column(t, px) == px / 3);
        }
    }

    TEST_CASE("view: 64-bit products (70000x70000 in 70000x1)") {
        const auto t = ViewTransform::fit({70000, 1}, {70000, 70000});
        CHECK(t.zoom() == 1.0 / 70000.0);
        CHECK(t.origin().y == 0.0);
        CHECK(t.origin().x == -34999.0 / (1.0 / 70000.0));
    }

    TEST_CASE("view: empty sizes") {
        const ViewTransform none{};
        CHECK(none.zoom() == 0.0);
        CHECK(none.origin() == PointD{0, 0});
        CHECK(none.viewport() == PixelSize{0, 0});
        CHECK(none.image() == PixelSize{0, 0});
        const PixelSize image{37, 23};
        for (const PixelSize viewport : {PixelSize{0, 0}, PixelSize{211, 0}}) {
            const auto t = ViewTransform::fit(viewport, image);
            CHECK(t.zoom() == 0.0);
            CHECK(t.origin() == PointD{0, 0});
            CHECK(t.viewport() == viewport);
            CHECK(t.image() == image);
        }
        const auto t = ViewTransform::fit({211, 97}, {0, 23});
        CHECK(t.zoom() == 0.0);
        CHECK(t.origin() == PointD{0, 0});
        CHECK(t.viewport() == PixelSize{211, 97});
        CHECK(t.image() == PixelSize{0, 23});
    }

    // A no-frame record is always Fit (RecordedInfo's rule).
    TEST_CASE("view: zoomed with an empty size or a zoom <= 0 is Fit at zoom 0") {
        for (const double zoom : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
            CAPTURE(zoom);
            const auto t = ViewTransform::zoomed(VP, IMG, zoom, {200, 150});
            CHECK(t.zoom() == 0.0);
            CHECK(t.origin() == PointD{0, 0});
            CHECK(t.mode() == ViewMode::Fit);
            CHECK(t.viewport() == VP);
            CHECK(t.image() == IMG);
        }
        for (const auto& [viewport, image] :
             {std::pair{PixelSize{0, 0}, IMG}, std::pair{VP, PixelSize{0, 23}}}) {
            const auto t = ViewTransform::zoomed(viewport, image, 2, {200, 150});
            CHECK(t.zoom() == 0.0);
            CHECK(t.origin() == PointD{0, 0});
            CHECK(t.mode() == ViewMode::Fit);
        }
    }

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

    TEST_CASE("resolve: zoom with overflow on both axes") {
        const auto t = resolveView({ViewMode::Zoom, 1, {200, 150}, IMG}, VP, IMG);
        CHECK(t.zoom() == 1);
        CHECK(t.origin() == PointD{100, 100});
        CHECK(t.mode() == ViewMode::Zoom);
    }

    TEST_CASE("resolve: the clamp at each edge") {
        CHECK(resolveView({ViewMode::Zoom, 1, {0, 0}, IMG}, VP, IMG).origin() == PointD{0, 0});
        CHECK(resolveView({ViewMode::Zoom, 1, {400, 300}, IMG}, VP, IMG).origin() ==
              PointD{200, 200});
    }

    TEST_CASE("resolve: no overflow is centred") {
        const PixelSize img{50, 40};
        CHECK(resolveView({ViewMode::Zoom, 2, {0, 0}, img}, VP, img).origin() == PointD{-25, -5});
    }

    TEST_CASE("resolve: overflow on one axis") {
        const PixelSize img{80, 60};
        CHECK(resolveView({ViewMode::Zoom, 2, {40, 30}, img}, VP, img).origin() == PointD{-10, 5});
    }

    TEST_CASE("resolve: whole-pixel offsets") {
        const PixelSize vp{201, 101};
        const PixelSize img{100, 50};
        const auto t = resolveView({ViewMode::Zoom, 3, {50.2, 25.1}, img}, vp, img);
        CHECK(offsetX(t) == doctest::Approx(-50));
        CHECK(offsetY(t) == doctest::Approx(-25));
    }

    TEST_CASE("resolve: a request for another frame size is Fit") {
        const PixelSize drawn{401, 300};
        const auto t = resolveView({ViewMode::Zoom, 1, {200, 150}, IMG}, VP, drawn);
        CHECK(t == ViewTransform::fit(VP, drawn));
        CHECK(t.mode() == ViewMode::Fit);
    }

    TEST_CASE("resolve: Fit and empty sizes") {
        CHECK(resolveView(ViewRequest{}, VP, IMG) == ViewTransform::fit(VP, IMG));
        const auto t = resolveView({ViewMode::Zoom, 1, {200, 150}, IMG}, {0, 0}, IMG);
        CHECK(t.zoom() == 0);
        CHECK(t.origin() == PointD{0, 0});
        CHECK(t.mode() == ViewMode::Fit);
    }

    TEST_CASE("imagePointAt inverts resolve") {
        const auto t = resolveView({ViewMode::Zoom, 1, {200, 150}, IMG}, VP, IMG);
        CHECK(imagePointAt(t, {100, 50}) == PointD{200, 150});
        CHECK(imagePointAt(t, {0, 0}) == t.origin());
    }

    TEST_CASE("zoomAround keeps the anchor's clip point") {
        const ViewRequest r{ViewMode::Zoom, 1, {200, 150}, IMG};
        const auto z = zoomAround(r, VP, IMG, 2, {50, 25});
        CHECK(z.zoom == 2);
        CHECK(z.centre == PointD{175, 137.5});
        const auto t = resolveView(z, VP, IMG);
        CHECK(imagePointAt(t, {50, 25}) == PointD{150, 125});
        const auto c = resolveView(zoomAround(r, VP, IMG, 2, {100, 50}), VP, IMG);
        CHECK(imagePointAt(c, {100, 50}) == PointD{200, 150});
    }

    TEST_CASE("zoomAround from Fit and at the edges") {
        const auto z = zoomAround(ViewRequest{}, VP, IMG, 0.5, {100, 50});
        CHECK(z.mode == ViewMode::Zoom);
        CHECK(z.zoom == 0.5);
        CHECK(z.centre == PointD{200, 150});
        CHECK(z.image == IMG);
        CHECK(zoomAround({ViewMode::Zoom, 2, {60, 40}, IMG}, VP, IMG, 1, {200, 100}).centre ==
              PointD{100, 50});
    }

    TEST_CASE("zoomAround from a request for another frame size starts from the fit") {
        // The drawn frame is IMG, so the request resolves as Fit (zoom 1/3, origin (-99, 0)).
        const ViewRequest other{ViewMode::Zoom, 4, {10, 10}, {401, 300}};
        const auto z = zoomAround(other, VP, IMG, 1, {100, 50});
        CHECK(z == zoomAround(ViewRequest{}, VP, IMG, 1, {100, 50}));
        CHECK(z.mode == ViewMode::Zoom);
        CHECK(z.image == IMG);
        CHECK(z.centre == PointD{201, 150});
    }

    TEST_CASE("zoomAround with a resolved zoom of 0 keeps the request's centre") {
        const ViewRequest r{ViewMode::Zoom, 2, {-5, 999}, IMG};
        const auto z = zoomAround(r, {0, 0}, IMG, 4, {0, 0});
        CHECK(z.zoom == 4);
        CHECK(z.centre == PointD{-5, 999});
    }

    TEST_CASE("panBy leaves a request for another frame size or a zoom of 0") {
        const ViewRequest other{ViewMode::Zoom, 1, {200, 150}, {401, 300}};
        CHECK(panBy(other, VP, IMG, 30, -20) == other);
        const ViewRequest none{ViewMode::Zoom, 0, {200, 150}, IMG};
        CHECK(panBy(none, VP, IMG, 30, -20) == none);
    }

    TEST_CASE("panBy moves the picture and clamps") {
        const ViewRequest r{ViewMode::Zoom, 1, {200, 150}, IMG};
        CHECK(panBy(r, VP, IMG, 30, -20).centre == PointD{170, 170});
        CHECK(panBy(r, VP, IMG, 1000, 0).centre.x == 100);
        const PixelSize narrow{150, 300};
        const ViewRequest n{ViewMode::Zoom, 1, {75, 150}, narrow};
        CHECK(panBy(n, VP, narrow, 30, 0).centre.x == 75);
        CHECK(panBy(ViewRequest{}, VP, IMG, 30, 0) == ViewRequest{});
    }

    TEST_CASE("canPan") {
        CHECK_FALSE(canPan(ViewRequest{}, VP, IMG));
        CHECK(canPan({ViewMode::Zoom, 1, {200, 150}, IMG}, VP, IMG));
        const PixelSize small{50, 40};
        CHECK_FALSE(canPan({ViewMode::Zoom, 2, {25, 20}, small}, VP, small));
        CHECK_FALSE(canPan({ViewMode::Zoom, 1, {200, 150}, {401, 300}}, VP, IMG));
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
