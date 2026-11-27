#include "core/zoom_keys.h"

#include <optional>

#include "core/commands.h"

namespace rawreel {

std::optional<ZoomKeyAction> zoomKeyAction(const KeyChord& chord) noexcept {
    if (chord.ctrl || chord.alt) {
        return std::nullopt;
    }
    const auto key = chord.key;
    const auto plain = [&](Key k) { return key == static_cast<std::int32_t>(k); };
    if (key == 'P') {
        return chord.shift ? std::optional{ZoomKeyAction{ZoomKey::LiveProbe}} : std::nullopt;
    }
    const auto move = [&](int dx, int dy) {
        return ZoomKeyAction{ZoomKey::MovePin, dx, dy, chord.shift};
    };
    if (plain(Key::Left)) {
        return move(-1, 0);
    }
    if (plain(Key::Right)) {
        return move(1, 0);
    }
    if (plain(Key::Up)) {
        return move(0, -1);
    }
    if (plain(Key::Down)) {
        return move(0, 1);
    }
    if (chord.shift) {
        return std::nullopt;
    }
    if (key == 'F') {
        return ZoomKeyAction{ZoomKey::ToggleMode};
    }
    if (key == 'S') {
        return ZoomKeyAction{ZoomKey::ToggleLayout};
    }
    if (key == 'C') {
        return ZoomKeyAction{ZoomKey::CentreOnPin};
    }
    if (plain(Key::Space)) {
        return ZoomKeyAction{ZoomKey::PlayPause};
    }
    return std::nullopt;
}

}  // namespace rawreel
