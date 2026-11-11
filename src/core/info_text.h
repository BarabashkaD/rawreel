#pragma once

// The Info window's values (window title and Info design §11, §13): wx-free, so they are unit
// tested without the GUI.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/session_types.h"

namespace rawreel {

// The Info window (design: window title and Info window, §11, §13).
struct InfoRow {
    std::string_view label;  // "Resolution", "Frame rate", "Format", "Standard", "Matrix", "Range",
                             // "Transfer", "Tone mapping" (this order)
    std::string value;
};
inline constexpr std::size_t INFO_PICTURE_ROWS = 3;  // rows 0..2 under "Picture", 3..7 "Colour"

// Always 8 rows.
[[nodiscard]] std::vector<InfoRow> infoRows(const StreamInfo& stream);
// "1920 x 1080 . 50 fps . v210 . BT.709 limited" (x is U+00D7, . is U+00B7): resolution, rate,
// rawreel format name, then the standard and the range word; a missing standard or an unspecified
// range is left out.
[[nodiscard]] std::string infoHint(const StreamInfo& stream);
// The rows as "Label: value" lines joined with '\n'; nullptr: "No stream".
[[nodiscard]] std::string infoCopyText(const StreamInfo* stream);
// The texts that size the value column: the widest fixed values and the Format value of every
// catalogue format.
[[nodiscard]] std::vector<std::string> infoValueSamples();

}  // namespace rawreel
