#include "core/layout.h"

#include <algorithm>
#include <cstddef>

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
    // Clamped: the right and bottom edges first, so a window larger than the work area keeps its
    // top-left corner (and its caption) inside it.
    place.x = std::max(std::min(place.x, work_right - place.width), work_area.x);
    place.y = std::max(std::min(place.y, work_bottom - place.height), work_area.y);
    return place;
}

}  // namespace rawreel
