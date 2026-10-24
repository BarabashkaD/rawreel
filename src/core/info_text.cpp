#include "core/info_text.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/types.h"

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

std::string_view assumed(std::uint8_t bits, std::uint8_t bit) noexcept {
    return (bits & bit) != 0 ? " (assumed)" : "";
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
        const std::uint8_t bits = stream.colour.assumed;
        text += std::format("Stream {}: {}\n", stream.slot + 1, stream.resolvedUri);
        text +=
            std::format("  {}, {}x{}, {} fps, {}\n", dvv::media::descriptor(info.desc.format).name,
                        info.desc.width, info.desc.height, fpsText(info.fps),
                        info.frameCount ? std::format("{} frames", *info.frameCount)
                                        : std::string("unbounded"));
        text += std::format(
            "  colour: matrix {}{}, range {}{}, transfer {}{}, primaries {}{}", matrixName(colour),
            assumed(bits, dvv::media::AssumedMatrix), rangeName(colour.range),
            assumed(bits, dvv::media::AssumedRange), transferName(colour.transfer),
            assumed(bits, dvv::media::AssumedTransfer), primariesName(colour.primaries),
            assumed(bits, dvv::media::AssumedPrimaries));
        if (colour.transfer == TransferFn::Pq || colour.transfer == TransferFn::Hlg) {
            text += "\n  tone-mapped (BT.2390, 1000 → 100 nits)";  // D4
        }
        for (const std::string& warning : info.warnings) {
            text += "\n  warning: " + warning;
        }
    }
    return text;
}

}  // namespace rawreel
