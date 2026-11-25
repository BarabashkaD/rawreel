#include "render/viewport_renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <mutex>  // frame.h no longer brings it
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "dvv/base/log.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/glyph_strip_vulkan.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/display.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/frame_vulkan.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "rawreel_shaders/composite.h"
#include "rawreel_shaders/grid_lines.h"
#include "rawreel_shaders/rulers.h"
#include "render/composite_constants.h"
#include "render/pass_regions.h"
#include "render/ruler_constants.h"

using dvv::media::resolveView;
using dvv::media::ViewTransform;

namespace rawreel {

static_assert(dvv::media::DISPLAY_IMAGE_FORMAT == VK_FORMAT_R16G16B16A16_SFLOAT,
              "shaders/composite.slang declares the display binding as rgba16f");

namespace {

// Whether a grid level is drawn: the picture pass reads the line table only then.
bool gridDrawn(const CompositeConstants& constants) noexcept {
    return ((constants.levelShape[0] | constants.levelShape[1] | constants.levelShape[2]) &
            LEVEL_DRAWN) != 0;
}

// Whether a ruler band draws numbers: only then does the ruler pass read the glyph strip.
bool numbered(const std::array<RulerConstants, 2>& bands) noexcept {
    return ((bands[0].flags | bands[1].flags) & RULER_NUMBERS) != 0;
}

void clearNumbers(std::array<RulerConstants, 2>& bands) noexcept {
    for (RulerConstants& band : bands) {
        band.flags &= ~RULER_NUMBERS;
    }
}

}  // namespace

// The caller's epoch check is what separates stacks: an old and a new context on the same GPU
// share the DeviceId, and every generation is FIRST_GENERATION in v1.0 (design v2 D7). The
// device/generation comparison only rejects an image of another physical device.
dvv::gpu::GpuImage* ViewportRenderer::displayImageOf(const dvv::media::ReadLease& lease,
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

dvv::Result<std::unique_ptr<ViewportRenderer>> ViewportRenderer::create(
    std::shared_ptr<dvv::gpu::ExecutionContext> context, std::uint64_t epoch) {
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
            dvv::gpu::createComputePipeline(
                *context, rawreel_shaders::composite(),
                static_cast<std::uint32_t>(sizeof(CompositeConstants)),
                dvv::gpu::ComputeBindings{.storageImages = MAX_STREAMS + 2}));
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline,
            dvv::gpu::createComputePipeline(*context, rawreel_shaders::gridLines(),
                                            static_cast<std::uint32_t>(sizeof(CompositeConstants)),
                                            dvv::gpu::ComputeBindings{.storageImages = 1}));
    DVV_TRY(std::shared_ptr<dvv::gpu::ComputePipeline> ruler_pipeline,
            dvv::gpu::createComputePipeline(*context, rawreel_shaders::rulers(),
                                            static_cast<std::uint32_t>(sizeof(RulerConstants)),
                                            dvv::gpu::ComputeBindings{.storageImages = 2}));
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> dummy,
            dvv::gpu::createImage(*context, {.width = 1,
                                             .height = 1,
                                             .format = dvv::media::DISPLAY_IMAGE_FORMAT,
                                             .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                             .name = "composite dummy"}));
    // The ruler pass's glyph binding is rgba8: the RGBA16F dummy must not be bound there.
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy,
            dvv::gpu::createImage(*context, {.width = 1,
                                             .height = 1,
                                             .format = VK_FORMAT_R8G8B8A8_UNORM,
                                             .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                             .name = "glyph dummy"}));
    return std::make_unique<ViewportRenderer>(
        Private{}, std::move(context), epoch, std::move(pipeline), std::move(lines_pipeline),
        std::move(ruler_pipeline), std::move(dummy), std::move(glyph_dummy));
}

ViewportRenderer::ViewportRenderer(Private /*key*/,
                                   std::shared_ptr<dvv::gpu::ExecutionContext> context,
                                   std::uint64_t epoch,
                                   std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                                   std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline,
                                   std::shared_ptr<dvv::gpu::ComputePipeline> ruler_pipeline,
                                   std::shared_ptr<dvv::gpu::GpuImage> dummy,
                                   std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy) noexcept
    : context_(std::move(context)),
      epoch_(epoch),
      pipeline_(std::move(pipeline)),
      lines_pipeline_(std::move(lines_pipeline)),
      ruler_pipeline_(std::move(ruler_pipeline)),
      dummy_(std::move(dummy)),
      glyph_dummy_(std::move(glyph_dummy)) {}

