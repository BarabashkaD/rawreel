#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

#include "core/view.h"

using dvv::platform::PixelSize;
using rawreel::MAX_STREAMS;
using rawreel::PointD;
using rawreel::ViewTransform;

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
}
