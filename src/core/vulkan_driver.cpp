#include "core/vulkan_driver.h"

#include <filesystem>
#include <optional>
#include <span>
#include <system_error>

#include "core/native_os.h"
#include "dvv/base/error.h"
#include "dvv/base/paths.h"
#include "dvv/gpu/bundled_drivers.h"

namespace rawreel {

std::optional<std::filesystem::path> bundledDriverManifest(const std::filesystem::path& exe_dir) {
    std::filesystem::path manifest = (exe_dir / ".." / "Resources" / "vulkan" / "icd.d" /
                                      std::filesystem::path(KOSMICKRISP_MANIFEST))
                                         .lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(manifest, error)) {
        return std::nullopt;
    }
    return manifest;
}

dvv::Result<dvv::gpu::DriverChoice> applyBundledDriver() {
    if constexpr (NATIVE_OS == Os::MacOs) {
        DVV_TRY(const std::filesystem::path executable, dvv::paths::executablePath());
        const std::optional<std::filesystem::path> manifest =
            bundledDriverManifest(executable.parent_path());
        if (!manifest) {
            return dvv::gpu::DriverChoice::NoManifest;
        }
        return dvv::gpu::useBundledDrivers(std::span(&*manifest, 1));
    } else {
        return dvv::gpu::DriverChoice::NoManifest;
    }
}

}  // namespace rawreel
