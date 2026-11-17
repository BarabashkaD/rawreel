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

namespace rawreel {

namespace {

// points x scale rounded to nearest, in [1, 2^32 - 1]; points and scale are > 0.
std::uint32_t toPixels(double points, double scale) noexcept {
    const double pixels = std::round(points * scale);
    constexpr double MAX = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(std::clamp(pixels, 1.0, MAX));
}

// The clamp of the centre on one axis: W viewport, I image, both in pixels.
double clampCentre(double centre, double viewport, double image, double zoom) noexcept {
    if (image * zoom > viewport) {
        const double half = viewport / (2 * zoom);
        return std::clamp(centre, half, image - half);
    }
    return image / 2;
}

PointD clampCentre(PointD centre, dvv::platform::PixelSize viewport, dvv::platform::PixelSize image,
                   double zoom) noexcept {
    return {clampCentre(centre.x, viewport.width, image.width, zoom),
            clampCentre(centre.y, viewport.height, image.height, zoom)};
}

// The viewport offset of the picture on one axis, in whole viewport pixels.
double axisOffset(double centre, double viewport, double image, double zoom) noexcept {
    const double size = image * zoom;
    if (size > viewport) {
        return std::clamp(std::round(viewport / 2 - centre * zoom), std::ceil(viewport - size),
                          0.0);
    }
    return std::floor((viewport - size) / 2);
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

ViewTransform ViewTransform::fit(dvv::platform::PixelSize viewport,
                                 dvv::platform::PixelSize image) {
    ViewTransform t;
    t.viewport_ = viewport;
    t.image_ = image;
    if (viewport.empty() || image.empty()) {
        return t;
    }
    // 64-bit before any product: (2^32 - 1)^2 fits.
    const std::uint64_t vw = viewport.width;
    const std::uint64_t vh = viewport.height;
    const std::uint64_t iw = image.width;
    const std::uint64_t ih = image.height;
    std::uint64_t offset_x = 0;
    std::uint64_t offset_y = 0;
    if (vw * ih <= vh * iw) {  // width-limited (a tie too)
        t.zoom_ = static_cast<double>(vw) / static_cast<double>(iw);
        offset_y = (vh * iw - ih * vw) / (2 * iw);
    } else {  // height-limited
        t.zoom_ = static_cast<double>(vh) / static_cast<double>(ih);
        offset_x = (vw * ih - iw * vh) / (2 * ih);
    }
    t.origin_ = {-static_cast<double>(offset_x) / t.zoom_,
                 -static_cast<double>(offset_y) / t.zoom_};
    return t;
}

double ViewTransform::zoom() const noexcept {
    return zoom_;
}
PointD ViewTransform::origin() const noexcept {
    return origin_;
}
dvv::platform::PixelSize ViewTransform::viewport() const noexcept {
    return viewport_;
}
dvv::platform::PixelSize ViewTransform::image() const noexcept {
    return image_;
}

ViewTransform ViewTransform::zoomed(dvv::platform::PixelSize viewport,
                                    dvv::platform::PixelSize image, double zoom, PointD centre) {
    ViewTransform t;
    t.viewport_ = viewport;
    t.image_ = image;
    if (viewport.empty() || image.empty() || !(zoom > 0)) {
        return t;  // mode Fit, zoom 0: only the background is drawn
    }
    t.mode_ = ViewMode::Zoom;
    t.zoom_ = zoom;
    const double offset_x = axisOffset(centre.x, viewport.width, image.width, zoom);
    const double offset_y = axisOffset(centre.y, viewport.height, image.height, zoom);
    t.origin_ = {-offset_x / zoom, -offset_y / zoom};
    return t;
}

ViewMode ViewTransform::mode() const noexcept {
    return mode_;
}

ViewTransform resolveView(const ViewRequest& request, dvv::platform::PixelSize viewport,
                          dvv::platform::PixelSize image) {
    if (request.mode == ViewMode::Fit || request.image != image) {
        return ViewTransform::fit(viewport, image);
    }
    return ViewTransform::zoomed(viewport, image, request.zoom, request.centre);
}

PointD imagePointAt(const ViewTransform& view, PointD viewport_point) noexcept {
    return {view.origin().x + viewport_point.x / view.zoom(),
            view.origin().y + viewport_point.y / view.zoom()};
}

ViewRequest zoomAround(const ViewRequest& request, dvv::platform::PixelSize viewport,
                       dvv::platform::PixelSize image, double zoom, PointD anchor) {
    const ViewTransform view = resolveView(request, viewport, image);
    ViewRequest result{
        .mode = ViewMode::Zoom, .zoom = zoom, .centre = request.centre, .image = image};
    if (!(view.zoom() > 0) || !(zoom > 0)) {
        return result;
    }
    const PointD point = imagePointAt(view, anchor);
    const PointD centre{point.x - anchor.x / zoom + viewport.width / (2 * zoom),
                        point.y - anchor.y / zoom + viewport.height / (2 * zoom)};
    result.centre = clampCentre(centre, viewport, image, zoom);
    return result;
}

ViewRequest panBy(const ViewRequest& request, dvv::platform::PixelSize viewport,
                  dvv::platform::PixelSize image, double dx, double dy) {
    if (request.mode == ViewMode::Fit || request.image != image || !(request.zoom > 0)) {
        return request;
    }
    ViewRequest result = request;
    result.centre =
        clampCentre({request.centre.x - dx / request.zoom, request.centre.y - dy / request.zoom},
                    viewport, image, request.zoom);
    return result;
}

bool canPan(const ViewRequest& request, dvv::platform::PixelSize viewport,
            dvv::platform::PixelSize image) noexcept {
    return request.mode == ViewMode::Zoom && request.image == image &&
           (image.width * request.zoom > viewport.width ||
            image.height * request.zoom > viewport.height);
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
    const double zoom = resolveView(request, viewport, image).zoom();
    const double fit = ViewTransform::fit(viewport, image).zoom();
    const std::optional<ZoomStep> next = in ? nextZoomIn(zoom, fit) : nextZoomOut(zoom, fit);
    if (!next) {
        return std::nullopt;
    }
    if (next->fit) {
        return ViewRequest{};
    }
    return zoomAround(request, viewport, image, next->zoom, anchor);
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
