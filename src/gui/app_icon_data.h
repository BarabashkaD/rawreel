#pragma once

// The PNGs embedded in rawreel-gui for this platform (app icon plan D7), written at build time by
// packaging/icons/embed-png.cmake into generated/app_icon_data.cpp from packaging/icons/
// owl-on-film-reel. Every platform has About's logo (owl-on-film-reel-transparent-background-1024);
// Linux also has every hicolor size of square-background as the window icons
// (owl-on-film-reel-square-background-<N>). The table is never empty.

#include <span>
#include <string_view>

namespace rawreel {

struct EmbeddedPng {
    std::string_view key;  // the file stem
    std::span<const unsigned char> png;
};

// In the order CMake lists the files.
[[nodiscard]] std::span<const EmbeddedPng> embeddedPngs();

}  // namespace rawreel
