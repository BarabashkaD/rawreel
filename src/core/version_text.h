#pragma once

// Version lines of rawreel-cli --version and the About dialog (design M4 v2 D23).

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/json.h"

namespace rawreel {

struct VulkanStatus {
    bool available = false;
    std::string device;  // e.g. "NVIDIA GeForce ..." or "llvmpipe (...)"
    std::string api;     // "1.4.318"
    std::string reason;  // when not available
};
struct VersionReport {
    std::vector<std::pair<std::string, std::string>> packages;  // packageVersions()
    std::string vulkan;                                         // vulkanStatusLine(...)
    std::string ffmpeg;                                         // ffmpegStatusLine(...)
};

// {name, version} for rawreel, dvv-base, dvv-registry, dvv-platform, dvv-gpu, dvv-media,
// dvv-components, wxWidgets, in this order, from rawreel_core's compile definitions. wxWidgets is
// "not part of this build" in builds without wx (the sanitizer presets).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> packageVersions();
// "Vulkan: <device>, API <api>" or "Vulkan: not available (<reason>)".
[[nodiscard]] std::string vulkanStatusLine(const VulkanStatus& status);
// "<name> <version>\n" per package, then the Vulkan line and the FFmpeg line, each ending in "\n".
[[nodiscard]] std::string versionText(const VersionReport& report);
// The Help -> About content (design M4 v2 §4.7). No directory or path anywhere:
// users share it in bug reports.
struct AboutContent {
    std::string heading;      // "rawreel <version>", the dialog heading (the only name + version)
    std::string description;  // "Raw video player (Vulkan)"
    std::string copyright;    // "Copyright (c) 2026 BarabashkaD · MIT licence" (the GitHub handle)
    std::string website;      // "https://github.com/BarabashkaD/rawreel"
    // {"GPU:", <Vulkan line without "Vulkan: ">}, {"FFmpeg:", <FFmpeg line without its "FFmpeg "
    // or "FFmpeg: ">}, {"wxWidgets:", "<version> · appearance: <appearance or unknown>"}.
    std::vector<std::pair<std::string, std::string>> system;
    std::vector<std::pair<std::string, std::string>> components;  // the dvv-* packages, in order
};
// The About content of a report (from packageVersions(), the Vulkan line and the FFmpeg line with
// StatusPaths::Hidden) and the appearance outcome (e.g. "dark -> Ok"; empty: "unknown").
[[nodiscard]] AboutContent aboutContent(const VersionReport& report, std::string_view appearance);
// The full About text ("Copy to clipboard"; the selftest reads it): heading, description,
// copyright and website lines, the system lines with their labels padded to one column,
// "Components:" and "  <name> <version>" per component ("  none" without any), each line ending
// in "\n".
[[nodiscard]] std::string aboutText(const AboutContent& content);
// {"<key>": "<version>", ..., "vulkan": "<line>", "ffmpeg": "<line>"}; key = the package name in
// lower case with '-' replaced by '_' (M2's keys "rawreel", "dvv_base", ... stay; new:
// "dvv_components", "wxwidgets").
[[nodiscard]] dvv::json::Value versionJson(const VersionReport& report);

}  // namespace rawreel
