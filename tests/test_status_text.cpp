#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

#include "core/session_types.h"
#include "core/status_text.h"
#include "dvv/media/types.h"

using rawreel::SessionStatus;
using rawreel::statusFields;

namespace {

SessionStatus withStream(std::int64_t position, std::optional<std::int64_t> length, double fps) {
    SessionStatus status;
    status.streams = 1;
    status.player.position = position;
    status.player.length = length;
    status.player.targetFps = fps;
    status.player.actualFps = fps;
    return status;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("status text: position, length and time") {
        CHECK(statusFields(withStream(123, 1000, 25.0)).position ==
              "frame 123 / 1000  00:00:04.920");
    }

    TEST_CASE("status text: an unbounded stream has no length") {
        CHECK(statusFields(withStream(123, std::nullopt, 25.0)).position ==
              "frame 123  00:00:04.920");
    }

    TEST_CASE("status text: an hour") {
        CHECK(statusFields(withStream(90000, std::nullopt, 25.0)).position ==
              "frame 90000  01:00:00.000");
    }

    TEST_CASE("status text: no time without a target rate") {
        CHECK(statusFields(withStream(7, 10, 0.0)).position == "frame 7 / 10");
    }

    TEST_CASE("status text: no frame shown yet") {
        CHECK(statusFields(withStream(-1, 10, 25.0)).position == "frame - / 10");
    }

    TEST_CASE("status text: target and actual rate") {
        SessionStatus status = withStream(0, 10, 60.0);
        status.player.actualFps = 59.83;
        CHECK(statusFields(status).fps == "60.0 / 59.8 fps");
    }

    TEST_CASE("status text: bottleneck") {
        SessionStatus status = withStream(0, 10, 25.0);
        CHECK(statusFields(status).bottleneck.empty());
        status.bottleneck = "disk";
        CHECK(statusFields(status).bottleneck == "bottleneck: disk");
    }

    TEST_CASE("status text: no streams, no fields") {
        SessionStatus status;
        status.player.position = 5;
        status.player.targetFps = 25.0;
        status.bottleneck = "disk";
        const rawreel::StatusFields fields = statusFields(status);
        CHECK(fields.position.empty());
        CHECK(fields.fps.empty());
        CHECK(fields.bottleneck.empty());
    }

    // D5R §2.2: the display mapping of the resolved colour (U+2192 arrows).
    TEST_CASE("status text: mapping") {
        using dvv::media::ColourParams;
        using dvv::media::TransferFn;
        const auto with = [](TransferFn transfer) {
            return ColourParams{.matrix = dvv::media::ColourMatrix::Bt2020Ncl,
                                .range = dvv::media::ColourRange::Limited,
                                .transfer = transfer,
                                .primaries = dvv::media::Primaries::Bt2020,
                                .kr = 0,
                                .kb = 0};
        };
        CHECK(rawreel::mappingText(nullptr).empty());
        const ColourParams pq = with(TransferFn::Pq);
        CHECK(rawreel::mappingText(&pq) ==
              "PQ \xE2\x86\x92 SDR tone mapping (1000 \xE2\x86\x92 100 nits)");
        const ColourParams hlg = with(TransferFn::Hlg);
        CHECK(rawreel::mappingText(&hlg) ==
              "HLG \xE2\x86\x92 SDR tone mapping (1000 \xE2\x86\x92 100 nits)");
        for (const TransferFn sdr : {TransferFn::Bt1886, TransferFn::Srgb, TransferFn::Linear}) {
            const ColourParams colour = with(sdr);
            CHECK(rawreel::mappingText(&colour) == "SDR, no tone mapping");
        }
    }
}
