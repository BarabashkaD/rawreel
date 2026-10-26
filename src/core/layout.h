#pragma once

// The main frame's size limits (M4 GUI D8): the clip view never gets smaller than
// MIN_VIEWPORT_DIP, the docked panes give way when the window shrinks and grow back to the size the
// user gave them with the sash. wx-free arithmetic in physical pixels; gui/dock_manager applies it.

#include <span>
#include <vector>

namespace rawreel {

struct LayoutSize {
    int width = 0;
    int height = 0;
    friend bool operator==(const LayoutSize&, const LayoutSize&) = default;
};

// The smallest clip view (viewport), in DIP; the caller scales it (FromDIP).
inline constexpr LayoutSize MIN_VIEWPORT_DIP{.width = 320, .height = 240};

// The centre pane's minimum: the viewport above the transport bar, with `info_bar` (the infobar's
// height, reserved even while it is hidden) on top. As wide as the wider of the viewport and the
// transport bar.
[[nodiscard]] LayoutSize centreMinimum(LayoutSize viewport, LayoutSize transport,
                                       int info_bar) noexcept;

// One docked pane row or column around the centre pane: `acrossWidth` docks sit left or right of
// the centre (their size is a width), the others above or below it (a height). `minimum` is the
// dock's smallest size, caption and borders included; `sash` is the sash next to it (0 if none).
struct DockLimit {
    bool acrossWidth = false;
    int minimum = 0;
    int sash = 0;
};

// The smallest client size that keeps the centre pane at `centre` with every dock at its minimum.
[[nodiscard]] LayoutSize minimumClient(LayoutSize centre,
                                       std::span<const DockLimit> docks) noexcept;

// One dock to fit: the size the user gave it (the sash) and its minimum.
struct DockFit {
    int user = 0;
    int minimum = 0;
};

// The sizes of the docks of one direction that leave `space` pixels (the client extent minus the
// centre minimum and the sashes) to them: every dock at its user size if they fit; otherwise the
// docks give way in order, the first one first, never below their minimum.
[[nodiscard]] std::vector<int> fitDocks(std::span<const DockFit> docks, int space);

}  // namespace rawreel
