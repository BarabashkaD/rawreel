#include "core/commands.h"

#include <array>
#include <format>

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

// In Command order (design v2 §4.3, D17, D18); menus in order File, Playback, View, Help.
constexpr std::array<CommandInfo, COMMAND_COUNT> TABLE{{
    {Command::Open, "open", "File", "Open...", "Open a raw video file", ctrlKey('O'), ACC},
    {Command::OpenUri, "open_uri", "File", "Open URI...", "Open a source by its URI", ctrlKey('U'),
     ACC},
    {Command::CloseStreams, "close_streams", "File", "Close stream", "Close the open stream",
     ctrlKey('W'), ACC},
    {Command::Quit, "quit", "File", "Quit", "Quit rawreel", ctrlKey('Q'), ACC},
    {Command::PlayPause, "play_pause", "Playback", "Play/Pause", "Start or pause playback",
     chord(Key::Space), HOOK},
    {Command::Stop, "stop", "Playback", "Stop", "Stop playback and go to the first frame",
     std::nullopt, ACC},
    {Command::StepBack, "step_back", "Playback", "Step back", "Show the previous frame",
     chord(Key::Left), HOOK},
    {Command::StepForward, "step_forward", "Playback", "Step forward", "Show the next frame",
     chord(Key::Right), HOOK},
    {Command::StepBack10, "step_back_10", "Playback", "Step back 10", "Go back 10 frames",
     chord(Key::Left, false, true), HOOK},
    {Command::StepForward10, "step_forward_10", "Playback", "Step forward 10",
     "Go forward 10 frames", chord(Key::Right, false, true), HOOK},
    {Command::First, "first", "Playback", "First frame", "Show the first frame", chord(Key::Home),
     HOOK},
    {Command::Last, "last", "Playback", "Last frame", "Show the last frame", chord(Key::End), HOOK},
    {Command::Loop, "loop", "Playback", "Loop", "Turn looping at the end on or off", plainKey('L'),
     HOOK},
    {Command::Format, "format", "View", "Format...",
     "Choose the pixel format, size, frame rate and colour of the stream", std::nullopt, ACC},
    {Command::Reopen, "reopen", "View", "Restart GPU and reopen",
     "Restart the GPU (after an error) and reopen the stream at the same frame, paused",
     std::nullopt, ACC},
    {Command::GpuInfo, "gpu_info", "Help", "GPU info", "Show the GPU and its driver", std::nullopt,
     ACC},
    {Command::Diagnostics, "diagnostics", "Help", "Diagnostics",
     "Show the pipeline statistics and the log", std::nullopt, ACC},
    {Command::About, "about", "Help", "About rawreel",
     "Show the versions, the licence and the FFmpeg status", std::nullopt, ACC},
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

std::optional<Command> commandForKey(const KeyChord& chord, bool text_focus) noexcept {
    if (text_focus) {
        return std::nullopt;
    }
    for (const CommandInfo& info : TABLE) {
        if (info.route == KeyRoute::CharHook && info.key == chord) {
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
    std::string label(info.label);
    if (!info.key) {
        return label;
    }
    if (info.route == KeyRoute::Accelerator) {
        return label + "\t" + keyText(*info.key);
    }
    return label + " (" + keyText(*info.key) + ")";
}

}  // namespace rawreel
