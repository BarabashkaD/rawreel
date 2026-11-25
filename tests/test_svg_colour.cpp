#include <doctest/doctest.h>

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
        // Every embedded icon: the list the transport bar draws from.
        for (const std::string_view icon : rawreel::lucide::ICONS) {
            REQUIRE(icon.contains("<svg"));
            CHECK(count(icon, "currentColor") > 0);
            const std::string replaced = replaceCurrentColor(icon, "#1d1d1f");
            CHECK(count(replaced, "currentColor") == 0);
            CHECK(count(replaced, "#1d1d1f") == count(icon, "currentColor"));
        }
    }
}
