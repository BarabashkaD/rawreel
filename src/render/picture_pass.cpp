#include "render/picture_pass.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "core/frame_set.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/glyph_strip_vulkan.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/resources.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "rawreel_shaders/padding_check.h"
#include "rawreel_shaders/picture.h"
#include "rawreel_shaders/picture_inspect.h"
#include "rawreel_shaders/picture_lines.h"
#include "rawreel_shaders/picture_markers.h"
#include "render/pass_regions.h"
#include "render/picture_constants.h"
#include "render/view_state.h"

namespace rawreel {

static_assert(dvv::media::DISPLAY_IMAGE_FORMAT == VK_FORMAT_R16G16B16A16_SFLOAT,
              "shaders/picture.slang declares the display binding as rgba16f");

bool gridDrawn(const PictureConstants& constants) noexcept {
    return ((constants.levelShape[0] | constants.levelShape[1] | constants.levelShape[2]) &
            LEVEL_DRAWN) != 0;
}

bool cellLinesDrawn(const PictureConstants& constants) noexcept {
    return ((constants.cellShape[0] | constants.cellShape[1] | constants.cellShape[2]) &
            LEVEL_DRAWN) != 0;
}

bool readsLines(const PictureConstants& constants) noexcept {
    return gridDrawn(constants) || cellLinesDrawn(constants);
}

LineInputs LineInputs::of(const PictureConstants& constants) noexcept {
    LineInputs inputs{.viewportW = constants.viewportW,
                      .viewportH = constants.viewportH,
                      .imageW = constants.imageW,
                      .imageH = constants.imageH,
                      .zoom = constants.zoom,
                      .offsetX = constants.offsetX,
                      .offsetY = constants.offsetY,
                      .levelShape = constants.levelShape,
                      .cellShape = constants.cellShape};
    for (std::uint32_t& shape : inputs.levelShape) {
        shape &= ~LEVEL_ADAPTIVE;
    }
    return inputs;
}

bool PictureInputs::equalButMarkers(const PictureInputs& other) const noexcept {
    PictureInputs mine = *this;
    PictureInputs theirs = other;
    mine.layers = 0;
    theirs.layers = 0;
    mine.markers = {};
    theirs.markers = {};
    return mine == theirs;
}

namespace {

// The marker geometry, in P pixels (shaders/markers.slang draws the same).
constexpr std::int64_t PIN_RING = 3;      // the pin's 2 px box and its 1 px rim
constexpr std::int64_t PIN_DOT_SPAN = 6;  // a smaller span gets the dot
constexpr std::int64_t PIN_DOT_RING = 4;  // the 6 x 6 dot and its 1 px rim, from the centre
constexpr std::int64_t OUTLINE_LINE = 2;

struct MarkerSpan {  // [begin, end) in P pixels, unclipped
    std::int64_t begin = 0, end = 0;
};

// The pixels of image pixels [first, first + count) on `axis`, as cursorSpan unclipped: begin
// min(firstPixelOf(first), firstPixelOf(size) - 1), end max(begin + 1, firstPixelOf(first +
// count)), with first and first + count kept in the image; none without an image.
std::optional<MarkerSpan> markerSpan(const dvv::media::AxisView& axis, std::uint32_t first,
                                     std::uint32_t count) {
    if (axis.size == 0 || !(axis.zoom > 0.0F) || count == 0) {
        return std::nullopt;
    }
    const std::uint32_t from = std::min(first, axis.size - 1);
    const std::uint32_t to = std::max(from + 1, static_cast<std::uint32_t>(std::min<std::uint64_t>(
                                                    std::uint64_t{first} + count, axis.size)));
    const std::int64_t last = std::int64_t{dvv::media::firstPixelOf(axis, axis.size)} - 1;
    const std::int64_t begin = std::min<std::int64_t>(dvv::media::firstPixelOf(axis, from), last);
    return MarkerSpan{.begin = begin,
                      .end = std::max<std::int64_t>(begin + 1, dvv::media::firstPixelOf(axis, to))};
}

// [x0, x1) x [y0, y1) of P, moved into the output and clipped to the drawn part; nothing when
// empty.
void addRect(std::vector<dvv::media::PixelRect>& rects, const PassRegions& regions, std::int64_t x0,
             std::int64_t y0, std::int64_t x1, std::int64_t y1) {
    const dvv::media::PixelRect& drawn = regions.pictureDrawn;
    const std::int64_t left = std::max<std::int64_t>(x0 + regions.picture.x, drawn.x);
    const std::int64_t top = std::max<std::int64_t>(y0 + regions.picture.y, drawn.y);
    const std::int64_t right =
        std::min<std::int64_t>(x1 + regions.picture.x, std::int64_t{drawn.x} + drawn.width);
    const std::int64_t bottom =
        std::min<std::int64_t>(y1 + regions.picture.y, std::int64_t{drawn.y} + drawn.height);
    if (left < right && top < bottom) {
        rects.push_back({.x = static_cast<std::uint32_t>(left),
                         .y = static_cast<std::uint32_t>(top),
                         .width = static_cast<std::uint32_t>(right - left),
                         .height = static_cast<std::uint32_t>(bottom - top)});
    }
}

// The four edge strips of the ring `ring` pixels wide outside [x, y].
void addRing(std::vector<dvv::media::PixelRect>& rects, const PassRegions& regions,
             const MarkerSpan& x, const MarkerSpan& y, std::int64_t ring) {
    addRect(rects, regions, x.begin - ring, y.begin - ring, x.end + ring, y.begin);
    addRect(rects, regions, x.begin - ring, y.end, x.end + ring, y.end + ring);
    addRect(rects, regions, x.begin - ring, y.begin, x.begin, y.end);
    addRect(rects, regions, x.end, y.begin, x.end + ring, y.end);
}

}  // namespace

std::vector<dvv::media::PixelRect> markerRects(const MarkerParams& markers,
                                               const dvv::media::ViewTransform& view,
                                               const PassRegions& regions) {
    std::vector<dvv::media::PixelRect> rects;
    const dvv::media::AxisView ax = dvv::media::axisX(view);
    const dvv::media::AxisView ay = dvv::media::axisY(view);
    if (markers.pin) {
        const std::optional<MarkerSpan> x = markerSpan(ax, markers.pin->x, 1);
        const std::optional<MarkerSpan> y = markerSpan(ay, markers.pin->y, 1);
        if (x && y) {
            if (std::min(x->end - x->begin, y->end - y->begin) >= PIN_DOT_SPAN) {
                addRing(rects, regions, *x, *y, PIN_RING);
            } else {
                const std::int64_t cx = x->begin + ((x->end - x->begin) / 2);
                const std::int64_t cy = y->begin + ((y->end - y->begin) / 2);
                addRect(rects, regions, cx - PIN_DOT_RING, cy - PIN_DOT_RING, cx + PIN_DOT_RING,
                        cy + PIN_DOT_RING);
            }
        }
    }
    if (markers.outline) {
        const std::optional<MarkerSpan> x =
            markerSpan(ax, markers.outline->x, markers.outline->width);
        const std::optional<MarkerSpan> y =
            markerSpan(ay, markers.outline->y, markers.outline->height);
        if (x && y) {
            addRing(rects, regions, *x, *y, OUTLINE_LINE);
        }
    }
    return rects;
}

PicturePass::PicturePass(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                         std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline,
                         std::shared_ptr<dvv::gpu::GpuImage> dummy,
                         std::shared_ptr<const Inspect> inspect,
                         std::shared_ptr<dvv::gpu::ComputePipeline> marker_pipeline) noexcept
    : pipeline_(std::move(pipeline)),
      lines_pipeline_(std::move(lines_pipeline)),
      marker_pipeline_(std::move(marker_pipeline)),
      dummy_(std::move(dummy)),
      inspect_(std::move(inspect)) {}

namespace {

dvv::Result<std::shared_ptr<dvv::gpu::GpuImage>> createDummy(dvv::gpu::ExecutionContext& context,
                                                             VkFormat format, const char* name) {
    return dvv::gpu::createImage(context, {.width = 1,
                                           .height = 1,
                                           .format = format,
                                           .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                           .name = name});
}

}  // namespace

dvv::Result<PicturePass> PicturePass::create(dvv::gpu::ExecutionContext& context) {
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
            dvv::gpu::createComputePipeline(
                context, rawreel_shaders::picture(),
                static_cast<std::uint32_t>(sizeof(PictureConstants)),
                dvv::gpu::ComputeBindings{.storageImages = MAX_STREAMS + 2}));
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline,
            dvv::gpu::createComputePipeline(context, rawreel_shaders::pictureLines(),
                                            static_cast<std::uint32_t>(sizeof(PictureConstants)),
                                            dvv::gpu::ComputeBindings{.storageImages = 1}));
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> dummy,
            createDummy(context, dvv::media::DISPLAY_IMAGE_FORMAT, "picture dummy"));
    auto inspect = std::make_shared<Inspect>();
    DVV_TRY(inspect->pipeline, dvv::gpu::createComputePipeline(
                                   context, rawreel_shaders::pictureInspect(),
                                   static_cast<std::uint32_t>(sizeof(InspectConstants)),
                                   dvv::gpu::ComputeBindings{.storageImages = INSPECT_BINDINGS}));
    for (std::shared_ptr<dvv::gpu::GpuImage>& plane : inspect->planes) {
        DVV_TRY(plane, createDummy(context, VK_FORMAT_R16_UINT, "inspect plane dummy"));
    }
    DVV_TRY(inspect->valueStrip,
            createDummy(context, VK_FORMAT_R8G8B8A8_UNORM, "inspect value strip dummy"));
    DVV_TRY(inspect->testCodes,
            createDummy(context, VK_FORMAT_R32_UINT, "inspect test codes dummy"));
    // The marker pass pushes the inspect pass's constants and binds the output only.
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> marker_pipeline,
            dvv::gpu::createComputePipeline(context, rawreel_shaders::pictureMarkers(),
                                            static_cast<std::uint32_t>(sizeof(InspectConstants)),
                                            dvv::gpu::ComputeBindings{.storageImages = 1}));
    DVV_TRY(inspect->padding,
            dvv::gpu::createComputePipeline(context, rawreel_shaders::paddingCheck(),
                                            static_cast<std::uint32_t>(sizeof(PaddingConstants)),
                                            dvv::gpu::ComputeBindings{}));
    return PicturePass(std::move(pipeline), std::move(lines_pipeline), std::move(dummy),
                       std::move(inspect), std::move(marker_pipeline));
}

