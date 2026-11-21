#pragma once

// On macOS the app bundles the KosmicKrisp Vulkan driver and must use only that one (never
// MoltenVK or a system-wide Vulkan SDK) unless the user chose a driver (dvv-gpu applies that
// rule). The bundle layout: Contents/MacOS/<executable>,
// Contents/Resources/vulkan/icd.d/<KOSMICKRISP_MANIFEST>.

#include <filesystem>
#include <optional>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/gpu/bundled_drivers.h"

namespace rawreel {

// The manifest of the KosmicKrisp driver (from the LunarG SDK).
inline constexpr std::string_view KOSMICKRISP_MANIFEST = "libkosmickrisp_icd.json";

// <exe_dir>/../Resources/vulkan/icd.d/<KOSMICKRISP_MANIFEST> when it is a regular file.
[[nodiscard]] std::optional<std::filesystem::path> bundledDriverManifest(
    const std::filesystem::path& exe_dir);

// macOS: dvv::gpu::useBundledDrivers on this executable's bundled manifest. Other OSes, and no
// manifest in the bundle: DriverChoice::NoManifest. Call it first, before any thread; the caller
// logs an error once its sinks exist.
[[nodiscard]] dvv::Result<dvv::gpu::DriverChoice> applyBundledDriver();

}  // namespace rawreel
