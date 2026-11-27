#pragma once

// rawreel's view policy on top of dvv-media's view geometry: the backing pixel size of the
// viewport, the mouse wheel, the zoom stops, the zoom menu and the zoom text.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel {

// The physical pixel size of a view `width_points` x `height_points` in size (fractional points
// allowed) whose layer has `scale` pixels per point (macOS: the CAMetalLayer's contentsScale, the
// window's backing scale factor): points x scale rounded to nearest, at least 1 on each axis of a
// non-empty view (both points and scale > 0); empty otherwise. The macOS viewport passes it as
// the presenter's extent.
[[nodiscard]] dvv::platform::PixelSize physicalPixelSize(double width_points, double height_points,
                                                         double scale) noexcept;

// Adds `rotation` to `accumulated` (reset to 0 first when the signs differ) and returns the whole
// wheel_delta notches in it (signed), leaving the remainder. wheel_delta <= 0: 0.
[[nodiscard]] int takeWheelNotches(double& accumulated, int rotation, int wheel_delta) noexcept;
// The picture's move in DIP on one axis: rotation / wheel_delta × lines × line_dip; lines < 0 (a
// page scroll) gives page_dip per wheel_delta. wheel_delta <= 0: 0.
[[nodiscard]] double wheelPanDip(int rotation, int wheel_delta, int lines, double line_dip,
                                 double page_dip) noexcept;

inline constexpr std::array<double, 9> ZOOM_STEPS{0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32};

struct ZoomStep {
    double zoom = 0;
    bool fit = false;  // the fit stop: the caller sets Fit
    bool operator==(const ZoomStep&) const = default;
};

// s* is the step with |s - fit| <= 0.01 s, if any. The stops are ZOOM_STEPS plus `fit` when there
// is no s*. A `zoom` within 1e-6 (relative) of `fit` counts as s* when there is one. In: the
// smallest stop > zoom (1 + 1e-6); Out: the largest stop < zoom (1 - 1e-6). `.fit` is set only
// for the `fit` stop. nullopt: at the limit.
[[nodiscard]] std::optional<ZoomStep> nextZoomIn(double zoom, double fit) noexcept;
[[nodiscard]] std::optional<ZoomStep> nextZoomOut(double zoom, double fit) noexcept;

// One zoom step in (`in`) or out from the zoom shown for `request`, with the fit of `viewport`
// and `image` as the fit stop: nextZoomIn or nextZoomOut; the fit stop gives ViewRequest{} (Fit),
// any other stop zoomAround(request, viewport, image, stop, anchor). nullopt: at the limit.
[[nodiscard]] std::optional<dvv::media::ViewRequest> stepZoom(
    const dvv::media::ViewRequest& request, dvv::platform::PixelSize viewport,
    dvv::platform::PixelSize image, bool in, dvv::media::PointD anchor);

// Zoom buttons design §3: ZOOM_STEPS from 25 % to 1600 % without 100 %.
inline constexpr std::array<double, 6> ZOOM_MENU_STEPS{0.25, 0.5, 2, 4, 8, 16};
enum class ZoomMenuCheck : std::uint8_t { None, Fit, ActualSize, Step };
struct ZoomMenuEntry {
    ZoomMenuCheck check = ZoomMenuCheck::None;
    std::size_t step = 0;  // Step: the index in ZOOM_MENU_STEPS
    bool operator==(const ZoomMenuEntry&) const = default;
};
// §2.2: Fit in Fit mode; ActualSize at a zoom within 1e-6 (relative) of 1; Step at one within
// 1e-6 of a ZOOM_MENU_STEPS value; None otherwise.
[[nodiscard]] ZoomMenuEntry zoomMenuChecked(dvv::media::ViewMode mode, double zoom) noexcept;

struct ZoomLimits {
    double minimum = 0, maximum = 0;
    bool operator==(const ZoomLimits&) const = default;
};
[[nodiscard]] ZoomLimits zoomLimits(double fit) noexcept;  // {min(fit, 1/8), max(fit, 32)}

// The zoom as a percentage with one decimal, a trailing ".0" dropped: "200 %", "33.3 %"; Fit
// wraps it: "Fit (47.3 %)".
[[nodiscard]] std::string zoomText(dvv::media::ViewMode mode, double zoom);

}  // namespace rawreel
