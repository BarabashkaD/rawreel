#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/commands.h"

using rawreel::chord;
using rawreel::Command;
using rawreel::COMMAND_COUNT;
using rawreel::commandForKey;
using rawreel::commandTable;
using rawreel::Key;
using rawreel::KeyChord;
using rawreel::KeyRoute;

namespace {

const rawreel::CommandInfo& info(Command command) {
    REQUIRE(commandTable().size() == COMMAND_COUNT);
    return commandTable()[static_cast<std::size_t>(command)];
}

KeyChord letter(char c, bool ctrl = false) {
    return {static_cast<std::int32_t>(c), ctrl};
}

// Log pane and Settings design §5.1: GTK calls the window Preferences.
#if defined(__linux__)
constexpr std::string_view SETTINGS_LABEL = "Preferences...";
#else
constexpr std::string_view SETTINGS_LABEL = "Settings...";
#endif

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("commands: the table lists every command once, in order") {
        static_assert(COMMAND_COUNT == 23);
        const auto table = commandTable();
        REQUIRE(table.size() == COMMAND_COUNT);
        std::set<std::string_view> ids;
        const std::set<std::string_view> menus{"File", "Playback", "View", "Help"};
        for (std::size_t i = 0; i < table.size(); ++i) {
            CAPTURE(i);
            CHECK(table[i].command == static_cast<Command>(i));
            CHECK(ids.insert(table[i].id).second);
            CHECK_FALSE(table[i].id.empty());
            CHECK(menus.contains(table[i].menu));
            CHECK_FALSE(table[i].label.empty());
            CHECK_FALSE(table[i].help.empty());
        }
    }

    TEST_CASE("commands: no two commands share a key") {
        const auto table = commandTable();
        for (std::size_t i = 0; i < table.size(); ++i) {
            CAPTURE(table[i].id);
            const auto& key = table[i].key;
            for (std::size_t j = i + 1; j < table.size(); ++j) {
                if (key && table[j].key) {
                    CHECK_FALSE(key == table[j].key);  // both set: compares the keys
                }
            }
            if (key) {
                if (table[i].route == KeyRoute::Accelerator) {
                    CHECK(key->ctrl);
                } else {
                    CHECK_FALSE(key->ctrl);
                }
            }
        }
        for (const Command command :
             {Command::Stop, Command::Format, Command::Reopen, Command::LogPane, Command::GpuInfo,
              Command::Diagnostics, Command::SaveLog, Command::SaveSelectedLines}) {
            CHECK_FALSE(info(command).key);
        }
    }

    TEST_CASE("commands: View -> Log") {
        static_assert(static_cast<std::size_t>(Command::LogPane) ==
                      static_cast<std::size_t>(Command::Reopen) + 1);
        const rawreel::CommandInfo& log = info(Command::LogPane);
        CHECK(log.id == "log_pane");
        CHECK(log.menu == "View");
        CHECK(log.label == "Log");  // "Show Log"/"Hide Log" on macOS, set by the GUI
        CHECK(log.help == "Show or hide the Log pane");
        CHECK(log.checkable);
        CHECK_FALSE(log.key);
        CHECK(log.route == KeyRoute::Accelerator);
        CHECK(rawreel::menuLabel(log) == "Log");
    }

    TEST_CASE("commands: View -> Info") {
        static_assert(static_cast<std::size_t>(Command::InfoWindow) ==
                      static_cast<std::size_t>(Command::LogPane) + 1);
        const rawreel::CommandInfo& info_window = info(Command::InfoWindow);
        CHECK(info_window.id == "info_window");
        CHECK(info_window.menu == "View");
        CHECK(info_window.label == "Info");  // "Show Info"/"Hide Info" on macOS, set by the GUI
        CHECK(info_window.help == "Show or hide the Info window");
        CHECK(info_window.key == letter('I', true));
        CHECK(info_window.route == KeyRoute::Accelerator);
        CHECK(info_window.checkable);
        CHECK(rawreel::menuLabel(info_window) == "Info\tCtrl+I");
    }

