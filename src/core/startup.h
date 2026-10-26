#pragma once

// Startup shared by rawreel-gui and rawreel-cli (design M4 v2 §4.6): the FFmpeg load, the
// capabilities (FFmpeg's only; "gpu.compute" returns in v1.x with the analysers) and the component
// registry that feeds the SourceFactory of the Session (GUI) and of the CLI commands.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_registry.h"

namespace rawreel {

struct Startup {
    std::shared_ptr<dvv::ComponentRegistry> components;
    dvv::Capabilities caps;
    dvv::components::FfmpegStatus ffmpeg;
};

// loadFfmpeg(options), addFfmpegCapabilities, ComponentRegistry::create(caps). Never fails because
// of FFmpeg: a missing or refused FFmpeg only leaves the capabilities off.
[[nodiscard]] Startup startComponents(const dvv::components::FfmpegLoadOptions& options);

// One line for rawreel-cli --version and About (design D21/D23):
//   core and lavfi: "FFmpeg <version> (<licence>) from <directory as UTF-8>"
//   core only:      the same + ", lavfi not available"
//   otherwise:      "FFmpeg: not available (<reason>)"
// The directory is printed at run time only. StatusPaths::Hidden (About, the GUI infobar) leaves
// out every path: no " from <directory>", and "FFmpeg: not available (details in the log)" instead
// of the reason (it may name the folders it looked in).
enum class StatusPaths : std::uint8_t { Shown, Hidden };
[[nodiscard]] std::string ffmpegStatusLine(const dvv::components::FfmpegStatus& status,
                                           StatusPaths paths = StatusPaths::Shown);

// The open-error text of the CLI and the GUI (design v2 §4.6): error.describe(), plus "; " +
// ffmpegStatusLine(status, paths) when error.code == Errc::Unsupported and either !status.core, or
// `uri` starts with "lavfi:" and !status.lavfi.
[[nodiscard]] std::string openErrorText(const dvv::Error& error, std::string_view uri,
                                        const dvv::components::FfmpegStatus& status,
                                        StatusPaths paths = StatusPaths::Shown);

// The same for machine-readable output (rawreel-cli --json): error.message instead of
// error.describe() (no code, source location or cause, as every other --json error), plus the same
// FFmpeg status suffix under the same condition.
[[nodiscard]] std::string openErrorMessage(const dvv::Error& error, std::string_view uri,
                                           const dvv::components::FfmpegStatus& status);

}  // namespace rawreel
