#pragma once

// The status bar fields of design D20, wx-free.

#include <string>

#include "core/session_types.h"
#include "dvv/media/types.h"

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

}  // namespace rawreel
