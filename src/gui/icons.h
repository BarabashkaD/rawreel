#pragma once

// The transport bar's icons (video control panel spec §7; window title and Info design §12): Lucide
// SVGs (gui/icons/lucide_icons.h) tinted with a colour and rendered by wx at 18 x 18 DIP.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <cstdint>

#include <wx/bmpbndl.h>
#include <wx/cursor.h>
#include <wx/window.h>

namespace rawreel {

enum class Icon : std::uint8_t {
    FolderOpen,
    SkipBack,
    StepBack,
    Play,
    Pause,
    Square,
    StepForward,
    SkipForward,
    Repeat,
    SlidersHorizontal,
    Grid,
    Info,
    ZoomOut,
    ZoomIn,
    Blend,
    Pipette,
    Binary,
};
inline constexpr std::size_t ICON_COUNT = static_cast<std::size_t>(Icon::Binary) + 1;
inline constexpr int ICON_DIP = 18;

// The icon with its `currentColor` replaced by `colour`, from wxBitmapBundle::FromSVG.
[[nodiscard]] wxBitmapBundle iconBundle(Icon icon, const wxColour& colour);

// The pipette cursor of `window`: the Lucide pipette at the system cursor size (wxSYS_CURSOR_X; 32
// where wx reports none), black with a 1 px white outline, the hotspot at the tip (2, size - 3).
[[nodiscard]] wxCursor pipetteCursor(const wxWindow& window);

}  // namespace rawreel
