#include "cli/output.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "dvv/base/uri.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/format_json.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"
#include "dvv/media/uri_colour.h"

namespace rawreel::cli {

namespace {

using dvv::media::ColourMatrix;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::SourceAccess;
using dvv::media::TransferFn;

[[nodiscard]] std::string_view matrixName(ColourMatrix matrix) noexcept {
    switch (matrix) {
        case ColourMatrix::Unspecified: return "unspecified";
        case ColourMatrix::Bt601: return "bt601";
        case ColourMatrix::Bt709: return "bt709";
        case ColourMatrix::Bt2020Ncl: return "bt2020";
        case ColourMatrix::Rgb: return "rgb";
        case ColourMatrix::Custom: return "custom";
    }
    return "unspecified";
}

[[nodiscard]] std::string_view rangeName(ColourRange range) noexcept {
    switch (range) {
        case ColourRange::Unspecified: return "unspecified";
        case ColourRange::Limited: return "limited";
        case ColourRange::Full: return "full";
    }
    return "unspecified";
}

[[nodiscard]] std::string_view transferName(TransferFn transfer) noexcept {
    switch (transfer) {
        case TransferFn::Unspecified: return "unspecified";
        case TransferFn::Srgb: return "srgb";
        case TransferFn::Bt1886: return "bt1886";
        case TransferFn::Pq: return "pq";
        case TransferFn::Hlg: return "hlg";
        case TransferFn::Linear: return "linear";
    }
    return "unspecified";
}

[[nodiscard]] std::string_view primariesName(Primaries primaries) noexcept {
    switch (primaries) {
        case Primaries::Unspecified: return "unspecified";
        case Primaries::Bt601_525: return "bt601-525";
        case Primaries::Bt601_625: return "bt601-625";
        case Primaries::Bt709: return "bt709";
        case Primaries::Bt2020: return "bt2020";
    }
    return "unspecified";
}

// Text form ("random-cheap") and JSON form ("randomCheap").
[[nodiscard]] std::string_view accessText(SourceAccess access) noexcept {
    switch (access) {
        case SourceAccess::RandomCheap: return "random-cheap";
        case SourceAccess::RandomCostly: return "random-costly";
        case SourceAccess::Sequential: return "sequential";
    }
    return "sequential";
}

[[nodiscard]] std::string_view accessJson(SourceAccess access) noexcept {
    switch (access) {
        case SourceAccess::RandomCheap: return "randomCheap";
        case SourceAccess::RandomCostly: return "randomCostly";
        case SourceAccess::Sequential: return "sequential";
    }
    return "sequential";
}

// The tight frame size; 0 when the geometry is invalid (a source never reports such a frame).
[[nodiscard]] std::uint64_t frameBytes(const dvv::media::FrameDesc& desc) {
    const auto geometry = dvv::media::geometry(desc.format, desc.width, desc.height);
    return geometry ? geometry->bytes : 0;
}

[[nodiscard]] std::string_view readerName(const std::string& reader) noexcept {
    return reader.empty() ? std::string_view("none") : std::string_view(reader);
}

// 0 for an empty run, so that no division by zero reaches the output.
[[nodiscard]] double benchFps(const BenchReport& report) noexcept {
    return report.seconds > 0 ? static_cast<double>(report.frames) / report.seconds : 0.0;
}

[[nodiscard]] double benchGbps(const BenchReport& report) noexcept {
    return report.seconds > 0 ? static_cast<double>(report.frames) *
                                    static_cast<double>(report.frameBytes) / report.seconds / 1e9
                              : 0.0;
}

// Bytes per nanosecond are GB/s.
[[nodiscard]] double stageGbps(const BenchStage& stage) noexcept {
    return stage.nanoseconds > 0
               ? static_cast<double>(stage.bytes) / static_cast<double>(stage.nanoseconds)
               : 0.0;
}

// The provider's colour hint labels the file's colour; with a
// colour= key in the URI the standard is the user's, so the hint's "no standard" does not count.
dvv::media::FileHint labelHint(const dvv::media::SourceInfo& info) {
    dvv::media::FileHint hint = info.colourHint;
    if (const auto uri = dvv::parseUri(info.resolvedUri)) {
        if (const auto spec = dvv::media::colourSpecFromQuery(*uri); spec && spec->preset) {
            hint.nonStandard = false;
        }
    }
    return hint;
}

// "<token> (<origin>)": no origin for the URI's colour=, "(default)" when nothing gave a colour,
// else "(from file)"; "no standard (from file)" for a file colour that matches no standard (the
// provider's colourHint counts).
std::string standardText(const dvv::media::SourceInfo& info,
                         const dvv::media::ResolvedColour& resolved) {
    const std::string_view origin =
        info.resolvedUri.starts_with("lavfi:") ? "(from the graph)" : "(from file)";
    if (!resolved.standard) {
        return std::format("no standard {}", origin);
    }
    const std::string_view token = dvv::media::colourPresetInfo(*resolved.standard).uriName;
    if (const auto uri = dvv::parseUri(info.resolvedUri)) {
        if (const auto spec = dvv::media::colourSpecFromQuery(*uri); spec && spec->preset) {
            return std::string(token);
        }
    }
    std::uint8_t unsignalled = dvv::media::AssumedTransfer | dvv::media::AssumedPrimaries;
    if (dvv::media::descriptor(info.desc.format).model == dvv::media::ColourModel::Yuv) {
        unsignalled |= dvv::media::AssumedMatrix;
    }
    const bool fallback = *resolved.standard == dvv::media::ColourPreset::Bt709 &&
                          (resolved.assumed & unsignalled) == unsignalled;
    return std::format("{} {}", token, fallback ? std::string_view("(default)") : origin);
}

}  // namespace
int exitCodeFor(const dvv::Error& error, bool gpu_stage) noexcept {
    switch (error.code) {
        case dvv::Errc::Io:
        case dvv::Errc::Format: return exit_code::INPUT_ERROR;
        case dvv::Errc::Unsupported:
            return gpu_stage ? exit_code::GPU_ERROR : exit_code::UNSUPPORTED;
        case dvv::Errc::DeviceLost:
        case dvv::Errc::ShaderCompile: return exit_code::GPU_ERROR;
        case dvv::Errc::OutOfMemory:
        case dvv::Errc::Internal:
        case dvv::Errc::Cancelled: return gpu_stage ? exit_code::GPU_ERROR : exit_code::INPUT_ERROR;
    }
    return exit_code::INPUT_ERROR;
}

std::string infoText(const dvv::media::SourceInfo& info) {
    std::string text;
    text += std::format("uri: {}\n", info.resolvedUri);
    text += std::format("provider: {}\n", info.provider);
    text += std::format("format: {}\n", dvv::media::descriptor(info.desc.format).name);
    text += std::format("size: {}x{}\n", info.desc.width, info.desc.height);
    text += std::format("fps: {}/{}\n", info.fps.num, info.fps.den);
    if (info.frameCount) {
        text += std::format("frames: {}\n", *info.frameCount);
    } else {
        text += "frames: unbounded\n";
    }
    text += std::format("frame bytes: {}\n", frameBytes(info.desc));
    // D5R §3.1: the standard in use and where it comes from, then the four values in use.
    // The values (and the assumed bits) are the renderer's, resolved with the
    // default hint as dvv-media's ToDisplay does; only the standard label takes the provider's
    // hint.
    dvv::media::ResolvedColour resolved = dvv::media::resolveColour(info.desc);
    resolved.standard = dvv::media::resolveColour(info.desc, labelHint(info)).standard;
    const dvv::media::ColourParams& used = resolved.params;
    text +=
        std::format("colour: {} matrix={} range={} transfer={} primaries={}\n",
                    standardText(info, resolved), matrixName(used.matrix), rangeName(used.range),
                    transferName(used.transfer), primariesName(used.primaries));
    text += std::format("access: {}\n", accessText(info.access));
    if (!info.reader.empty()) {
        text += std::format("reads: {}\n", info.reader);  // the file read mode
    }
    for (const std::string& warning : info.warnings) {
        text += std::format("warning: {}\n", warning);
    }
    for (const std::string& note : info.notes) {
        text += std::format("note: {}\n", note);
    }
    return text;
}

dvv::json::Value infoJson(const dvv::media::SourceInfo& info) {
    const dvv::media::ColourParams& colour = dvv::media::colourParams(info.desc.colour);
    dvv::json::Value value = dvv::json::Value::object();
    value["uri"] = info.resolvedUri;
    value["provider"] = info.provider;
    value["format"] = dvv::media::formatToJson(info.desc.format);
    value["width"] = info.desc.width;
    value["height"] = info.desc.height;
    value["fps"] = {{"num", info.fps.num}, {"den", info.fps.den}};
    value["frames"] = info.frameCount ? dvv::json::Value(*info.frameCount) : dvv::json::Value();
    value["frameBytes"] = frameBytes(info.desc);
    // The provider's values as it reports them (a BT.601 matrix tag also fills
    // in the PAL or NTSC primaries it implies); "standard" is the resolved standard.
    value["colour"] = {{"matrix", matrixName(colour.matrix)},
                       {"range", rangeName(colour.range)},
                       {"transfer", transferName(colour.transfer)},
                       {"primaries", primariesName(colour.primaries)}};
    const std::optional<dvv::media::ColourPreset> standard =
        dvv::media::resolveColour(info.desc, labelHint(info)).standard;
    value["standard"] =
        standard ? dvv::json::Value(std::string(dvv::media::colourPresetInfo(*standard).uriName))
                 : dvv::json::Value();
    value["access"] = accessJson(info.access);
    value["deterministic"] = info.deterministic;
    value["reader"] = info.reader.empty() ? dvv::json::Value() : dvv::json::Value(info.reader);
    value["warnings"] = dvv::json::Value::array();
    for (const std::string& warning : info.warnings) {
        value["warnings"].push_back(warning);
    }
    value["notes"] = dvv::json::Value::array();
    for (const std::string& note : info.notes) {
        value["notes"].push_back(note);
    }
    return value;
}

dvv::json::Value convertersJson(const dvv::media::StoreStats& stats) {
    dvv::json::Value converters = dvv::json::Value::array();
    for (const dvv::media::ConverterStats& converter : stats.converters) {
        converters.push_back({{"name", converter.name},
                              {"runs", converter.runs},
                              {"bytes", converter.bytes},
                              {"nanoseconds", converter.nanoseconds}});
    }
    return converters;
}

dvv::json::Value errorJson(std::string_view code, std::string_view message) {
    dvv::json::Value value = dvv::json::Value::object();
    value["error"] = {{"code", code}, {"message", message}};
    return value;
}

dvv::json::Value ambiguousJson(std::string_view uri, const dvv::media::Ambiguous& ambiguous) {
    dvv::json::Value value = errorJson("Ambiguous", std::format("ambiguous source '{}'", uri));
    dvv::json::Value candidates = dvv::json::Value::array();
    for (const dvv::media::Candidate& candidate : ambiguous.candidates) {
        candidates.push_back({{"provider", candidate.component},
                              {"confidence", candidate.confidence},
                              {"why", candidate.why}});
    }
    value["error"]["candidates"] = std::move(candidates);
    return value;
}

std::string ambiguousText(std::string_view uri, const dvv::media::Ambiguous& ambiguous) {
    std::string text = std::format(
        "ambiguous source '{}'; candidates (pass ?impl=<id> or more parameters):\n", uri);
    for (const dvv::media::Candidate& candidate : ambiguous.candidates) {
        text += std::format("  {}: confidence {}, {}\n", candidate.component, candidate.confidence,
                            candidate.why);
    }
    return text;
}

std::string benchText(const BenchReport& report) {
    std::string text = std::format(
        "bench {}: {} frames {}x{} {}, inflight {}{}, reads {}, device {}\n", report.uri,
        report.frames, report.width, report.height, report.format, report.inflight,
        report.readOnly ? ", read only" : "", readerName(report.reader), report.device);
    text += std::format("{}: {:.1f} frames/s, {:.2f} GB/s in {:.3f} s\n",
                        report.readOnly ? "read wall clock" : "end-to-end", benchFps(report),
                        benchGbps(report), report.seconds);
    text +=
        std::format("{:<14}{:>5}{:>13}{:>10}{:>7}\n", "stage", "runs", "bytes", "seconds", "GB/s");
    for (const BenchStage& stage : report.stages) {
        text +=
            std::format("{:<14}{:>5}{:>13}{:>10.3f}{:>7.2f}\n", stage.name, stage.runs, stage.bytes,
                        static_cast<double>(stage.nanoseconds) / 1e9, stageGbps(stage));
    }
    return text;
}

dvv::json::Value benchJson(const BenchReport& report) {
    dvv::json::Value value = dvv::json::Value::object();
    value["uri"] = report.uri;
    value["device"] = report.device;
    value["format"] = report.format;
    value["width"] = report.width;
    value["height"] = report.height;
    value["frameBytes"] = report.frameBytes;
    value["frames"] = report.frames;
    value["inflight"] = report.inflight;
    value["reader"] = readerName(report.reader);
    value["readOnly"] = report.readOnly;
    value["seconds"] = report.seconds;
    value["fps"] = benchFps(report);
    value["gbps"] = benchGbps(report);
    dvv::json::Value stages = dvv::json::Value::array();
    for (const BenchStage& stage : report.stages) {
        stages.push_back({{"name", stage.name},
                          {"runs", stage.runs},
                          {"bytes", stage.bytes},
                          {"nanoseconds", stage.nanoseconds},
                          {"gbps", stageGbps(stage)}});
    }
    value["stages"] = std::move(stages);
    return value;
}

}  // namespace rawreel::cli
