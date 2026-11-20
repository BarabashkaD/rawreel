#pragma once

// The fit-to-window view of design M3 v2 (D23). Viewport sizes are physical pixels, image sizes
// image pixels; pixel (i, j) covers [i, i+1) x [j, j+1). The compositor maps output pixel (px, py)
// to the image point (originX + (px + 0.5) / zoom, originY + (py + 0.5) / zoom) (design §4.4).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "dvv/platform/pixel_size.h"

namespace rawreel {

inline constexpr std::uint32_t MAX_STREAMS = 1;  // D2: one stream at a time; v1.x tiles raise it

struct PointD {
    double x = 0, y = 0;
    bool operator==(const PointD&) const = default;
};

// The physical pixel size of a view `width_points` x `height_points` in size (fractional points
// allowed) whose layer has `scale` pixels per point (macOS: the CAMetalLayer's contentsScale, the
// window's backing scale factor): points x scale rounded to nearest, at least 1 on each axis of a
// non-empty view (both points and scale > 0); empty otherwise. The macOS viewport passes it as
// the presenter's extent.
[[nodiscard]] dvv::platform::PixelSize physicalPixelSize(double width_points, double height_points,
                                                         double scale) noexcept;

// What the user asked to see; the renderer resolves it against the viewport and frame size.
// Fit: the whole frame, centred. Zoom: `zoom` backing pixels per clip pixel with clip point
// `centre` at the viewport's centre, for frames of size `image`; a request for another frame
// size resolves as Fit.
enum class ViewMode : std::uint8_t { Fit, Zoom };
struct ViewRequest {
    ViewMode mode = ViewMode::Fit;
    double zoom = 1.0;                 // Zoom: backing pixels per clip pixel
    PointD centre{};                   // Zoom: the clip point wanted at the viewport's centre
    dvv::platform::PixelSize image{};  // Zoom: the frame size the request was made for
    bool operator==(const ViewRequest&) const = default;
};

class ViewTransform {  // value type
public:
    ViewTransform() = default;  // viewport 0x0, image 0x0, zoom 0, origin (0, 0)

    // zoom = min(W/w, H/h) with no clamp. Width-limited iff W·h <= H·w (64-bit integers; a tie is
    // width-limited). Width-limited: zoom = W/w, x offset 0, y offset = (H·w − h·W) / (2·w);
    // height-limited: zoom = H/h, y offset 0, x offset = (W·h − w·H) / (2·h); offsets by integer
    // division (whole viewport pixels). origin = −offset / zoom on each axis.
    // An empty viewport or image -> zoom 0, origin (0, 0) (only the background is drawn).
    [[nodiscard]] static ViewTransform fit(dvv::platform::PixelSize viewport,
                                           dvv::platform::PixelSize image);

    // Per axis, with W the viewport and s = image x zoom: s > W -> offset = round(W/2 - centre x
    // zoom) clamped to [ceil(W - s), 0]; else floor((W - s) / 2). origin = -offset / zoom.
    // An empty viewport or image, or a zoom <= 0 (or NaN) -> zoom 0, origin (0, 0), mode Fit (only
    // the background is drawn), so a record without a frame is always Fit.
    [[nodiscard]] static ViewTransform zoomed(dvv::platform::PixelSize viewport,
                                              dvv::platform::PixelSize image, double zoom,
                                              PointD centre);

    [[nodiscard]] double zoom() const noexcept;
    [[nodiscard]] PointD origin() const noexcept;  // the image point at the viewport's (0, 0)
    [[nodiscard]] dvv::platform::PixelSize viewport() const noexcept;
    [[nodiscard]] dvv::platform::PixelSize image() const noexcept;
    // Fit from fit() and the default; Zoom from zoomed() with a zoom > 0 (else Fit).
    [[nodiscard]] ViewMode mode() const noexcept;

    bool operator==(const ViewTransform&) const = default;

private:
    dvv::platform::PixelSize viewport_{};
    dvv::platform::PixelSize image_{};
    double zoom_ = 0;
    PointD origin_{};
    ViewMode mode_ = ViewMode::Fit;
};

// Fit, or Zoom with `request.image != image` -> ViewTransform::fit; else ViewTransform::zoomed.
[[nodiscard]] ViewTransform resolveView(const ViewRequest& request,
                                        dvv::platform::PixelSize viewport,
                                        dvv::platform::PixelSize image);

// The image point under a viewport point: origin + point / zoom.
[[nodiscard]] PointD imagePointAt(const ViewTransform& view, PointD viewport_point) noexcept;

// Starts from resolveView (a Fit request works): the clip point under `anchor` stays under it at
// the new zoom. The stored centre is clamped, not rounded: on an axis where image x zoom exceeds
// the viewport to [W/(2 zoom), image - W/(2 zoom)], else image/2. Mode Zoom, `image` set; the
// zoom itself is not clamped. When the resolved zoom is 0 (an empty viewport or image) or `zoom`
// <= 0, the centre is the request's own, unclamped.
[[nodiscard]] ViewRequest zoomAround(const ViewRequest& request, dvv::platform::PixelSize viewport,
                                     dvv::platform::PixelSize image, double zoom, PointD anchor);

// Moves the picture by (dx, dy) backing pixels (positive: right, down): centre -= d / zoom,
// clamped as in zoomAround. A Fit request, or one for another frame size, is returned unchanged.
[[nodiscard]] ViewRequest panBy(const ViewRequest& request, dvv::platform::PixelSize viewport,
                                dvv::platform::PixelSize image, double dx, double dy);

// A pan can move the picture: mode Zoom, `request.image == image`, and image x zoom exceeds the
// viewport on at least one axis.
[[nodiscard]] bool canPan(const ViewRequest& request, dvv::platform::PixelSize viewport,
                          dvv::platform::PixelSize image) noexcept;

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
[[nodiscard]] std::optional<ViewRequest> stepZoom(const ViewRequest& request,
                                                  dvv::platform::PixelSize viewport,
                                                  dvv::platform::PixelSize image, bool in,
                                                  PointD anchor);

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
[[nodiscard]] ZoomMenuEntry zoomMenuChecked(ViewMode mode, double zoom) noexcept;

struct ZoomLimits {
    double minimum = 0, maximum = 0;
    bool operator==(const ZoomLimits&) const = default;
};
[[nodiscard]] ZoomLimits zoomLimits(double fit) noexcept;  // {min(fit, 1/8), max(fit, 32)}

// The zoom as a percentage with one decimal, a trailing ".0" dropped: "200 %", "33.3 %"; Fit
// wraps it: "Fit (47.3 %)".
[[nodiscard]] std::string zoomText(ViewMode mode, double zoom);

}  // namespace rawreel
