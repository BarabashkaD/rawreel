// The bundled Vulkan driver of the macOS app (rawreel uses KosmicKrisp only).

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <system_error>

#include "core/vulkan_driver.h"

namespace {

namespace fs = std::filesystem;

// Unique per run, so concurrent test processes (several presets) do not collide.
fs::path uniqueRoot() {
    std::random_device random;
    return fs::temp_directory_path() /
           std::format("rawreel_vulkan_driver_test_{:08x}{:08x}", random(), random());
}

struct Bundle {
    fs::path root = uniqueRoot();
    fs::path exeDir = root / "Contents" / "MacOS";
    fs::path manifest = root / "Contents" / "Resources" / "vulkan" / "icd.d" /
                        fs::path(rawreel::KOSMICKRISP_MANIFEST);

    Bundle() {
        fs::remove_all(root);
        fs::create_directories(exeDir);
    }
    ~Bundle() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    Bundle(const Bundle&) = delete;
    Bundle& operator=(const Bundle&) = delete;
    void install() const {
        fs::create_directories(manifest.parent_path());
        std::ofstream(manifest) << "{}";
    }
};

}  // namespace

TEST_SUITE("vulkan_driver") {
    TEST_CASE("the manifest in the bundle is found") {
        const Bundle bundle;
        bundle.install();
        const std::optional<fs::path> result = rawreel::bundledDriverManifest(bundle.exeDir);
        REQUIRE(result.has_value());
        CHECK(result->lexically_normal() == bundle.manifest.lexically_normal());
    }

    TEST_CASE("no manifest, no path") {
        const Bundle bundle;
        CHECK(rawreel::bundledDriverManifest(bundle.exeDir) == std::nullopt);
    }
}
