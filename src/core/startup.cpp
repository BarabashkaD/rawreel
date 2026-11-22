#include "core/startup.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/environment.h"
#include "dvv/base/paths.h"
#include "dvv/base/utf8.h"
#include "dvv/components/ffmpeg.h"
#include "dvv/media/source_factory.h"

namespace rawreel {

Startup startComponents() {
    return Startup{.components = dvv::ComponentRegistry::load()};
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
        origin.homeFolder = dvv::pathFromText(*home);
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
            std::string from = dvv::pathDisplayText(form);
            while (from.size() > 1 && (from.back() == '/' || from.back() == '\\')) {
                from.pop_back();
            }
            masks.emplace_back(std::move(from), shown);
        }
    };
    if (!origin.ffmpegDirVariable.empty()) {
        add(dvv::pathFromText(origin.ffmpegDirVariable), "$DVV_FFMPEG_DIR");
    }
    if (!origin.appFolder.empty()) {
        const std::filesystem::path lib = std::filesystem::path("..") / "lib" / "ffmpeg";
        add((origin.appFolder / lib).lexically_normal(),
            dvv::pathDisplayText(std::filesystem::path("<app>") / lib));
        // macOS bundle: Contents/MacOS/../Frameworks/ffmpeg.
        const std::filesystem::path frameworks =
            std::filesystem::path("..") / "Frameworks" / "ffmpeg";
        add((origin.appFolder / frameworks).lexically_normal(),
            dvv::pathDisplayText(std::filesystem::path("<app>") / frameworks));
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

std::string ffmpegStatusLine(const dvv::ModuleStatus* module, StatusPaths paths) {
    return ffmpegStatusLine(module, paths, currentFfmpegOrigin());
}

std::string ffmpegStatusLine(const dvv::ModuleStatus* module, StatusPaths paths,
                             const FfmpegOrigin& origin) {
    if (module == nullptr) {
        return "FFmpeg: not available (not in this build)";
    }
    const bool hidden = paths == StatusPaths::Hidden;
    if (!std::ranges::contains(module->provides, dvv::components::FFMPEG_CAPABILITY)) {
        return std::format("FFmpeg: not available ({})", hidden
                                                             ? std::string("details in the log")
                                                             : maskFolders(module->reason, origin));
    }
    // Where the build comes from, never the folder it was loaded from (users share this line, and
    // the folder names their machine). The repository only for a bundled folder: any other build
    // would send bug reports to the wrong place. The module says when DVV_FFMPEG_DIR chose the
    // folder; the environment is not read for this.
    std::string from;
    if (module->property(dvv::MODULE_VARIABLE_PROPERTY).has_value()) {
        from = " from DVV_FFMPEG_DIR";
    } else if (const std::optional<std::string_view> folder =
                   module->property(dvv::MODULE_DIRECTORY_PROPERTY);
               folder && !origin.appFolder.empty()) {
        const std::filesystem::path directory = dvv::pathFromText(*folder).lexically_normal();
        if (directory == (origin.appFolder / "ffmpeg").lexically_normal() ||
            directory == (origin.appFolder / ".." / "lib" / "ffmpeg").lexically_normal() ||
            directory == (origin.appFolder / ".." / "Frameworks" / "ffmpeg").lexically_normal()) {
            from = std::format(" from {}", CUSTOM_FFMPEG_URL);
        }
    }
    const std::optional<std::string_view> version = module->property(dvv::MODULE_VERSION_PROPERTY);
    const std::optional<std::string_view> licence = module->property(dvv::MODULE_LICENCE_PROPERTY);
    std::string line = std::format("FFmpeg {} ({}){}", version.value_or(std::string_view{}),
                                   licence.value_or(std::string_view{}), from);
    if (!std::ranges::contains(module->provides, dvv::components::FFMPEG_LAVFI_CAPABILITY)) {
        line += ", lavfi not available";
    }
    return line;
}

namespace {

// A URI scheme a Source provider opens ("file" includes bare paths).
struct ProviderScheme {
    std::string_view provider;  // the provider's component id
    std::string_view scheme;
};

// The schemes of dvv-components' FFmpeg Source providers, the ones a module can withhold. An
// unavailable provider is never created, so its schemes() cannot be asked, and the factory offers
// a request only to the providers of its scheme.
constexpr std::array<ProviderScheme, 2> PROVIDER_SCHEMES{
    ProviderScheme{.provider = "ffmpeg", .scheme = "file"},
    ProviderScheme{.provider = "lavfi", .scheme = "lavfi"}};

// True when `provider` is a known provider that opens `scheme`.
bool opensScheme(const dvv::ComponentStatus& provider, std::string_view scheme) {
    return std::ranges::any_of(PROVIDER_SCHEMES, [&provider, scheme](const ProviderScheme& entry) {
        return entry.provider == provider.info.id && entry.scheme == scheme;
    });
}

// "; " + the status line of each module that withholds a capability an unavailable Source
// provider for `scheme` misses, when `error` is the factory's no-match (design v2 §4.6); empty
// otherwise.
std::string moduleSuffix(const dvv::Error& error, std::string_view scheme,
                         std::span<const dvv::ModuleStatus> modules,
                         std::span<const dvv::ComponentStatus> providers, StatusPaths paths) {
    std::string suffix;
    if (!dvv::media::isNoProviderMatch(error)) {
        return suffix;
    }
    for (const dvv::ModuleStatus& module : modules) {
        const bool withholds =
            std::ranges::any_of(providers, [&module, scheme](const dvv::ComponentStatus& provider) {
                return provider.unavailable && opensScheme(provider, scheme) &&
                       std::ranges::contains(module.withheld, provider.unavailable->capability);
            });
        if (withholds) {
            suffix += "; " + ffmpegStatusLine(&module, paths);
        }
    }
    return suffix;
}

// The Source providers of `startup`'s registry, available or not; empty without a registry.
std::vector<dvv::ComponentStatus> sourceProviders(const Startup& startup) {
    if (!startup.components) {
        return {};
    }
    return startup.components->list<dvv::media::ISourceProvider>();
}

// The modules of `startup`'s registry; empty without a registry.
std::span<const dvv::ModuleStatus> modulesOf(const Startup& startup) {
    if (!startup.components) {
        return {};
    }
    return startup.components->modules();
}

}  // namespace

std::string openErrorText(const dvv::Error& error, std::string_view scheme,
                          std::span<const dvv::ModuleStatus> modules,
                          std::span<const dvv::ComponentStatus> providers, StatusPaths paths) {
    return error.describe() + moduleSuffix(error, scheme, modules, providers, paths);
}

std::string openErrorText(const dvv::Error& error, std::string_view scheme, const Startup& startup,
                          StatusPaths paths) {
    const std::vector<dvv::ComponentStatus> providers = sourceProviders(startup);
    return openErrorText(error, scheme, modulesOf(startup), providers, paths);
}

std::string openErrorMessage(const dvv::Error& error, std::string_view scheme,
                             std::span<const dvv::ModuleStatus> modules,
                             std::span<const dvv::ComponentStatus> providers) {
    return error.message + moduleSuffix(error, scheme, modules, providers, StatusPaths::Shown);
}

std::string openErrorMessage(const dvv::Error& error, std::string_view scheme,
                             const Startup& startup) {
    const std::vector<dvv::ComponentStatus> providers = sourceProviders(startup);
    return openErrorMessage(error, scheme, modulesOf(startup), providers);
}

}  // namespace rawreel
