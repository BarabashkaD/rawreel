#include "core/format_presets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/format_choice.h"
#include "dvv/base/error.h"
#include "dvv/base/json.h"
#include "dvv/media/clip_json.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

namespace rawreel {

namespace {

using dvv::media::ColourMatrix;
using dvv::media::ColourModel;
using dvv::media::ColourPreset;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::TransferFn;

constexpr std::array<SizePreset, 11> SIZES{{
    {.width = 176, .height = 144, .name = "QCIF"},
    {.width = 352, .height = 288, .name = "CIF"},
    {.width = 640, .height = 360, .name = "360p"},
    {.width = 640, .height = 480, .name = "VGA"},
    {.width = 720, .height = 480, .name = "SD 525"},
    {.width = 720, .height = 576, .name = "SD 625"},
    {.width = 1280, .height = 720, .name = "720p"},
    {.width = 1920, .height = 1080, .name = "1080p"},
    {.width = 2048, .height = 1080, .name = "DCI 2K"},
    {.width = 3840, .height = 2160, .name = "2160p UHD"},
    {.width = 4096, .height = 2160, .name = "DCI 4K"},
}};

constexpr std::array<std::string_view, 12> FRAME_RATES{
    "23.976", "24", "25", "29.97", "30", "48", "50", "59.94", "60", "100", "119.88", "120"};

// The model named by the "model" key of a pixel-format JSON file; nullopt when it cannot be read
// (readSmallFile(): above 1 MiB it is not read at all).
std::optional<ColourModel> jsonModel(std::string_view utf8_path) {
    const dvv::Result<std::string> text = dvv::media::readSmallFile(utf8_path);
    if (!text) {
        return std::nullopt;
    }
    const auto root = dvv::json::parse(*text);
    if (!root) {
        return std::nullopt;
    }
    const auto model = dvv::json::get<std::string>(*root, "model");
    if (!model) {
        return std::nullopt;
    }
    if (*model == "rgb") {
        return ColourModel::Rgb;
    }
    if (*model == "gray") {
        return ColourModel::Gray;
    }
    return ColourModel::Yuv;
}

constexpr std::array<ColourPreset, 5> STANDARDS{ColourPreset::Bt601Ntsc, ColourPreset::Bt601Pal,
                                                ColourPreset::Bt709, ColourPreset::Bt2020,
                                                ColourPreset::Bt2100Pq};

}  // namespace

std::span<const SizePreset> sizePresets() noexcept {
    return SIZES;
}

std::string sizeLabel(const SizePreset& preset) {
    return std::format("{}x{} ({})", preset.width, preset.height, preset.name);
}

std::optional<std::size_t> findSizePreset(std::uint32_t width, std::uint32_t height) noexcept {
    for (std::size_t i = 0; i < SIZES.size(); ++i) {
        if (SIZES.at(i).width == width && SIZES.at(i).height == height) {
            return i;
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> frameRatePresets() noexcept {
    return FRAME_RATES;
}

std::optional<std::size_t> findFrameRatePreset(dvv::media::Rational fps) noexcept {
    for (std::size_t i = 0; i < FRAME_RATES.size(); ++i) {
        if (parseFpsText(FRAME_RATES.at(i)) == fps) {
            return i;
        }
    }
    return std::nullopt;
}

std::string frameRateValueLabel(std::optional<dvv::media::Rational> fps) {
    if (!fps || fps->den <= 0) {
        return {};
    }
    if (fps->den == 1) {
        return std::format("= {} fps", fps->num);
    }
    return std::format("= {:.3f} fps ({}/{})",
                       static_cast<double>(fps->num) / static_cast<double>(fps->den), fps->num,
                       fps->den);
}

ColourModel formatModel(std::string_view format) {
    if (format.starts_with('@')) {
        return jsonModel(format.substr(1)).value_or(ColourModel::Yuv);
    }
    if (const auto id = dvv::media::findFormat(format)) {
        return dvv::media::descriptor(*id).model;
    }
    return ColourModel::Yuv;
}

dvv::media::ResolvedColour defaultColour(std::string_view format, std::uint32_t height) {
    auto id = dvv::media::findFormat(format);
    if (!id) {
        // A JSON or unknown format: a catalogue format of the same model gives the same defaults.
        switch (formatModel(format)) {
            case ColourModel::Rgb: id = dvv::media::findFormat("rgb24"); break;
            case ColourModel::Gray: id = dvv::media::findFormat("gray"); break;
            case ColourModel::Yuv: id = dvv::media::findFormat("yuv420p"); break;
        }
    }
    const dvv::media::FrameDesc desc{
        .format = id.value_or(dvv::media::FormatId{}), .colour = 0, .width = 0, .height = height};
    return dvv::media::resolveColour(desc);
}

std::string_view matrixLabel(ColourMatrix matrix) noexcept {
    switch (matrix) {
        case ColourMatrix::Unspecified: return "";
        case ColourMatrix::Bt601: return "BT.601";
        case ColourMatrix::Bt709: return "BT.709";
        case ColourMatrix::Bt2020Ncl: return "BT.2020 NCL";
        case ColourMatrix::Rgb: return "GBR (planes hold G, B, R)";
        case ColourMatrix::Custom: return "Custom";
    }
    return "";
}

std::string_view rangeLabel(ColourRange range) noexcept {
    switch (range) {
        case ColourRange::Unspecified: return "";
        case ColourRange::Limited: return "Limited";
        case ColourRange::Full: return "Full";
    }
    return "";
}

std::string_view transferLabel(TransferFn transfer) noexcept {
    switch (transfer) {
        case TransferFn::Unspecified: return "";
        case TransferFn::Srgb: return "sRGB";
        case TransferFn::Bt1886: return "BT.1886";
        case TransferFn::Linear: return "Linear";
        case TransferFn::Pq: return "PQ";
        case TransferFn::Hlg: return "HLG";
    }
    return "";
}

std::string_view primariesLabel(Primaries primaries) noexcept {
    switch (primaries) {
        case Primaries::Unspecified: return "";
        case Primaries::Bt601_525: return "BT.601-525";
        case Primaries::Bt601_625: return "BT.601-625";
        case Primaries::Bt709: return "BT.709";
        case Primaries::Bt2020: return "BT.2020";
    }
    return "";
}

std::string_view matrixNotUsedLabel(ColourModel model) noexcept {
    switch (model) {
        case ColourModel::Rgb: return "Not used (RGB format)";
        case ColourModel::Gray: return "Not used (gray format)";
        case ColourModel::Yuv: return "";
    }
    return "";
}

std::span<const ColourPreset> standardEntries() noexcept {
    return STANDARDS;
}

std::size_t standardEntry(ColourPreset standard) noexcept {
    if (standard == ColourPreset::Bt2100Hlg) {
        standard = ColourPreset::Bt2100Pq;
    }
    const auto at = std::ranges::find(STANDARDS, standard);
    return at != STANDARDS.end() ? static_cast<std::size_t>(at - STANDARDS.begin()) : 0;
}

std::string_view standardLabel(ColourPreset standard) noexcept {
    switch (standard) {
        case ColourPreset::Bt601Ntsc: return "BT.601 (NTSC)";
        case ColourPreset::Bt601Pal: return "BT.601 (PAL)";
        case ColourPreset::Bt709: return "BT.709";
        case ColourPreset::Bt2020: return "BT.2020";
        case ColourPreset::Bt2100Pq:
        case ColourPreset::Bt2100Hlg: return "BT.2100";
    }
    return "";
}

std::string originSuffix(const ColourBase& base) {
    switch (base.origin) {
        case ColourOrigin::Default: return " (default)";
        case ColourOrigin::File: return " (from file)";
        case ColourOrigin::FileName: return " (from the file name)";
        case ColourOrigin::Clip: return std::format(" (from {})", base.clipName);
        case ColourOrigin::Graph: return " (from the graph)";
        case ColourOrigin::Auto: return " (auto)";
    }
    return {};
}

std::vector<std::string> colourListLabels(const ColourBase& base) {
    std::vector<std::string> labels;
    if (!base.standard) {
        labels.emplace_back(NO_STANDARD_LABEL);
    }
    const std::optional<std::size_t> suffixed =
        base.standard ? std::optional<std::size_t>(standardEntry(*base.standard)) : std::nullopt;
    for (std::size_t i = 0; i < STANDARDS.size(); ++i) {
        std::string label(standardLabel(STANDARDS.at(i)));
        if (suffixed == i) {
            label += originSuffix(base);
        }
        labels.push_back(std::move(label));
    }
    return labels;
}

std::string colourValuesText(const dvv::media::ColourParams& values, ColourModel model, bool all) {
    std::string text = model == ColourModel::Yuv
                           ? std::format("Matrix {}", matrixLabel(values.matrix))
                           : std::format("Matrix not used ({} format)",
                                         model == ColourModel::Rgb ? "RGB" : "gray");
    if (all) {
        text += std::format(", range {}, curve {}",
                            values.range == ColourRange::Full ? "full" : "limited",
                            transferLabel(values.transfer));
    }
    text += std::format(", primaries {} (not converted)", primariesLabel(values.primaries));
    return text;
}

}  // namespace rawreel
