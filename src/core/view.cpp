#include "core/view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>

#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel {

using dvv::media::PointD;
using dvv::media::ViewMode;
using dvv::media::ViewRequest;
using dvv::media::ViewTransform;

namespace {

// points x scale rounded to nearest, in [1, 2^32 - 1]; points and scale are > 0.
std::uint32_t toPixels(double points, double scale) noexcept {
    const double pixels = std::round(points * scale);
    constexpr double MAX = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(std::clamp(pixels, 1.0, MAX));
}

struct Stop {
    double zoom;
    bool fit;
};

// The stops in increasing order, and `zoom` snapped to s* when it is within 1e-6 of `fit`.
struct Stops {
    std::array<Stop, ZOOM_STEPS.size() + 1> stops{};
    std::size_t count = 0;
    double zoom = 0;
};

Stops makeStops(double zoom, double fit) noexcept {
    Stops result;
    result.zoom = zoom;
    bool has_step = false;
    for (const double s : ZOOM_STEPS) {
        if (std::abs(s - fit) <= 0.01 * s) {
            has_step = true;
            if (std::abs(zoom - fit) <= 1e-6 * fit) {
                result.zoom = s;
            }
        }
    }
    bool fit_placed = has_step;
    for (const double s : ZOOM_STEPS) {
        if (!fit_placed && fit < s) {
            result.stops[result.count++] = {fit, true};
            fit_placed = true;
        }
        result.stops[result.count++] = {s, false};
    }
    if (!fit_placed) {
        result.stops[result.count++] = {fit, true};
    }
    return result;
}

}  // namespace

dvv::platform::PixelSize physicalPixelSize(double width_points, double height_points,
                                           double scale) noexcept {
    // Written so that NaN gives an empty size too.
    if (!(width_points > 0 && height_points > 0 && scale > 0)) {
        return {};
    }
    return {.width = toPixels(width_points, scale), .height = toPixels(height_points, scale)};
}

int takeWheelNotches(double& accumulated, int rotation, int wheel_delta) noexcept {
    if (wheel_delta <= 0) {
        return 0;
    }
    if ((accumulated > 0 && rotation < 0) || (accumulated < 0 && rotation > 0)) {
        accumulated = 0;
    }
    accumulated += rotation;
    const double notches = std::trunc(accumulated / wheel_delta);
    accumulated -= notches * wheel_delta;
    return static_cast<int>(notches);
}

double wheelPanDip(int rotation, int wheel_delta, int lines, double line_dip,
                   double page_dip) noexcept {
    if (wheel_delta <= 0) {
        return 0;
    }
    const double notches = static_cast<double>(rotation) / wheel_delta;
    return lines < 0 ? notches * page_dip : notches * lines * line_dip;
}

std::optional<ZoomStep> nextZoomIn(double zoom, double fit) noexcept {
    const Stops s = makeStops(zoom, fit);
    for (std::size_t i = 0; i < s.count; ++i) {
        if (s.stops[i].zoom > s.zoom * (1 + 1e-6)) {
            return ZoomStep{s.stops[i].zoom, s.stops[i].fit};
        }
    }
    return std::nullopt;
}

std::optional<ZoomStep> nextZoomOut(double zoom, double fit) noexcept {
    const Stops s = makeStops(zoom, fit);
    for (std::size_t i = s.count; i-- > 0;) {
        if (s.stops[i].zoom < s.zoom * (1 - 1e-6)) {
            return ZoomStep{s.stops[i].zoom, s.stops[i].fit};
        }
    }
    return std::nullopt;
}

std::optional<ViewRequest> stepZoom(const ViewRequest& request, dvv::platform::PixelSize viewport,
                                    dvv::platform::PixelSize image, bool in, PointD anchor) {
    const double zoom = dvv::media::resolveView(request, viewport, image).zoom();
    const double fit = ViewTransform::fit(viewport, image).zoom();
    const std::optional<ZoomStep> next = in ? nextZoomIn(zoom, fit) : nextZoomOut(zoom, fit);
    if (!next) {
        return std::nullopt;
    }
    if (next->fit) {
        return ViewRequest{};
    }
    return dvv::media::zoomAround(request, viewport, image, next->zoom, anchor);
}

ZoomMenuEntry zoomMenuChecked(ViewMode mode, double zoom) noexcept {
    if (mode == ViewMode::Fit) {
        return {.check = ZoomMenuCheck::Fit};
    }
    const auto near = [zoom](double value) { return std::abs(zoom - value) <= 1e-6 * value; };
    if (near(1)) {
        return {.check = ZoomMenuCheck::ActualSize};
    }
    for (std::size_t i = 0; i < ZOOM_MENU_STEPS.size(); ++i) {
        if (near(ZOOM_MENU_STEPS[i])) {
            return {.check = ZoomMenuCheck::Step, .step = i};
        }
    }
    return {};
}

ZoomLimits zoomLimits(double fit) noexcept {
    return {.minimum = std::min(fit, ZOOM_STEPS.front()),
            .maximum = std::max(fit, ZOOM_STEPS.back())};
}

std::string zoomText(ViewMode mode, double zoom) {
    std::string percent = std::format("{:.1f}", zoom * 100);
    if (percent.ends_with(".0")) {
        percent.resize(percent.size() - 2);
    }
    return mode == ViewMode::Fit ? "Fit (" + percent + " %)" : percent + " %";
}

}  // namespace rawreel
