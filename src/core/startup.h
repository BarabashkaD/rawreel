#pragma once

// Startup shared by rawreel-gui and rawreel-cli (design M4 v2 §4.6): the component registry, which
// loads every registered module (FFmpeg among them) and feeds the SourceFactory of the Session
// (GUI) and of the CLI commands, and the FFmpeg status line from the FFmpeg module's status.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/registry/component_registry.h"
#include "dvv/registry/module.h"

namespace rawreel {

struct Startup {
    std::shared_ptr<dvv::ComponentRegistry> components;
};
// The component registry, which loads every registered module (FFmpeg among them). Never fails
// because of FFmpeg: a missing or refused FFmpeg only withholds its capabilities.
[[nodiscard]] Startup startComponents();

// What the FFmpeg line needs to know about this machine: where the bundled FFmpeg lives, and the
// folders it must not print (users paste the line into bug reports, and a folder names their
// machine).
struct FfmpegOrigin {
    std::filesystem::path appFolder;   // the executable's folder; empty when unknown
    std::string ffmpegDirVariable;     // DVV_FFMPEG_DIR, for its mask only; empty when unset
    std::filesystem::path homeFolder;  // HOME (Linux) or USERPROFILE (Windows); empty when unset
};
// The origin of this process (executable path and environment).
[[nodiscard]] FfmpegOrigin currentFfmpegOrigin();

// One line for rawreel-cli --version, its --json, the log and About (design D21/D23), from the
// FFmpeg module's status (registry.module(dvv::components::FFMPEG_MODULE)):
//   core and lavfi: "FFmpeg <version> (<licence>)<from>"
//   core only:      the same + ", lavfi not available"
//   otherwise:      "FFmpeg: not available (<reason>)"
// <from> names where the build comes from, never the folder it was loaded from:
//   " from DVV_FFMPEG_DIR" when the module says the variable chose the folder (the loader then
//   looks nowhere else),
//   " from <CUSTOM_FFMPEG_URL>" when the folder is a bundled one (<app>/ffmpeg, <app>/../lib/ffmpeg
//   or, in a macOS bundle, <app>/../Frameworks/ffmpeg) and nothing otherwise.
// The reason is masked with maskFolders(), the log included: <app>, $DVV_FFMPEG_DIR and ~ stay
// readable for the user who reads them. StatusPaths::Hidden (About, the GUI infobar) leaves out the
// reason: "FFmpeg: not available (details in the log)". `module` null (no FFmpeg module in this
// build): "FFmpeg: not available (not in this build)".
inline constexpr std::string_view CUSTOM_FFMPEG_URL =
    "https://github.com/BarabashkaD/custom_ffmpeg";
enum class StatusPaths : std::uint8_t { Shown, Hidden };
[[nodiscard]] std::string ffmpegStatusLine(const dvv::ModuleStatus* module,
                                           StatusPaths paths = StatusPaths::Shown);
[[nodiscard]] std::string ffmpegStatusLine(const dvv::ModuleStatus* module, StatusPaths paths,
                                           const FfmpegOrigin& origin);

// `text` with the folders of `origin` replaced: the DVV_FFMPEG_DIR value by "$DVV_FFMPEG_DIR",
// <app>/../lib/ffmpeg and <app>/../Frameworks/ffmpeg (normalized) by their "<app>/..." names, the
// executable's folder by "<app>" and the home folder by "~" (the longest match first; root
// folders are never replaced).
[[nodiscard]] std::string maskFolders(std::string text, const FfmpegOrigin& origin);

// The open-error text of the CLI and the GUI (design v2 §4.6): error.describe(), plus "; " + the
// status line of each module that withholds a capability an unavailable Source provider misses,
// when isNoProviderMatch(error). The Startup form reads the registry's modules and Source
// providers.
[[nodiscard]] std::string openErrorText(const dvv::Error& error, const Startup& startup,
                                        StatusPaths paths = StatusPaths::Shown);
[[nodiscard]] std::string openErrorText(const dvv::Error& error,
                                        std::span<const dvv::ModuleStatus> modules,
                                        std::span<const dvv::ComponentStatus> providers,
                                        StatusPaths paths = StatusPaths::Shown);

// The same for machine-readable output (rawreel-cli --json): error.message instead of
// error.describe() (no code, source location or cause, as every other --json error), plus the same
// suffix under the same condition.
[[nodiscard]] std::string openErrorMessage(const dvv::Error& error, const Startup& startup);
[[nodiscard]] std::string openErrorMessage(const dvv::Error& error,
                                           std::span<const dvv::ModuleStatus> modules,
                                           std::span<const dvv::ComponentStatus> providers);

}  // namespace rawreel
