// The bundled Vulkan driver of the macOS app (rawreel uses KosmicKrisp only).

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
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

std::optional<std::string> noVariables(std::string_view /*name*/) {
    return std::nullopt;
}

}  // namespace

TEST_SUITE("vulkan_driver") {
    TEST_CASE("the manifest in the bundle is used when no variable is set") {
        const Bundle bundle;
        bundle.install();
        const auto result = rawreel::bundledDriverEnvironment(bundle.exeDir, noVariables);
        REQUIRE(result);
        CHECK(fs::path(result.value_or(std::string{})).lexically_normal() ==
              bundle.manifest.lexically_normal());
    }

    TEST_CASE("VK_DRIVER_FILES and VK_ICD_FILENAMES win") {
        const Bundle bundle;
        bundle.install();
        for (const std::string_view name : {"VK_DRIVER_FILES", "VK_ICD_FILENAMES"}) {
            const auto get = [name](std::string_view asked) -> std::optional<std::string> {
                return asked == name ? std::optional<std::string>("/somewhere/icd.json")
                                     : std::nullopt;
            };
            CHECK_FALSE(rawreel::bundledDriverEnvironment(bundle.exeDir, get));
        }
    }

    TEST_CASE("empty VK_DRIVER_FILES and VK_ICD_FILENAMES count as unset") {
        const Bundle bundle;
        bundle.install();
        for (const std::string_view name : {"VK_DRIVER_FILES", "VK_ICD_FILENAMES"}) {
            const auto get = [name](std::string_view asked) -> std::optional<std::string> {
                return asked == name ? std::optional<std::string>("") : std::nullopt;
            };
            const auto result = rawreel::bundledDriverEnvironment(bundle.exeDir, get);
            REQUIRE(result);
            CHECK(fs::path(result.value_or(std::string{})).lexically_normal() ==
                  bundle.manifest.lexically_normal());
        }
    }

    TEST_CASE("no manifest (a build tree) leaves the loader alone") {
        const Bundle bundle;
        CHECK_FALSE(rawreel::bundledDriverEnvironment(bundle.exeDir, noVariables));
    }
}
