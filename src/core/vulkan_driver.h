#pragma once

// On macOS the app bundles the KosmicKrisp Vulkan driver and must use only that one (never
// MoltenVK or a system-wide Vulkan SDK) unless the user overrides it with VK_DRIVER_FILES or
// VK_ICD_FILENAMES. The bundle layout: Contents/MacOS/<executable>,
// Contents/Resources/vulkan/icd.d/<KOSMICKRISP_MANIFEST>.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace rawreel {

// The manifest of the KosmicKrisp driver (from the LunarG SDK).
inline constexpr std::string_view KOSMICKRISP_MANIFEST = "libkosmickrisp_icd.json";

// The value for VK_DRIVER_FILES: <exe_dir>/../Resources/vulkan/icd.d/<KOSMICKRISP_MANIFEST>, or
// nullopt when VK_DRIVER_FILES or VK_ICD_FILENAMES is set (the user decided) or the manifest does
// not exist (a build tree, or a platform without a bundle).
[[nodiscard]] std::optional<std::string> bundledDriverEnvironment(
    const std::filesystem::path& exe_dir,
    const std::function<std::optional<std::string>(std::string_view)>& get_env);

// macOS: sets VK_DRIVER_FILES from bundledDriverEnvironment() for this executable, never
// overwriting a variable. Elsewhere: nothing. Must run before anything can open the Vulkan loader,
// i.e. as the first statement of main().
void applyBundledDriver();

}  // namespace rawreel
