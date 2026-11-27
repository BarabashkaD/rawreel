#include "render/view_renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>  // frame.h no longer brings it
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "core/frame_set.h"
#include "dvv/base/error.h"
#include "dvv/base/log.h"
#include "dvv/base/move_only_function.h"
#include "dvv/gpu/command_recorder.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/gpu/resources.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/display.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/frame_vulkan.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/layer_block.h"
#include "render/picture_constants.h"
#include "render/picture_pass.h"
#include "render/ruler_constants.h"
#include "render/ruler_pass.h"
#include "render/view_layout.h"
#include "render/view_state.h"

namespace rawreel {

// The caller's epoch check is what separates stacks: an old and a new context on the same GPU
// share the DeviceId, and every generation is FIRST_GENERATION in v1.0 (design v2 D7). The
// device/generation comparison only rejects an image of another physical device.
dvv::gpu::GpuImage* ViewRenderer::displayImageOf(const dvv::media::ReadLease& lease,
                                                 std::uint64_t epoch) {
    if (!lease || !dvv::media::isDisplay(lease.view().format)) {
        return nullptr;
    }
    dvv::gpu::GpuImage* const image = dvv::media::vulkan::image(lease, 0);
    if (image == nullptr || dvv::gpu::vulkanFormat(*image) != dvv::media::DISPLAY_IMAGE_FORMAT ||
        image->deviceId() != context_->deviceId() ||
        image->generation() != context_->generation()) {
        if (reported_binding_epoch_ != epoch) {  // one Error per epoch
            reported_binding_epoch_ = epoch;
            DVV_LOG_ERROR("display view with an unexpected binding (epoch {})", epoch);
        }
        return nullptr;
    }
    return image;
}

dvv::Result<std::unique_ptr<ViewRenderer>> ViewRenderer::create(
    std::shared_ptr<dvv::gpu::ExecutionContext> context, std::uint64_t epoch) {
    DVV_TRY(PicturePass picture, PicturePass::create(*context));
    DVV_TRY(RulerPass rulers, RulerPass::create(*context));
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuBuffer> readback,
            dvv::gpu::createBuffer(*context, {.size = READBACK_SLOTS * READBACK_SLOT_BYTES,
                                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              .memory = dvv::gpu::MemoryUsage::HostReadback,
                                              .name = "view readback"}));
    return std::make_unique<ViewRenderer>(Private{}, std::move(context), epoch, std::move(picture),
                                          std::move(rulers), std::move(readback));
}

ViewRenderer::ViewRenderer(Private /*key*/, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                           std::uint64_t epoch, PicturePass picture, RulerPass rulers,
                           std::shared_ptr<dvv::gpu::GpuBuffer> readback) noexcept
    : context_(std::move(context)),
      epoch_(epoch),
      picture_(std::move(picture)),
      rulers_(std::move(rulers)),
      readback_(std::move(readback)) {}

void ViewRenderer::countValueFailure(const PicturePass::StripUpload& upload) {
    if (upload.failed) {
        std::lock_guard lock(recorded_mutex_);
        ++recorded_.valueStripFailures;
    }
}

void ViewRenderer::countFailure(const RulerPass::GlyphBinding& glyphs) {
    if (glyphs.failed) {
        std::lock_guard lock(recorded_mutex_);
        ++recorded_.glyphFailures;
    }
}

dvv::Result<ViewRenderer::Targets> ViewRenderer::makeTargets(dvv::platform::PixelSize extent,
                                                             std::size_t panels) const {
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> output,
            dvv::gpu::createImage(
                *context_, {.width = extent.width,
                            .height = extent.height,
                            .format = VK_FORMAT_R8G8B8A8_UNORM,
                            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            .name = "view output"}));
    Targets targets{.output = std::move(output), .lines = {}};
    for (std::size_t i = 0; i < panels; ++i) {
        DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> lines,
                PicturePass::createLineTable(*context_, extent));
        targets.lines.push_back(std::move(lines));
    }
    return targets;
}

ViewRenderer::PanelPlan ViewRenderer::panelPlan(const FrameSet& frames,
                                                const ViewParams& params) const {
    PanelPlan plan;
    switch (params.inspect.panels) {
        case PanelKind::Picture: break;
        case PanelKind::Planes: {
            const dvv::media::ReadLease& planes = frames.lease(ViewRole::Planes);
            if (!params.inspect.on || frames.epoch != epoch_ || frames.count < 1 ||
                frames.count > MAX_STREAMS || !planes) {
                break;
            }
            if (dvv::media::formatDesc(planes.desc().format).componentCount >= 1) {
                plan = {.kind = PanelKind::Planes,
                        .count = panelCount(PanelKind::Planes, planes.desc().format),
                        .desc = planes.desc()};
            }
            break;
        }
    }
    return plan;
}

