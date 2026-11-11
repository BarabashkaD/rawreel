#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "core/svg_colour.h"
#include "gui/icons/lucide_icons.h"

using rawreel::replaceCurrentColor;

namespace {

std::size_t count(std::string_view text, std::string_view needle) {
    std::size_t found = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + needle.size())) {
        ++found;
    }
    return found;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("svg colour: every currentColor is replaced") {
        const std::string replaced =
            replaceCurrentColor(R"(<path stroke="currentColor" fill="currentColor"/>)", "#ffffff");
        CHECK(count(replaced, "currentColor") == 0);
        CHECK(count(replaced, "#ffffff") == 2);
        CHECK(replaced == R"(<path stroke="#ffffff" fill="#ffffff"/>)");
        CHECK(replaceCurrentColor("no colour here", "#000000") == "no colour here");
    }

    TEST_CASE("svg colour: no Lucide icon keeps a currentColor") {
        namespace lucide = rawreel::lucide;
        const std::array<std::string_view, 11> icons{
            lucide::FOLDER_OPEN,  lucide::SKIP_BACK,
            lucide::STEP_BACK,    lucide::PLAY,
            lucide::PAUSE,        lucide::SQUARE,
            lucide::STEP_FORWARD, lucide::SKIP_FORWARD,
            lucide::REPEAT,       lucide::SLIDERS_HORIZONTAL,
            lucide::INFO};
        for (const std::string_view icon : icons) {
            REQUIRE(icon.contains("<svg"));
            CHECK(count(icon, "currentColor") > 0);
            const std::string replaced = replaceCurrentColor(icon, "#1d1d1f");
            CHECK(count(replaced, "currentColor") == 0);
            CHECK(count(replaced, "#1d1d1f") == count(icon, "currentColor"));
        }
    }
}
