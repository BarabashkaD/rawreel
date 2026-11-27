#include "core/inspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "core/overlay.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel {

namespace {

using dvv::media::ComponentDesc;
using dvv::media::ImagePixel;
using dvv::media::PixelBlock;
using dvv::media::SamplePosition;

// The index of the most subsampled component (the first of equals); 0 without components. A
// format's subsampled components share one grid, so the sum of the two shifts finds it.
std::size_t mostSubsampled(const dvv::media::PixelFormatDesc& desc) noexcept {
    std::size_t best = 0;
    unsigned best_sub = 0;
    for (std::size_t c = 0; c < desc.componentCount; ++c) {
        const unsigned sub = desc.components[c].log2SubX + desc.components[c].log2SubY;
        if (sub > best_sub) {
            best = c;
            best_sub = sub;
        }
    }
    return best;
}

std::uint32_t clampMove(std::uint32_t value, int delta, std::uint32_t size) noexcept {
    const std::int64_t last = static_cast<std::int64_t>(size) - 1;
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(static_cast<std::int64_t>(value) + delta, 0, last));
}

std::uint64_t ceilDiv(std::uint64_t value, std::uint64_t divisor) noexcept {
    return (value + divisor - 1) / divisor;
}

std::uint32_t clampZoom(std::uint64_t zoom) noexcept {
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(zoom, 1, WHOLE_ZOOM_MAX));
}

// The digits of 2^bits - 1.
std::uint32_t digitsOf(std::uint8_t bits) noexcept {
    std::uint32_t max = bits >= 32 ? 0xFFFFFFFFU : (1U << bits) - 1U;
    std::uint32_t digits = 1;
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return digits;
}

}  // namespace

PixelBlock chromaBlockAt(const ClipGrid& clip, ImagePixel pixel) noexcept {
    const auto& desc = dvv::media::formatDesc(clip.format);
    if (desc.componentCount == 0) {
        return {.x = pixel.x, .y = pixel.y, .width = 1, .height = 1};
    }
    const ComponentDesc& component = desc.components[mostSubsampled(desc)];
    if (component.log2SubX == 0 && component.log2SubY == 0) {
        return {.x = pixel.x, .y = pixel.y, .width = 1, .height = 1};
    }
    return dvv::media::blockOf(component, dvv::media::sampleOf(component, pixel), clip.image);
}

std::optional<ImagePixel> Inspector::shown() const noexcept {
    return live_ && probe_.has_value() ? probe_ : pin_;
}

bool Inspector::movePin(int dx, int dy, bool block, const ClipGrid& clip) {
    if (!pin_.has_value()) {
        return false;
    }
    if (clip.image.empty()) {
        return true;
    }
    const ImagePixel pin = *pin_;
    const auto& desc = dvv::media::formatDesc(clip.format);
    if (!block || desc.componentCount == 0) {
        pin_ = ImagePixel{.x = clampMove(pin.x, dx, clip.image.width),
                          .y = clampMove(pin.y, dy, clip.image.height)};
        return true;
    }
    const ComponentDesc& component = desc.components[mostSubsampled(desc)];
    const SamplePosition sample = dvv::media::sampleOf(component, pin);
    const PixelBlock old_block = dvv::media::blockOf(component, sample, clip.image);
    const auto sample_width =
        static_cast<std::uint32_t>(ceilDiv(clip.image.width, 1ULL << component.log2SubX));
    const auto sample_height =
        static_cast<std::uint32_t>(ceilDiv(clip.image.height, 1ULL << component.log2SubY));
    const SamplePosition moved{.x = clampMove(sample.x, dx, sample_width),
                               .y = clampMove(sample.y, dy, sample_height)};
    const PixelBlock new_block = dvv::media::blockOf(component, moved, clip.image);
    if (new_block.width == 0 || new_block.height == 0) {
        return true;
    }
    const std::uint32_t offset_x = std::min(pin.x - old_block.x, new_block.width - 1);
    const std::uint32_t offset_y = std::min(pin.y - old_block.y, new_block.height - 1);
    pin_ = ImagePixel{.x = new_block.x + offset_x, .y = new_block.y + offset_y};
    return true;
}

void Inspector::clipChanged(dvv::platform::PixelSize image) {
    const auto inside = [&image](const std::optional<ImagePixel>& pixel) {
        return pixel.has_value() && pixel->x < image.width && pixel->y < image.height;
    };
    if (!inside(pin_)) {
        pin_.reset();
    }
    if (!inside(probe_)) {
        probe_.reset();
    }
}

std::uint32_t wholeFitZoom(std::span<const dvv::platform::PixelSize> pictures,
                           dvv::platform::PixelSize image) noexcept {
    if (pictures.empty() || image.empty()) {
        return 1;
    }
    std::uint64_t fit = WHOLE_ZOOM_MAX;
    for (const dvv::platform::PixelSize picture : pictures) {
        fit = std::min({fit, std::uint64_t{picture.width} / image.width,
                        std::uint64_t{picture.height} / image.height});
    }
    return clampZoom(fit);
}

std::uint32_t minZoom(std::span<const dvv::platform::PixelSize> pictures, std::uint32_t major_step,
                      dvv::platform::PixelSize image) noexcept {
    const std::uint64_t cells =
        static_cast<std::uint64_t>(MIN_ZOOM_CELLS) * std::max<std::uint32_t>(major_step, 1);
    std::uint32_t low = 1;
    for (const dvv::platform::PixelSize picture : pictures) {
        low = std::max(low, clampZoom(ceilDiv(std::max(picture.width, picture.height), cells)));
    }
    if (!image.empty() && image.width <= cells && image.height <= cells) {
        low = std::min(low, wholeFitZoom(pictures, image));  // a small clip shows whole
    }
    return low;
}

