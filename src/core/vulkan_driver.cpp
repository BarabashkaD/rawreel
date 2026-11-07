#include "core/vulkan_driver.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "dvv/base/environment.h"
#include "dvv/base/error.h"
#include "dvv/base/paths.h"
#include "dvv/media/types.h"

namespace rawreel {

std::optional<std::string> bundledDriverEnvironment(
    const std::filesystem::path& exe_dir,
    const std::function<std::optional<std::string>(std::string_view)>& get_env) {
    // The loader treats an empty value as unset, and so does this.
    const auto is_set = [&get_env](std::string_view name) {
        const std::optional<std::string> value = get_env(name);
        return value && !value->empty();
    };
    if (is_set("VK_DRIVER_FILES") || is_set("VK_ICD_FILENAMES")) {
        return std::nullopt;
    }
    const std::filesystem::path manifest = (exe_dir / ".." / "Resources" / "vulkan" / "icd.d" /
                                            std::filesystem::path(KOSMICKRISP_MANIFEST))
                                               .lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(manifest, error)) {
        return std::nullopt;
    }
    return dvv::media::utf8FromPath(manifest);
}

void applyBundledDriver() {
#ifdef __APPLE__
    const dvv::Result<std::filesystem::path> executable = dvv::paths::executablePath();
    if (!executable) {
        return;
    }
    const std::optional<std::string> manifest =
        bundledDriverEnvironment(executable->parent_path(), dvv::getEnvironment);
    if (manifest) {
        // Not set (bundledDriverEnvironment checked): nothing is overwritten.
        static_cast<void>(dvv::setEnvironment("VK_DRIVER_FILES", *manifest));
    }
#endif
}

}  // namespace rawreel
