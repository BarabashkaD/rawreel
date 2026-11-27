#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/commands.h"
#include "core/native_os.h"
#include "dvv/media/pixel_format.h"

using rawreel::chord;
using rawreel::Command;
using rawreel::COMMAND_COUNT;
using rawreel::commandForKey;
using rawreel::commandTable;
using rawreel::Key;
using rawreel::KeyChord;
using rawreel::KeyRoute;
using rawreel::MenuCheck;

namespace {

const rawreel::CommandInfo& info(Command command) {
    REQUIRE(commandTable().size() == COMMAND_COUNT);
    return commandTable()[static_cast<std::size_t>(command)];
}

KeyChord letter(char c, bool ctrl = false) {
    return {static_cast<std::int32_t>(c), ctrl};
}

// Log pane and Settings design §5.1: GTK calls the window Preferences.
constexpr std::string_view SETTINGS_LABEL =
    rawreel::NATIVE_OS == rawreel::Os::Linux ? "Preferences..." : "Settings...";

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("commands: the table lists every command once, in order") {
        static_assert(COMMAND_COUNT == 42);
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
                if (table[i].route == KeyRoute::CharHook) {
                    CHECK_FALSE(key->ctrl);
                    CHECK_FALSE(key->alt);
                } else {
                    CHECK((key->ctrl || key->alt));
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
        CHECK(log.check == MenuCheck::Shown);
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
        CHECK(info_window.check == MenuCheck::Shown);
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
        CHECK(save.check == MenuCheck::None);
        CHECK(rawreel::menuLabel(save) == "Save Log...");
        const rawreel::CommandInfo& selected = info(Command::SaveSelectedLines);
        CHECK(selected.id == "save_selected_lines");
        CHECK(selected.menu == "File");
        CHECK(selected.label == "Save Selected Lines...");
        CHECK(selected.help == "Save the selected lines of the Log pane to a file");
        CHECK_FALSE(selected.key);
        CHECK(selected.route == KeyRoute::Accelerator);
        CHECK(selected.check == MenuCheck::None);
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
        CHECK(settings.check == MenuCheck::None);
        CHECK(rawreel::menuLabel(settings) == std::string(SETTINGS_LABEL) + "\tCtrl+,");
    }

    TEST_CASE("commands: Log, Info, Grid and Rulers are shown parts; planes, fill, Alpha modes") {
        for (const rawreel::CommandInfo& entry : commandTable()) {
            CAPTURE(entry.id);
            const bool shown = entry.command == Command::LogPane ||
                               entry.command == Command::InfoWindow ||
                               entry.command == Command::Grid || entry.command == Command::Rulers ||
                               entry.command == Command::ZoomWindow;
            const bool mode =
                entry.command == Command::Plane1 || entry.command == Command::Plane2 ||
                entry.command == Command::Plane3 || entry.command == Command::HiddenChromaZero ||
                entry.command == Command::Alpha || entry.command == Command::Pipette ||
                entry.command == Command::LiveProbe || entry.command == Command::PaddingCheck;
            MenuCheck expected = MenuCheck::None;
            if (shown) {
                expected = MenuCheck::Shown;
            } else if (mode) {
                expected = MenuCheck::Mode;
            }
            CHECK(entry.check == expected);
        }
    }

    TEST_CASE("commands: View -> Alpha") {
        static_assert(static_cast<std::size_t>(Command::Alpha) ==
                      static_cast<std::size_t>(Command::HiddenChromaZero) + 1);
        const rawreel::CommandInfo& alpha = info(Command::Alpha);
        CHECK(alpha.id == "alpha");
        CHECK(alpha.menu == "View");
        CHECK(alpha.label == "Alpha");
        CHECK(alpha.help == "Apply or ignore the clip's alpha");
        CHECK(alpha.key == letter('A'));
        CHECK(alpha.route == KeyRoute::CharHook);
        CHECK(alpha.check == MenuCheck::Mode);
        CHECK(rawreel::menuLabel(alpha) == "Alpha (A)");
        CHECK(commandForKey(letter('A'), false) == Command::Alpha);
        CHECK(rawreel::commandOfChord(letter('A')) == Command::Alpha);
        for (const rawreel::CommandInfo& entry : commandTable()) {
            CAPTURE(entry.id);
            if (entry.command != Command::Alpha) {
                CHECK(entry.key != letter('A'));
            }
        }
    }

    TEST_CASE("commands: View -> the plane items and the hidden U and V fill") {
        static_assert(static_cast<std::size_t>(Command::Plane1) ==
                      static_cast<std::size_t>(Command::GridWindow) + 1);
        static_assert(static_cast<std::size_t>(Command::Plane2) ==
                      static_cast<std::size_t>(Command::Plane1) + 1);
        static_assert(static_cast<std::size_t>(Command::Plane3) ==
                      static_cast<std::size_t>(Command::Plane2) + 1);
        static_assert(static_cast<std::size_t>(Command::HiddenChromaZero) ==
                      static_cast<std::size_t>(Command::Plane3) + 1);
        struct Row {
            Command command;
            std::string_view id, label, help;
            char key;
        };
        for (const Row& row : {
                 Row{Command::Plane1, "plane_1", "Plane 1", "Show or hide plane 1", '1'},
                 Row{Command::Plane2, "plane_2", "Plane 2", "Show or hide plane 2", '2'},
                 Row{Command::Plane3, "plane_3", "Plane 3", "Show or hide plane 3", '3'},
             }) {
            const rawreel::CommandInfo& entry = info(row.command);
            CAPTURE(entry.id);
            CHECK(entry.id == row.id);
            CHECK(entry.menu == "View");
            CHECK(entry.label == row.label);
            CHECK(entry.help == row.help);
            CHECK(entry.key == letter(row.key));
            CHECK(entry.route == KeyRoute::CharHook);
            CHECK(entry.check == MenuCheck::Mode);
            CHECK(commandForKey(letter(row.key), false) == row.command);
            CHECK_FALSE(commandForKey(letter(row.key), true));
            CHECK(rawreel::commandOfChord(letter(row.key)) == row.command);
        }
        const rawreel::CommandInfo& fill = info(Command::HiddenChromaZero);
        CHECK(fill.id == "hidden_chroma_zero");
        CHECK(fill.menu == "View");
        CHECK(fill.label == "Hidden U and V at 0");
        CHECK(fill.help == "Set hidden U and V to 0 instead of the mid value");
        CHECK_FALSE(fill.key);
        CHECK(fill.route == KeyRoute::Accelerator);
        CHECK(fill.check == MenuCheck::Mode);
        CHECK(rawreel::menuLabel(fill) == "Hidden U and V at 0");
        // The keypad's 1 to 3 (WXK_NUMPAD0 + 1..3) give the plain chords.
        CHECK(rawreel::normaliseKey(325, false, false, false) == letter('1'));
        CHECK(rawreel::normaliseKey(326, false, false, false) == letter('2'));
        CHECK(rawreel::normaliseKey(327, false, false, false) == letter('3'));
    }

    TEST_CASE("commands: plane letters, names and titles") {
        using dvv::media::ColourModel;
        using Letters = std::array<char, 3>;
        CHECK(rawreel::planeLetters(ColourModel::Rgb, false) == Letters{'R', 'G', 'B'});
        CHECK(rawreel::planeLetters(ColourModel::Yuv, true) == Letters{'Y', 'U', 'V'});
        CHECK(rawreel::planeLetters(ColourModel::Yuv, false) == Letters{'G', 'B', 'R'});
        CHECK(rawreel::planeLetters(ColourModel::Gray, false) == Letters{'Y', 'U', 'V'});
        const bool mac = rawreel::NATIVE_OS == rawreel::Os::MacOs;
        CHECK(rawreel::planeTitle('Y') == (mac ? "Y Plane" : "Y plane"));
        CHECK(rawreel::planeName('U') == "U plane");
        CHECK(rawreel::planeName('R') == "R plane");
        CHECK(rawreel::menuLabel(info(Command::Plane1), rawreel::planeTitle('Y')) ==
              (mac ? "Y Plane (1)" : "Y plane (1)"));
        CHECK(rawreel::menuLabel(info(Command::Plane3), rawreel::planeTitle('B')) ==
              (mac ? "B Plane (3)" : "B plane (3)"));
        CHECK(rawreel::menuLabel(info(Command::Plane1)) == "Plane 1 (1)");
        CHECK(rawreel::planeHelp('G') == "Show or hide the G plane");
        CHECK(rawreel::tooltipText(info(Command::Plane1), rawreel::planeHelp('Y'), mac) ==
              "Show or hide the Y plane (1)");
        CHECK(rawreel::tooltipText(info(Command::Plane1), mac) == "Show or hide plane 1 (1)");
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
        CHECK_FALSE(commandForKey(letter('J'), false));
        CHECK(commandForKey(letter('1'), false) == Command::Plane1);
        CHECK_FALSE(commandForKey(letter('4'), false));
        CHECK_FALSE(commandForKey(chord(Key::Escape), false));
        CHECK_FALSE(commandForKey(chord(Key::Space), true));
        CHECK_FALSE(commandForKey(letter('O', true), false));
        CHECK_FALSE(commandForKey(letter('Q'), false));
    }

    TEST_CASE("commands: commandOfChord matches every route") {
        CHECK(rawreel::commandOfChord(letter('O', true)) == Command::Open);
        CHECK(rawreel::commandOfChord(chord(Key::Space)) == Command::PlayPause);
        CHECK(rawreel::commandOfChord(letter('I', true)) == Command::InfoWindow);
        CHECK(rawreel::commandOfChord(chord(Key::Plus, true)) == Command::ZoomIn);
        CHECK_FALSE(rawreel::commandOfChord(KeyChord{.key = 'Q', .ctrl = true, .shift = true}));
        CHECK_FALSE(rawreel::commandOfChord(chord(Key::Escape)));
    }

    TEST_CASE("commands: shownItemTitle") {
        const rawreel::CommandInfo& info_window = info(Command::InfoWindow);
        REQUIRE(info_window.key.has_value());
        const std::string key = rawreel::keyText(info_window.key.value_or(rawreel::KeyChord{}));
        CHECK(rawreel::shownItemTitle(info_window, true) == "Hide Info\t" + key);
        CHECK(rawreel::shownItemTitle(info_window, false) == "Show Info\t" + key);
        CHECK(key == "Ctrl+I");
        CHECK(rawreel::shownItemTitle(info(Command::LogPane), true) == "Hide Log");
        CHECK(rawreel::shownItemTitle(info(Command::LogPane), false) == "Show Log");
    }

    TEST_CASE("commands: the grid commands") {
        static_assert(static_cast<std::size_t>(Command::Grid) ==
                      static_cast<std::size_t>(Command::InfoWindow) + 1);
        static_assert(static_cast<std::size_t>(Command::Rulers) ==
                      static_cast<std::size_t>(Command::Grid) + 1);
        const rawreel::CommandInfo& grid = info(Command::Grid);
        const rawreel::CommandInfo& rulers = info(Command::Rulers);
        CHECK(grid.id == "grid");
        CHECK(rulers.id == "rulers");
        CHECK(grid.menu == "View");
        CHECK(rulers.menu == "View");
        CHECK(grid.help == "Show or hide the grid");
        CHECK(rulers.help == "Show or hide the rulers");
        CHECK(grid.route == rawreel::KeyRoute::CharHook);
        CHECK(rulers.route == rawreel::KeyRoute::CharHook);
        CHECK(grid.check == MenuCheck::Shown);
        CHECK(rulers.check == MenuCheck::Shown);
        CHECK(rawreel::menuLabel(grid) == "Grid (G)");
        CHECK(rawreel::menuLabel(rulers) == "Rulers (R)");
        CHECK(commandForKey(letter('G'), false) == Command::Grid);
        CHECK(commandForKey(letter('R'), false) == Command::Rulers);
        CHECK(rawreel::shownItemTitle(grid, false) == "Show Grid (G)");
        CHECK(rawreel::shownItemTitle(rulers, true) == "Hide Rulers (R)");
        static_assert(static_cast<std::size_t>(Command::GridWindow) ==
                      static_cast<std::size_t>(Command::Rulers) + 1);
        const rawreel::CommandInfo& window = info(Command::GridWindow);
        CHECK(window.id == "grid_window");
        CHECK(window.menu == "View");
        CHECK(window.help == "Show the Grid and Rulers window");
        CHECK_FALSE(window.key);
        CHECK(window.route == rawreel::KeyRoute::Accelerator);
        CHECK(window.check == MenuCheck::None);
        CHECK(rawreel::menuLabel(window) == "Grid and Rulers...");
    }

    TEST_CASE("commands: the view commands") {
        static_assert(static_cast<std::size_t>(Command::FitToWindow) ==
                      static_cast<std::size_t>(Command::Loop) + 1);
        static_assert(static_cast<std::size_t>(Command::Format) ==
                      static_cast<std::size_t>(Command::ResizeWindowToActualSize) + 1);
#if defined(__APPLE__)
        constexpr KeyRoute ROUTE = KeyRoute::Accelerator;
        const std::string_view labels[5] = {"Zoom to Fit", "Actual Size", "Zoom In", "Zoom Out",
                                            "Resize Window to Actual Size"};
        const KeyChord keys[5] = {letter('9', true), letter('0', true), chord(Key::Plus, true),
                                  chord(Key::Minus, true), letter('1', true)};
#else
        constexpr KeyRoute ROUTE = KeyRoute::AcceleratorAndCharHook;
        const std::string_view labels[5] = {"Fit to window", "Actual size", "Zoom in", "Zoom out",
                                            "Resize window to actual size"};
        const KeyChord keys[5] = {letter('0', true), letter('1', true), chord(Key::Plus, true),
                                  chord(Key::Minus, true), KeyChord{'1', false, false, true}};
#endif
        const std::string_view ids[5] = {"fit_to_window", "actual_size", "zoom_in", "zoom_out",
                                         "resize_window_to_actual_size"};
        const std::string_view helps[5] = {
            "Fit the clip to the window", "Show one clip pixel per screen pixel", "Zoom in",
            "Zoom out", "Resize the window to the clip at actual size"};
        const Command commands[5] = {Command::FitToWindow, Command::ActualSize, Command::ZoomIn,
                                     Command::ZoomOut, Command::ResizeWindowToActualSize};
        for (std::size_t i = 0; i < 5; ++i) {
            CAPTURE(ids[i]);
            const rawreel::CommandInfo& row = info(commands[i]);
            CHECK(row.id == ids[i]);
            CHECK(row.menu == "View");
            CHECK(row.label == labels[i]);
            CHECK(row.help == helps[i]);
            CHECK(row.key == keys[i]);
            CHECK(row.route == ROUTE);
            CHECK(row.check == MenuCheck::None);
        }
    }

    TEST_CASE("commands: menu labels of the view commands") {
        using rawreel::menuLabel;
#if defined(__APPLE__)
        CHECK(menuLabel(info(Command::FitToWindow)) == "Zoom to Fit\tCtrl+9");
        CHECK(menuLabel(info(Command::ActualSize)) == "Actual Size\tCtrl+0");
        CHECK(menuLabel(info(Command::ZoomIn)) == "Zoom In\tCtrl++");
        CHECK(menuLabel(info(Command::ZoomOut)) == "Zoom Out\tCtrl+-");
        CHECK(menuLabel(info(Command::ResizeWindowToActualSize)) ==
              "Resize Window to Actual Size\tCtrl+1");
#else
        CHECK(menuLabel(info(Command::FitToWindow)) == "Fit to window\tCtrl+0");
        CHECK(menuLabel(info(Command::ActualSize)) == "Actual size\tCtrl+1");
        CHECK(menuLabel(info(Command::ZoomIn)) == "Zoom in\tCtrl++");
        CHECK(menuLabel(info(Command::ZoomOut)) == "Zoom out\tCtrl+-");
        CHECK(menuLabel(info(Command::ResizeWindowToActualSize)) ==
              "Resize window to actual size\tAlt+1");
#endif
    }

    TEST_CASE("commands: view keys through the char hook") {
        using rawreel::normaliseKey;
#if defined(__APPLE__)
        CHECK_FALSE(commandForKey(letter('0', true), false));
        CHECK_FALSE(commandForKey(normaliseKey('0', true, false, false), false));
#else
        const auto hook = [](std::int32_t code, bool ctrl, bool shift, bool alt) {
            return commandForKey(normaliseKey(code, ctrl, shift, alt), false);
        };
        CHECK(hook('=', true, true, false) == Command::ZoomIn);
        CHECK(hook('+', true, false, false) == Command::ZoomIn);
        CHECK(hook(388, true, false, false) == Command::ZoomIn);
        CHECK(hook('-', true, false, false) == Command::ZoomOut);
        CHECK(hook(390, true, false, false) == Command::ZoomOut);
        CHECK(hook('0', true, false, false) == Command::FitToWindow);
        CHECK(hook(324, true, false, false) == Command::FitToWindow);
        CHECK(hook('1', true, false, false) == Command::ActualSize);
        CHECK(hook(325, true, false, false) == Command::ActualSize);
        CHECK(hook('1', false, false, true) == Command::ResizeWindowToActualSize);
        CHECK_FALSE(hook('0', true, true, false));  // German Ctrl+= is Ctrl+Shift+0
        for (const KeyChord& key :
             {chord(Key::Plus, true), chord(Key::Minus, true), letter('0', true), letter('1', true),
              KeyChord{'1', false, false, true}}) {
            CHECK_FALSE(commandForKey(key, true));
        }
#endif
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

    TEST_CASE("commands: the inspector commands and keys") {
        static_assert(static_cast<std::size_t>(Command::PaddingCheck) + 1 ==
                      static_cast<std::size_t>(Command::GpuInfo));
        CHECK(commandForKey(letter('P'), false) == Command::Pipette);
        CHECK(commandForKey(KeyChord{'P', false, true, false}, false) == Command::LiveProbe);
        CHECK(commandForKey(letter('X'), false) == Command::PaddingCheck);
        CHECK_FALSE(commandForKey(letter('P'), true));  // an editable field keeps its keys
        const rawreel::CommandInfo& pipette = info(Command::Pipette);
        CHECK(pipette.id == "pipette");
        CHECK(pipette.menu == "View");
        CHECK(pipette.check == MenuCheck::Mode);
        CHECK(pipette.route == KeyRoute::CharHook);
        CHECK(info(Command::LiveProbe).id == "live_probe");
        CHECK(info(Command::ZoomWindow).id == "zoom_window");
        CHECK(info(Command::ZoomWindow).check == MenuCheck::Shown);
        CHECK(info(Command::ZoomWindow).route == KeyRoute::Accelerator);
        CHECK_FALSE(info(Command::ZoomWindow).key);
        CHECK(info(Command::ClearPin).id == "clear_pin");
        CHECK(info(Command::CopyValues).label == "Copy Pixel Values");
        CHECK(info(Command::PaddingCheck).id == "padding_check");
        CHECK(info(Command::PaddingCheck).check == MenuCheck::Mode);
    }
}