namespace {

// The plane images of a planes view lease, the first `count`; null where a plane is not an
// R8_UINT or R16_UINT image of `context`'s device (the inspect pipeline binds a dummy there).
std::array<dvv::gpu::GpuImage*, INSPECT_PLANES> planeImagesOf(
    const dvv::media::ReadLease& lease, std::uint32_t count,
    const dvv::gpu::ExecutionContext& context) {
    std::array<dvv::gpu::GpuImage*, INSPECT_PLANES> planes{};
    for (std::size_t k = 0; k < std::min<std::size_t>(count, planes.size()); ++k) {
        dvv::gpu::GpuImage* const image = dvv::media::vulkan::image(lease, k);
        if (image == nullptr || image->deviceId() != context.deviceId() ||
            image->generation() != context.generation()) {
            continue;
        }
        const VkFormat format = dvv::gpu::vulkanFormat(*image);
        if (format == VK_FORMAT_R8_UINT || format == VK_FORMAT_R16_UINT) {
            planes.at(k) = image;
        }
    }
    return planes;
}

}  // namespace

dvv::Result<ViewRenderer::Drawn> ViewRenderer::prepare(dvv::media::PixelRect panel,
                                                       const FrameSet& frames,
                                                       const ViewParams& params, bool skip_encode,
                                                       const PanelPlan& plan, std::uint32_t index,
                                                       LayerCache& layers,
                                                       const PicturePass::ValueStrip& values) {
    Drawn drawn;
    if (frames.epoch == epoch_ && frames.count >= 1 && frames.count <= MAX_STREAMS) {
        drawn.image =
            displayImageOf(frames.lease(ViewRole::Display), frames.epoch);  // null: background only
    }
    drawn.index = drawn.image != nullptr ? frames.index : -1;
    const dvv::platform::PixelSize image_size =
        drawn.image != nullptr
            ? dvv::platform::PixelSize{drawn.image->width(), drawn.image->height()}
            : dvv::platform::PixelSize{};
    // P for the view; panelRegions owns it from here on.
    const dvv::platform::PixelSize picture =
        dvv::media::pictureRegion(panel.size(), params.overlay.strips).size();
    drawn.view = dvv::media::resolveView(params.view, picture, image_size);
    drawn.regions = panelRegions(panel, params.overlay.strips, drawn.view);
    drawn.picture = pictureConstants(params, drawn.view, drawn.regions, skip_encode);
    const std::optional<dvv::media::ImagePixel> cursor =
        params.cursor ? dvv::media::cursorPixel(drawn.view, drawn.regions.picture, *params.cursor)
                      : std::nullopt;
    drawn.rulers = rulerConstants(params.overlay, drawn.view, drawn.regions, cursor, skip_encode);
    // The layers: a Planes panel reads its plane, a Picture panel's values the planes view, its
    // padding marks the Raw view; no layer on keeps today's pipeline.
    const dvv::media::ReadLease& planes = frames.lease(ViewRole::Planes);
    const bool values_on = valuesOn(params) && planes;
    const dvv::gpu::GpuBuffer* const raw = drawn.image != nullptr ? rawBufferOf(frames) : nullptr;
    drawn.inspect.rawBase = raw != nullptr ? raw->deviceAddress() : 0;
    const bool marks_on = params.paddingMarks && plan.kind == PanelKind::Picture && raw != nullptr;
    const bool markers_on = markersOn(params.markers);
    // Markers alone read no stored words: their block stays the same across frames.
    const bool reads_words = plan.kind == PanelKind::Planes || values_on || marks_on;
    if ((reads_words || markers_on) && drawn.image != nullptr) {
        const dvv::media::FrameDesc desc = plan.kind == PanelKind::Planes ? plan.desc
                                           : !reads_words                 ? dvv::media::FrameDesc{}
                                           : planes                       ? planes.desc()
                                                    : frames.lease(ViewRole::Raw).desc();
        LayerBlock block = layerBlock(params, desc, index, plan.kind);
        // The padding reads use desc's layouts on the Raw buffer: only when they fit it.
        const dvv::media::FrameDesc raw_desc =
            raw != nullptr ? frames.lease(ViewRole::Raw).desc() : dvv::media::FrameDesc{};
        const bool raw_fits =
            reads_words && raw != nullptr &&
            rawReadsFit(raw_desc, raw->size(), desc, drawn.image->width(), drawn.image->height());
        if (reads_words && raw != nullptr && !raw_fits && !reported_raw_mismatch_.exchange(true)) {
            DVV_LOG_WARN("viewport: the Raw view does not match the shown frame; no padding flags");
        }
        if (!raw_fits) {  // the flags and the marks read the Raw view
            block.flags &= ~(LAYER_CELL_FLAGS | LAYER_PADDING_MARKS);
            drawn.inspect.rawBase = 0;
        }
        const bool marks = (block.flags & LAYER_PADDING_MARKS) != 0 && block.tileSize != 0;
        if (marks) {
            block.tilesX = (picture.width + block.tileSize - 1) / block.tileSize;
            block.tilesY = (picture.height + block.tileSize - 1) / block.tileSize;
        }
        // The cell lines go with the values, which read the stored words.
        drawn.picture.cellShape =
            reads_words ? cellShapes(params, desc) : std::array<std::uint32_t, 3>{};
        if (!layers.block || std::memcmp(&*layers.block, &block, sizeof(LayerBlock)) != 0) {
            DVV_TRY(std::shared_ptr<dvv::gpu::GpuBuffer> buffer,
                    dvv::gpu::createBuffer(*context_, {.size = sizeof(LayerBlock),
                                                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       .memory = dvv::gpu::MemoryUsage::HostUpload,
                                                       .name = "layer block"}));
            dvv::gpu::MappedWrite write = buffer->mapWrite();
            std::memcpy(write.bytes().data(), &block, sizeof(LayerBlock));
            DVV_TRY_VOID(write.flush());
            // The block but for its markers: unchanged keeps the base serial.
            const auto base_of = [](LayerBlock b) {
                b.pinRect = {};
                b.outlineRect = {};
                return b;
            };
            const LayerBlock base = base_of(block);
            const std::optional<LayerBlock> last_base =
                layers.block ? std::optional(base_of(*layers.block)) : std::nullopt;
            const bool same_base =
                last_base && std::memcmp(&base, &*last_base, sizeof(LayerBlock)) == 0;
            std::lock_guard lock(recorded_mutex_);
            const std::uint64_t serial = ++recorded_.layerUploads;
            layers.block = block;
            layers.buffer = std::move(buffer);
            layers.serial = serial;
            layers.baseSerial = same_base ? layers.baseSerial : serial;
        }
        drawn.inspect.layers = layers.buffer;
        drawn.inspect.markersOnly = block.flags == LAYER_MARKERS;
        drawn.layers = layers.serial;
        drawn.layersBase = layers.baseSerial;
        drawn.markers = markerCells(params.markers, desc, index, plan.kind);
        drawn.inspect.planes =
            planeImagesOf(planes, dvv::media::formatDesc(desc.format).componentCount, *context_);
        if (values_on && values.image != nullptr) {
            drawn.inspect.valueStrip = values.image;
            drawn.valueStrip = values.serial;
        }
        if (marks) {  // the tile map, reused while big enough; the record clears it
            const std::uint64_t bytes =
                std::uint64_t{block.tilesX} * block.tilesY * sizeof(std::uint32_t);
            if (!layers.tiles || layers.tiles->size() < bytes) {
                DVV_TRY(layers.tiles, dvv::gpu::createBuffer(
                                          *context_, {.size = bytes,
                                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                      .memory = dvv::gpu::MemoryUsage::DeviceOnly,
                                                      .name = "padding tiles"}));
            }
            const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(desc.format);
            drawn.inspect.tiles = layers.tiles;

            drawn.inspect.counter = readback_;
            drawn.inspect.components =
                static_cast<std::uint32_t>(std::min<std::size_t>(format.componentCount, 4));
            for (std::size_t k = 0; k < drawn.inspect.components; ++k) {  // the widest grid
                const dvv::media::ComponentDesc& component = format.components.at(k);
                drawn.inspect.samplesX =
                    std::max(drawn.inspect.samplesX,
                             (desc.width + (1U << component.log2SubX) - 1U) >> component.log2SubX);
                drawn.inspect.samplesY =
                    std::max(drawn.inspect.samplesY,
                             (desc.height + (1U << component.log2SubY) - 1U) >> component.log2SubY);
            }
        }
    }
    return drawn;
}

