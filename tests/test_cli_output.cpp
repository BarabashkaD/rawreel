#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "cli/output.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/source.h"
#include "dvv/media/types.h"

using dvv::Errc;
using rawreel::cli::exitCodeFor;

namespace {

dvv::Error errorOf(Errc code) {
    return dvv::Error{.code = code, .message = "x", .where = {}, .cause = nullptr};
}

// v210 34x17, fps 25/1, 3 frames, one warning, read buffered.
dvv::media::SourceInfo v210Info() {
    const std::optional<dvv::media::FormatId> v210 = dvv::media::findFormat("v210");
    REQUIRE(v210.has_value());
    dvv::media::SourceInfo info;
    info.desc = dvv::media::FrameDesc{
        .format = v210.value_or(dvv::media::FormatId{0}), .colour = 0, .width = 34, .height = 17};
    info.fps = dvv::media::Rational{.num = 25, .den = 1};
    info.frameCount = 3;
    info.resolvedUri = "file:clip_34x17_v210.yuv?fmt=v210";
    info.provider = "raw";
    info.warnings = {"5 trailing bytes ignored"};
    info.reader = "buffered";
    return info;
}

}  // namespace

TEST_CASE("exit codes: table") {
    CHECK(exitCodeFor(errorOf(Errc::Io), false) == 2);
    CHECK(exitCodeFor(errorOf(Errc::Io), true) == 2);
    CHECK(exitCodeFor(errorOf(Errc::Format), false) == 2);
    CHECK(exitCodeFor(errorOf(Errc::Format), true) == 2);
    CHECK(exitCodeFor(errorOf(Errc::Unsupported), false) == 3);
    CHECK(exitCodeFor(errorOf(Errc::Unsupported), true) == 4);
    CHECK(exitCodeFor(errorOf(Errc::OutOfMemory), false) == 2);
    CHECK(exitCodeFor(errorOf(Errc::OutOfMemory), true) == 4);
    CHECK(exitCodeFor(errorOf(Errc::DeviceLost), false) == 4);
    CHECK(exitCodeFor(errorOf(Errc::DeviceLost), true) == 4);
    CHECK(exitCodeFor(errorOf(Errc::ShaderCompile), false) == 4);
    CHECK(exitCodeFor(errorOf(Errc::ShaderCompile), true) == 4);
    CHECK(exitCodeFor(errorOf(Errc::Internal), false) == 2);
    CHECK(exitCodeFor(errorOf(Errc::Internal), true) == 4);
    CHECK(exitCodeFor(errorOf(Errc::Cancelled), false) == 2);
    CHECK(exitCodeFor(errorOf(Errc::Cancelled), true) == 4);
}

TEST_CASE("output: infoJson keys") {
    dvv::media::SourceInfo info = v210Info();
    const dvv::json::Value j = rawreel::cli::infoJson(info);
    CHECK(j["uri"] == "file:clip_34x17_v210.yuv?fmt=v210");
    CHECK(j["provider"] == "raw");
    CHECK(j["format"]["name"] == "v210");
    CHECK(j["width"] == 34);
    CHECK(j["height"] == 17);
    CHECK(j["fps"]["num"] == 25);
    CHECK(j["fps"]["den"] == 1);
    CHECK(j["frames"] == 3);
    const auto geometry = dvv::media::geometry(info.desc.format, 34, 17);
    REQUIRE(geometry.has_value());
    CHECK(j["frameBytes"] == geometry->bytes);
    CHECK(j["colour"]["matrix"] == "unspecified");
    CHECK(j["colour"]["range"] == "unspecified");
    CHECK(j["colour"]["transfer"] == "unspecified");
    CHECK(j["colour"]["primaries"] == "unspecified");
    // D5R §3.1: the standard in use; nothing given gives BT.709.
    CHECK(j["standard"] == "bt709");
    CHECK(j["access"] == "randomCheap");
    CHECK(j["deterministic"] == true);
    CHECK(j["reader"] == "buffered");
    REQUIRE(j["warnings"].is_array());
    CHECK(j["warnings"][0] == "5 trailing bytes ignored");

    info.frameCount = std::nullopt;
    info.reader.clear();
    const dvv::json::Value unbounded = rawreel::cli::infoJson(info);
    CHECK(unbounded["frames"].is_null());
    CHECK(unbounded["reader"].is_null());
}

namespace {

// 8 frames of 1000 bytes in 0.5 s; one Upload stage of 3e9 bytes in 1 s.
rawreel::cli::BenchReport benchReport() {
    rawreel::cli::BenchReport report;
    report.uri = "synthetic:noise?fmt=yuv420p10le&size=64x64&frames=8";
    report.format = "yuv420p10le";
    report.reader = "unbuffered";
    report.device = "Test GPU";
    report.width = 64;
    report.height = 64;
    report.frames = 8;
    report.inflight = 4;
    report.frameBytes = 1000;
    report.seconds = 0.5;
    report.stages = {
        {.name = "Upload", .runs = 8, .bytes = 3'000'000'000, .nanoseconds = 1'000'000'000}};
    return report;
}

}  // namespace