std::uint32_t wholeZoom(double zoom, std::uint32_t minimum) noexcept {
    const std::uint32_t low = clampZoom(minimum);
    if (std::isnan(zoom)) {
        return low;
    }
    return static_cast<std::uint32_t>(std::clamp(std::round(zoom), static_cast<double>(low),
                                                 static_cast<double>(WHOLE_ZOOM_MAX)));
}

std::optional<std::uint32_t> nextWholeZoom(std::uint32_t zoom, bool in,
                                           std::uint32_t minimum) noexcept {
    const std::uint32_t low = clampZoom(minimum);
    if (in) {
        const auto it = std::ranges::upper_bound(WHOLE_ZOOM_STEPS, zoom);
        return it == WHOLE_ZOOM_STEPS.end() ? std::nullopt
                                            : std::optional<std::uint32_t>{std::max(*it, low)};
    }
    if (zoom <= low) {
        return std::nullopt;
    }
    const auto it = std::ranges::lower_bound(WHOLE_ZOOM_STEPS, zoom);
    return it == WHOLE_ZOOM_STEPS.begin() ? low : std::max(*(it - 1), low);
}

dvv::media::PointD followCentre(ImagePixel pin) noexcept {
    return {.x = static_cast<double>(pin.x) + 0.5, .y = static_cast<double>(pin.y) + 0.5};
}

std::optional<CellView> cellView(ImagePixel pin, const GridConfig& grid, std::uint32_t zoom,
                                 dvv::platform::PixelSize picture,
                                 dvv::platform::PixelSize image) noexcept {
    const std::uint64_t room = std::min(picture.width, picture.height);
    for (std::size_t level = 0; level < grid.levels.size(); ++level) {
        const std::uint32_t step = grid.levels[level].step;
        if (step == 0 || static_cast<std::uint64_t>(step) * zoom > room) {
            continue;
        }
        const std::uint32_t x = pin.x / step * step;
        const std::uint32_t y = pin.y / step * step;
        const std::uint32_t right = std::min(x + step, std::max(image.width, x + 1));
        const std::uint32_t bottom = std::min(y + step, std::max(image.height, y + 1));
        return CellView{.level = level,
                        .centre = {.x = (static_cast<double>(x) + right) / 2.0,
                                   .y = (static_cast<double>(y) + bottom) / 2.0}};
    }
    return std::nullopt;
}

std::uint32_t numbersFitZoom(const ValueMetrics& m, InspectorLayout layout,
                             const ClipGrid& clip) noexcept {
    return numbersFitZoom(m, layout, dvv::media::formatDesc(clip.format));
}

std::uint32_t numbersFitZoom(const ValueMetrics& m, InspectorLayout layout,
                             const dvv::media::PixelFormatDesc& desc) noexcept {
    const std::uint64_t pad_x = 2ULL * m.padX;
    const std::uint64_t pad_y = 2ULL * m.padY;
    std::uint64_t need = 1;
    if (layout == InspectorLayout::Planes) {
        for (std::size_t c = 0; c < desc.componentCount; ++c) {
            const ComponentDesc& component = desc.components[c];
            need =
                std::max(need, ceilDiv(std::uint64_t{digitsOf(component.bits)} * m.advance + pad_x,
                                       1ULL << component.log2SubX));
            need = std::max(need, ceilDiv(m.lineHeight + pad_y, 1ULL << component.log2SubY));
        }
        return clampZoom(need);
    }
    std::uint64_t cells = 0, max_digits = 0, label_width = 0, label_count = 0;
    std::uint64_t block_width = 1;
    for (std::size_t c = 0; c < desc.componentCount; ++c) {
        const ComponentDesc& component = desc.components[c];
        const std::uint64_t digits = digitsOf(component.bits);
        if (component.log2SubX == 0 && component.log2SubY == 0) {
            ++cells;
            max_digits = std::max(max_digits, digits);
        } else {
            label_width += 2 + digits;
            ++label_count;
            block_width = std::max<std::uint64_t>(block_width, 1ULL << component.log2SubX);
        }
    }
    // A cell's lines in its padded box; a block's bottom row also holds the chroma label, its line
    // and padY above the cell's bottom pad (the shader's bottom-row shrink).
    const std::uint64_t height =
        cells * m.lineHeight + pad_y + (label_count != 0 ? m.lineHeight + m.padY : 0);
    need = std::max(need, height);
    if (cells != 0) {
        need = std::max(need, (cells == 1 ? max_digits : 2 + max_digits) * m.advance + pad_x);
    }
    if (label_count != 0) {
        need = std::max(need,
                        ceilDiv((label_width + label_count - 1) * m.advance + pad_x, block_width));
    }
    return clampZoom(need);
}

std::uint32_t fitCellZoom(const GridConfig& grid, dvv::platform::PixelSize picture) noexcept {
    const std::uint32_t step = grid.levels[0].step;
    if (step == 0) {
        return 1;
    }
    return clampZoom(std::min(picture.width, picture.height) / step);
}

bool recentres(InspectorEvent event) noexcept {
    switch (event) {
        case InspectorEvent::FirstPin:
        case InspectorEvent::PinMoved:
        case InspectorEvent::ProbeMoved:
        case InspectorEvent::ModeChanged:
        case InspectorEvent::Centre: return true;
        case InspectorEvent::Other: return false;
    }
    return false;
}

}  // namespace rawreel