dvv::Result<std::shared_ptr<dvv::gpu::GpuImage>> PicturePass::createLineTable(
    dvv::gpu::ExecutionContext& context, dvv::platform::PixelSize extent) {
    // P lies in the output. R32_UINT storage images are mandatory in Vulkan, so no other format
    // is needed.
    return dvv::gpu::createImage(context, {.width = std::max({extent.width, extent.height, 1U}),
                                           .height = 4,
                                           .format = VK_FORMAT_R32_UINT,
                                           .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                           .name = "viewport grid lines"});
}

PicturePass::Due PicturePass::due(State& state, const PictureInputs& inputs, bool all) {
    // A change in the markers only redraws the old and the new markers: the line table and the
    // rest of the drawn part stay as the last record drew them.
    if (!all && state.inputs && *state.inputs != inputs && state.inputs->equalButMarkers(inputs)) {
        const PassRegions regions{.picture = inputs.picture, .pictureDrawn = inputs.pictureDrawn};
        std::vector<dvv::media::PixelRect> parts =
            markerRects(state.inputs->markers, inputs.view, regions);
        const std::vector<dvv::media::PixelRect> now =
            markerRects(inputs.markers, inputs.view, regions);
        parts.insert(parts.end(), now.begin(), now.end());
        state.inputs = inputs;
        return {.record = !parts.empty(), .buildLines = false, .parts = std::move(parts)};
    }
    const bool record = all || state.inputs != inputs;
    // The line table outlives the picture pass that built it: a picture pass rebuilds it only when
    // its inputs changed or the last build may not have run (a dropped frame, new targets).
    // The picture pass reads the table when it dispatches and readsLines holds.
    const bool reads_lines = record && !inputs.pictureDrawn.empty() && readsLines(inputs.constants);
    const LineInputs line_key = LineInputs::of(inputs.constants);
    const bool build_lines = reads_lines && (all || state.lines != line_key);
    if (record) {
        state.lines = reads_lines ? std::optional<LineInputs>(line_key) : std::nullopt;
    }
    state.inputs = inputs;
    return {.record = record, .buildLines = build_lines};
}

