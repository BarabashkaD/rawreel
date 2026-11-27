#pragma once

// Text of the pixel inspector (pixel inspector design §4.6, §4.7): the Zoom window's header, the
// status bar fields, the copied text and the raw word lines. wx-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"

namespace rawreel {

// The glyph cells of the value font: a letter's cell is its index here.
inline constexpr std::string_view VALUE_GLYPHS = "0123456789YUVRGBA";

// The index of `letter` in VALUE_GLYPHS (shaders take a component's cell from its letter, never
// from its ComponentKind, so a matrix-Rgb YUV layout reads G B R); 0 for a letter not in it.
[[nodiscard]] std::uint32_t letterCell(char letter) noexcept;

// The letter of each component of `desc` in its order: planeLetters for the first three, 'A' for
// an alpha component; '\0' for the unused entries.
[[nodiscard]] std::array<char, 4> componentLetters(const dvv::media::PixelFormatDesc& desc,
                                                   bool has_chroma);

// The values of one pixel as the GPU readback reports them (§4.6).
struct PixelValues {
    dvv::media::ImagePixel pixel;
    std::int64_t frame = -1;  // the frame's index; negative: unknown
    bool live = false;        // the cursor's pixel (Live on) rather than the pinned one
    std::uint8_t count = 0;   // components in use
    std::array<char, 4> letters{};
    std::array<std::uint32_t, 4> codes{};  // each component's code value
    std::array<std::uint32_t, 4> words{};  // each component's raw stored word
    std::array<dvv::media::StoredWord, 4> stored{};
    std::uint8_t bits = 0;  // the bit depth the header names
};

// "one plane" | "packed: Y0 U Y1 V per 2 pixels" | "packed: R G B per pixel" |
// "planar: Y, U, V planes" | "semi-planar: Y + interleaved UV".
[[nodiscard]] std::string storageText(const dvv::media::PixelFormatDesc& desc);

// Four lines: "pinned x 1236 y 565 · frame 42" ("cursor" when live); "Y 630 U 502 V 527 (10-bit)"
// plus "  ⚠ Y padding 45 (must be 0)" per component with padding; "<name> · <storageText>";
// "x 1220–1251  y 549–580 · zoom ×32" plus " · panels Y | U | V" when `planes`.
[[nodiscard]] std::array<std::string, 4> headerLines(const PixelValues& values,
                                                     std::string_view format_name,
                                                     const dvv::media::PixelFormatDesc& desc,
                                                     dvv::media::PixelRange x_shown,
                                                     dvv::media::PixelRange y_shown,
                                                     std::uint32_t zoom, bool planes);

// "pinned 1236 565: Y 630 U 502 V 527" ("cursor" when live); "" without values.
[[nodiscard]] std::string pixelStatusText(const std::optional<PixelValues>& values);

// "⚠ padding ≠ 0: N samples in this frame"; "" for none or 0.
[[nodiscard]] std::string paddingStatusText(std::optional<std::uint64_t> samples);

// "frame 42 x 1236 y 565 Y 630 U 502 V 527" and "raw Y 0x9DAD U 0x7D80 V 0x84C0", two lines.
[[nodiscard]] std::string copyText(const PixelValues& values);

enum class BitKind : std::uint8_t { Value, OtherSample, Padding, PaddingSet };
// Bits [begin, end) of the word, bit 0 the LSB. Value: a sample of this pixel; OtherSample: a
// sample of the word that belongs to another pixel; Padding: padding bits all 0; PaddingSet:
// padding bits with a 1. `own`: the sample of the line's component.
struct BitRun {
    std::size_t begin = 0, end = 0;
    BitKind kind = BitKind::Value;
    bool own = false;
    bool operator==(const BitRun&) const = default;
};
struct RawWordLine {
    std::string text;
    std::vector<BitRun> runs;
    bool operator==(const RawWordLine&) const = default;
};

// One line per distinct stored word, under the first component that has it:
// "Y 0x9DAD  1001 1101 1010 1101  → value 630, padding 45" (no ", padding …" for a format
// without padding).
[[nodiscard]] std::vector<RawWordLine> rawWordLines(const PixelValues& values);

}  // namespace rawreel
