#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/info_text.h"
#include "core/session_types.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"

using dvv::media::ColourMatrix;
using dvv::media::ColourParams;
using dvv::media::TransferFn;
using rawreel::formatInfo;
using rawreel::StreamInfo;

namespace {

constexpr std::string_view URI =
    "synthetic:gradient?fmt=yuv420p&size=64x48&fps=25&frames=40&seed=1";

// yuv420p 64x48 at 25 fps, 40 frames, with `colour`.
StreamInfo stream(const ColourParams& colour = {}) {
    dvv::media::SourceInfo info;
    info.desc = {dvv::media::findFormat("yuv420p").value_or(0), dvv::media::internColour(colour),
                 64, 48};
    info.fps = {25, 1};
    info.frameCount = 40;
    info.resolvedUri = std::string(URI);
    return StreamInfo{0, std::string(URI), info, dvv::media::resolveColour(info.desc)};
}

bool contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

std::string infoOf(const StreamInfo& info) {
    const std::vector<StreamInfo> streams{info};
    return formatInfo(streams);
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("info: no stream") {
        CHECK(formatInfo({}) ==
              "No stream. Open a file (Ctrl+O) or a URI (Ctrl+U), or drop a file here.");
    }

    TEST_CASE("info: assumed defaults") {
        const std::string text = infoOf(stream());
        CHECK_MESSAGE(contains(text,
                               "Stream 1: synthetic:gradient?fmt=yuv420p&size=64x48&fps=25&"
                               "frames=40&seed=1"),
                      text);
        CHECK_MESSAGE(contains(text, "yuv420p, 64x48, 25 fps, 40 frames"), text);
        CHECK_MESSAGE(contains(text,
                               "colour: matrix bt601 (assumed), range limited (assumed), "
                               "transfer srgb (assumed), primaries bt709 (assumed)"),
                      text);
        CHECK_FALSE(contains(text, "tone-mapped"));
    }

    TEST_CASE("info: fps and length") {
        StreamInfo info = stream();
        info.info.fps = {30000, 1001};
        info.info.frameCount = std::nullopt;
        const std::string text = infoOf(info);
        CHECK_MESSAGE(contains(text, "yuv420p, 64x48, 29.970 fps, unbounded"), text);
    }

    TEST_CASE("info: tone mapping line") {
        constexpr std::string_view LINE = "  tone-mapped (BT.2390, 1000 → 100 nits)";
        CHECK(contains(infoOf(stream({.transfer = TransferFn::Pq})), LINE));
        CHECK(contains(infoOf(stream({.transfer = TransferFn::Hlg})), LINE));
        CHECK_FALSE(contains(infoOf(stream({.transfer = TransferFn::Srgb})), "tone-mapped"));
    }

    TEST_CASE("info: custom matrix") {
        const std::string text =
            infoOf(stream({.matrix = ColourMatrix::Custom, .kr = 0.212F, .kb = 0.087F}));
        CHECK_MESSAGE(contains(text, "matrix custom kr=0.2120 kb=0.0870,"), text);
    }

    TEST_CASE("info: warnings") {
        StreamInfo info = stream();
        info.info.warnings.emplace_back("5 trailing bytes ignored");
        const std::string text = infoOf(info);
        CHECK_MESSAGE(contains(text, "\n  warning: 5 trailing bytes ignored"), text);
    }
}
