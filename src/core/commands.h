#pragma once

// The commands of the player and their keys (design M3 v2 D17, §4.3): wx-free, so the menus, the
// accelerators and the char hook of the GUI are all built from one table.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "dvv/media/pixel_format.h"

namespace rawreel {

// The reduced table of design v2 §4.3.
enum class Command : std::uint8_t {
    Open,
    OpenUri,
    SaveLog,            // File -> Save Log... (Log pane and Settings design §4.3)
    SaveSelectedLines,  // File -> Save Selected Lines...
    CloseStreams,
    Settings,  // File -> Settings... (Log pane and Settings design §5.1; Edit on Linux)
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
    FitToWindow,  // View -> Fit, Actual size, Zoom in, Zoom out, Resize window (view zoom)
    ActualSize,
    ZoomIn,
    ZoomOut,
    ResizeWindowToActualSize,
    Format,
    Reopen,
    LogPane,     // View -> Log (Log pane and Settings design §2)
    InfoWindow,  // View -> Info (window title and Info design §10)
    Grid,        // View -> Grid: the grid on the picture
    Rulers,      // View -> Rulers: the ruler strips
    GridWindow,  // View -> Grid and Rulers...: the Grid and Rulers window
    Plane1,      // View -> the plane items: show or hide colour component c0, c1 or c2
    Plane2,
    Plane3,
    HiddenChromaZero,  // View -> Hidden U and V at 0: hidden U and V take 0, not the mid code
    Alpha,             // View -> Alpha: apply or ignore the clip's alpha
    Pipette,    // View -> Pipette, Live Probe, Zoom Window, Clear Pin, Copy Pixel Values, Padding
    LiveProbe,  // Check (the pixel inspector)
    ZoomWindow,
    ClearPin,
    CopyValues,
    PaddingCheck,
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

// AcceleratorAndCharHook: the menu registers and shows the key as an accelerator and commandForKey
// matches it too, so the char hook dispatches it first (through normaliseKey, which folds '=', '+'
// and the keypad keys).
enum class KeyRoute : std::uint8_t { Accelerator, CharHook, AcceleratorAndCharHook };

// How a command's menu item shows its state. Shown: the command shows or hides a part (View ->
// Log, Info, Grid, Rulers): a check item on Windows and Linux, the changing "Show "/"Hide " title
// on macOS (shownItemTitle). Mode: the command turns a mode on or off (View -> Alpha): a check
// item on every OS.
enum class MenuCheck : std::uint8_t { None, Shown, Mode };

struct CommandInfo {
    Command command;
    std::string_view id, menu, label, help;
    std::optional<KeyChord> key;
    KeyRoute route;
    MenuCheck check = MenuCheck::None;
};

[[nodiscard]] std::span<const CommandInfo> commandTable() noexcept;  // in Command order
// CharHook and AcceleratorAndCharHook commands only; nullopt when `focus_keeps_keys` (the focus is
// an editable wxTextEntry or inside the Log pane, Log pane and Settings design §4.1) or when no
// command matches.
[[nodiscard]] std::optional<Command> commandForKey(const KeyChord& chord,
                                                   bool focus_keeps_keys) noexcept;
// The command whose key is `chord`, whatever its route; nullopt when no command has it. A floating
// window's modified chords reach the main window through it where the menu bar does not take them.
[[nodiscard]] std::optional<Command> commandOfChord(const KeyChord& chord) noexcept;
// wx 3.3 key code + modifiers -> KeyChord. Unknown codes keep their value.
[[nodiscard]] KeyChord normaliseKey(std::int32_t keycode, bool ctrl, bool shift, bool alt) noexcept;
// "Ctrl+O", "Ctrl+Shift+O", "F11", "Space", "Shift+Left", "+", "Esc", "1", "L".
[[nodiscard]] std::string keyText(const KeyChord& chord);
// Accelerator or AcceleratorAndCharHook with a key: "<label>\t<keyText>"; CharHook: "<label>
// (<keyText>)"; no key: "<label>".
[[nodiscard]] std::string menuLabel(const CommandInfo& info);
// As menuLabel(info) with `label` for the table's label (the plane items' titles).
[[nodiscard]] std::string menuLabel(const CommandInfo& info, std::string_view label);
// A show-or-hide item's title where it changes instead of a check mark: "Hide " or "Show " and
// menuLabel(info) ("Hide Info\tCtrl+I", "Show Log"). The key stays in the title: wxOSX builds the
// key equivalent from the label at every SetText.
[[nodiscard]] std::string shownItemTitle(const CommandInfo& info, bool shown);
// A button's tooltip (video control panel spec §7): "<help> (<keyText>)", or the help alone for
// a command without a key. With `cmd_for_ctrl` (macOS) an Accelerator chord's "Ctrl+" reads "Cmd+",
// as the menus show it there.
[[nodiscard]] std::string tooltipText(const CommandInfo& info, bool cmd_for_ctrl);
// As tooltipText(info, cmd_for_ctrl) with `help` for the table's help (the plane buttons' tips).
[[nodiscard]] std::string tooltipText(const CommandInfo& info, std::string_view help,
                                      bool cmd_for_ctrl);

// The letters of colour components c0, c1, c2 of a clip (Plane1..3): Y U V for a Yuv format with
// chroma (dvv::media::hasChroma) and for gray, R G B for RGB, G B R for a Yuv-layout format
// without chroma (matrix Rgb: its planes hold G, B and R).
[[nodiscard]] std::array<char, 3> planeLetters(dvv::media::ColourModel model, bool has_chroma);
// "Y plane" on every OS: a plane button's name and accessible name, and its tooltip's object.
[[nodiscard]] std::string planeName(char letter);
// A plane's visible title in a menu: "Y Plane" on macOS (title case), else planeName(letter).
[[nodiscard]] std::string planeTitle(char letter);
// A plane's help, "Show or hide the Y plane": its menu items' help and its button's tooltip.
[[nodiscard]] std::string planeHelp(char letter);

}  // namespace rawreel
