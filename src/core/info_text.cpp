#include "core/info_text.h"

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "core/format_choice.h"
#include "core/format_presets.h"
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
using dvv::media::Primaries;
using dvv::media::TransferFn;

// The names of rawreel-cli (M2).
std::string matrixName(const dvv::media::ColourParams& colour) {
    switch (colour.matrix) {
        case ColourMatrix::Unspecified: return "unspecified";
        case ColourMatrix::Bt601: return "bt601";
        case ColourMatrix::Bt709: return "bt709";
        case ColourMatrix::Bt2020Ncl: return "bt2020";
        case ColourMatrix::Rgb: return "rgb";
        case ColourMatrix::Custom:
            return std::format("custom kr={:.4f} kb={:.4f}", colour.kr, colour.kb);
    }
    return "unspecified";
}

std::string_view rangeName(ColourRange range) noexcept {
    switch (range) {
        case ColourRange::Unspecified: return "unspecified";
        case ColourRange::Limited: return "limited";
        case ColourRange::Full: return "full";
    }
    return "unspecified";
}

std::string_view transferName(TransferFn transfer) noexcept {
    switch (transfer) {
        case TransferFn::Unspecified: return "unspecified";
        case TransferFn::Srgb: return "srgb";
        case TransferFn::Bt1886: return "bt1886";
        case TransferFn::Pq: return "pq";
        case TransferFn::Hlg: return "hlg";
        case TransferFn::Linear: return "linear";
    }
    return "unspecified";
}

std::string_view primariesName(Primaries primaries) noexcept {
    switch (primaries) {
        case Primaries::Unspecified: return "unspecified";
        case Primaries::Bt601_525: return "bt601-525";
        case Primaries::Bt601_625: return "bt601-625";
        case Primaries::Bt709: return "bt709";
        case Primaries::Bt2020: return "bt2020";
    }
    return "unspecified";
}

std::string fpsText(dvv::media::Rational fps) {
    if (fps.den == 1) {
        return std::format("{}", fps.num);
    }
    return std::format("{:.3f}", static_cast<double>(fps.num) / static_cast<double>(fps.den));
}

// D5R §4.5: the two colour lines of a stream, and the size-mismatch hint.
std::string colourLines(const StreamInfo& stream) {
    const dvv::media::ColourParams& colour = stream.colour.params;
    const dvv::media::ColourModel model = dvv::media::descriptor(stream.info.desc.format).model;
    dvv::media::ColourSpec keys{};
    if (const auto uri = dvv::parseUri(stream.resolvedUri)) {
        if (auto spec = dvv::media::colourSpecFromQuery(*uri)) {
            keys = *spec;
        }
    }
    const bool lavfi = stream.resolvedUri.starts_with("lavfi:");
    const std::string_view file_tag = lavfi ? " (from the graph)" : " (from file)";
    // Line 1: the standard and where it comes from (no tag for the URI's colour=).
    std::string name;
    std::string standard_tag;
    bool standard_from_file = false;
    const std::optional<dvv::media::ColourPreset> standard =
        keys.preset ? keys.preset : stream.colour.standard;
    if (keys.preset) {
        name = dvv::media::colourPresetInfo(*keys.preset).label;
    } else {
        const ColourBase base = streamColourBase(stream);
        name = base.standard ? std::string(dvv::media::colourPresetInfo(*base.standard).label)
                             : std::string("no standard");
        standard_tag = originSuffix(base);
        standard_from_file = base.origin != ColourOrigin::Default;
    }
    // Line 2: the range is tagged only when its origin differs from line 1's.
    std::string_view range_tag;
    if (keys.range == ColourRange::Unspecified) {
        range_tag =
            (stream.colour.assumed & dvv::media::AssumedRange) != 0 ? " (default)" : file_tag;
        // A clip description's range comes from the same file as its standard.
        const bool clip = standard_tag.starts_with(" (from ") && standard_tag != file_tag &&
                          standard_tag != " (from the file name)";
        if (range_tag == standard_tag || (clip && range_tag == file_tag)) {
            range_tag = {};
        }
    }
    std::string text = std::format("  colour: {}{}\n", name, standard_tag);
    text += std::format(
        "  matrix {}, range {}{}, curve {}, primaries {} (not converted)",
        model == dvv::media::ColourModel::Yuv ? matrixName(colour) : std::string("not used"),
        rangeName(colour.range), range_tag, transferName(colour.transfer),
        primariesName(colour.primaries));
    if (const std::string_view hint =
            mismatchHint(standard, standard_from_file, stream.info.desc.height);
        !hint.empty()) {
        text += std::format("\n  hint: {}", hint);
    }
    return text;
}

}  // namespace

std::string formatInfo(std::span<const StreamInfo> streams) {
    if (streams.empty()) {
        return "No stream. Open a file (Ctrl+O) or a URI (Ctrl+U), or drop a file here.";
    }
    std::string text;
    for (const StreamInfo& stream : streams) {
        if (!text.empty()) {
            text += '\n';
        }
        const dvv::media::SourceInfo& info = stream.info;
        const dvv::media::ColourParams& colour = stream.colour.params;
        text += std::format("Stream {}: {}\n", stream.slot + 1, stream.resolvedUri);
        text +=
            std::format("  {}, {}x{}, {} fps, {}\n", dvv::media::descriptor(info.desc.format).name,
                        info.desc.width, info.desc.height, fpsText(info.fps),
                        info.frameCount ? std::format("{} frames", *info.frameCount)
                                        : std::string("unbounded"));
        text += colourLines(stream);
        if (colour.transfer == TransferFn::Pq || colour.transfer == TransferFn::Hlg) {
            text += "\n  tone-mapped (BT.2390, 1000 → 100 nits)";  // D4
        }
        if (!info.clipFile.empty()) {  // D5 design §5: clip=@ or the sidecar, by its file name
            const std::size_t slash = info.clipFile.find_last_of("/\\");
            text += "\n  clip description: " +
                    (slash == std::string::npos ? info.clipFile : info.clipFile.substr(slash + 1));
        }
        if (!info.reader.empty()) {
            text += "\n  reads: " + info.reader;  // the file read mode
        }
        for (const std::string& warning : info.warnings) {
            text += "\n  warning: " + warning;
        }
    }
    return text;
}

}  // namespace rawreel