ViewportRenderer::GlyphBinding ViewportRenderer::uploadGlyphs(
    const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip) {
    if (strip == glyph_failed_) {
        return {};  // failed before: only another strip is tried
    }
    dvv::Result<dvv::gpu::GlyphStripUpload> upload = dvv::gpu::uploadGlyphStrip(*context_, *strip);
    if (!upload) {
        glyph_failed_ = strip;
        {
            std::lock_guard lock(recorded_mutex_);
            ++recorded_.glyphFailures;
        }
        DVV_LOG_WARN("viewport: the ruler glyph strip did not upload, no ruler numbers: {}",
                     upload.error().describe());
        return {};
    }
    return {.image = std::move(upload->image), .upload = std::move(upload->record)};
}

dvv::Result<ViewportRenderer::Targets> ViewportRenderer::makeTargets(
    dvv::platform::PixelSize extent) const {
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> output,
            dvv::gpu::createImage(
                *context_, {.width = extent.width,
                            .height = extent.height,
                            .format = VK_FORMAT_R8G8B8A8_UNORM,
                            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            .name = "viewport composite"}));
    // Row 0 holds P's columns, row 1 its rows (shaders/grid_lines.slang); P lies in the output.
    // R32_UINT storage images are mandatory in Vulkan, so no other format is needed.
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> lines,
            dvv::gpu::createImage(*context_, {.width = std::max({extent.width, extent.height, 1U}),
                                              .height = 2,
                                              .format = VK_FORMAT_R32_UINT,
                                              .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                              .name = "viewport grid lines"}));
    return Targets{.output = std::move(output), .lines = std::move(lines)};
}

ViewportRenderer::Drawn ViewportRenderer::prepare(dvv::platform::PixelSize extent,
                                                  const FrameSet& frames,
                                                  const DisplayParams& params, bool skip_encode) {
    Drawn drawn;
    if (frames.epoch == epoch_ && frames.count >= 1 && frames.count <= MAX_STREAMS) {
        drawn.image = displayImageOf(frames.views[0], frames.epoch);  // null: background only
    }
    drawn.index = drawn.image != nullptr ? frames.index : -1;
    const dvv::platform::PixelSize image_size =
        drawn.image != nullptr
            ? dvv::platform::PixelSize{drawn.image->width(), drawn.image->height()}
            : dvv::platform::PixelSize{};
    // P for the view; passRegions owns it from here on.
    const dvv::platform::PixelSize picture =
        dvv::media::pictureRegion(extent, params.overlay.strips).size();
    drawn.view = resolveView(params.view, picture, image_size);
    drawn.regions = passRegions(extent, params.overlay.strips, drawn.view);
    drawn.composite = compositeConstants(params, drawn.view, drawn.regions, skip_encode);
    const std::optional<dvv::media::ImagePixel> cursor =
        params.cursor ? dvv::media::cursorPixel(drawn.view, drawn.regions.picture, *params.cursor)
                      : std::nullopt;
    drawn.rulers = rulerConstants(params.overlay, drawn.view, drawn.regions, cursor, skip_encode);
    return drawn;
}

