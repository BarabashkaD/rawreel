#pragma once

// The fit-to-window view of design M3 v2 (D23). Viewport sizes are physical pixels, image sizes
// image pixels; pixel (i, j) covers [i, i+1) x [j, j+1). The compositor maps output pixel (px, py)
// to the image point (originX + (px + 0.5) / zoom, originY + (py + 0.5) / zoom) (design §4.4).

#include <cstdint>

#include "dvv/platform/native_window.h"

namespace rawreel {

inline constexpr std::uint32_t MAX_STREAMS = 1;  // D2: one stream at a time; v1.x tiles raise it

struct PointD {
    double x = 0, y = 0;
    bool operator==(const PointD&) const = default;
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

    [[nodiscard]] double zoom() const noexcept;
    [[nodiscard]] PointD origin() const noexcept;  // the image point at the viewport's (0, 0)
    [[nodiscard]] dvv::platform::PixelSize viewport() const noexcept;
    [[nodiscard]] dvv::platform::PixelSize image() const noexcept;

    bool operator==(const ViewTransform&) const = default;

private:
    dvv::platform::PixelSize viewport_{};
    dvv::platform::PixelSize image_{};
    double zoom_ = 0;
    PointD origin_{};
};

}  // namespace rawreel
