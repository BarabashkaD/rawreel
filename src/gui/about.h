#pragma once

// Help -> About (design M4 v2 §4.7, D23): a wxGenericAboutDialog subclass owned
// by the frame. The heading "rawreel <version>" (once), the description, the copyright and MIT
// licence line, the GitHub link, the GPU, FFmpeg and wxWidgets/appearance lines, a collapsed
// "Components" pane with the dvv-* versions, and the buttons "Third-party notices" (opens the
// notices file next to the exe), "Copy to clipboard" (the full aboutText()) and OK. Its content
// is aboutContent() (core/version_text.h); no directory or path is shown or copied.

#include <string_view>

namespace rawreel {

class MainFrame;

inline constexpr std::string_view ABOUT_NOTICES_LABEL = "Third-party notices";
inline constexpr std::string_view ABOUT_COPY_LABEL = "Copy to clipboard";
inline constexpr std::string_view ABOUT_NOTICES_FILE = "THIRD_PARTY_NOTICES.txt";

// Builds the About content and text, records the text on the frame (noteAbout) and shows the
// dialog modally through showModal (the selftest hook closes it).
void showAbout(MainFrame& frame);

}  // namespace rawreel