ViewportRenderer::Recording ViewportRenderer::record(const Targets& targets,
                                                     dvv::platform::PixelSize extent,
                                                     const FrameSet& frames, const Drawn& drawn,
                                                     bool picture, bool build_lines,
                                                     RulerPass rulers, GlyphBinding glyphs,
                                                     std::optional<std::uint64_t> version) {
    const dvv::media::PixelRect region = drawn.regions.picture;
    const dvv::media::PixelRect dispatched = drawn.regions.pictureDrawn;
    const bool picture_dispatched = picture && !dispatched.empty();
    // The prepass (one thread per column and per row of P) builds the line table before the
    // picture pass when `build_lines`; else the picture pass reads the table in place.
    const bool prepass = picture_dispatched && build_lines;
    // The part of each band the ruler pass dispatches: the shader places every pixel by its
    // output position, so any part of a band draws as the whole band does there.
    std::array<RulerConstants, 2> bands = drawn.rulers;
    std::array<dvv::media::PixelRect, 2> band_parts{};
    for (std::size_t b = 0; b < bands.size(); ++b) {
        const dvv::media::PixelRect part = rulers == RulerPass::Rulers
                                               ? drawn.regions.bandRulers[b]
                                               : dvv::media::PixelRect{.x = bands[b].regionX,
                                                                       .y = bands[b].regionY,
                                                                       .width = bands[b].regionW,
                                                                       .height = bands[b].regionH};
        if (rulers != RulerPass::None && !part.empty()) {
            band_parts[b] = part;
        }
        bands[b].regionX = band_parts[b].x;
        bands[b].regionY = band_parts[b].y;
        bands[b].regionW = band_parts[b].width;  // 0: not dispatched below
        bands[b].regionH = band_parts[b].height;
    }
    const bool rulers_dispatched = !band_parts[0].empty() || !band_parts[1].empty();
    if (!picture_dispatched && !rulers_dispatched) {
        return {};
    }
    if (!rulers_dispatched) {
        glyphs = {};  // nothing reads the strip
    }
    const bool uploads = static_cast<bool>(glyphs.upload);
    {
        std::lock_guard lock(recorded_mutex_);
        // Any record: the layout it drew into and what its ruler pass dispatched. A due picture
        // pass: what P shows (nothing when P is empty). The ruler rects are exact on every
        // record: a ruler pass that is not due has unchanged constants, and so unchanged rects,
        // and hidden rulers have empty ones.
        recorded_.epoch = epoch_;
        recorded_.extent = extent;
        recorded_.picture = region;
        recorded_.rulers = drawn.regions.rulers;
        recorded_.rulerDispatched = band_parts;
        recorded_.rulerNumbers = std::ranges::any_of(bands, [](const RulerConstants& band) {
            return band.regionW != 0 && band.regionH != 0 && (band.flags & RULER_NUMBERS) != 0;
        });
        if (picture) {
            recorded_.index = picture_dispatched ? drawn.index : -1;
            recorded_.zoom = drawn.view.zoom();
            recorded_.origin = drawn.view.origin();
            recorded_.mode = drawn.view.mode();
            recorded_.pictureDrawn = dispatched;
            if (version) {
                recorded_.version = *version;
            }
        }
        if (picture_dispatched) {
            ++recorded_.pictureRecords;
            recorded_.pictureFlags = drawn.composite.flags;
        }
        if (rulers_dispatched) {
            ++recorded_.rulerRecords;
        }
        if (prepass) {
            ++recorded_.linePasses;
        }
        if (uploads) {
            ++recorded_.glyphUploads;
        }
    }
    const CompositeConstants constants = drawn.composite;
    // The picture pass dispatches the drawn part of P; the prepass covers all of P, so the line
    // image stays indexed by P pixel.
    const std::uint32_t groups_x = picture_dispatched ? (dispatched.width + 7) / 8 : 0;
    const std::uint32_t groups_y = (dispatched.height + 7) / 8;
    const std::uint32_t line_groups =
        prepass ? (std::max(region.width, region.height) + 63) / 64 : 0;
    // Everything the GPU touches stays alive until the submission has completed (M1a resource
    // rule): the pipelines, the targets, the dummies, the bound glyph strip, the upload (it holds
    // its staging buffer), and with the picture pass a share of the FrameSet's leases, which keeps
    // the display image (`image`) in the store. A ruler-only record holds neither: the ruler pass
    // never reads the image. The presenter releases the function on its own thread.
    // CommandRecorder puts a barrier before every dispatch.
    dvv::gpu::RecordFunction commands =
        [pipeline = pipeline_, lines_pipeline = lines_pipeline_, ruler_pipeline = ruler_pipeline_,
         targets, dummy = dummy_,
         glyph_image = glyphs.image != nullptr ? std::move(glyphs.image) : glyph_dummy_,
         upload = std::move(glyphs.upload), image = picture_dispatched ? drawn.image : nullptr,
         leases = picture_dispatched ? frames.share() : FrameSet{}, constants, groups_x, groups_y,
         line_groups, bands](dvv::gpu::CommandRecorder& recorder) mutable {
            static_cast<void>(leases);  // held only for their lifetime
            if (groups_x != 0) {
                const std::span<const std::byte> pushed = std::as_bytes(std::span(&constants, 1));
                if (line_groups != 0) {
                    const std::array<dvv::gpu::GpuImage*, 1> line_table{targets.lines.get()};
                    recorder.dispatch(*lines_pipeline, pushed, line_table,
                                      dvv::gpu::GroupCount{.x = line_groups, .y = 2});
                }
                const std::array<dvv::gpu::GpuImage*, MAX_STREAMS + 2> bound{
                    image != nullptr ? image : dummy.get(), targets.output.get(),
                    targets.lines.get()};
                recorder.dispatch(*pipeline, pushed, bound,
                                  dvv::gpu::GroupCount{.x = groups_x, .y = groups_y});
            }
            if (upload) {
                upload(recorder);  // the strip in glyph_image, before the ruler pass reads it
            }
            const std::array<dvv::gpu::GpuImage*, 2> ruler_bound{targets.output.get(),
                                                                 glyph_image.get()};
            for (const RulerConstants& band : bands) {
                if (band.regionW == 0 || band.regionH == 0) {
                    continue;
                }
                recorder.dispatch(
                    *ruler_pipeline, std::as_bytes(std::span(&band, 1)), ruler_bound,
                    dvv::gpu::GroupCount{.x = (band.regionW + 7) / 8, .y = (band.regionH + 7) / 8});
            }
        };
    return Recording{.commands = std::move(commands), .uploads = uploads};
}

