#include "core/view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rawreel {

namespace {

// points x scale rounded to nearest, in [1, 2^32 - 1]; points and scale are > 0.
std::uint32_t toPixels(double points, double scale) noexcept {
    const double pixels = std::round(points * scale);
    constexpr double MAX = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(std::clamp(pixels, 1.0, MAX));
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

}  // namespace rawreel
