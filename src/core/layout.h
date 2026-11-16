#pragma once

// The main frame's size limits (M4 GUI D8): the clip view never gets smaller than
// MIN_VIEWPORT_DIP, the docked panes give way when the window shrinks and grow back to the size the
// user gave them with the sash. wx-free arithmetic in physical pixels; gui/dock_manager applies it.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "dvv/platform/native_window.h"

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

// A rectangle in screen coordinates (physical pixels on Windows, logical pixels on GTK and macOS).
struct ScreenRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const ScreenRect&) const = default;
};

// The Info window's place (window title and Info design §10): right of `frame` (top level with
// frame.y, left = frame right + gap) when that fits in `work_area`; else inside the frame's right
// edge: x = min(frame right, work right) - gap - size.width, y = client.y + gap; clamped into
// `work_area`. `client` is the frame's client area in screen coordinates.
[[nodiscard]] ScreenRect infoWindowPlace(ScreenRect frame, ScreenRect client, LayoutSize size,
                                         ScreenRect work_area, int gap) noexcept;

// `rect` moved into `work_area`: right and bottom edges first, then left and top, so a rectangle
// larger than the work area keeps its top-left corner inside it.
[[nodiscard]] ScreenRect clampInto(ScreenRect rect, ScreenRect work_area) noexcept;

// The main window as it is now, in window units.
struct WindowNow {
    ScreenRect outer;          // the window's outer rectangle
    LayoutSize client;         // its client size
    LayoutSize viewport;       // the viewport's size
    LayoutSize minimumClient;  // the smallest client size
};

enum class WindowSizing : std::uint8_t {
    Exact,      // the viewport becomes `wanted`; nullopt if that window exceeds the work area
    Clamped,    // as Exact, each axis of the window then clamped to the work area
    KeepShape,  // the largest viewport of wanted's shape whose window fits, centred (never a
                // client smaller than minimumClient)
};

// The window rectangle that gives the viewport the size `wanted` (never a client smaller than
// minimumClient), at the current top-left moved back inside `work_area`. Empty `wanted` or work
// area: nullopt.
[[nodiscard]] std::optional<ScreenRect> windowForViewport(const WindowNow& now, LayoutSize wanted,
                                                          ScreenRect work_area,
                                                          WindowSizing sizing) noexcept;

// The viewport size that shows `clip_pixels` at 100 %: ceil(pixels / scale), scale = backing
// pixels per window unit; a scale <= 0 (or NaN) counts as 1.
[[nodiscard]] LayoutSize viewportForClip(LayoutSize clip_pixels, double scale) noexcept;

// A frame size in layout units (window units at a scale of 1).
[[nodiscard]] constexpr LayoutSize layoutSizeOf(dvv::platform::PixelSize size) noexcept {
    return {.width = static_cast<int>(size.width), .height = static_cast<int>(size.height)};
}

}  // namespace rawreel