PicturePass::Dispatch PicturePass::dispatch(bool dispatch, dvv::gpu::GpuImage* image,
                                            const PictureConstants& constants,
                                            dvv::media::PixelRect picture,
                                            dvv::media::PixelRect drawn, bool build_lines,
                                            InspectBinding inspect,
                                            std::vector<dvv::media::PixelRect> parts,
                                            std::vector<dvv::media::PixelRect> marker_parts) const {
    const bool dispatched = dispatch && !drawn.empty();
    const bool layered = dispatched && inspect.layers != nullptr;
    const bool inspects = layered && !inspect.markersOnly;
    const bool marks = layered && inspect.markersOnly && !marker_parts.empty();
    // The picture pass dispatches the drawn part of P; the prepass (one thread per column and per
    // row of P) covers all of P, so the line image stays indexed by P pixel.
    return Dispatch{
        .pipeline = pipeline_,
        .linesPipeline = lines_pipeline_,
        .dummy = dummy_,
        .inspectPass = inspects ? inspect_ : nullptr,
        .markerPass = marks ? marker_pipeline_ : nullptr,
        .inspect = inspects || marks ? std::move(inspect) : InspectBinding{},
        .image = dispatched ? image : nullptr,
        .constants = constants,
        .groupsX = dispatched ? (drawn.width + 7) / 8 : 0,
        .groupsY = (drawn.height + 7) / 8,
        .lineGroups =
            dispatched && build_lines ? (std::max(picture.width, picture.height) + 63) / 64 : 0,
        .parts = dispatched ? std::move(parts) : std::vector<dvv::media::PixelRect>{},
        .markerParts = marks ? std::move(marker_parts) : std::vector<dvv::media::PixelRect>{}};
}