    TEST_CASE("commands: File -> Save Log... and Save Selected Lines...") {
        static_assert(static_cast<std::size_t>(Command::SaveLog) ==
                      static_cast<std::size_t>(Command::OpenUri) + 1);
        static_assert(static_cast<std::size_t>(Command::SaveSelectedLines) ==
                      static_cast<std::size_t>(Command::SaveLog) + 1);
        const rawreel::CommandInfo& save = info(Command::SaveLog);
        CHECK(save.id == "save_log");
        CHECK(save.menu == "File");
        CHECK(save.label == "Save Log...");
        CHECK(save.help == "Save the lines of the Log pane to a file");
        CHECK_FALSE(save.key);  // spec §4.3: Ctrl+S stays free
        CHECK(save.route == KeyRoute::Accelerator);
        CHECK_FALSE(save.checkable);
        CHECK(rawreel::menuLabel(save) == "Save Log...");
        const rawreel::CommandInfo& selected = info(Command::SaveSelectedLines);
        CHECK(selected.id == "save_selected_lines");
        CHECK(selected.menu == "File");
        CHECK(selected.label == "Save Selected Lines...");
        CHECK(selected.help == "Save the selected lines of the Log pane to a file");
        CHECK_FALSE(selected.key);
        CHECK(selected.route == KeyRoute::Accelerator);
        CHECK_FALSE(selected.checkable);
        CHECK(rawreel::menuLabel(selected) == "Save Selected Lines...");
    }

    TEST_CASE("commands: the File menu's order") {
        std::vector<std::string_view> file;
        for (const rawreel::CommandInfo& entry : commandTable()) {
            if (entry.menu == "File") {
                file.push_back(entry.label);
            }
        }
        const std::vector<std::string_view> expected{
            "Open...",      "Open URI...",  "Save Log...", "Save Selected Lines...",
            "Close stream", SETTINGS_LABEL, "Quit"};
        CHECK(file == expected);
    }

    TEST_CASE("commands: File -> Settings...") {
        static_assert(static_cast<std::size_t>(Command::Settings) ==
                      static_cast<std::size_t>(Command::CloseStreams) + 1);
        static_assert(static_cast<std::size_t>(Command::Quit) ==
                      static_cast<std::size_t>(Command::Settings) + 1);
        const rawreel::CommandInfo& settings = info(Command::Settings);
        CHECK(settings.id == "settings");
        CHECK(settings.menu == "File");  // wxOSX moves it to the app menu, GTK's GUI to Edit
        CHECK(settings.label == SETTINGS_LABEL);
        CHECK(settings.help == "Change the settings of rawreel");
        CHECK(settings.key.has_value());
        if (settings.key) {
            CHECK(*settings.key == letter(',', true));
        }
        CHECK(settings.route == KeyRoute::Accelerator);
        CHECK_FALSE(settings.checkable);
        CHECK(rawreel::menuLabel(settings) == std::string(SETTINGS_LABEL) + "\tCtrl+,");
    }

    TEST_CASE("commands: only View -> Log and View -> Info are checkable") {
        for (const rawreel::CommandInfo& entry : commandTable()) {
            CAPTURE(entry.id);
            CHECK(entry.checkable ==
                  (entry.command == Command::LogPane || entry.command == Command::InfoWindow));
        }
    }

    TEST_CASE("commands: commandForKey") {
        CHECK(commandForKey(chord(Key::Space), false) == Command::PlayPause);
        CHECK(commandForKey(chord(Key::Left), false) == Command::StepBack);
        CHECK(commandForKey(chord(Key::Right), false) == Command::StepForward);
        CHECK(commandForKey(chord(Key::Left, false, true), false) == Command::StepBack10);
        CHECK(commandForKey(chord(Key::Right, false, true), false) == Command::StepForward10);
        CHECK(commandForKey(chord(Key::Home), false) == Command::First);
        CHECK(commandForKey(chord(Key::End), false) == Command::Last);
        CHECK(commandForKey(letter('L'), false) == Command::Loop);
        CHECK_FALSE(commandForKey(chord(Key::Plus), false));
        CHECK_FALSE(commandForKey(letter('G'), false));
        CHECK_FALSE(commandForKey(letter('1'), false));
        CHECK_FALSE(commandForKey(chord(Key::Escape), false));
        CHECK_FALSE(commandForKey(chord(Key::Space), true));
        CHECK_FALSE(commandForKey(letter('O', true), false));
        CHECK_FALSE(commandForKey(letter('Q'), false));
    }

