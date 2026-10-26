#include "core/status_text.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <string>

#include "dvv/media/types.h"

namespace rawreel {

namespace {

// HH:MM:SS.mmm of `position` frames at `fps` (> 0); the hours grow past 99 when needed.
std::string timeText(std::int64_t position, double fps) {
    const auto ms =
        static_cast<std::int64_t>(std::llround(static_cast<double>(position) * 1000.0 / fps));
    constexpr std::int64_t MS_PER_SECOND = 1000;
    constexpr std::int64_t SECONDS_PER_MINUTE = 60;
    constexpr std::int64_t MINUTES_PER_HOUR = 60;
    const std::int64_t seconds = ms / MS_PER_SECOND;
    const std::int64_t minutes = seconds / SECONDS_PER_MINUTE;
    return std::format("{:02}:{:02}:{:02}.{:03}", minutes / MINUTES_PER_HOUR,
                       minutes % MINUTES_PER_HOUR, seconds % SECONDS_PER_MINUTE,
                       ms % MS_PER_SECOND);
}

}  // namespace

StatusFields statusFields(const SessionStatus& status) {
    if (status.streams == 0) {
        return {};
    }
    const PlayerStatus& player = status.player;
    StatusFields fields;
    // Before the first show (position -1) the frame is "-" and there is no time.
    fields.position = player.position >= 0 ? std::format("frame {}", player.position) : "frame -";
    if (player.length) {
        fields.position += std::format(" / {}", *player.length);
    }
    if (player.position >= 0 && player.targetFps > 0) {
        fields.position += "  " + timeText(player.position, player.targetFps);
    }
    fields.fps = std::format("{:.1f} / {:.1f} fps", player.targetFps, player.actualFps);
    if (!status.bottleneck.empty()) {
        fields.bottleneck = "bottleneck: " + status.bottleneck;
    }
    return fields;
}

std::string mappingText(const dvv::media::ColourParams* resolved) {
    if (resolved == nullptr) {
        return {};
    }
    // "\xE2\x86\x92" is U+2192 (rightwards arrow) in UTF-8.
    switch (resolved->transfer) {
        case dvv::media::TransferFn::Pq:
            return "PQ \xE2\x86\x92 SDR tone mapping (1000 \xE2\x86\x92 100 nits)";
        case dvv::media::TransferFn::Hlg:
            return "HLG \xE2\x86\x92 SDR tone mapping (1000 \xE2\x86\x92 100 nits)";
        default: return "SDR, no tone mapping";
    }
}

}  // namespace rawreel
