#pragma once

// The status bar fields of design D20, wx-free.

#include <optional>
#include <string>
#include <string_view>

#include "core/session_types.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"

namespace rawreel {

struct StatusFields {
    std::string position;  // "frame 123 / 1000  00:00:04.920"; unbounded: "frame 123  00:00:04.920"
    std::string fps;       // "60.0 / 59.8 fps" (target / actual)
    std::string bottleneck;  // "bottleneck: disk" when the Session names one, else ""
};
// Without streams every field is empty. The time is position / targetFps as HH:MM:SS.mmm (left
// out when targetFps <= 0).
[[nodiscard]] StatusFields statusFields(const SessionStatus& status);

// D5R §2.2: the display mapping of the active stream's resolved colour (StreamInfo::colour); ""
// without one. "PQ → SDR tone mapping (1000 → 100 nits)", "HLG → SDR tone mapping (1000 → 100
// nits)", else "SDR, no tone mapping".
[[nodiscard]] std::string mappingText(const dvv::media::ColourParams* resolved);

// The image pixel under the cursor: "x 812  y 403" (two spaces between the axes); "" for none.
[[nodiscard]] std::string cursorText(std::optional<dvv::media::ImagePixel> pixel);
// The widest cursorText for images of up to 16384 pixels per side, for sizing its field.
inline constexpr std::string_view WIDEST_CURSOR_TEXT = "x 16383  y 16383";

}  // namespace rawreel
