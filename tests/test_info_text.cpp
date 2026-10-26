#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/info_text.h"
#include "core/session_types.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"

using dvv::media::ColourMatrix;
using dvv::media::ColourParams;
using dvv::media::ColourRange;
using dvv::media::TransferFn;
using rawreel::formatInfo;
using rawreel::StreamInfo;

namespace {

constexpr std::string_view URI =
    "synthetic:gradient?fmt=yuv420p&size=64x48&fps=25&frames=40&seed=1";

// yuv420p 64x48 at 25 fps, 40 frames, with `colour`, opened as `uri`.
StreamInfo stream(const ColourParams& colour = {}, std::string_view uri = URI) {
    dvv::media::SourceInfo info;
    info.desc = {dvv::media::findFormat("yuv420p").value_or(0), dvv::media::internColour(colour),
                 64, 48};
    info.fps = {25, 1};
    info.frameCount = 40;
    info.resolvedUri = std::string(uri);
    return StreamInfo{0, std::string(uri), info, dvv::media::resolveColour(info.desc)};
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

    TEST_CASE("info: defaults") {
        const std::string text = infoOf(stream());
        CHECK_MESSAGE(contains(text,
                               "Stream 1: synthetic:gradient?fmt=yuv420p&size=64x48&fps=25&"
                               "frames=40&seed=1"),
                      text);
        CHECK_MESSAGE(contains(text, "yuv420p, 64x48, 25 fps, 40 frames"), text);
        // D5R §4.5: the default standard; the range shares line 1's origin, so it has no tag.
        CHECK_MESSAGE(contains(text,
                               "\n  colour: BT.709 (default)\n  matrix bt709, range limited, "
                               "curve bt1886, primaries bt709 (not converted)\n  hint: SD size: "
                               "BT.601 is common for SD"),
                      text);
        CHECK_FALSE(contains(text, "tone-mapped"));
        CHECK_FALSE(contains(text, "assumed"));
    }

    // D5R §4.5: line 1 names the standard and its origin; the range is tagged only when its origin
    // differs from line 1's, never for range=.
    TEST_CASE("info: colour origin") {
        const std::string text =
            infoOf(stream({.range = ColourRange::Full, .transfer = TransferFn::Pq},
                          "file:a.yuv?fmt=yuv420p&size=64x48&fps=25&range=full"));
        CHECK_MESSAGE(contains(text, "\n  colour: BT.2100 PQ (from file)\n"), text);
        CHECK_MESSAGE(contains(text, ", range full, curve pq, "), text);
        CHECK_FALSE(contains(text, "hint:"));  // the standard comes from the file
        const std::string graph =
            infoOf(stream({.transfer = TransferFn::Hlg}, "lavfi:testsrc2=s=64x48"));
        CHECK_MESSAGE(contains(graph, "\n  colour: BT.2100 HLG (from the graph)\n"), graph);
        CHECK_MESSAGE(contains(graph, ", range limited (default), curve hlg, "), graph);
        // The raw provider: the file name's colour token.
        ColourParams pal = dvv::media::colourPresetInfo(dvv::media::ColourPreset::Bt601Pal).colour;
        pal.range = ColourRange::Unspecified;
        StreamInfo named = stream(pal, "file:clip_64x48_bt601-pal.yuv?fmt=yuv420p");
        named.info.provider = "raw";
        const std::string name = infoOf(named);
        CHECK_MESSAGE(contains(name,
                               "\n  colour: BT.601 (PAL) (from the file name)\n  matrix bt601, "
                               "range limited (default), curve bt1886, primaries bt601-625 (not "
                               "converted)"),
                      name);
        // A file colour that matches no standard.
        const std::string none = infoOf(stream({.matrix = ColourMatrix::Bt709,
                                                .range = ColourRange::Unspecified,
                                                .transfer = TransferFn::Linear,
                                                .primaries = dvv::media::Primaries::Unspecified,
                                                .kr = 0,
                                                .kb = 0},
                                               "file:a.mkv?impl=ffmpeg"));
        CHECK_MESSAGE(contains(none, "\n  colour: no standard (from file)\n"), none);
        CHECK_MESSAGE(contains(none, "curve linear"), none);
    }

    TEST_CASE("info: colour key") {
        const ColourParams pq =
            dvv::media::colourPresetInfo(dvv::media::ColourPreset::Bt2100Pq).colour;
        const std::string text =
            infoOf(stream(pq, "file:a.yuv?fmt=yuv420p&size=64x48&fps=25&colour=bt2100-pq"));
        CHECK_MESSAGE(contains(text,
                               "\n  colour: BT.2100 PQ\n  matrix bt2020, range limited (from "
                               "file), curve pq, primaries bt2020 (not converted)\n"),
                      text);
        // colour= is no file colour: BT.2100 at 48 lines gets the size hint.
        CHECK_MESSAGE(contains(text, "\n  hint: SD size: BT.601 is common for SD"), text);
        const std::string field =
            infoOf(stream({.matrix = ColourMatrix::Bt2020Ncl, .range = ColourRange::Full},
                          "file:a.yuv?fmt=yuv420p&size=64x48&fps=25&colour=bt2100-pq&range=full"));
        CHECK_MESSAGE(contains(field, "\n  colour: BT.2100 PQ\n  matrix bt2020, range full,"),
                      field);
    }

    // The range of a gray file keeps its tag under a standard (a standard gives no range).
    TEST_CASE("info: colour key on gray") {
        ColourParams own = dvv::media::colourPresetInfo(dvv::media::ColourPreset::Bt709).colour;
        own.matrix = ColourMatrix::Unspecified;
        own.range = ColourRange::Limited;
        StreamInfo gray = stream(own, "file:g.y4m?colour=bt709");
        gray.info.desc.format = dvv::media::findFormat("gray").value_or(0);
        gray.colour = dvv::media::resolveColour(gray.info.desc);
        const std::string text = infoOf(gray);
        CHECK_MESSAGE(contains(text,
                               "\n  colour: BT.709\n  matrix not used, range limited (from "
                               "file), curve bt1886, "),
                      text);
        own.range = ColourRange::Unspecified;
        StreamInfo defaulted = stream(own, "file:g.y4m?colour=bt709");
        defaulted.info.desc.format = gray.info.desc.format;
        defaulted.colour = dvv::media::resolveColour(defaulted.info.desc);
        const std::string full = infoOf(defaulted);
        CHECK_MESSAGE(contains(full, "range full (default), curve "), full);
    }

    // The size-mismatch hint for a standard that no file gives.
    TEST_CASE("info: size hint") {
        const auto sized = [](std::string_view uri, std::uint32_t width, std::uint32_t height) {
            StreamInfo info = stream({}, uri);
            info.info.desc.width = width;
            info.info.desc.height = height;
            info.colour = dvv::media::resolveColour(info.info.desc);
            return infoOf(info);
        };
        const std::string sd = sized("file:a.yuv?fmt=yuv420p&size=720x576&fps=25", 720, 576);
        CHECK_MESSAGE(contains(sd, "\n  hint: SD size: BT.601 is common for SD"), sd);
        CHECK_FALSE(contains(
            sized("file:a.yuv?fmt=yuv420p&size=720x576&colour=bt601-pal", 720, 576), "hint:"));
        const std::string hd =
            sized("file:a.yuv?fmt=yuv420p&size=1920x1080&colour=bt601-pal", 1920, 1080);
        CHECK_MESSAGE(contains(hd, "\n  hint: HD size: BT.709 is common"), hd);
        CHECK_FALSE(contains(sized("file:a.yuv?fmt=yuv420p&size=1920x1080", 1920, 1080), "hint:"));
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

    TEST_CASE("info: file read mode") {
        CHECK_FALSE(contains(infoOf(stream()), "reads:"));  // not a file source
        StreamInfo info = stream();
        info.info.reader = "unbuffered";
        CHECK_MESSAGE(contains(infoOf(info), "\n  reads: unbuffered"), infoOf(info));
        info.info.reader = "buffered (fallback: the unbuffered open failed)";
        CHECK_MESSAGE(contains(infoOf(info),
                               "\n  reads: buffered (fallback: the unbuffered open "
                               "failed)"),
                      infoOf(info));
    }

    TEST_CASE("info: clip description") {
        CHECK_FALSE(contains(infoOf(stream()), "clip description"));
        StreamInfo info = stream();
        info.info.clipFile = "clips/sub/clip.yuv.json";
        CHECK_MESSAGE(contains(infoOf(info), "\n  clip description: clip.yuv.json"), infoOf(info));
        info.info.clipFile = "X:\\v\\desc.json";
        CHECK_MESSAGE(contains(infoOf(info), "\n  clip description: desc.json"), infoOf(info));
    }

    TEST_CASE("info: warnings") {
        StreamInfo info = stream();
        info.info.warnings.emplace_back("5 trailing bytes ignored");
        const std::string text = infoOf(info);
        CHECK_MESSAGE(contains(text, "\n  warning: 5 trailing bytes ignored"), text);
    }
}