ViewRenderer::Recording ViewRenderer::record(
    const std::shared_ptr<dvv::gpu::GpuImage>& output,
    const std::shared_ptr<dvv::gpu::GpuImage>& lines, dvv::platform::PixelSize extent,
    const FrameSet& frames, const Drawn& drawn, bool picture, bool build_lines,
    const std::vector<dvv::media::PixelRect>& parts, RulerPass::Part rulers,
    std::shared_ptr<dvv::gpu::GpuImage> glyphs, std::optional<std::uint64_t> version,
    std::uint32_t index, std::uint32_t panels) {
    const dvv::media::PixelRect region = drawn.regions.picture;
    const dvv::media::PixelRect dispatched = drawn.regions.pictureDrawn;
    const bool picture_dispatched = picture && !dispatched.empty();
    // The prepass builds the line table before the picture pass when `build_lines`; else the
    // picture pass reads the table in place.
    const bool prepass = picture_dispatched && build_lines;
    const RulerPass::Bands bands =
        RulerPass::bandsOf(drawn.rulers, drawn.regions.bandRulers, rulers);
    const bool rulers_dispatched = bands.dispatched();
    if (!picture_dispatched && !rulers_dispatched) {
        return {};
    }
    if (!rulers_dispatched) {
        glyphs = {};  // nothing reads the strip
    }
    // With the markers alone the marker pass draws them over picture.slang: over their rects, or
    // over the parts of a change in the markers only (the old and the new rects).
    const bool marker_pass =
        picture_dispatched && drawn.inspect.layers != nullptr && drawn.inspect.markersOnly;
    std::vector<dvv::media::PixelRect> marker_parts;
    if (marker_pass) {
        marker_parts =
            parts.empty() ? markerRects(drawn.markers, drawn.view, drawn.regions) : parts;
    }
    {
        std::lock_guard lock(recorded_mutex_);
        // Any record: the layout it drew into and what its ruler pass dispatched (panel 0's). A
        // due picture pass: what P shows (nothing when P is empty). The ruler rects are exact on
        // every record: a ruler pass that is not due has unchanged constants, and so unchanged
        // rects, and hidden rulers have empty ones.
        if (index == 0) {
            recorded_.epoch = epoch_;
            recorded_.extent = extent;
            recorded_.panels = panels;
            recorded_.picture = region;
            recorded_.rulers = drawn.regions.rulers;
            recorded_.rulerDispatched = bands.parts;
            recorded_.rulerNumbers = bands.numbers();
            if (picture) {
                recorded_.index = picture_dispatched ? drawn.index : -1;
                recorded_.zoom = drawn.view.zoom();
                recorded_.origin = drawn.view.origin();
                recorded_.mode = drawn.view.mode();
                recorded_.pictureDrawn = dispatched;
                recorded_.inspectPipeline = picture_dispatched && drawn.inspect.layers != nullptr &&
                                            !drawn.inspect.markersOnly;
                recorded_.markerParts = marker_parts;
                recorded_.markers = drawn.markers;
                recorded_.pictureParts = !picture_dispatched ? std::vector<dvv::media::PixelRect>{}
                                         : parts.empty()     ? std::vector{dispatched}
                                                             : parts;
                if (version) {
                    recorded_.version = *version;
                }
            }
        }
        if (picture) {
            std::vector<PanelMarkers>& drew = recorded_.panelMarkers;
            if (markersOn(drawn.markers) || !drew.empty()) {
                drew.resize(panels);
                // Built only when the panel drew or its markers changed: a record that drew
                // nothing leaves the entry as it was.
                if (index < panels && (picture_dispatched || drew[index].cells != drawn.markers)) {
                    drew[index] = {.cells = drawn.markers,
                                   .rects = markerRects(drawn.markers, drawn.view, drawn.regions)};
                }
            }
            if (std::ranges::none_of(drew,
                                     [](const PanelMarkers& m) { return markersOn(m.cells); })) {
                drew.clear();  // no panel draws a marker
            }
        }
        if (picture_dispatched && !parts.empty()) {
            ++recorded_.markerRecords;
        } else if (picture_dispatched) {
            ++recorded_.pictureRecords;
            recorded_.pictureFlags = drawn.picture.flags;
        }
        if (rulers_dispatched) {
            ++recorded_.rulerRecords;
        }
        if (picture_dispatched && drawn.inspect.tiles != nullptr &&
            drawn.inspect.counter != nullptr) {
            ++recorded_.paddingPasses;
        }
        if (prepass) {
            ++recorded_.linePasses;
        }
    }
    // Everything the GPU touches stays alive until the submission has completed (M1a resource
    // rule): the pipelines, the targets, the dummies, the layer block, the bound glyph strip, and
    // with the picture pass a share of the FrameSet's
    // leases, which keeps the display image and the planes in the store. A ruler-only record holds
    // neither: the ruler pass never reads the image. The presenter releases the function on its
    // own thread. CommandRecorder puts a barrier before every dispatch.
    dvv::gpu::RecordFunction commands =
        [output, lines,
         picture_pass = picture_.dispatch(picture, drawn.image, drawn.picture, region, dispatched,
                                          build_lines, drawn.inspect, parts, marker_parts),
         ruler_pass = rulers_.dispatch(bands, std::move(glyphs)),
         leases = picture_dispatched ? frames.share()
                                     : FrameSet{}](dvv::gpu::CommandRecorder& recorder) mutable {
            static_cast<void>(leases);  // held only for their lifetime
            picture_pass.record(recorder, *output, *lines);
            ruler_pass.record(recorder, *output);
        };
    return Recording{.commands = std::move(commands), .rulers = rulers_dispatched};
}

