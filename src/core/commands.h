#pragma once

// The commands of the player and their keys (design M3 v2 D17, §4.3): wx-free, so the menus, the
// accelerators and the char hook of the GUI are all built from one table.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rawreel {

// The reduced table of design v2 §4.3.
enum class Command : std::uint8_t {
    Open,
    OpenUri,
    CloseStreams,
    Quit,
    PlayPause,
    Stop,
    StepBack,
    StepForward,
    StepBack10,
    StepForward10,
    First,
    Last,
    Loop,
    Format,
    Reopen,
    GpuInfo,
    Diagnostics,
    About,
};
inline constexpr std::size_t COMMAND_COUNT = static_cast<std::size_t>(Command::About) + 1;

// The full key set stays (the normaliser is kept as designed; Plus, Minus, Escape and F11 have no
// command in v1.0 and serve v1.x zoom and fullscreen keys).
enum class Key : std::int32_t {  // letters and digits: their ASCII upper-case code
    Space = ' ',
    Plus = '+',
    Minus = '-',
    Left = 0x10000,
    Right,
    Up,
    Down,
    Home,
    End,
    Escape,
    F11,
};

struct KeyChord {
    std::int32_t key = 0;
    bool ctrl = false, shift = false, alt = false;
    bool operator==(const KeyChord&) const = default;
};
[[nodiscard]] constexpr KeyChord chord(Key key, bool ctrl = false, bool shift = false) noexcept {
    return {static_cast<std::int32_t>(key), ctrl, shift, false};
}

enum class KeyRoute : std::uint8_t { Accelerator, CharHook };

struct CommandInfo {
    Command command;
    std::string_view id, menu, label, help;
    std::optional<KeyChord> key;
    KeyRoute route;
};

[[nodiscard]] std::span<const CommandInfo> commandTable() noexcept;  // in Command order
// CharHook commands only; nullopt when `text_focus` (an editable wxTextEntry has focus) or when
// no command matches.
[[nodiscard]] std::optional<Command> commandForKey(const KeyChord& chord, bool text_focus) noexcept;
// wx 3.3 key code + modifiers -> KeyChord. Unknown codes keep their value.
[[nodiscard]] KeyChord normaliseKey(std::int32_t keycode, bool ctrl, bool shift, bool alt) noexcept;
// "Ctrl+O", "Ctrl+Shift+O", "F11", "Space", "Shift+Left", "+", "Esc", "1", "L".
[[nodiscard]] std::string keyText(const KeyChord& chord);
// Accelerator with a key: "<label>\t<keyText>"; CharHook: "<label> (<keyText>)"; no key: "<label>".
[[nodiscard]] std::string menuLabel(const CommandInfo& info);

}  // namespace rawreel