    TEST_CASE("commands: normaliseKey") {
        using rawreel::normaliseKey;
        CHECK(normaliseKey(61, false, true, false) == chord(Key::Plus));
        for (const std::int32_t code : {43, 335, 388}) {
            CHECK(normaliseKey(code, false, false, false) == chord(Key::Plus));
        }
        for (const std::int32_t code : {45, 337, 390}) {
            CHECK(normaliseKey(code, false, false, false) == chord(Key::Minus));
        }
        for (std::int32_t digit = 0; digit < 10; ++digit) {
            CHECK(normaliseKey(324 + digit, false, false, false) ==
                  letter(static_cast<char>('0' + digit)));
        }
        CHECK(normaliseKey('a', false, false, false) == letter('A'));
        CHECK(normaliseKey(314, false, true, false) == chord(Key::Left, false, true));
        CHECK(normaliseKey(27, false, false, false) == chord(Key::Escape));
        CHECK(normaliseKey(350, false, false, false) == chord(Key::F11));
        CHECK(normaliseKey(32, false, false, false) == chord(Key::Space));
        CHECK(normaliseKey(79, true, false, false) == letter('O', true));
        CHECK(normaliseKey(312, false, false, false) == chord(Key::End));
        CHECK(normaliseKey(313, false, false, false) == chord(Key::Home));
        CHECK(normaliseKey(315, false, false, false) == chord(Key::Up));
        CHECK(normaliseKey(316, false, false, false) == chord(Key::Right));
        CHECK(normaliseKey(317, false, false, false) == chord(Key::Down));
        CHECK(normaliseKey('L', false, false, true) == KeyChord{'L', false, false, true});
        CHECK(normaliseKey(1234, false, false, false) == KeyChord{1234});
    }

    TEST_CASE("commands: menu labels") {
        using rawreel::keyText;
        using rawreel::menuLabel;
        CHECK(menuLabel(info(Command::Open)) == "Open...\tCtrl+O");
        CHECK(menuLabel(info(Command::OpenUri)) == "Open URI...\tCtrl+U");
        CHECK(menuLabel(info(Command::Quit)) == "Quit\tCtrl+Q");
        CHECK(menuLabel(info(Command::PlayPause)) == "Play/Pause (Space)");
        CHECK(menuLabel(info(Command::StepBack10)) == "Step back 10 (Shift+Left)");
        CHECK(menuLabel(info(Command::Loop)) == "Loop (L)");
        CHECK(menuLabel(info(Command::First)) == "First frame (Home)");
        CHECK(menuLabel(info(Command::Stop)) == "Stop");
        CHECK(menuLabel(info(Command::Reopen)) == "Restart GPU and reopen");  // M4 GUI D10
        CHECK(info(Command::Reopen).id == "reopen");
        CHECK(info(Command::About).menu == "Help");
        CHECK(info(Command::About).label == "About rawreel");
        CHECK_FALSE(info(Command::About).key.has_value());
        CHECK(menuLabel(info(Command::About)) == "About rawreel");
        CHECK(keyText(chord(Key::F11)) == "F11");
        CHECK(keyText(chord(Key::Escape)) == "Esc");
        CHECK(keyText(KeyChord{'O', true, true}) == "Ctrl+Shift+O");
        CHECK(keyText(chord(Key::Plus)) == "+");
        CHECK(keyText(chord(Key::Minus, false, false)) == "-");
        CHECK(keyText(letter('1')) == "1");
    }

    TEST_CASE("commands: tooltips read the help and the key, Cmd for an Accelerator on macOS") {
        using rawreel::tooltipText;
        CHECK(tooltipText(info(Command::PlayPause), false) == "Start or pause playback (Space)");
        CHECK(tooltipText(info(Command::Stop), false) == "Stop playback and go to the first frame");
        CHECK(tooltipText(info(Command::Open), false) == "Open a raw video file (Ctrl+O)");
        CHECK(tooltipText(info(Command::Open), true) == "Open a raw video file (Cmd+O)");
        CHECK(tooltipText(info(Command::StepBack), true) == "Show the previous frame (Left)");
    }
}