namespace {

// The commands of the panels, in panel order; one panel's as they are.
dvv::gpu::RecordFunction joined(std::vector<dvv::gpu::RecordFunction> parts) {
    if (parts.size() == 1) {
        return std::move(parts.front());
    }
    return [parts = std::move(parts)](dvv::gpu::CommandRecorder& recorder) mutable {
        for (dvv::gpu::RecordFunction& part : parts) {
            part(recorder);
        }
    };
}

}  // namespace

ReadbackUse readbackUse(const ViewReadback& r, std::uint64_t epoch,
                        std::uint64_t stream_set) noexcept {
    if (r.epoch != epoch) {
        return r.epoch > epoch ? ReadbackUse::Hold : ReadbackUse::Drop;
    }
    if (r.streamSet != stream_set) {
        return r.streamSet > stream_set ? ReadbackUse::Hold : ReadbackUse::Drop;
    }
    return ReadbackUse::Show;
}

void ViewRenderer::setReadbackHandler(dvv::MoveOnlyFunction<void(ViewReadback)> handler) {
    dvv::MoveOnlyFunction<void(ViewReadback)> old;
    {
        std::lock_guard lock(handler_mutex_);
        old = std::exchange(handler_, std::move(handler));
    }
    // `old` is destroyed outside the lock.
}

