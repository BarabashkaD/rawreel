#pragma once

// A unique directory under the system temporary directory, removed with its contents.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix) {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                std::format("{}-{:08x}{:08x}", prefix, random(), random());
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
        }
        FAIL("no temporary directory could be created");
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};