ViewportRenderer::LineInputs ViewportRenderer::LineInputs::of(
    const CompositeConstants& constants) noexcept {
    LineInputs inputs{.viewportW = constants.viewportW,
                      .viewportH = constants.viewportH,
                      .imageW = constants.imageW,
                      .imageH = constants.imageH,
                      .zoom = constants.zoom,
                      .offsetX = constants.offsetX,
                      .offsetY = constants.offsetY,
                      .levelShape = constants.levelShape};
    for (std::uint32_t& shape : inputs.levelShape) {
        shape &= ~LEVEL_ADAPTIVE;
    }
    return inputs;
}

bool ViewportRenderer::RulerInputs::equalButCursor(const RulerInputs& other) const noexcept {
    RulerInputs mine = *this;
    RulerInputs theirs = other;
    for (std::size_t b = 0; b < mine.constants.size(); ++b) {
        mine.constants[b].flags &= ~(RULER_CURSOR | RULER_LIT);
        mine.constants[b].cursor = 0;
        mine.constants[b].litTick = 0;
        theirs.constants[b].flags &= ~(RULER_CURSOR | RULER_LIT);
        theirs.constants[b].cursor = 0;
        theirs.constants[b].litTick = 0;
    }
    return mine == theirs;
}

dvv::Result<dvv::gpu::PresentFrame> ViewportRenderer::frame(
    const dvv::gpu::PresentFrameParams& params, const DisplaySnapshot& snapshot) {
    const bool skip_encode = params.encoding == dvv::gpu::PresentEncoding::Srgb;
    if (skip_encode && !logged_srgb_) {
        logged_srgb_ = true;
        DVV_LOG_WARN(
            "viewport: the presenter applies the sRGB encode; the compositor skips its own");
    }
    // A dropped frame's commands never ran, and a new output holds nothing: both passes run.
    bool all = !params.previousFrameCompleted;
    if (!params.previousFrameCompleted) {
        // Its glyph upload may not have run either: the strip is uploaded again before it is bound
        // (a dropped record held its own share of the image).
        glyph_source_.reset();
        glyph_image_.reset();
    }
    if (!targets_.output || params.extent != extent_) {
        DVV_TRY(targets_, makeTargets(params.extent));
        extent_ = params.extent;
        all = true;
    }
    Drawn drawn = prepare(params.extent, snapshot.frames, snapshot.params, skip_encode);
    const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip = snapshot.params.overlay.glyphs;
    const PictureInputs picture{.version = snapshot.version,
                                .view = drawn.view,
                                .picture = drawn.regions.picture,
                                .pictureDrawn = drawn.regions.pictureDrawn,
                                .constants = drawn.composite,
                                .extent = params.extent,
                                .skipEncode = skip_encode};
    // A strip no band draws is no ruler input.
    const RulerInputs rulers{.constants = drawn.rulers,
                             .extent = params.extent,
                             .skipEncode = skip_encode,
                             .glyphs = numbered(drawn.rulers) ? strip : nullptr};
    const bool strips =
        snapshot.params.overlay.strips.top != 0 || snapshot.params.overlay.strips.left != 0;
    const bool record_picture = all || picture_inputs_ != picture;
    const bool record_rulers = strips && (all || ruler_inputs_ != rulers);
    // Only the cursor moved (its marker, the lit tick): the ruler pixels alone. Any other change
    // redraws the whole bands, whose background changes only together with the ruler rects.
    RulerPass ruler_pass = RulerPass::None;
    if (record_rulers) {
        ruler_pass = !all && ruler_inputs_ && ruler_inputs_->equalButCursor(rulers)
                         ? RulerPass::Rulers
                         : RulerPass::Bands;
    }
    // The glyph strip, decided before the bands are recorded: uploaded again when it is another
    // strip than the uploaded one (none after a dropped frame); a new output keeps it. A failed
    // upload draws the bands without numbers.
    GlyphBinding glyphs;
    if (record_rulers && numbered(drawn.rulers)) {
        if (strip != glyph_source_) {
            glyphs = uploadGlyphs(strip);
            if (glyphs.image == nullptr) {
                clearNumbers(drawn.rulers);
            } else {
                ruler_pass = RulerPass::Bands;  // every label redrawn with the new strip
            }
        } else {
            glyphs.image = glyph_image_;
        }
    } else if (record_rulers && (strip == nullptr || !snapshot.params.overlay.numbers)) {
        glyph_source_.reset();  // a recording frame still holds its own share of the image
        glyph_image_.reset();
    }
    const std::shared_ptr<dvv::gpu::GpuImage> uploaded = glyphs.upload ? glyphs.image : nullptr;
    // The line table outlives the picture pass that built it: a picture pass rebuilds it only when
    // its inputs changed or the last build may not have run (a dropped frame, new targets).
    // The picture pass reads the table when it dispatches and a grid level is drawn.
    const bool reads_lines =
        record_picture && !drawn.regions.pictureDrawn.empty() && gridDrawn(drawn.composite);
    const LineInputs line_key = LineInputs::of(drawn.composite);
    const bool build_lines = reads_lines && (all || line_inputs_ != line_key);
    if (record_picture) {
        line_inputs_ = reads_lines ? std::optional<LineInputs>(line_key) : std::nullopt;
    }
    picture_inputs_ = picture;
    ruler_inputs_ = rulers;
    if (!record_picture && !record_rulers) {
        return dvv::gpu::PresentFrame{.record = {}, .image = targets_.output};
    }
    Recording recording = record(targets_, params.extent, snapshot.frames, drawn, record_picture,
                                 build_lines, ruler_pass, std::move(glyphs), snapshot.version);
    if (recording.uploads) {  // the upload counts only when the record carries it
        glyph_source_ = strip;
        glyph_image_ = uploaded;
    }
    return dvv::gpu::PresentFrame{.record = std::move(recording.commands),
                                  .image = targets_.output};
}

