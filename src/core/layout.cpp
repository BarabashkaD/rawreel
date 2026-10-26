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

}  // namespace rawreel
