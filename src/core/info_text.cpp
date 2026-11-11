#include "core/info_text.h"

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/format_choice.h"
#include "dvv/base/uri.h"
#include "dvv/media/colour_preset.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"
#include "dvv/media/uri_colour.h"

namespace rawreel {

namespace {

using dvv::media::ColourMatrix;
using dvv::media::ColourRange;
using dvv::media::TransferFn;

// The effective standard: the URI's colour= preset, else the stream's own (streamColourBase).
struct StandardChoice {
    dvv::media::ColourSpec keys{};  // the URI's colour keys
    ColourBase base{};              // streamColourBase(); not in use when keys.preset is set
    std::optional<dvv::media::ColourPreset> preset;  // nullopt: no standard matches
};

StandardChoice standardChoice(const StreamInfo& stream) {
    StandardChoice choice;
    if (const auto uri = dvv::parseUri(stream.resolvedUri)) {
        if (auto spec = dvv::media::colourSpecFromQuery(*uri)) {
            choice.keys = *spec;
        }
    }
    if (choice.keys.preset) {
        choice.preset = choice.keys.preset;
    } else {
        choice.base = streamColourBase(stream);
        choice.preset = choice.base.standard;
    }
    return choice;
}

// ---- The Info window (spec §11, §13). "\xC3\x97" is U+00D7, "\xE2\x80\x93" U+2013, "\xE2\x86\x92"
// U+2192 and "\xC2\xB7" U+00B7 in UTF-8.

std::string rateText(dvv::media::Rational fps) {
    if (fps.den <= 0) {
        return "Unspecified";
    }
    if (fps.num % fps.den == 0) {
        return std::format("{} fps", fps.num / fps.den);
    }
    std::string text =
        std::format("{:.3f}", static_cast<double>(fps.num) / static_cast<double>(fps.den));
    while (text.ends_with('0')) {
        text.pop_back();
    }
    if (text.ends_with('.')) {
        text.pop_back();
    }
    return text + " fps";
}

std::string formatText(dvv::media::FormatId format) {
    const dvv::media::PixelFormatDescriptor& desc = dvv::media::descriptor(format);
    const std::string_view model = desc.model == dvv::media::ColourModel::Yuv   ? "YUV"
                                   : desc.model == dvv::media::ColourModel::Rgb ? "RGB"
                                                                                : "Gray";
    std::string chroma;
    bool has_alpha = false;
    int depth = -1;
    bool same_depth = true;
    for (std::size_t i = 0; i < desc.componentCount; ++i) {
        const dvv::media::ComponentDesc& component = desc.components[i];
        if (component.kind == dvv::media::ComponentKind::A) {
            has_alpha = true;
            continue;
        }
        if (depth >= 0 && depth != component.bits) {
            same_depth = false;
        }
        depth = component.bits;
        if (component.kind == dvv::media::ComponentKind::U) {
            if (component.log2SubX == 0 && component.log2SubY == 0) {
                chroma = " 4:4:4";
            } else if (component.log2SubX == 1 && component.log2SubY == 0) {
                chroma = " 4:2:2";
            } else if (component.log2SubX == 1 && component.log2SubY == 1) {
                chroma = " 4:2:0";
            } else {
                return desc.name;  // any other subsampling: the name alone
            }
        }
    }
    if (!same_depth || depth < 0) {
        return desc.name;
    }
    return std::format("{}{} {}-bit{} ({})", model, chroma, depth, has_alpha ? " + alpha" : "",
                       desc.name);
}

std::string_view matrixText(const dvv::media::ColourParams& colour,
                            dvv::media::ColourModel model) noexcept {
    if (model != dvv::media::ColourModel::Yuv) {
        return "Not used";
    }
    switch (colour.matrix) {
        case ColourMatrix::Unspecified: return "Unspecified";
        case ColourMatrix::Bt601: return "BT.601";
        case ColourMatrix::Bt709: return "BT.709";
        case ColourMatrix::Bt2020Ncl: return "BT.2020";
        case ColourMatrix::Rgb: return "Identity";
        case ColourMatrix::Custom: return {};  // formatted by the caller (kr and kb)
    }
    return "Unspecified";
}

std::string rangeText(const dvv::media::ColourParams& colour,
                      const dvv::media::PixelFormatDescriptor& desc) {
    if (colour.range == ColourRange::Unspecified) {
        return "Unspecified";
    }
    // The Y component's depth for YUV and gray, the first component's for RGB: component 0.
    const unsigned bits = desc.componentCount > 0 ? desc.components[0].bits : 8U;
    if (colour.range == ColourRange::Full) {
        return std::format("Full (0\xE2\x80\x93{})", (1U << bits) - 1U);
    }
    return std::format("Limited ({}\xE2\x80\x93{})", (16U << bits) / 256U, (235U << bits) / 256U);
}

std::string_view transferText(TransferFn transfer) noexcept {
    switch (transfer) {
        case TransferFn::Unspecified: return "Unspecified";
        case TransferFn::Srgb: return "sRGB";
        case TransferFn::Bt1886: return "BT.1886";
        case TransferFn::Pq: return "PQ";
        case TransferFn::Hlg: return "HLG";
        case TransferFn::Linear: return "Linear";
    }
    return "Unspecified";
}

std::string standardText(const StreamInfo& stream) {
    const StandardChoice choice = standardChoice(stream);
    return choice.preset ? std::string(dvv::media::colourPresetInfo(*choice.preset).label)
                         : std::string("None");
}

constexpr std::string_view TONE_MAPPING = "BT.2390, 1000 \xE2\x86\x92 100 nits";

constexpr std::array<std::string_view, 3> VALUE_SAMPLES = {
    TONE_MAPPING, "YUV 4:2:2 10-bit (yuv422p10le)", "Custom (Kr 0.2990, Kb 0.1140)"};

std::string resolutionText(const StreamInfo& stream) {
    return std::format("{} \xC3\x97 {}", stream.info.desc.width, stream.info.desc.height);
}

}  // namespace

std::vector<InfoRow> infoRows(const StreamInfo& stream) {
    const dvv::media::ColourParams& colour = stream.colour.params;
    const dvv::media::PixelFormatDescriptor& desc = dvv::media::descriptor(stream.info.desc.format);
    std::string matrix(matrixText(colour, desc.model));
    if (matrix.empty()) {  // ColourMatrix::Custom
        matrix = std::format("Custom (Kr {:.4f}, Kb {:.4f})", colour.kr, colour.kb);
    }
    const bool tone_mapped =
        colour.transfer == TransferFn::Pq || colour.transfer == TransferFn::Hlg;
    return {
        {"Resolution", resolutionText(stream)},
        {"Frame rate", rateText(stream.info.fps)},
        {"Format", formatText(stream.info.desc.format)},
        {"Standard", standardText(stream)},
        {"Matrix", std::move(matrix)},
        {"Range", rangeText(colour, desc)},
        {"Transfer", std::string(transferText(colour.transfer))},
        {"Tone mapping", tone_mapped ? std::string(TONE_MAPPING) : std::string("None (SDR)")},
    };
}

std::string infoHint(const StreamInfo& stream) {
    std::string text =
        std::format("{} \xC2\xB7 {} \xC2\xB7 {}", resolutionText(stream), rateText(stream.info.fps),
                    dvv::media::descriptor(stream.info.desc.format).name);
    std::string tail = standardText(stream);
    if (tail == "None") {
        tail.clear();
    }
    const ColourRange range = stream.colour.params.range;
    const std::string_view range_word = range == ColourRange::Limited ? "limited"
                                        : range == ColourRange::Full  ? "full"
                                                                      : "";
    if (!tail.empty() && !range_word.empty()) {
        tail += ' ';
    }
    tail += range_word;
    if (!tail.empty()) {
        text += " \xC2\xB7 " + tail;
    }
    return text;
}

std::string infoCopyText(const StreamInfo* stream) {
    if (stream == nullptr) {
        return "No stream";
    }
    std::string text;
    for (const InfoRow& row : infoRows(*stream)) {
        if (!text.empty()) {
            text += '\n';
        }
        text += std::format("{}: {}", row.label, row.value);
    }
    return text;
}

std::vector<std::string> infoValueSamples() {
    std::vector<std::string> samples(VALUE_SAMPLES.begin(), VALUE_SAMPLES.end());
    for (const dvv::media::FormatId format : dvv::media::catalogue()) {
        samples.push_back(formatText(format));
    }
    return samples;
}

}  // namespace rawreel
