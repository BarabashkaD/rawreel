#pragma once

// The keys of the Zoom window (pixel inspector design §4.2): which action a key chord asks for.

#include <cstdint>
#include <optional>

#include "core/commands.h"

namespace rawreel {

enum class ZoomKey : std::uint8_t {
    ToggleMode,
    ToggleLayout,
    CentreOnPin,
    LiveProbe,
    MovePin,
    PlayPause
};

struct ZoomKeyAction {
    ZoomKey key;
    int dx = 0, dy = 0;  // MovePin: the step in samples; a block step when `block`
    bool block = false;
    bool operator==(const ZoomKeyAction&) const = default;
};

// F, S and C: ToggleMode, ToggleLayout, CentreOnPin; Shift+P: LiveProbe; the arrows: MovePin by 1,
// Shift+arrow by a block; Space: PlayPause. Everything else, Ctrl+C included, gives nullopt (the
// Zoom window handles the copy before this).
[[nodiscard]] std::optional<ZoomKeyAction> zoomKeyAction(const KeyChord& chord) noexcept;

}  // namespace rawreel
