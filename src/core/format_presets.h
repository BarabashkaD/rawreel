#pragma once

// The wx-free lists and labels of the format dialog (D5 design §1.3-1.5): the size presets, the
// frame-rate entries, the colour value labels and the label of the Colour list's entry 0.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/format_choice.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

namespace rawreel {

// The last entry of the size choice and of the Colour list.
inline constexpr std::string_view CUSTOM_LABEL = "Custom";

struct SizePreset {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string_view name;  // e.g. "1080p"
};

// QCIF, CIF, 360p, VGA, SD 525, SD 625, 720p, 1080p, DCI 2K, 2160p UHD, DCI 4K (§1.3).
[[nodiscard]] std::span<const SizePreset> sizePresets() noexcept;
// "WxH (name)", e.g. "1920x1080 (1080p)".
[[nodiscard]] std::string sizeLabel(const SizePreset& preset);
// The index of the preset of exactly that size, or nullopt (the choice shows "Custom").
[[nodiscard]] std::optional<std::size_t> findSizePreset(std::uint32_t width,
                                                        std::uint32_t height) noexcept;

// The frame-rate combo's entries: 23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 100, 119.88, 120.
[[nodiscard]] std::span<const std::string_view> frameRatePresets() noexcept;
// The index of the entry whose parseFpsText() equals `fps`, or nullopt.
[[nodiscard]] std::optional<std::size_t> findFrameRatePreset(dvv::media::Rational fps) noexcept;
// "= 29.970 fps (30000/1001)", "= 25 fps" for an integer rate, "" for nullopt.
[[nodiscard]] std::string frameRateValueLabel(std::optional<dvv::media::Rational> fps);

// The colour model of a catalogue name or of an "@<path>" JSON (only its "model" key is read:
// "gray", "yuv", "rgb"); Yuv for an unknown name or a file that cannot be read.
[[nodiscard]] dvv::media::ColourModel formatModel(std::string_view format);
// dvv::media::resolveColour() for a frame of that format and height with no colour given.
[[nodiscard]] dvv::media::ResolvedColour defaultColour(std::string_view format,
                                                       std::uint32_t height);

// The dialog labels of the field values (§1.5): "BT.601", "BT.709", "BT.2020 NCL",
// "GBR (planes hold G, B, R)", "Custom"; "Limited", "Full"; "sRGB", "BT.1886", "Linear", "PQ",
// "HLG"; "BT.601-525", "BT.601-625", "BT.709", "BT.2020". "" for Unspecified.
[[nodiscard]] std::string_view matrixLabel(dvv::media::ColourMatrix matrix) noexcept;
[[nodiscard]] std::string_view rangeLabel(dvv::media::ColourRange range) noexcept;
[[nodiscard]] std::string_view transferLabel(dvv::media::TransferFn transfer) noexcept;
[[nodiscard]] std::string_view primariesLabel(dvv::media::Primaries primaries) noexcept;
// "Not used (RGB format)", "Not used (gray format)"; "" for Yuv.
[[nodiscard]] std::string_view matrixNotUsedLabel(dvv::media::ColourModel model) noexcept;

// The Colour list (D5R §4.4): the first entry for a base whose file colour matches no standard.
inline constexpr std::string_view NO_STANDARD_LABEL = "From file (no standard)";
// The five standards of the Colour list: BT.601 (NTSC), BT.601 (PAL), BT.709, BT.2020 and BT.2100
// (Bt2100Pq stands for both curves; the curve is a separate option).
[[nodiscard]] std::span<const dvv::media::ColourPreset> standardEntries() noexcept;
// The list entry of a standard (index into standardEntries(); both BT.2100 curves give 4).
[[nodiscard]] std::size_t standardEntry(dvv::media::ColourPreset standard) noexcept;
// The GUI name: "BT.601 (NTSC)", "BT.601 (PAL)", "BT.709", "BT.2020", "BT.2100" (both curves).
[[nodiscard]] std::string_view standardLabel(dvv::media::ColourPreset standard) noexcept;
// The suffix of the entry that applies without a key: " (default)", " (from file)",
// " (from the file name)", " (from <clipName>)", " (from the graph)" or " (auto)".
[[nodiscard]] std::string originSuffix(const ColourBase& base);
// The Colour list's labels: NO_STANDARD_LABEL first for a base without a standard, then the five
// standards, the base's one with originSuffix().
[[nodiscard]] std::vector<std::string> colourListLabels(const ColourBase& base);
// The dialog's read-only values line (D5R §4.4): "Matrix BT.709, primaries BT.709 (not
// converted)"; with `all` ("From file (no standard)") also the range and the curve, e.g. "Matrix
// BT.601, range limited, curve sRGB, primaries BT.709 (not converted)". RGB and gray formats read
// "Matrix not used (RGB format)" and "Matrix not used (gray format)".
[[nodiscard]] std::string colourValuesText(const dvv::media::ColourParams& values,
                                           dvv::media::ColourModel model, bool all);

}  // namespace rawreel
