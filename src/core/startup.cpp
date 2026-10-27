#include "core/startup.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/environment.h"
#include "dvv/base/paths.h"
#include "dvv/media/types.h"

namespace rawreel {

Startup startComponents(const dvv::components::FfmpegLoadOptions& options) {
    Startup startup;
    startup.ffmpeg = dvv::components::loadFfmpeg(options);
    dvv::components::addFfmpegCapabilities(startup.ffmpeg, startup.caps);
    startup.components = dvv::ComponentRegistry::create(startup.caps);
    return startup;
}

FfmpegOrigin currentFfmpegOrigin() {
    FfmpegOrigin origin;
    if (const dvv::Result<std::filesystem::path> executable = dvv::paths::executablePath();
        executable) {
        origin.appFolder = executable->parent_path();
    }
    origin.ffmpegDirVariable = dvv::getEnvironment("DVV_FFMPEG_DIR").value_or("");
#ifdef _WIN32
    const char* const home_variable = "USERPROFILE";
#else
    const char* const home_variable = "HOME";
#endif
    if (const std::optional<std::string> home = dvv::getEnvironment(home_variable);
        home && !home->empty()) {
        origin.homeFolder = dvv::media::pathFromUtf8(*home);
    }
    return origin;
}

std::string maskFolders(std::string text, const FfmpegOrigin& origin) {
    // (folder as the loader prints it, what the user sees instead); the raw and the normalized
    // form of each, because the loader prints both.
    std::vector<std::pair<std::string, std::string>> masks;
    const auto add = [&masks](const std::filesystem::path& folder, const std::string& shown) {
        if (!folder.has_relative_path()) {
            return;  // empty or a root: replacing it would garble every path
        }
        for (const std::filesystem::path& form : {folder, folder.lexically_normal()}) {
            std::string from = dvv::media::utf8FromPath(form);
            while (from.size() > 1 && (from.back() == '/' || from.back() == '\\')) {
                from.pop_back();
            }
            masks.emplace_back(std::move(from), shown);
        }
    };
    if (!origin.ffmpegDirVariable.empty()) {
        add(dvv::media::pathFromUtf8(origin.ffmpegDirVariable), "$DVV_FFMPEG_DIR");
    }
    if (!origin.appFolder.empty()) {
        const std::filesystem::path lib = std::filesystem::path("..") / "lib" / "ffmpeg";
        add((origin.appFolder / lib).lexically_normal(),
            dvv::media::utf8FromPath(std::filesystem::path("<app>") / lib));
        add(origin.appFolder, "<app>");
    }
    add(origin.homeFolder, "~");
    // Longest first, so a folder inside another one keeps its own name; one pass, so a replacement
    // is never matched again.
    std::ranges::stable_sort(masks, std::ranges::greater{},
                             [](const auto& mask) { return mask.first.size(); });
    std::string masked;
    masked.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::string_view rest = std::string_view(text).substr(at);
        const auto match = std::ranges::find_if(
            masks, [rest](const auto& mask) { return rest.starts_with(mask.first); });
        if (match != masks.end()) {
            masked += match->second;
            at += match->first.size();
        } else {
            masked += text[at];
            ++at;
        }
    }
    return masked;
}

std::string ffmpegStatusLine(const dvv::components::FfmpegStatus& status, StatusPaths paths) {
    return ffmpegStatusLine(status, paths, currentFfmpegOrigin());
}

std::string ffmpegStatusLine(const dvv::components::FfmpegStatus& status, StatusPaths paths,
                             const FfmpegOrigin& origin) {
    const bool hidden = paths == StatusPaths::Hidden;
    if (!status.core) {
        return std::format("FFmpeg: not available ({})", hidden
                                                             ? std::string("details in the log")
                                                             : maskFolders(status.reason, origin));
    }
    // Where the build comes from, never the folder it was loaded from (users share this line, and
    // the folder names their machine). The repository only for a bundled folder: any other build
    // would send bug reports to the wrong place.
    std::string from;
    if (!origin.ffmpegDirVariable.empty()) {
        from = " from DVV_FFMPEG_DIR";
    } else if (!origin.appFolder.empty()) {
        const std::filesystem::path directory = status.directory.lexically_normal();
        if (directory == (origin.appFolder / "ffmpeg").lexically_normal() ||
            directory == (origin.appFolder / ".." / "lib" / "ffmpeg").lexically_normal()) {
            from = std::format(" from {}", CUSTOM_FFMPEG_URL);
        }
    }
    std::string line = std::format("FFmpeg {} ({}){}", status.version, status.licence, from);
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
