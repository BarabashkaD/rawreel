#include "core/inspector_text.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/commands.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view_lines.h"

namespace rawreel {

namespace {

using dvv::media::ComponentKind;
using dvv::media::PixelFormatDesc;

std::string valuesText(const PixelValues& values) {
    std::string text;
    for (std::size_t i = 0; i < values.count; ++i) {
        if (i != 0) {
            text += ' ';
        }
        text += std::format("{} {}", values.letters[i], values.codes[i]);
    }
    return text;
}

std::string hexWord(std::uint32_t word, std::uint8_t word_bytes) {
    return std::format("0x{:0{}X}", word, static_cast<std::size_t>(word_bytes) * 2);
}

// The padding field as a number: masked and shifted down to bit 0 (0 without a mask).
std::uint32_t paddingValue(std::uint32_t word, std::uint32_t mask) {
    return mask == 0 ? 0 : (word & mask) >> static_cast<unsigned>(std::countr_zero(mask));
}

std::uint32_t paddingOf(const PixelValues& values, std::size_t i) {
    return paddingValue(values.words[i], values.stored[i].paddingMask);
}

std::string pinText(const PixelValues& values) {
    return std::format("{} x {} y {}", values.live ? "cursor" : "pinned", values.pixel.x,
                       values.pixel.y);
}

std::string lettersJoined(const PixelFormatDesc& desc, std::string_view separator) {
    std::string text;
    for (std::size_t i = 0; i < desc.componentCount; ++i) {
        if (i != 0) {
            text += separator;
        }
        text += componentLetters(desc, true)[i];
    }
    return text;
}

}  // namespace

std::uint32_t letterCell(char letter) noexcept {
    const std::size_t at = VALUE_GLYPHS.find(letter);
    return at == std::string_view::npos ? 0U : static_cast<std::uint32_t>(at);
}

std::array<char, 4> componentLetters(const PixelFormatDesc& desc, bool has_chroma) {
    const std::array<char, 3> colour = planeLetters(desc.model, has_chroma);
    std::array<char, 4> letters{};
    std::size_t next = 0;
    for (std::size_t i = 0; i < desc.componentCount && i < letters.size(); ++i) {
        if (desc.components[i].kind == ComponentKind::A) {
            letters[i] = 'A';
        } else {
            letters[i] = colour[std::min<std::size_t>(next, colour.size() - 1)];
            ++next;
        }
    }
    return letters;
}

std::string storageText(const PixelFormatDesc& desc) {
    if (desc.planeCount <= 1 && desc.componentCount <= 1) {
        return "one plane";
    }
    if (desc.planeCount <= 1) {
        const std::uint8_t pixels = desc.planes[0].groupPixels;
        if (pixels == 2 && desc.model == dvv::media::ColourModel::Yuv) {
            return "packed: Y0 U Y1 V per 2 pixels";
        }
        if (pixels <= 1) {
            return "packed: " + lettersJoined(desc, " ") + " per pixel";
        }
        return std::format("packed: {} per {} pixels", lettersJoined(desc, " "), pixels);
    }
    if (desc.planeCount < desc.componentCount) {
        const std::string first = lettersJoined(desc, "");
        return std::format("semi-planar: {} + interleaved {}", first.substr(0, 1), first.substr(1));
    }
    return "planar: " + lettersJoined(desc, ", ") + " planes";
}

std::array<std::string, 4> headerLines(const PixelValues& values, std::string_view format_name,
                                       const PixelFormatDesc& desc, dvv::media::PixelRange x_shown,
                                       dvv::media::PixelRange y_shown, std::uint32_t zoom,
                                       bool planes) {
    std::array<std::string, 4> lines;
    lines[0] = pinText(values);
    if (values.frame >= 0) {
        lines[0] += std::format(" · frame {}", values.frame);
    }
    lines[1] = std::format("{} ({}-bit)", valuesText(values), values.bits);
    for (std::size_t i = 0; i < values.count; ++i) {
        if (const std::uint32_t padding = paddingOf(values, i); padding != 0) {
            lines[1] += std::format("  ⚠ {} padding {} (must be 0)", values.letters[i], padding);
        }
    }
    lines[2] = std::format("{} · {}", format_name, storageText(desc));
    lines[3] = std::format("x {}–{}  y {}–{} · zoom ×{}", x_shown.first, x_shown.last,
                           y_shown.first, y_shown.last, zoom);
    if (planes) {
        lines[3] += " · panels ";
        for (std::size_t i = 0; i < values.count; ++i) {
            lines[3] += i == 0 ? std::string(1, values.letters[i])
                               : std::format(" | {}", values.letters[i]);
        }
    }
    return lines;
}

std::string pixelStatusText(const std::optional<PixelValues>& values) {
    if (!values) {
        return {};
    }
    return std::format("{} {} {}: {}", values->live ? "cursor" : "pinned", values->pixel.x,
                       values->pixel.y, valuesText(*values));
}

std::string paddingStatusText(std::optional<std::uint64_t> samples) {
    if (!samples || *samples == 0) {
        return {};
    }
    return std::format("⚠ padding ≠ 0: {} samples in this frame", *samples);
}

std::string copyText(const PixelValues& values) {
    std::string text;
    if (values.frame >= 0) {
        text += std::format("frame {} ", values.frame);
    }
    text += std::format("x {} y {} {}\nraw", values.pixel.x, values.pixel.y, valuesText(values));
    for (std::size_t i = 0; i < values.count; ++i) {
        text += std::format(" {} {}", values.letters[i],
                            hexWord(values.words[i], values.stored[i].wordBytes));
    }
    return text;
}

std::vector<RawWordLine> rawWordLines(const PixelValues& values) {
    std::vector<RawWordLine> lines;
    for (std::size_t i = 0; i < values.count; ++i) {
        const dvv::media::StoredWord& word = values.stored[i];
        const auto same_word = [&](std::size_t j) {
            return values.stored[j].plane == word.plane &&
                   values.stored[j].byteOffset == word.byteOffset;
        };
        bool listed = false;
        for (std::size_t j = 0; j < i; ++j) {
            listed = listed || same_word(j);
        }
        if (listed) {
            continue;  // a word holding several samples is listed under its first component
        }
        const std::size_t width = static_cast<std::size_t>(word.wordBytes) * 8;
        const std::uint32_t raw = values.words[i];
        RawWordLine line;
        line.text = std::format("{} {}  ", values.letters[i], hexWord(raw, word.wordBytes));
        for (std::size_t bit = width; bit-- > 0;) {
            line.text += ((raw >> bit) & 1U) != 0 ? '1' : '0';
            if (bit != 0 && bit % 4 == 0) {
                line.text += ' ';
            }
        }
        line.text += std::format("  → value {}", values.codes[i]);
        if (word.paddingMask != 0) {
            line.text += std::format(", padding {}", paddingValue(raw, word.paddingMask));
        }
        // The samples of the word: this pixel's (a component of the same word) and the others'.
        for (std::size_t s = 0; s < word.sharedCount; ++s) {
            const std::uint8_t shift = word.sharedShifts[s];
            std::uint8_t bits = word.bits;
            BitKind kind = BitKind::OtherSample;
            bool own = false;
            for (std::size_t j = 0; j < values.count; ++j) {
                if (same_word(j) && values.stored[j].shift == shift) {
                    kind = BitKind::Value;
                    bits = values.stored[j].bits;
                    own = j == i;
                }
            }
            line.runs.push_back({.begin = shift,
                                 .end = static_cast<std::size_t>(shift) + bits,
                                 .kind = kind,
                                 .own = own});
        }
        for (std::size_t bit = 0; bit < width;) {
            if (((word.paddingMask >> bit) & 1U) == 0) {
                ++bit;
                continue;
            }
            const std::size_t begin = bit;
            bool set = false;
            for (; bit < width && ((word.paddingMask >> bit) & 1U) != 0; ++bit) {
                set = set || ((raw >> bit) & 1U) != 0;
            }
            line.runs.push_back(
                {.begin = begin, .end = bit, .kind = set ? BitKind::PaddingSet : BitKind::Padding});
        }
        std::ranges::sort(line.runs, {}, &BitRun::begin);
        lines.push_back(std::move(line));
    }
    return lines;
}

}  // namespace rawreel