namespace {

// The window of a stored word: the 4-aligned 8 bytes from its byteOffset & ~3.
constexpr std::uint64_t windowStart(const dvv::media::StoredWord& word) noexcept {
    return word.byteOffset & ~std::uint64_t{3};
}

// Component k's window in a slot (bytes 0-7 are the padding counter's).
constexpr std::uint64_t windowOffset(std::uint64_t slot, std::size_t k) noexcept {
    return (slot * READBACK_SLOT_BYTES) + (READBACK_WINDOW_BYTES * (k + 1));
}

}  // namespace

const dvv::gpu::GpuBuffer* ViewRenderer::rawBufferOf(const FrameSet& frames) const {
    if (frames.epoch != epoch_ || frames.count < 1 || frames.count > MAX_STREAMS) {
        return nullptr;
    }
    const dvv::media::ReadLease& lease = frames.lease(ViewRole::Raw);
    const dvv::gpu::GpuBuffer* const raw = lease ? dvv::media::vulkan::buffer(lease) : nullptr;
    if (raw == nullptr || raw->deviceId() != context_->deviceId() ||
        raw->generation() != context_->generation()) {
        return nullptr;
    }
    return raw;
}

std::uint64_t ViewRenderer::nextSlot() {
    const std::uint64_t slot = readback_serial_++ % READBACK_SLOTS;
    std::lock_guard lock(recorded_mutex_);
    recorded_.readbackSlot = static_cast<std::uint32_t>(slot);
    return slot;
}

