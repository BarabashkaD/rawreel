#pragma once

// Help -> About (design M4 v2 §4.7, D23): a wxGenericAboutDialog subclass owned
// by the frame. The heading "rawreel <version>" (once), the description, the copyright line with
// the MIT licence link, the GitHub link, the FFmpeg and wxWidgets lines (name, version and licence
// with links), a collapsed "Components" pane with the dvv-* versions, and the buttons "Third-party
// notices" (opens the notices file next to the exe), "Copy to clipboard" (the full aboutText())
// and OK. Its content is aboutContent() (core/version_text.h); no directory or path is shown or
// copied. The GPU is not in About: Help -> GPU info shows it. Above the heading, the rawreel logo
// (the owl on a transparent background, 128 x 128 DIP, one image for light and dark; app icon
// plan D6).

#include <string_view>

namespace rawreel {

class MainFrame;

inline constexpr std::string_view ABOUT_NOTICES_LABEL = "Third-party notices";
inline constexpr std::string_view ABOUT_COPY_LABEL = "Copy to clipboard";
inline constexpr std::string_view ABOUT_NOTICES_FILE = "THIRD_PARTY_NOTICES.txt";
inline constexpr std::string_view ABOUT_LOGO_NAME = "about-logo";  // the logo's window name

// Builds the About content and text, records the text on the frame (noteAbout) and shows the
// dialog modally through showModal (the selftest hook closes it).
void showAbout(MainFrame& frame);

}  // namespace rawreel
