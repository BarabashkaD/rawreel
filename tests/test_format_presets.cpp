#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/format_presets.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/types.h"

using dvv::media::ColourMatrix;
using dvv::media::ColourModel;
using dvv::media::ColourParams;
using dvv::media::Rational;

TEST_SUITE("core") {
    TEST_CASE("format presets: size labels") {
        const std::vector<std::string_view> expected{
            "176x144 (QCIF)",     "352x288 (CIF)",         "640x360 (360p)",    "640x480 (VGA)",
            "720x480 (SD 525)",   "720x576 (SD 625)",      "1280x720 (720p)",   "1920x1080 (1080p)",
            "2048x1080 (DCI 2K)", "3840x2160 (2160p UHD)", "4096x2160 (DCI 4K)"};
        const auto presets = rawreel::sizePresets();
        REQUIRE(presets.size() == expected.size());
        for (std::size_t i = 0; i < presets.size(); ++i) {
            CHECK(rawreel::sizeLabel(presets[i]) == expected[i]);
        }
        CHECK(rawreel::CUSTOM_LABEL == "Custom");
    }

    TEST_CASE("format presets: size match") {
        const std::optional<std::size_t> hd = rawreel::findSizePreset(1920, 1080);
        REQUIRE(hd);
        CHECK(rawreel::sizePresets()[hd.value_or(0)].width == 1920);
        CHECK(rawreel::sizeLabel(rawreel::sizePresets()[hd.value_or(0)]) == "1920x1080 (1080p)");
        CHECK(rawreel::findSizePreset(176, 144) == std::size_t{0});
        CHECK_FALSE(rawreel::findSizePreset(1921, 1080));
        CHECK_FALSE(rawreel::findSizePreset(1080, 1920));
    }

    TEST_CASE("format presets: frame rates") {
        const std::vector<std::string_view> expected{
            "23.976", "24", "25", "29.97", "30", "48", "50", "59.94", "60", "100", "119.88", "120"};
        const auto rates = rawreel::frameRatePresets();
        REQUIRE(rates.size() == expected.size());
        for (std::size_t i = 0; i < rates.size(); ++i) {
            CHECK(rates[i] == expected[i]);
        }
        CHECK(rawreel::findFrameRatePreset({30000, 1001}) == std::size_t{3});
        CHECK(rawreel::findFrameRatePreset({25, 1}) == std::size_t{2});
        CHECK_FALSE(rawreel::findFrameRatePreset({2997, 100}));
        CHECK(rawreel::frameRateValueLabel(Rational{30000, 1001}) == "= 29.970 fps (30000/1001)");
        CHECK(rawreel::frameRateValueLabel(Rational{25, 1}) == "= 25 fps");
        CHECK(rawreel::frameRateValueLabel(std::nullopt).empty());
    }

    TEST_CASE("format presets: format model") {
        CHECK(rawreel::formatModel("yuv420p") == ColourModel::Yuv);
        CHECK(rawreel::formatModel("rgb24") == ColourModel::Rgb);
        CHECK(rawreel::formatModel("gray") == ColourModel::Gray);
        CHECK(rawreel::formatModel("@no such file.json") == ColourModel::Yuv);
        CHECK(rawreel::formatModel("nope") == ColourModel::Yuv);
    }

    // A JSON file above 1 MiB is not read (the model falls back to Yuv).
    TEST_CASE("format presets: formatModel of a JSON file") {
        const std::filesystem::path path =
            std::filesystem::current_path() / "rawreel-test-format-model.json";
        const std::u8string utf8 = path.u8string();
        const std::string name = "@" + std::string(utf8.begin(), utf8.end());
        const std::string json = R"({"model": "rgb"})";
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            REQUIRE(file.is_open());
            file << json;
        }
        CHECK(rawreel::formatModel(name) == ColourModel::Rgb);
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            REQUIRE(file.is_open());
            file << json << std::string((std::size_t{1} << 20U) + 1, ' ');
        }
        CHECK(rawreel::formatModel(name) == ColourModel::Yuv);
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    // The Colour list (D5R §4.4): the five standards, the base's one with its origin suffix.
    TEST_CASE("format presets: Colour list labels") {
        using dvv::media::ColourPreset;
        using rawreel::ColourOrigin;
        CHECK(rawreel::standardEntries().size() == 5);
        CHECK(rawreel::standardEntry(ColourPreset::Bt2100Hlg) == 4);
        CHECK(rawreel::standardEntry(ColourPreset::Bt601Pal) == 1);
        CHECK(rawreel::standardLabel(ColourPreset::Bt601Ntsc) == "BT.601 (NTSC)");
        CHECK(rawreel::standardLabel(ColourPreset::Bt2100Hlg) == "BT.2100");
        rawreel::ColourBase base{};
        CHECK(rawreel::originSuffix(base) == " (default)");
        base.origin = ColourOrigin::File;
        CHECK(rawreel::originSuffix(base) == " (from file)");
        base.origin = ColourOrigin::FileName;
        CHECK(rawreel::originSuffix(base) == " (from the file name)");
        base.origin = ColourOrigin::Clip;
        base.clipName = "clip.yuv.json";
        CHECK(rawreel::originSuffix(base) == " (from clip.yuv.json)");
        base.origin = ColourOrigin::Graph;
        CHECK(rawreel::originSuffix(base) == " (from the graph)");
        base.origin = ColourOrigin::Auto;
        CHECK(rawreel::originSuffix(base) == " (auto)");
        base.origin = ColourOrigin::File;
        base.standard = ColourPreset::Bt2100Hlg;
        CHECK(rawreel::colourListLabels(base) ==
              std::vector<std::string>{"BT.601 (NTSC)", "BT.601 (PAL)", "BT.709", "BT.2020",
                                       "BT.2100 (from file)"});
        base.standard.reset();
        const std::vector<std::string> none = rawreel::colourListLabels(base);
        REQUIRE(none.size() == 6);
        CHECK(none.front() == rawreel::NO_STANDARD_LABEL);
        CHECK(none.back() == "BT.2100");
    }

    // The dialog's values line (D5R §4.4).
    TEST_CASE("format presets: values line") {
        const ColourParams pal =
            dvv::media::colourPresetInfo(dvv::media::ColourPreset::Bt601Pal).colour;
        CHECK(rawreel::colourValuesText(pal, ColourModel::Yuv, false) ==
              "Matrix BT.601, primaries BT.601-625 (not converted)");
        CHECK(rawreel::colourValuesText(pal, ColourModel::Yuv, true) ==
              "Matrix BT.601, range limited, curve BT.1886, primaries BT.601-625 (not converted)");
        CHECK(rawreel::colourValuesText(pal, ColourModel::Rgb, false) ==
              "Matrix not used (RGB format), primaries BT.601-625 (not converted)");
        CHECK(rawreel::colourValuesText(pal, ColourModel::Gray, false) ==
              "Matrix not used (gray format), primaries BT.601-625 (not converted)");
    }

    TEST_CASE("format presets: RGB and gray") {
        const auto rgb = rawreel::defaultColour("rgb24", 1080);
        CHECK(rgb.params.range == dvv::media::ColourRange::Full);
        CHECK(rawreel::matrixNotUsedLabel(ColourModel::Rgb) == "Not used (RGB format)");
        CHECK(rawreel::matrixNotUsedLabel(ColourModel::Gray) == "Not used (gray format)");
        CHECK(rawreel::matrixNotUsedLabel(ColourModel::Yuv).empty());
        CHECK(rawreel::matrixLabel(ColourMatrix::Rgb) == "GBR (planes hold G, B, R)");
        CHECK(rawreel::matrixLabel(ColourMatrix::Bt2020Ncl) == "BT.2020 NCL");
    }
}
