#include "core/layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

ScreenRect infoWindowPlace(ScreenRect frame, ScreenRect client, LayoutSize size,
                           ScreenRect work_area, int gap) noexcept {
    const int work_right = work_area.x + work_area.width;
    const int work_bottom = work_area.y + work_area.height;
    const int frame_right = frame.x + frame.width;
    ScreenRect place{
        .x = frame_right + gap, .y = frame.y, .width = size.width, .height = size.height};
    const bool fits = place.x >= work_area.x && place.y >= work_area.y &&
                      place.x + place.width <= work_right && place.y + place.height <= work_bottom;
    if (!fits) {
        place.x = std::min(frame_right, work_right) - gap - size.width;
        place.y = client.y + gap;
    }
    return clampInto(place, work_area);
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
