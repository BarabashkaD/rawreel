#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

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

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("commands: the table lists every command once, in order") {
        static_assert(COMMAND_COUNT == 17);
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
        for (const Command command : {Command::Stop, Command::Format, Command::Reopen,
                                      Command::GpuInfo, Command::Diagnostics}) {
            CHECK_FALSE(info(command).key);
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
        CHECK(menuLabel(info(Command::Reopen)) == "Reopen");
        CHECK(keyText(chord(Key::F11)) == "F11");
        CHECK(keyText(chord(Key::Escape)) == "Esc");
        CHECK(keyText(KeyChord{'O', true, true}) == "Ctrl+Shift+O");
        CHECK(keyText(chord(Key::Plus)) == "+");
        CHECK(keyText(chord(Key::Minus, false, false)) == "-");
        CHECK(keyText(letter('1')) == "1");
    }
}