TEST_CASE("output: benchJson") {
    rawreel::cli::BenchReport report = benchReport();
    const dvv::json::Value j = rawreel::cli::benchJson(report);
    CHECK(j["fps"] == 16.0);
    CHECK(j["gbps"].get<double>() == doctest::Approx(8 * 1000 / 0.5 / 1e9));
    REQUIRE(j["stages"].is_array());
    CHECK(j["stages"][0]["gbps"].get<double>() == doctest::Approx(3.0));
    CHECK(j["reader"] == "unbuffered");
    CHECK(j["readOnly"] == false);
    report.reader.clear();
    CHECK(rawreel::cli::benchJson(report)["reader"] == "none");
    report.readOnly = true;
    CHECK(rawreel::cli::benchJson(report)["readOnly"] == true);
}

TEST_CASE("output: benchText") {
    rawreel::cli::BenchReport report = benchReport();
    const std::string text = rawreel::cli::benchText(report);
    CHECK(text.starts_with("bench "));
    CHECK(text.find(", reads unbuffered, device Test GPU\n") != std::string::npos);  // as info
    CHECK(text.find("\nend-to-end: 16.0 frames/s") != std::string::npos);
    CHECK(text.find("\nUpload") != std::string::npos);
    // --read-only: the wall time is the read stage's wall clock.
    report.readOnly = true;
    const std::string read = rawreel::cli::benchText(report);
    CHECK(read.find(", read only,") != std::string::npos);
    CHECK(read.find("\nread wall clock: 16.0 frames/s, 0.00 GB/s in 0.500 s\n") !=
          std::string::npos);
    CHECK(read.find("end-to-end") == std::string::npos);
}

TEST_CASE("output: infoText lines") {
    const std::string text = rawreel::cli::infoText(v210Info());
    CHECK(text.find("uri: file:clip_34x17_v210.yuv?fmt=v210\n") != std::string::npos);
    CHECK(text.find("provider: raw\n") != std::string::npos);
    CHECK(text.find("format: v210\n") != std::string::npos);
    CHECK(text.find("size: 34x17\n") != std::string::npos);
    CHECK(text.find("fps: 25/1\n") != std::string::npos);
    CHECK(text.find("frames: 3\n") != std::string::npos);
    CHECK(text.find("access: random-cheap\n") != std::string::npos);
    CHECK(text.find("reads: buffered\n") != std::string::npos);
    CHECK(text.find("warning: 5 trailing bytes ignored\n") != std::string::npos);
    CHECK(text.find("note: ") == std::string::npos);
}

// The provider's notes and its colour hint.
TEST_CASE("output: notes and a non-standard file colour") {
    dvv::media::SourceInfo info = v210Info();
    info.notes = {"stream and frame differ: transfer 14, 18", "colour fits no standard"};
    const std::string text = rawreel::cli::infoText(info);
    CHECK(text.find("\nnote: stream and frame differ: transfer 14, 18\nnote: colour fits no "
                    "standard\n") != std::string::npos);
    CHECK(text.find("\ncolour: bt709 (default) ") != std::string::npos);
    const dvv::json::Value json = rawreel::cli::infoJson(info);
    REQUIRE(json["notes"].is_array());
    CHECK(json["notes"][1] == "colour fits no standard");
    CHECK(json["standard"] == "bt709");

    info.colourHint.nonStandard = true;  // a signalled code that no field stands for
    CHECK(rawreel::cli::infoText(info).find("\ncolour: no standard (from file) ") !=
          std::string::npos);
    CHECK(rawreel::cli::infoJson(info)["standard"].is_null());
    // A colour= key is the user's standard: the hint's "no standard" does not count.
    info.resolvedUri = "file:clip_34x17_v210.yuv?fmt=v210&colour=bt709";
    CHECK(rawreel::cli::infoJson(info)["standard"] == "bt709");
}

// The values shown are the values rendered. SMPTE 240M tags
// (M/T/P 7/7/7) map to primaries BT.601-525 only, plus the "no standard" hint. The renderer
// (dvv-media ToDisplay) resolves with the default hint, so the primaries point to BT.601 NTSC and
// the matrix is BT.601; the label still says "no standard".
TEST_CASE("output: a partly standard non-standard colour shows the rendered values") {
    dvv::media::SourceInfo info = v210Info();
    dvv::media::ColourParams given;  // matrix, range and transfer Unspecified
    given.primaries = dvv::media::Primaries::Bt601_525;
    info.desc.colour = dvv::media::internColour(given);
    info.colourHint.nonStandard = true;
    const dvv::media::ResolvedColour rendered = dvv::media::resolveColour(info.desc);
    REQUIRE(rendered.params.matrix == dvv::media::ColourMatrix::Bt601);
    const std::string text = rawreel::cli::infoText(info);
    CHECK_MESSAGE(text.find("\ncolour: no standard (from file) matrix=bt601 range=limited ") !=
                      std::string::npos,
                  text);
    CHECK(rawreel::cli::infoJson(info)["standard"].is_null());
}