dvv::Result<dvv::gpu::PresentFrame> ViewportRenderer::render(dvv::platform::PixelSize extent,
                                                             const FrameSet& frames,
                                                             const DisplayParams& params) {
    DVV_TRY(Targets targets, makeTargets(extent));
    Drawn drawn = prepare(extent, frames, params, false);
    GlyphBinding glyphs;
    if (numbered(drawn.rulers)) {
        glyphs = uploadGlyphs(params.overlay.glyphs);
        if (glyphs.image == nullptr) {
            clearNumbers(drawn.rulers);
        }
    }
    Recording recording = record(targets, extent, frames, drawn, true, gridDrawn(drawn.composite),
                                 RulerPass::Bands, std::move(glyphs), std::nullopt);
    return dvv::gpu::PresentFrame{.record = std::move(recording.commands),
                                  .image = std::move(targets.output)};
}

RecordedInfo ViewportRenderer::lastRecorded() const {
    std::lock_guard lock(recorded_mutex_);
    return recorded_;
}

dvv::Result<std::unique_ptr<dvv::gpu::Presenter>> createViewportPresenter(
    ViewportRenderer& renderer, std::shared_ptr<DisplayState> display,
    dvv::gpu::PresenterOptions options, bool fail_for_test) {
    return dvv::gpu::createPresenter(
        *renderer.context(), std::move(options),
        [display = std::move(display), renderer = &renderer,
         fail = fail_for_test](const dvv::gpu::PresentFrameParams& params) {
            // Presenter thread: one consistent read of the state, then record (design §5.3).
            const DisplaySnapshot snapshot = display->snapshot();
            dvv::Result<dvv::gpu::PresentFrame> frame = renderer->frame(params, snapshot);
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
