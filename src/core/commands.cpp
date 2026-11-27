#include "core/commands.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/native_os.h"

namespace rawreel {

namespace {

constexpr KeyChord ctrlKey(char letter) noexcept {
    return {static_cast<std::int32_t>(letter), true, false, false};
}
constexpr KeyChord plainKey(char letter) noexcept {
    return {static_cast<std::int32_t>(letter), false, false, false};
}

constexpr auto ACC = KeyRoute::Accelerator;
constexpr auto HOOK = KeyRoute::CharHook;
// macOS dispatches the view keys only as menu accelerators; Windows and Linux also through the char
// hook (the keys '=', '+' and the keypad need normaliseKey).
struct ViewKeys {
    KeyRoute route;
    std::string_view fitLabel;
    std::string_view actualLabel;
    std::string_view zoomInLabel;
    std::string_view zoomOutLabel;
    std::string_view resizeLabel;
    KeyChord fitKey;
    KeyChord actualKey;
    KeyChord resizeKey;
};
constexpr ViewKeys MAC_VIEW_KEYS{KeyRoute::Accelerator,
                                 "Zoom to Fit",
                                 "Actual Size",
                                 "Zoom In",
                                 "Zoom Out",
                                 "Resize Window to Actual Size",
                                 {'9', true, false, false},
                                 {'0', true, false, false},
                                 {'1', true, false, false}};
constexpr ViewKeys PC_VIEW_KEYS{KeyRoute::AcceleratorAndCharHook,
                                "Fit to window",
                                "Actual size",
                                "Zoom in",
                                "Zoom out",
                                "Resize window to actual size",
                                {'0', true, false, false},
                                {'1', true, false, false},
                                {'1', false, false, true}};
constexpr const ViewKeys& VIEW_KEYS = NATIVE_OS == Os::MacOs ? MAC_VIEW_KEYS : PC_VIEW_KEYS;

// GTK calls the window Preferences (Log pane and Settings design §5.1).
constexpr std::string_view SETTINGS_LABEL =
    NATIVE_OS == Os::Linux ? "Preferences..." : "Settings...";

// In Command order (design v2 §4.3, D17, D18); menus in order File, Playback, View, Help (the GUI
// inserts Edit after File).
constexpr std::array<CommandInfo, COMMAND_COUNT> TABLE{{
    {Command::Open, "open", "File", "Open...", "Open a raw video file", ctrlKey('O'), ACC,
     MenuCheck::None},
    {Command::OpenUri, "open_uri", "File", "Open URI...", "Open a source by its URI", ctrlKey('U'),
     ACC, MenuCheck::None},
    // Log pane and Settings design §4.3: no key (rawreel has no documents, so Ctrl+S stays free).
    {Command::SaveLog, "save_log", "File", "Save Log...",
     "Save the lines of the Log pane to a file", std::nullopt, ACC, MenuCheck::None},
    {Command::SaveSelectedLines, "save_selected_lines", "File", "Save Selected Lines...",
     "Save the selected lines of the Log pane to a file", std::nullopt, ACC, MenuCheck::None},
    {Command::CloseStreams, "close_streams", "File", "Close stream", "Close the open stream",
     ctrlKey('W'), ACC, MenuCheck::None},
    // Log pane and Settings design §5.1: wxID_PREFERENCES; wxOSX moves it to the app menu.
    {Command::Settings, "settings", "File", SETTINGS_LABEL, "Change the settings of rawreel",
     ctrlKey(','), ACC, MenuCheck::None},
    {Command::Quit, "quit", "File", "Quit", "Quit rawreel", ctrlKey('Q'), ACC, MenuCheck::None},
    {Command::PlayPause, "play_pause", "Playback", "Play/Pause", "Start or pause playback",
     chord(Key::Space), HOOK, MenuCheck::None},
    {Command::Stop, "stop", "Playback", "Stop", "Stop playback and go to the first frame",
     std::nullopt, ACC, MenuCheck::None},
    {Command::StepBack, "step_back", "Playback", "Step back", "Show the previous frame",
     chord(Key::Left), HOOK, MenuCheck::None},
    {Command::StepForward, "step_forward", "Playback", "Step forward", "Show the next frame",
     chord(Key::Right), HOOK, MenuCheck::None},
    {Command::StepBack10, "step_back_10", "Playback", "Step back 10", "Go back 10 frames",
     chord(Key::Left, false, true), HOOK, MenuCheck::None},
    {Command::StepForward10, "step_forward_10", "Playback", "Step forward 10",
     "Go forward 10 frames", chord(Key::Right, false, true), HOOK, MenuCheck::None},
    {Command::First, "first", "Playback", "First frame", "Show the first frame", chord(Key::Home),
     HOOK, MenuCheck::None},
    {Command::Last, "last", "Playback", "Last frame", "Show the last frame", chord(Key::End), HOOK,
     MenuCheck::None},
    {Command::Loop, "loop", "Playback", "Loop", "Turn looping at the end on or off", plainKey('L'),
     HOOK, MenuCheck::None},
    {Command::FitToWindow, "fit_to_window", "View", VIEW_KEYS.fitLabel,
     "Fit the clip to the window", VIEW_KEYS.fitKey, VIEW_KEYS.route, MenuCheck::None},
    {Command::ActualSize, "actual_size", "View", VIEW_KEYS.actualLabel,
     "Show one clip pixel per screen pixel", VIEW_KEYS.actualKey, VIEW_KEYS.route, MenuCheck::None},
    {Command::ZoomIn, "zoom_in", "View", VIEW_KEYS.zoomInLabel, "Zoom in", chord(Key::Plus, true),
     VIEW_KEYS.route, MenuCheck::None},
    {Command::ZoomOut, "zoom_out", "View", VIEW_KEYS.zoomOutLabel, "Zoom out",
     chord(Key::Minus, true), VIEW_KEYS.route, MenuCheck::None},
    {Command::ResizeWindowToActualSize, "resize_window_to_actual_size", "View",
     VIEW_KEYS.resizeLabel, "Resize the window to the clip at actual size", VIEW_KEYS.resizeKey,
     VIEW_KEYS.route, MenuCheck::None},
    {Command::Format, "format", "View", "Format...",
     "Choose the pixel format, size, frame rate and colour of the stream", std::nullopt, ACC,
     MenuCheck::None},
    {Command::Reopen, "reopen", "View", "Restart GPU and reopen",
     "Restart the GPU (after an error) and reopen the stream at the same frame, paused",
     std::nullopt, ACC, MenuCheck::None},
    {Command::LogPane, "log_pane", "View", "Log", "Show or hide the Log pane", std::nullopt, ACC,
     MenuCheck::Shown},
    {Command::InfoWindow, "info_window", "View", "Info", "Show or hide the Info window",
     ctrlKey('I'), ACC, MenuCheck::Shown},
    {Command::Grid, "grid", "View", "Grid", "Show or hide the grid", plainKey('G'), HOOK,
     MenuCheck::Shown},
    {Command::Rulers, "rulers", "View", "Rulers", "Show or hide the rulers", plainKey('R'), HOOK,
     MenuCheck::Shown},
    {Command::GridWindow, "grid_window", "View", "Grid and Rulers...",
     "Show the Grid and Rulers window", std::nullopt, ACC, MenuCheck::None},
    // The plane items show planeTitle and planeHelp (the clip's letters), not the label and help.
    {Command::Plane1, "plane_1", "View", "Plane 1", "Show or hide plane 1", plainKey('1'), HOOK,
     MenuCheck::Mode},
    {Command::Plane2, "plane_2", "View", "Plane 2", "Show or hide plane 2", plainKey('2'), HOOK,
     MenuCheck::Mode},
    {Command::Plane3, "plane_3", "View", "Plane 3", "Show or hide plane 3", plainKey('3'), HOOK,
     MenuCheck::Mode},
    {Command::HiddenChromaZero, "hidden_chroma_zero", "View", "Hidden U and V at 0",
     "Set hidden U and V to 0 instead of the mid value", std::nullopt, ACC, MenuCheck::Mode},
    {Command::Alpha, "alpha", "View", "Alpha", "Apply or ignore the clip's alpha", plainKey('A'),
     HOOK, MenuCheck::Mode},
    {Command::Pipette, "pipette", "View", "Pipette", "Pin a pixel for the Zoom window",
     plainKey('P'), HOOK, MenuCheck::Mode},
    {Command::LiveProbe, "live_probe", "View", "Live Probe",
     "Show the pixel under the cursor in the Zoom window", KeyChord{'P', false, true, false}, HOOK,
     MenuCheck::Mode},
    {Command::ZoomWindow, "zoom_window", "View", "Zoom Window", "Show or hide the Zoom window",
     std::nullopt, ACC, MenuCheck::Shown},
    {Command::ClearPin, "clear_pin", "View", "Clear Pin", "Remove the pinned pixel", std::nullopt,
     ACC, MenuCheck::None},
    {Command::CopyValues, "copy_values", "View", "Copy Pixel Values",
     "Copy the pixel's code values and raw words", std::nullopt, ACC, MenuCheck::None},
    {Command::PaddingCheck, "padding_check", "View", "Padding Check",
     "Mark samples whose padding bits are not 0", plainKey('X'), HOOK, MenuCheck::Mode},
    {Command::GpuInfo, "gpu_info", "Help", "GPU info", "Show the GPU and its driver", std::nullopt,
     ACC, MenuCheck::None},
    {Command::Diagnostics, "diagnostics", "Help", "Diagnostics",
     "Show the pipeline statistics and the log", std::nullopt, ACC, MenuCheck::None},
    {Command::About, "about", "Help", "About rawreel",
     "Show the versions, the licence and the FFmpeg status", std::nullopt, ACC, MenuCheck::None},
}};

consteval bool tableInOrder() {
    for (std::size_t i = 0; i < TABLE.size(); ++i) {
        if (TABLE[i].command != static_cast<Command>(i)) {
            return false;
        }
    }
    return true;
}
static_assert(tableInOrder(), "the command table must be in Command order");

// wx 3.3 wxKeyCode values (wx/defs.h); rawreel_core has no wx, the GUI checks them (Task 10).
constexpr std::int32_t WX_ESCAPE = 27;
constexpr std::int32_t WX_SPACE = 32;
constexpr std::int32_t WX_END = 312;
constexpr std::int32_t WX_HOME = 313;
constexpr std::int32_t WX_LEFT = 314;
constexpr std::int32_t WX_UP = 315;
constexpr std::int32_t WX_RIGHT = 316;
constexpr std::int32_t WX_DOWN = 317;
constexpr std::int32_t WX_NUMPAD0 = 324;
constexpr std::int32_t WX_NUMPAD9 = 333;
constexpr std::int32_t WX_ADD = 335;
constexpr std::int32_t WX_SUBTRACT = 337;
constexpr std::int32_t WX_F11 = 350;
constexpr std::int32_t WX_NUMPAD_ADD = 388;
constexpr std::int32_t WX_NUMPAD_SUBTRACT = 390;

}  // namespace

std::span<const CommandInfo> commandTable() noexcept {
    return TABLE;
}

std::optional<Command> commandForKey(const KeyChord& chord, bool focus_keeps_keys) noexcept {
    if (focus_keeps_keys) {
        return std::nullopt;
    }
    for (const CommandInfo& info : TABLE) {
        if (info.route != KeyRoute::Accelerator && info.key == chord) {
            return info.command;
        }
    }
    return std::nullopt;
}

std::optional<Command> commandOfChord(const KeyChord& chord) noexcept {
    for (const CommandInfo& info : TABLE) {
        if (info.key == chord) {
            return info.command;
        }
    }
    return std::nullopt;
}

KeyChord normaliseKey(std::int32_t keycode, bool ctrl, bool shift, bool alt) noexcept {
    const auto make = [&](Key key) {
        return KeyChord{static_cast<std::int32_t>(key), ctrl, shift, alt};
    };
    switch (keycode) {
        case WX_ESCAPE: return make(Key::Escape);
        case WX_SPACE: return make(Key::Space);
        case WX_END: return make(Key::End);
        case WX_HOME: return make(Key::Home);
        case WX_LEFT: return make(Key::Left);
        case WX_UP: return make(Key::Up);
        case WX_RIGHT: return make(Key::Right);
        case WX_DOWN: return make(Key::Down);
        case WX_F11: return make(Key::F11);
        case '=':
        case '+':
        case WX_ADD:
        case WX_NUMPAD_ADD: return {static_cast<std::int32_t>(Key::Plus), ctrl, false, alt};
        case '-':
        case WX_SUBTRACT:
        case WX_NUMPAD_SUBTRACT: return {static_cast<std::int32_t>(Key::Minus), ctrl, false, alt};
        default: break;
    }
    if (keycode >= WX_NUMPAD0 && keycode <= WX_NUMPAD9) {
        return {'0' + (keycode - WX_NUMPAD0), ctrl, shift, alt};
    }
    if (keycode >= 'a' && keycode <= 'z') {
        return {keycode - 'a' + 'A', ctrl, shift, alt};
    }
    return {keycode, ctrl, shift, alt};
}

std::string keyText(const KeyChord& chord) {
    std::string text;
    if (chord.ctrl) {
        text += "Ctrl+";
    }
    if (chord.shift) {
        text += "Shift+";
    }
    if (chord.alt) {
        text += "Alt+";
    }
    switch (static_cast<Key>(chord.key)) {
        case Key::Space: return text + "Space";
        case Key::Plus: return text + "+";
        case Key::Minus: return text + "-";
        case Key::Left: return text + "Left";
        case Key::Right: return text + "Right";
        case Key::Up: return text + "Up";
        case Key::Down: return text + "Down";
        case Key::Home: return text + "Home";
        case Key::End: return text + "End";
        case Key::Escape: return text + "Esc";
        case Key::F11: return text + "F11";
        default: break;
    }
    if (chord.key > ' ' && chord.key < 0x7F) {
        text += static_cast<char>(chord.key);
    } else {
        text += std::format("#{}", chord.key);  // no command uses such a key
    }
    return text;
}

std::string menuLabel(const CommandInfo& info) {
    return menuLabel(info, info.label);
}

std::string menuLabel(const CommandInfo& info, std::string_view label) {
    std::string text(label);
    if (!info.key) {
        return text;
    }
    if (info.route != KeyRoute::CharHook) {
        return text + "\t" + keyText(*info.key);
    }
    return text + " (" + keyText(*info.key) + ")";
}

std::string shownItemTitle(const CommandInfo& info, bool shown) {
    return (shown ? "Hide " : "Show ") + menuLabel(info);
}

std::string tooltipText(const CommandInfo& info, bool cmd_for_ctrl) {
    return tooltipText(info, info.help, cmd_for_ctrl);
}

std::string tooltipText(const CommandInfo& info, std::string_view help, bool cmd_for_ctrl) {
    std::string text(help);
    if (!info.key) {
        return text;
    }
    std::string key = keyText(*info.key);
    if (cmd_for_ctrl && info.route != KeyRoute::CharHook && info.key->ctrl) {
        key.replace(0, std::string_view("Ctrl").size(), "Cmd");
    }
    return text + " (" + key + ")";
}

std::array<char, 3> planeLetters(dvv::media::ColourModel model, bool has_chroma) {
    switch (model) {
        case dvv::media::ColourModel::Rgb: return {'R', 'G', 'B'};
        case dvv::media::ColourModel::Yuv:
            if (!has_chroma) {
                return {'G', 'B', 'R'};  // matrix Rgb: the planes hold G, B and R
            }
            return {'Y', 'U', 'V'};
        case dvv::media::ColourModel::Gray: break;
    }
    return {'Y', 'U', 'V'};
}

std::string planeName(char letter) {
    return std::string(1, letter) + " plane";
}

std::string planeTitle(char letter) {
    return NATIVE_OS == Os::MacOs ? std::string(1, letter) + " Plane" : planeName(letter);
}

std::string planeHelp(char letter) {
    return "Show or hide the " + planeName(letter);
}

}  // namespace rawreel
