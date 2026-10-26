#include "core/startup.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace rawreel {

namespace {

// The path as UTF-8 (std::filesystem::path::string() uses the ANSI code page on Windows).
[[nodiscard]] std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}  // namespace

Startup startComponents(const dvv::components::FfmpegLoadOptions& options) {
    Startup startup;
    startup.ffmpeg = dvv::components::loadFfmpeg(options);
    dvv::components::addFfmpegCapabilities(startup.ffmpeg, startup.caps);
    startup.components = dvv::ComponentRegistry::create(startup.caps);
    return startup;
}

std::string ffmpegStatusLine(const dvv::components::FfmpegStatus& status, StatusPaths paths) {
    const bool hidden = paths == StatusPaths::Hidden;
    if (!status.core) {
        return std::format("FFmpeg: not available ({})",
                           hidden ? std::string("details in the log") : status.reason);
    }
    std::string line = std::format("FFmpeg {} ({})", status.version, status.licence);
    if (!hidden) {
        line += " from " + utf8(status.directory);
    }
    if (!status.lavfi) {
        line += ", lavfi not available";
    }
    return line;
}

namespace {

// `text` + "; " + the FFmpeg status line when FFmpeg is the likely reason (design v2 §4.6).
std::string withFfmpegStatus(std::string text, const dvv::Error& error, std::string_view uri,
                             const dvv::components::FfmpegStatus& status, StatusPaths paths) {
    const bool missing = !status.core || (uri.starts_with("lavfi:") && !status.lavfi);
    if (error.code == dvv::Errc::Unsupported && missing) {
        text += "; " + ffmpegStatusLine(status, paths);
    }
    return text;
}

}  // namespace

std::string openErrorText(const dvv::Error& error, std::string_view uri,
                          const dvv::components::FfmpegStatus& status, StatusPaths paths) {
    return withFfmpegStatus(error.describe(), error, uri, status, paths);
}

std::string openErrorMessage(const dvv::Error& error, std::string_view uri,
                             const dvv::components::FfmpegStatus& status) {
    return withFfmpegStatus(error.message, error, uri, status, StatusPaths::Shown);
}

}  // namespace rawreel