std::optional<ViewRenderer::ProbePlan> ViewRenderer::probePlan(const FrameSet& frames,
                                                               const ViewParams& params) const {
    const dvv::gpu::GpuBuffer* const raw = rawBufferOf(frames);
    if (!params.inspect.on || !params.inspect.probe || raw == nullptr ||
        (raw->desc().usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0) {
        return std::nullopt;
    }
    const dvv::media::FrameDesc desc = frames.lease(ViewRole::Raw).desc();
    const dvv::media::ImagePixel pixel = *params.inspect.probe;
    if (pixel.x >= desc.width || pixel.y >= desc.height) {
        return std::nullopt;
    }
    const dvv::media::PixelFormatDesc& format = dvv::media::formatDesc(desc.format);
    ProbePlan plan{.tags = {.index = frames.index,
                            .epoch = frames.epoch,
                            .streamSet = frames.streamSet,
                            .paddingSamples = std::nullopt,
                            .probe = std::nullopt},
                   .pixel = pixel,
                   .count = static_cast<std::uint8_t>(
                       std::min<std::size_t>(format.componentCount, PROBE_COMPONENTS)),
                   .words = {},
                   .raw = raw};
    if (plan.count == 0) {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < plan.count; ++k) {
        dvv::Result<dvv::media::StoredWord> word =
            dvv::media::storedWord(desc.format, desc.width, desc.height, k,
                                   dvv::media::sampleOf(format.components.at(k), pixel));
        // The window stays inside the buffer (the unpack's alignUp(bytes, 4) + 4 rule).
        if (!word || windowStart(*word) + READBACK_WINDOW_BYTES > raw->size()) {
            return std::nullopt;
        }
        plan.words.at(k) = *word;
    }
    return plan;
}

ViewRenderer::ReadbackRecord ViewRenderer::readbackRecord(const FrameSet& frames,
                                                          const std::optional<ProbePlan>& plan,
                                                          bool padding, std::uint64_t slot,
                                                          std::optional<ReadbackKey> key) {
    // One frame is in flight per presenter and `completed` runs before the next provider call, so
    // the slot is read before a record writes it again.
    dvv::gpu::RecordFunction commands;
    if (plan) {
        commands = [readback = readback_, plan = *plan, slot,
                    leases = frames.share(ViewRoles::of(ViewRole::Raw))](
                       dvv::gpu::CommandRecorder& recorder) {
            static_cast<void>(leases);  // held only for their lifetime: they keep plan.raw
            for (std::size_t k = 0; k < plan.count; ++k) {
                recorder.copyBuffer(*plan.raw, *readback,
                                    {.sourceOffset = windowStart(plan.words.at(k)),
                                     .destinationOffset = windowOffset(slot, k),
                                     .size = READBACK_WINDOW_BYTES});
            }
        };
    }
    // Only a handler call that returned, while the pin is still `key`, delivers the readback
    // (pin_delivered_); frame() records any other again.
    const ViewReadback tags{.index = frames.index,
                            .epoch = frames.epoch,
                            .streamSet = frames.streamSet,
                            .paddingSamples = std::nullopt,
                            .probe = std::nullopt};
    Completion completed = [this, plan, padding, tags, slot,
                            key](const dvv::Result<std::uint64_t>& result) noexcept {
        try {
            if (!result) {
                return;
            }
            const dvv::Result<dvv::gpu::MappedRead> mapped = readback_->mapRead();
            if (!mapped) {
                DVV_LOG_WARN("viewport: the pin's readback did not map: {}",
                             mapped.error().describe());
                return;
            }
            ViewReadback readback = tags;
            if (padding) {
                std::uint64_t counter = 0;
                std::memcpy(&counter, mapped->bytes().subspan(slot * READBACK_SLOT_BYTES, 8).data(),
                            sizeof(counter));
                readback.paddingSamples = counter;
            }
            if (plan) {
                ProbeSample probe{
                    .pixel = plan->pixel, .count = plan->count, .codes = {}, .words = {}};
                for (std::size_t k = 0; k < plan->count; ++k) {
                    const dvv::media::StoredWord& word = plan->words.at(k);
                    const std::uint32_t value = dvv::media::readStoredWord(
                        mapped->bytes().subspan(windowOffset(slot, k), READBACK_WINDOW_BYTES),
                        windowStart(word), word);
                    probe.words.at(k) = value;
                    probe.codes.at(k) = dvv::media::codeOf(value, word.shift, word.bits);
                }
                readback.probe = probe;
            }
            std::lock_guard lock(handler_mutex_);
            if (handler_) {
                handler_(readback);
                std::lock_guard keys(key_mutex_);
                if (plan && key && key == pin_key_) {
                    pin_delivered_ = true;
                }
            }
        } catch (...) {  // the handler, or a log line
            DVV_LOG_ERROR("viewport: the readback failed: {}", dvv::describeCurrentException());
        }
    };
    return {.commands = std::move(commands), .completed = std::move(completed)};
}

dvv::Result<dvv::gpu::PresentFrame> ViewRenderer::frame(const dvv::gpu::PresentFrameParams& params,
                                                        const ViewSnapshot& snapshot) {
    const bool skip_encode = params.encoding == dvv::gpu::PresentEncoding::Srgb;
    if (skip_encode && !logged_srgb_) {
        logged_srgb_ = true;
        DVV_LOG_WARN("viewport: the presenter applies the sRGB encode; the renderer skips its own");
    }
    // A dropped frame's commands never ran, and a new output holds nothing: both passes run.
    bool all = !params.previousFrameCompleted;
    if (!params.previousFrameCompleted) {
        RulerPass::dropUploaded(glyphs_);
        value_strip_ = {};
    }
    // The value strip: uploaded before every panel's commands when the values draw with another
    // strip, and kept only when a recorded picture pass binds it (else the next frame uploads).
    const PicturePass::ValueStrip before_values = value_strip_;
    dvv::gpu::RecordFunction value_upload;
    if (!valuesOn(snapshot.params)) {
        value_strip_ = {};
    } else if (snapshot.params.inspect.glyphs != value_strip_.source) {
        PicturePass::StripUpload upload =
            picture_.uploadValueStrip(*context_, snapshot.params.inspect.glyphs);
        countValueFailure(upload);
        value_strip_ = {};
        if (upload.image != nullptr) {
            value_upload = std::move(upload.upload);
            value_strip_ = {.source = snapshot.params.inspect.glyphs,
                            .image = std::move(upload.image),
                            .serial = ++value_serial_};
        }
    }
    bool value_bound = false;
    const PanelPlan plan = panelPlan(snapshot.feed.frames, snapshot.params);
    const std::vector<dvv::media::PixelRect> rects =
        panelRects(params.extent, panelGrid(params.extent, plan.count));
    if (!targets_.output || params.extent != extent_ || targets_.lines.size() < rects.size()) {
        DVV_TRY(targets_, makeTargets(params.extent, rects.size()));
        extent_ = params.extent;
        all = true;
    }
    // Another panel count moves every panel: each draws again.
    if (rects.size() != last_panels_) {
        last_panels_ = rects.size();
        all = true;
    }
    const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip = snapshot.params.overlay.glyphs;
    // A strip no band draws is no ruler input.
    const bool strips =
        snapshot.params.overlay.strips.top != 0 || snapshot.params.overlay.strips.left != 0;
    std::vector<dvv::gpu::RecordFunction> parts;
    // The strip upload of this frame: the first due panel's, recorded before every part, and the
    // strip state before it (restored when no ruler pass binds the new strip).
    dvv::gpu::RecordFunction glyph_upload;
    RulerPass::Glyphs before_upload;
    bool upload_bound = false;
    std::optional<std::uint64_t> padding_slot;  // the readback slot of this frame's padding check
    for (std::uint32_t i = 0; i < rects.size(); ++i) {
        PanelState& state = panels_.at(i);
        DVV_TRY(Drawn drawn, prepare(rects[i], snapshot.feed.frames, snapshot.params, skip_encode,
                                     plan, i, state.layers, value_strip_));
        const PicturePass::Due picture =
            PicturePass::due(state.picture,
                             PictureInputs{.version = snapshot.feed.version,
                                           .view = drawn.view,
                                           .picture = drawn.regions.picture,
                                           .pictureDrawn = drawn.regions.pictureDrawn,
                                           .constants = drawn.picture,
                                           .extent = params.extent,
                                           .skipEncode = skip_encode,
                                           .layers = drawn.layers,
                                           .valueStrip = drawn.valueStrip,
                                           .layersBase = drawn.layersBase,
                                           .markers = drawn.markers},
                             all);
        // The padding check runs with a due picture pass that draws its whole part, into this
        // frame's slot. A redraw of the marker rects alone keeps the tile map of the last check
        // (nothing it reads changed) and draws the marks from it.
        if (picture.record && drawn.inspect.tiles != nullptr &&
            !drawn.regions.pictureDrawn.empty()) {
            if (!picture.parts.empty()) {
                drawn.inspect.counter = nullptr;
            } else {
                if (!padding_slot) {
                    padding_slot = nextSlot();
                }
                drawn.inspect.counterOffset = *padding_slot * READBACK_SLOT_BYTES;
            }
        } else {
            drawn.inspect.tiles = nullptr;
        }
        RulerPass::Part ruler_part =
            RulerPass::due(state.rulers,
                           RulerInputs{.constants = drawn.rulers,
                                       .extent = params.extent,
                                       .skipEncode = skip_encode,
                                       .glyphs = numbered(drawn.rulers) ? strip : nullptr},
                           strips, all);
        // The glyph strip, decided before the bands are recorded.
        RulerPass::GlyphBinding glyphs;
        if (ruler_part != RulerPass::Part::None) {
            glyphs = rulers_.frameGlyphs(glyphs_, *context_, strip, snapshot.params.overlay.numbers,
                                         drawn.rulers, ruler_part);
            countFailure(glyphs);
            if (glyphs.upload) {  // later panels bind the new strip
                glyph_upload = std::move(glyphs.upload);
                before_upload = glyphs_;
                RulerPass::uploaded(glyphs_, strip, glyphs.image);
            } else if (glyph_upload && glyphs.image != nullptr) {
                ruler_part = RulerPass::Part::Bands;  // every label redrawn with the new strip
            }
        }
        if (!picture.record && ruler_part == RulerPass::Part::None) {
            continue;
        }
        const bool binds_upload = glyph_upload && glyphs.image != nullptr;
        Recording recording = record(
            targets_.output, targets_.lines[i], params.extent, snapshot.feed.frames, drawn,
            picture.record, picture.buildLines, picture.parts, ruler_part, std::move(glyphs.image),
            snapshot.feed.version, i, static_cast<std::uint32_t>(rects.size()));
        upload_bound = upload_bound || (binds_upload && recording.rulers);
        value_bound = value_bound || (picture.record && drawn.valueStrip != 0 &&
                                      !drawn.regions.pictureDrawn.empty());
        if (recording.commands) {
            parts.push_back(std::move(recording.commands));
        }
    }
    if (value_upload && value_bound) {
        parts.insert(parts.begin(), std::move(value_upload));
        std::lock_guard lock(recorded_mutex_);
        ++recorded_.valueStripUploads;
    } else if (value_upload) {
        value_strip_ = before_values;
    }
    // The upload counts only when a record binds it: else the next due frame uploads.
    if (upload_bound) {
        parts.insert(parts.begin(), std::move(glyph_upload));
        std::lock_guard lock(recorded_mutex_);
        ++recorded_.glyphUploads;
    } else if (glyph_upload) {
        glyphs_ = std::move(before_upload);
    }
    // The pin, from the FrameSet the panels drew: recorded until the handler got this FrameSet's
    // and pin's readback. The padding check's counter shares the frame's slot.
    Completion completed;
    const std::optional<ProbePlan> probe = probePlan(snapshot.feed.frames, snapshot.params);
    std::optional<ReadbackKey> pin;
    if (probe) {
        const ReadbackKey key{.version = snapshot.feed.version,
                              .index = snapshot.feed.frames.index,
                              .epoch = snapshot.feed.frames.epoch,
                              .streamSet = snapshot.feed.frames.streamSet,
                              .pixel = probe->pixel};
        std::lock_guard keys(key_mutex_);
        if (pin_key_ != key) {
            pin_key_ = key;
            pin_delivered_ = false;
        }
        if (!pin_delivered_) {
            pin = key;
        }
    } else {
        std::lock_guard keys(key_mutex_);
        pin_key_.reset();
        pin_delivered_ = false;
    }
    if (pin || padding_slot) {
        ReadbackRecord readback = readbackRecord(snapshot.feed.frames, pin ? probe : std::nullopt,
                                                 padding_slot.has_value(),
                                                 padding_slot ? *padding_slot : nextSlot(), pin);
        if (readback.commands) {
            parts.push_back(std::move(readback.commands));
        }
        completed = std::move(readback.completed);
    }
    if (parts.empty()) {
        return dvv::gpu::PresentFrame{.record = {}, .image = targets_.output, .completed = {}};
    }
    return dvv::gpu::PresentFrame{.record = joined(std::move(parts)),
                                  .image = targets_.output,
                                  .completed = std::move(completed)};
}

dvv::Result<dvv::gpu::PresentFrame> ViewRenderer::render(dvv::platform::PixelSize extent,
                                                         const FrameSet& frames,
                                                         const ViewParams& params) {
    const PanelPlan plan = panelPlan(frames, params);
    const std::vector<dvv::media::PixelRect> rects =
        panelRects(extent, panelGrid(extent, plan.count));
    DVV_TRY(Targets targets, makeTargets(extent, rects.size()));
    std::vector<dvv::gpu::RecordFunction> parts;
    // Its own layer caches (frame()'s stay), and one strip upload every panel binds.
    std::array<LayerCache, MAX_PANELS> layers{};
    RulerPass::GlyphBinding strip;
    bool upload_bound = false;
    // The value strip, uploaded on every call with the values on.
    PicturePass::StripUpload value_upload;
    PicturePass::ValueStrip values;
    if (valuesOn(params)) {
        value_upload = picture_.uploadValueStrip(*context_, params.inspect.glyphs);
        countValueFailure(value_upload);
        values = {.source = params.inspect.glyphs, .image = value_upload.image, .serial = 1};
    }
    bool value_bound = false;
    std::optional<std::uint64_t> padding_slot;  // the readback slot of the padding check
    for (std::uint32_t i = 0; i < rects.size(); ++i) {
        DVV_TRY(Drawn drawn,
                prepare(rects[i], frames, params, false, plan, i, layers.at(i), values));
        if (drawn.inspect.tiles != nullptr && !drawn.regions.pictureDrawn.empty()) {
            if (!padding_slot) {
                padding_slot = nextSlot();
            }
            drawn.inspect.counterOffset = *padding_slot * READBACK_SLOT_BYTES;
        } else {
            drawn.inspect.tiles = nullptr;
        }
        value_bound = value_bound || (drawn.valueStrip != 0 && !drawn.regions.pictureDrawn.empty());
        std::shared_ptr<dvv::gpu::GpuImage> glyphs = strip.image;
        if (glyphs == nullptr) {  // none uploaded yet, or failed: clears the numbers
            strip = rulers_.renderGlyphs(*context_, params.overlay.glyphs, drawn.rulers);
            countFailure(strip);
            glyphs = strip.image;
        } else if (!numbered(drawn.rulers)) {
            glyphs = nullptr;
        }
        const bool binds = glyphs != nullptr;
        Recording recording =
            record(targets.output, targets.lines[i], extent, frames, drawn, true,
                   readsLines(drawn.picture), {}, RulerPass::Part::Bands, std::move(glyphs),
                   std::nullopt, i, static_cast<std::uint32_t>(rects.size()));
        upload_bound = upload_bound || (binds && recording.rulers);
        if (recording.commands) {
            parts.push_back(std::move(recording.commands));
        }
    }
    if (upload_bound && strip.upload) {
        parts.insert(parts.begin(), std::move(strip.upload));
        std::lock_guard lock(recorded_mutex_);
        ++recorded_.glyphUploads;
    }
    if (value_bound && value_upload.upload) {
        parts.insert(parts.begin(), std::move(value_upload.upload));
        std::lock_guard lock(recorded_mutex_);
        ++recorded_.valueStripUploads;
    }
    Completion completed;
    const std::optional<ProbePlan> probe = probePlan(frames, params);
    if (probe || padding_slot) {
        ReadbackRecord readback =
            readbackRecord(frames, probe, padding_slot.has_value(),
                           padding_slot ? *padding_slot : nextSlot(), std::nullopt);
        if (readback.commands) {
            parts.push_back(std::move(readback.commands));
        }
        completed = std::move(readback.completed);
    }
    return dvv::gpu::PresentFrame{
        .record = parts.empty() ? dvv::gpu::RecordFunction{} : joined(std::move(parts)),
        .image = std::move(targets.output),
        .completed = std::move(completed)};
}

RecordedInfo ViewRenderer::lastRecorded() const {
    std::lock_guard lock(recorded_mutex_);
    return recorded_;
}

dvv::Result<std::unique_ptr<dvv::gpu::Presenter>> createViewPresenter(
    ViewRenderer& renderer, dvv::MoveOnlyFunction<ViewSnapshot()> snapshot,
    dvv::gpu::PresenterOptions options, bool fail_for_test) {
    return dvv::gpu::createPresenter(
        *renderer.context(), std::move(options),
        [snapshot = std::move(snapshot), renderer = &renderer,
         fail = fail_for_test](const dvv::gpu::PresentFrameParams& params) mutable {
            // Presenter thread: one read of the feed, then one of the params, then record (design
            // §5.3).
            dvv::Result<dvv::gpu::PresentFrame> frame = renderer->frame(params, snapshot());
            if (fail && frame) {
                // Test only: the renderer's record function is dropped unrun (as after a
                // failed acquire); the failed recording fails the presenter.
                frame->record = [](dvv::gpu::CommandRecorder& /*recorder*/) {
                    throw std::runtime_error("injected presenter failure");
                };
            }
            return frame;
        });
}

}  // namespace rawreel
