#include "core/layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace rawreel {

LayoutSize centreMinimum(LayoutSize viewport, LayoutSize transport, int info_bar) noexcept {
    return {.width = std::max(viewport.width, transport.width),
            .height = viewport.height + transport.height + std::max(info_bar, 0)};
}

LayoutSize minimumClient(LayoutSize centre, std::span<const DockLimit> docks) noexcept {
    LayoutSize client = centre;
    for (const DockLimit& dock : docks) {
        (dock.acrossWidth ? client.width : client.height) += dock.minimum + dock.sash;
    }
    return client;
}

std::vector<int> fitDocks(std::span<const DockFit> docks, int space) {
    std::vector<int> sizes;
    sizes.reserve(docks.size());
    int total = 0;
    for (const DockFit& dock : docks) {
        sizes.push_back(std::max(dock.user, dock.minimum));
        total += sizes.back();
    }
    int excess = total - space;
    for (std::size_t i = 0; i < sizes.size() && excess > 0; ++i) {
        const int give = std::min(excess, sizes[i] - docks[i].minimum);
        sizes[i] -= give;
        excess -= give;
    }
    return sizes;
}

namespace {

bool fitsIn(const ScreenRect& rect, const ScreenRect& area) noexcept {
    return rect.x >= area.x && rect.y >= area.y && rect.x + rect.width <= area.x + area.width &&
           rect.y + rect.height <= area.y + area.height;
}

}  // namespace

ScreenRect floatingWindowPlace(ScreenRect frame, ScreenRect client, LayoutSize size,
                               ScreenRect work_area, std::optional<ScreenRect> other,
                               int gap) noexcept {
    const auto at = [&](int x, int y) {
        return ScreenRect{.x = x, .y = y, .width = size.width, .height = size.height};
    };
    const int inside_y = client.y + gap;
    if (!other) {
        const int frame_right = frame.x + frame.width;
        const ScreenRect right = at(frame_right + gap, frame.y);
        if (fitsIn(right, work_area)) {
            return right;
        }
        const int work_right = work_area.x + work_area.width;
        return clampInto(at(std::min(frame_right, work_right) - gap - size.width, inside_y),
                         work_area);
    }
    if (const ScreenRect below = at(other->x, other->y + other->height + gap);
        fitsIn(below, work_area)) {
        return below;
    }
    if (const ScreenRect left = at(frame.x - gap - size.width, frame.y); fitsIn(left, work_area)) {
        return left;
    }
    return clampInto(at(std::max(frame.x, work_area.x) + gap, inside_y), work_area);
}

RulerStrip rulerStrip(const RulerConfig& rulers, double font_dip, DisplayScale scale) noexcept {
    if (!rulers.show) {
        return {};
    }
    const auto positive = [](double value, double otherwise) {
        return std::isfinite(value) && value > 0 ? value : otherwise;
    };
    const double per_dip = positive(scale.backingPerDip, 1);
    const double content = positive(scale.contentScale, 1);
    const double font = positive(font_dip, 0);
    const double dip = RULER_TICK_BAND_DIP + (rulers.numbers ? RULER_LABEL_LINE * font : 0);
    // The small epsilon keeps a product that lands a rounding error above a whole unit on it; no
    // strip is wider than MOST_UNITS, which keeps the conversions below in range.
    constexpr double EPSILON = 1e-9;
    constexpr double MOST_UNITS = 65536;
    const int units =
        static_cast<int>(std::min(std::ceil((dip * per_dip / content) - EPSILON), MOST_UNITS));
    const auto backing = static_cast<std::uint32_t>(std::min(
        std::floor((units * content) + 0.5), double{std::numeric_limits<std::uint32_t>::max()}));
    const auto tick_band = static_cast<std::uint32_t>(
        std::min(std::floor((RULER_TICK_BAND_DIP * per_dip) + 0.5), static_cast<double>(backing)));
    return {.windowUnits = units, .backing = backing, .tickBand = tick_band};
}

ScreenRect clampInto(ScreenRect rect, ScreenRect work_area) noexcept {
    // The right and bottom edges first, so a rectangle larger than the work area keeps its
    // top-left corner (and its caption) inside it.
    rect.x = std::max(std::min(rect.x, work_area.x + work_area.width - rect.width), work_area.x);
    rect.y = std::max(std::min(rect.y, work_area.y + work_area.height - rect.height), work_area.y);
    return rect;
}

std::optional<ScreenRect> windowForViewport(const WindowNow& now, LayoutSize wanted,
                                            ScreenRect work_area, WindowSizing sizing) noexcept {
    if (wanted.width <= 0 || wanted.height <= 0 || work_area.width <= 0 || work_area.height <= 0) {
        return std::nullopt;
    }
    const int frame_w = now.outer.width - now.viewport.width;
    const int frame_h = now.outer.height - now.viewport.height;
    if (sizing == WindowSizing::KeepShape) {
        const std::int64_t avail_w = work_area.width - frame_w;
        const std::int64_t avail_h = work_area.height - frame_h;
        if (avail_w <= 0 || avail_h <= 0) {
            return std::nullopt;
        }
        std::int64_t w = 0;
        std::int64_t h = 0;
        if (avail_w * wanted.height <= avail_h * wanted.width) {
            w = avail_w;
            h = avail_w * wanted.height / wanted.width;
        } else {
            w = avail_h * wanted.width / wanted.height;
            h = avail_h;
        }
        // Never a client smaller than minimumClient, as for Exact and Clamped.
        const int width = std::max(static_cast<int>(w) + frame_w,
                                   now.minimumClient.width + (now.outer.width - now.client.width));
        const int height =
            std::max(static_cast<int>(h) + frame_h,
                     now.minimumClient.height + (now.outer.height - now.client.height));
        return clampInto(ScreenRect{.x = work_area.x + (work_area.width - width) / 2,
                                    .y = work_area.y + (work_area.height - height) / 2,
                                    .width = width,
                                    .height = height},
                         work_area);
    }
    const int client_w =
        std::max(now.client.width + (wanted.width - now.viewport.width), now.minimumClient.width);
    const int client_h = std::max(now.client.height + (wanted.height - now.viewport.height),
                                  now.minimumClient.height);
    ScreenRect rect{.x = now.outer.x,
                    .y = now.outer.y,
                    .width = client_w + (now.outer.width - now.client.width),
                    .height = client_h + (now.outer.height - now.client.height)};
    if (sizing == WindowSizing::Exact &&
        (rect.width > work_area.width || rect.height > work_area.height)) {
        return std::nullopt;
    }
    rect.width = std::min(rect.width, work_area.width);
    rect.height = std::min(rect.height, work_area.height);
    return clampInto(rect, work_area);
}

LayoutSize viewportForClip(LayoutSize clip_pixels, double scale) noexcept {
    if (!(scale > 0)) {  // NaN too: a division by it would convert inf or NaN to int
        scale = 1.0;
    }
    return {.width = static_cast<int>(std::ceil(clip_pixels.width / scale)),
            .height = static_cast<int>(std::ceil(clip_pixels.height / scale))};
}

}  // namespace rawreel
