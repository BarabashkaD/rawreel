#include "cli/output.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "dvv/media/format_json.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

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
    const dvv::media::ColourParams& colour = dvv::media::colourParams(info.desc.colour);
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
    text += std::format("colour: matrix={} range={} transfer={} primaries={}\n",
                        matrixName(colour.matrix), rangeName(colour.range),
                        transferName(colour.transfer), primariesName(colour.primaries));
    text += std::format("access: {}\n", accessText(info.access));
    if (!info.reader.empty()) {
        text += std::format("reads: {}\n", info.reader);  // M3.1 U-IO2: the file read mode
    }
    for (const std::string& warning : info.warnings) {
        text += std::format("warning: {}\n", warning);
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
    value["colour"] = {{"matrix", matrixName(colour.matrix)},
                       {"range", rangeName(colour.range)},
                       {"transfer", transferName(colour.transfer)},
                       {"primaries", primariesName(colour.primaries)}};
    value["access"] = accessJson(info.access);
    value["deterministic"] = info.deterministic;
    value["reader"] = info.reader.empty() ? dvv::json::Value() : dvv::json::Value(info.reader);
    value["warnings"] = dvv::json::Value::array();
    for (const std::string& warning : info.warnings) {
        value["warnings"].push_back(warning);
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
