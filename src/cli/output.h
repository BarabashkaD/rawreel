#pragma once

// rawreel-cli's exit codes and the text and JSON forms of its results and errors (spec §10.3, M2
// design §4.6).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"

namespace rawreel::cli {

namespace exit_code {
inline constexpr int SUCCESS = 0;
inline constexpr int BAD_ARGUMENTS = 1;  // also: a frame range outside the source
inline constexpr int INPUT_ERROR = 2;
inline constexpr int UNSUPPORTED = 3;  // also: an ambiguous source
inline constexpr int GPU_ERROR = 4;
}  // namespace exit_code

// The exit code of an error. `gpu_stage`: the error comes from the GPU setup (GpuRegistry::create,
// createContext, FrameStore::create with a context) or is OutOfMemory from an acquire in a store
// with a context.
[[nodiscard]] int exitCodeFor(const dvv::Error& error, bool gpu_stage) noexcept;

// One "name: value" line per field, each ending with a newline.
[[nodiscard]] std::string infoText(const dvv::media::SourceInfo& info);
[[nodiscard]] dvv::json::Value infoJson(const dvv::media::SourceInfo& info);
// [{"name","runs","bytes","nanoseconds"}...] in the order of stats.converters.
[[nodiscard]] dvv::json::Value convertersJson(const dvv::media::StoreStats& stats);
// {"error":{"code":code,"message":message}}
[[nodiscard]] dvv::json::Value errorJson(std::string_view code, std::string_view message);
// {"error":{"code":"Ambiguous","message":...,"candidates":[{"provider","confidence","why"}...]}}
[[nodiscard]] dvv::json::Value ambiguousJson(std::string_view uri,
                                             const dvv::media::Ambiguous& ambiguous);
// "ambiguous source '<uri>'; candidates (pass ?impl=<id> or more parameters):", then one line per
// candidate, "  <id>: confidence <n>, <why>"; every line ends with a newline.
[[nodiscard]] std::string ambiguousText(std::string_view uri,
                                        const dvv::media::Ambiguous& ambiguous);

// One converter of a bench run (the store's ConverterStats).
struct BenchStage {
    std::string name;
    std::uint64_t runs = 0, bytes = 0, nanoseconds = 0;
};
// The result of rawreel-cli bench.
struct BenchReport {
    std::string uri, format, reader, device;  // reader: SourceInfo::reader ("" -> "none")
    std::uint32_t width = 0, height = 0;
    std::int64_t frames = 0, inflight = 0;
    std::uint64_t frameBytes = 0;    // geometry bytes of one original frame
    double seconds = 0;              // wall time of the measured loop
    std::vector<BenchStage> stages;  // converters with runs > 0, Produce first
};
// "bench <uri>: ..." and "end-to-end: ..." lines, then a table with one line per stage; every line
// ends with a newline. End-to-end frames/s = frames / seconds and GB/s = frames * frameBytes /
// seconds / 1e9; a stage's GB/s = bytes / nanoseconds (per run, as concurrent run times add up).
[[nodiscard]] std::string benchText(const BenchReport& report);
// {"uri","device","format","width","height","frameBytes","frames","inflight","reader","seconds",
// "fps","gbps","stages":[{"name","runs","bytes","nanoseconds","gbps"}...]}
[[nodiscard]] dvv::json::Value benchJson(const BenchReport& report);

}  // namespace rawreel::cli
