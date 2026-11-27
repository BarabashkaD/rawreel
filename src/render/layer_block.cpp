#include "render/layer_block.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/inspector.h"
#include "dvv/base/error.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"
#include "render/picture_constants.h"
#include "render/view_state.h"

namespace rawreel {

namespace {

// 0xRRGGBB (OverlayParams::cursorColour) as an opaque layer colour.
std::uint32_t opaqueColour(std::uint32_t rgb) noexcept {
    const std::uint32_t r = (rgb >> 16U) & 0xFFU;
    const std::uint32_t g = (rgb >> 8U) & 0xFFU;
    const std::uint32_t b = rgb & 0xFFU;
    return r | (g << 8U) | (b << 16U) | (0xFFU << 24U);
}

}  // namespace

LayerBlock layerBlock(const ViewParams& params, const dvv::media::FrameDesc& desc,
                      std::uint32_t panel, PanelKind kind) {
    LayerBlock block;
    const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(desc.format);
    const std::size_t count = std::min<std::size_t>(format.componentCount, block.words.size());
    block.componentCount = static_cast<std::uint32_t>(count);
    for (std::size_t k = 0; k < count; ++k) {
        const dvv::Result<dvv::media::StoredWordLayout> words =
            dvv::media::storedWordLayout(desc.format, desc.width, desc.height, k);
        if (words) {
            block.words[k] = *words;
        }
        const dvv::media::ComponentDesc& component = format.components[k];
        block.component[k] = component.log2SubX |
                             (std::uint32_t{component.log2SubY} << LAYER_SUB_Y_SHIFT) |
                             (std::uint32_t{component.bits} << LAYER_BITS_SHIFT) |
                             ((params.inspect.letterCells[k] & 0xFFFFU) << LAYER_LETTER_SHIFT);
    }
    switch (kind) {
        case PanelKind::Picture: break;
        case PanelKind::Planes:
            block.flags |= LAYER_PLANE_SOURCE;
            block.sourceComponent = panel;
            break;
    }
    if (valuesOn(params)) {
        block.flags |= LAYER_VALUES;
    }
    // Level 2, the Zoom window's cell flags, always with the inspect layers on; level 1, the
    // padding marks, on a Picture panel while the check is on (both need the Raw view, which the
    // renderer checks).
    if (params.inspect.on) {
        block.flags |= LAYER_CELL_FLAGS;
    }
    if (params.paddingMarks && kind == PanelKind::Picture) {
        block.flags |= LAYER_PADDING_MARKS;
        block.markSize = params.overlay.markSize;
        block.markRim = params.overlay.markRim;
        block.tileSize = std::max(params.overlay.markSize, 2 * (params.overlay.markRim + 1));
    }
    const ValueMetrics& metrics = params.inspect.metrics;
    block.metrics = {metrics.advance, metrics.lineHeight, metrics.padX, metrics.padY};
    block.colours0 = {LAYER_CELL_LINE_COLOUR, LAYER_CHROMA_COLOUR, LAYER_LABEL_TEXT_COLOUR,
                      LAYER_LABEL_BOX_COLOUR};
    block.warning = LAYER_WARNING_COLOUR;
    block.marker = opaqueColour(params.overlay.cursorColour);
    block.markerRim = LAYER_MARKER_RIM_COLOUR;
    if (markersOn(params.markers)) {
        block.flags |= LAYER_MARKERS;
    }
    const MarkerCells cells = markerCells(params.markers, desc, panel, kind);
    if (const std::optional<ImageRect> pin = cells.pin) {
        block.pinRect = {pin->x, pin->y, pin->x + pin->width, pin->y + pin->height};
    }
    if (const std::optional<ImageRect> outline = cells.outline) {
        block.outlineRect = {outline->x, outline->y, outline->x + outline->width,
                             outline->y + outline->height};
    }
    return block;
}

MarkerCells markerCells(const MarkerParams& markers, const dvv::media::FrameDesc& desc,
                        std::uint32_t panel, PanelKind kind) {
    MarkerCells cells = pixelCells(markers);
    const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(desc.format);
    if (!markers.pin || kind != PanelKind::Planes || panel >= format.componentCount) {
        return cells;
    }
    const dvv::media::ComponentDesc& component = format.components.at(panel);
    const dvv::media::PixelBlock block =
        dvv::media::blockOf(component, dvv::media::sampleOf(component, *markers.pin),
                            {.width = desc.width, .height = desc.height});
    if (block.width != 0 && block.height != 0) {
        cells.pin =
            ImageRect{.x = block.x, .y = block.y, .width = block.width, .height = block.height};
    }
    return cells;
}

std::array<std::uint32_t, 3> cellShapes(const ViewParams& params,
                                        const dvv::media::FrameDesc& desc) {
    if (!valuesOn(params)) {
        return {};
    }
    const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(desc.format);
    std::uint32_t sub_x = 0;
    std::uint32_t sub_y = 0;
    for (std::size_t k = 0; k < std::min<std::size_t>(format.componentCount, 4); ++k) {
        sub_x = std::max<std::uint32_t>(sub_x, format.components[k].log2SubX);
        sub_y = std::max<std::uint32_t>(sub_y, format.components[k].log2SubY);
    }
    const auto chroma = [](std::uint32_t sub) {
        return sub == 0 ? 0U : sub | (2U << LEVEL_THICKNESS_SHIFT) | LEVEL_DRAWN;
    };
    return {chroma(sub_x), chroma(sub_y), (1U << LEVEL_THICKNESS_SHIFT) | LEVEL_DRAWN};
}

bool rawReadsFit(const dvv::media::FrameDesc& raw, std::uint64_t raw_bytes,
                 const dvv::media::FrameDesc& desc, std::uint32_t display_width,
                 std::uint32_t display_height) {
    if (raw.format != desc.format || raw.width != desc.width || raw.height != desc.height ||
        display_width != desc.width || display_height != desc.height || desc.width == 0 ||
        desc.height == 0) {
        return false;
    }
    const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(desc.format);
    if (format.componentCount == 0) {
        return false;
    }
    for (std::size_t k = 0; k < std::min<std::size_t>(format.componentCount, 4); ++k) {
        const dvv::media::ComponentDesc& component = format.components.at(k);
        const dvv::media::SamplePosition last{
            .x = ((desc.width + (1U << component.log2SubX) - 1U) >> component.log2SubX) - 1U,
            .y = ((desc.height + (1U << component.log2SubY) - 1U) >> component.log2SubY) - 1U};
        const dvv::Result<dvv::media::StoredWord> word =
            dvv::media::storedWord(desc.format, desc.width, desc.height, k, last);
        if (!word || (word->byteOffset & ~std::uint64_t{3}) + 8 > raw_bytes) {
            return false;
        }
    }
    return true;
}

}  // namespace rawreel