void PicturePass::Dispatch::record(dvv::gpu::CommandRecorder& recorder, dvv::gpu::GpuImage& output,
                                   dvv::gpu::GpuImage& lines) const {
    if (groupsX == 0) {
        return;
    }
    const std::span<const std::byte> pushed = std::as_bytes(std::span(&constants, 1));
    if (lineGroups != 0) {
        const std::array<dvv::gpu::GpuImage*, 1> line_table{&lines};
        recorder.dispatch(
            *linesPipeline, pushed, line_table,
            dvv::gpu::GroupCount{.x = lineGroups, .y = cellLinesDrawn(constants) ? 4U : 2U});
    }
    dvv::gpu::GpuImage* const display = image != nullptr ? image : dummy.get();
    // The drawn part in one dispatch, or each part in its own: thread id draws P pixel id + draw.
    const auto each = [&](const auto& draw) {
        if (parts.empty()) {
            draw(constants, dvv::gpu::GroupCount{.x = groupsX, .y = groupsY});
            return;
        }
        for (const dvv::media::PixelRect& part : parts) {
            PictureConstants at = constants;
            at.drawX = part.x - constants.regionX;
            at.drawY = part.y - constants.regionY;
            draw(at, dvv::gpu::GroupCount{.x = (part.width + 7) / 8, .y = (part.height + 7) / 8});
        }
    };
    if (inspectPass == nullptr) {
        const std::array<dvv::gpu::GpuImage*, MAX_STREAMS + 2> bound{display, &output, &lines};
        each([&](const PictureConstants& at, dvv::gpu::GroupCount groups) {
            recorder.dispatch(*pipeline, std::as_bytes(std::span(&at, 1)), bound, groups);
        });
        if (markerPass == nullptr) {
            return;
        }
        // The markers over what the picture pass drew (the recorder's barrier orders the writes):
        // the marker pass writes only marker pixels, so a group's threads past a part's edges
        // draw what a full render draws there.
        const std::array<dvv::gpu::GpuImage*, 1> marked{&output};
        for (const dvv::media::PixelRect& part : markerParts) {
            InspectConstants at{.picture = constants, .layers = inspect.layers->deviceAddress()};
            at.picture.drawX = part.x - constants.regionX;
            at.picture.drawY = part.y - constants.regionY;
            recorder.dispatch(
                *markerPass, std::as_bytes(std::span(&at, 1)), marked,
                dvv::gpu::GroupCount{.x = (part.width + 7) / 8, .y = (part.height + 7) / 8});
        }
        return;
    }
    const InspectConstants inspect_constants{
        .picture = constants,
        .layers = inspect.layers->deviceAddress(),
        .rawBase = inspect.rawBase,
        .readback = 0,
        .tiles = inspect.tiles != nullptr ? inspect.tiles->deviceAddress() : 0};
    if (inspect.tiles != nullptr && inspect.counter != nullptr) {
        // Level 1: the counter and the tile map cleared, then one thread per sample per
        // component; the barrier before each dispatch orders the fills and the atomics.
        recorder.fillBuffer(*inspect.counter, 0, inspect.counterOffset, 8);
        recorder.fillBuffer(*inspect.tiles, 0xFFFFFFFFU);
        const PaddingConstants padding{
            .layers = inspect_constants.layers,
            .rawBase = inspect.rawBase,
            .counter = inspect.counter->deviceAddress() + inspect.counterOffset,
            .tiles = inspect_constants.tiles,
            .offsetX = constants.offsetX,
            .offsetY = constants.offsetY,
            .zoom = constants.zoom,
            .imageW = constants.imageW,
            .imageH = constants.imageH,
            .viewportW = constants.viewportW,
            .viewportH = constants.viewportH};
        recorder.dispatch(*inspectPass->padding, std::as_bytes(std::span(&padding, 1)),
                          dvv::gpu::GroupCount{.x = (inspect.samplesX + 7) / 8,
                                               .y = (inspect.samplesY + 7) / 8,
                                               .z = inspect.components});
    }
    std::array<dvv::gpu::GpuImage*, INSPECT_BINDINGS> bound{
        display,
        &output,
        &lines,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        inspect.valueStrip != nullptr ? inspect.valueStrip.get() : inspectPass->valueStrip.get(),
        inspect.testCodes != nullptr ? inspect.testCodes : inspectPass->testCodes.get()};
    for (std::size_t k = 0; k < INSPECT_PLANES; ++k) {
        bound.at(3 + k) = inspect.planes.at(k) != nullptr ? inspect.planes.at(k)
                                                          : inspectPass->planes.at(k).get();
    }
    each([&](const PictureConstants& at, dvv::gpu::GroupCount groups) {
        InspectConstants part = inspect_constants;
        part.picture = at;
        recorder.dispatch(*inspectPass->pipeline, std::as_bytes(std::span(&part, 1)), bound,
                          groups);
    });
}

PicturePass::StripUpload PicturePass::uploadValueStrip(
    dvv::gpu::ExecutionContext& context, const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip) {
    if (strip == nullptr || strip == value_failed_) {
        return {};  // failed before: only another strip is tried
    }
    dvv::Result<dvv::gpu::GlyphStripUpload> upload = dvv::gpu::uploadGlyphStrip(context, *strip);
    if (!upload) {
        value_failed_ = strip;
        DVV_LOG_WARN("viewport: the value glyph strip did not upload, no values drawn: {}",
                     upload.error().describe());
        return {.image = nullptr, .upload = {}, .failed = true};
    }
    return {.image = std::move(upload->image), .upload = std::move(upload->record)};
}

}  // namespace rawreel
