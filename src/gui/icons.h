#pragma once

// The transport bar's icons (video control panel spec §7): Lucide SVGs (gui/icons/lucide_icons.h)
// tinted with a colour and rendered by wx at 18 x 18 DIP.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <cstddef>
#include <cstdint>

#include <wx/bmpbndl.h>

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
};
inline constexpr std::size_t ICON_COUNT = static_cast<std::size_t>(Icon::Repeat) + 1;
inline constexpr int ICON_DIP = 18;

// The icon with its `currentColor` replaced by `colour`, from wxBitmapBundle::FromSVG.
[[nodiscard]] wxBitmapBundle iconBundle(Icon icon, const wxColour& colour);

}  // namespace rawreel
