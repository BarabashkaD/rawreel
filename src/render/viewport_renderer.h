#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "dvv/base/error.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/composite_constants.h"
#include "render/display_state.h"
#include "render/pass_regions.h"
#include "render/ruler_constants.h"

namespace rawreel {

// What the last record shows (selftests `player` and `lost`), and how many passes were recorded. A
// record is a frame() or render() that dispatches at least one pass; epoch, extent, picture, rulers
// and rulerDispatched are the last record's, so a ruler-only record into a new output (P below the
// strips) sets them too.
// index and the view fields are those of the last record whose picture pass was due: a record with
// an empty P sets index -1 and the view resolved against it (zoom 0).
struct RecordedInfo {
    std::uint64_t epoch = 0;            // the renderer's epoch; 0 before the first record
    std::int64_t index = -1;            // the shown FrameSet's index; -1: no image drawn
    dvv::platform::PixelSize extent{};  // the output extent of that record
    double zoom = 0.0;                  // the resolved view's zoom; 0: no image
    dvv::media::PointD origin{};        // the resolved view's origin
    dvv::media::ViewMode mode = dvv::media::ViewMode::Fit;  // the resolved view's mode
    dvv::media::PixelRect picture{};  // the picture region P of that record (maybe empty)
    // The part of P the last due picture pass dispatches (maybe empty).
    dvv::media::PixelRect pictureDrawn{};
    // The ruler rects of the last record (all empty with the rulers hidden).
    dvv::media::RulerRects rulers{};
    // What the last record's ruler pass dispatched: the two bands, after a cursor-only change
    // only their rulers (PassRegions::bandRulers), all empty when it dispatched no ruler pass.
    std::array<dvv::media::PixelRect, 2> rulerDispatched{};
    bool rulerNumbers = false;         // whether a band it dispatched draws numbers (RULER_NUMBERS)
    std::uint64_t pictureRecords = 0;  // picture passes recorded (frame() and render())
    std::uint32_t pictureFlags = 0;    // the last recorded picture pass's CompositeConstants::flags
    std::uint64_t rulerRecords = 0;    // ruler passes recorded
    std::uint64_t linePasses = 0;      // grid prepasses recorded (the line table built)
    std::uint64_t glyphUploads = 0;    // glyph strip uploads recorded
    std::uint64_t glyphFailures = 0;   // glyph strip uploads tried that failed
    // The DisplaySnapshot version of the last frame() record whose picture pass was due (render()
    // leaves it).
    std::uint64_t version = 0;
    bool operator==(const RecordedInfo&) const = default;
};

// Presenter-thread object, one per presenter (design §4.4, §5.3 GUI side): the composite
// pipeline and its grid prepass, the ruler pipeline, its RGBA8 output image (STORAGE |
// TRANSFER_SRC), the prepass's R32_UINT line image (max(width, height) x 2 of the output) and one
// 1x1 RGBA16F dummy bound when there is no frame, the glyph strip it uploads (an RGBA8
// image) and a 1x1 RGBA8 glyph dummy bound without numbers. Created on the GUI thread, used on the
// presenter thread (frame()), destroyed on the GUI thread after the presenter. frame() and render()
// are never called concurrently; lastRecorded() may be called from any thread.
class ViewportRenderer {
    struct Private {
        explicit Private() = default;
    };

public:
    // `epoch`: the Session stackEpoch of `context` (ISessionEvents::stackChanged). Errors of
    // the free createComputePipeline and createImage pass through (DeviceLost on a lost context).
    [[nodiscard]] static dvv::Result<std::unique_ptr<ViewportRenderer>> create(
        std::shared_ptr<dvv::gpu::ExecutionContext> context, std::uint64_t epoch);
    ViewportRenderer(Private key, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                     std::uint64_t epoch, std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                     std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline,
                     std::shared_ptr<dvv::gpu::ComputePipeline> ruler_pipeline,
                     std::shared_ptr<dvv::gpu::GpuImage> dummy,
                     std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy) noexcept;

    // The PresentFrame for the presenter. The picture pass draws snapshot.frames.views[0] over
    // P = dvv::media::pictureRegion(params.extent, snapshot.params.overlay.strips) with
    // resolveView(snapshot.params.view, P's size, display image size) (nothing when P is empty);
    // passRegions splits the output: the picture pass dispatches its pictureDrawn, the ruler
    // pass the two bands of rulerConstants around it (the rulers hugging the visible picture),
    // with the cursor mapped to the image pixel by dvv::media::cursorPixel (none outside P). No
    // pixel belongs to two dispatches, so a new frame redraws no ruler. Each pass records again
    // only when its own inputs changed (PictureInputs, RulerInputs: derived values, so a cursor
    // move records only the ruler pass, and a move inside one image pixel records nothing). When
    // the ruler constants differ from the last record's in the cursor fields only, the ruler pass
    // dispatches only the ruler pixels of the bands (PassRegions::bandRulers); any other change
    // (view, P, H, theme, settings, extent, encoding) dispatches the whole bands. Both
    // record, each when it is shown (its region or a band not empty), after a dropped frame
    // (!params.previousFrameCompleted) and into a new output image. Otherwise the same image and no
    // commands. When a grid level is drawn, a picture pass rebuilds the line table first only when
    // its LineInputs differ from the table's, or after a dropped frame, or into new targets; a new
    // FrameSet in the same view reads the table in place. FrameSets of another epoch
    // (or without a usable display view) draw the background only. A record function with the
    // picture pass holds a share of the FrameSet's leases until the submission has completed.
    // The ruler pass binds the glyph strip (overlay.glyphs) where a band has RULER_NUMBERS, else
    // the glyph dummy. A due ruler pass with RULER_NUMBERS uploads the strip first when it is
    // another object than the one uploaded last; a new output keeps the uploaded strip. A dropped
    // frame (!params.previousFrameCompleted) releases the uploaded strip, whose upload may not
    // have run, so it is never bound unfilled. The upload counts (and replaces the uploaded strip,
    // releasing the old one) only when the record carries it: a frame that records nothing leaves
    // the next due frame to upload. A strip that fails to upload logs one warning and the bands
    // draw without numbers; it is not tried again until overlay.glyphs is another object. A due
    // ruler pass releases the uploaded strip when overlay.glyphs is null or the numbers are off.
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> frame(
        const dvv::gpu::PresentFrameParams& params, const DisplaySnapshot& snapshot);
    // Tests: always records both passes, into a new image of `extent`, and returns it (no sRGB
    // rule), as frame() draws them; uploads the glyph strip on every call unless it is the strip
    // that failed last (frame()'s uploaded strip stays).
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> render(dvv::platform::PixelSize extent,
                                                             const FrameSet& frames,
                                                             const DisplayParams& params);
    // The last record (frame() or render(), see RecordedInfo); RecordedInfo{} before the first.
    // Test use only (selftests); read under recorded_mutex_.
    [[nodiscard]] RecordedInfo lastRecorded() const;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] const std::shared_ptr<dvv::gpu::ExecutionContext>& context() const noexcept {
        return context_;
    }
    // The glyph strip image frame() uploaded last and binds, null when none: STORAGE |
    // TRANSFER_DST | TRANSFER_SRC (tests read it back). Presenter thread only, between frames.
    [[nodiscard]] const std::shared_ptr<dvv::gpu::GpuImage>& glyphImage() const noexcept {
        return glyph_image_;
    }

private:
    // The images one extent draws into: the output and the grid prepass's line image.
    struct Targets {
        std::shared_ptr<dvv::gpu::GpuImage> output;
        std::shared_ptr<dvv::gpu::GpuImage> lines;
    };
    [[nodiscard]] dvv::Result<Targets> makeTargets(dvv::platform::PixelSize extent) const;
    // What one record of a snapshot draws: the display image (null: background only), the
    // regions of both passes, the resolved view and the constants of both passes.
    struct Drawn {
        dvv::gpu::GpuImage* image = nullptr;
        std::int64_t index = -1;
        PassRegions regions{};
        dvv::media::ViewTransform view{};
        CompositeConstants composite{};
        std::array<RulerConstants, 2> rulers{};
    };
    // The inputs a pass last recorded; a pass records again when they differ.
    struct PictureInputs {
        std::uint64_t version = 0;
        dvv::media::ViewTransform view{};
        dvv::media::PixelRect picture{};
        dvv::media::PixelRect pictureDrawn{};
        CompositeConstants constants{};
        dvv::platform::PixelSize extent{};
        bool skipEncode = false;
        bool operator==(const PictureInputs&) const = default;
    };
    // What the grid prepass reads (shaders/grid_lines.slang): P's size, the image size, the
    // view's zoom and offset, and per level its step, thickness, halo and drawn bits; a level's
    // colour, opacity and adaptive bit draw in the picture pass only.
    struct LineInputs {
        std::uint32_t viewportW = 0, viewportH = 0;
        std::uint32_t imageW = 0, imageH = 0;
        float zoom = 0.0F;
        std::int32_t offsetX = 0, offsetY = 0;
        std::array<std::uint32_t, 4> levelShape{};  // without LEVEL_ADAPTIVE
        bool operator==(const LineInputs&) const = default;
        [[nodiscard]] static LineInputs of(const CompositeConstants& constants) noexcept;
    };
    struct RulerInputs {
        std::array<RulerConstants, 2> constants{};
        dvv::platform::PixelSize extent{};
        bool skipEncode = false;
        // Owned, so a new strip at a freed strip's address never compares equal.
        std::shared_ptr<const dvv::gpu::GlyphStrip> glyphs;
        bool operator==(const RulerInputs&) const = default;
        // These inputs equal `other` once the cursor fields (RULER_CURSOR, cursor) and the lit
        // tick's (RULER_LIT, litTick) are cleared: every pixel they change lies on a ruler.
        [[nodiscard]] bool equalButCursor(const RulerInputs& other) const noexcept;
    };
    [[nodiscard]] Drawn prepare(dvv::platform::PixelSize extent, const FrameSet& frames,
                                const DisplayParams& params, bool skip_encode);
    // Which part of each band a ruler pass dispatches.
    enum class RulerPass : std::uint8_t {
        None,    // not due
        Bands,   // the whole bands
        Rulers,  // only a cursor field changed: the ruler pixels of each band (bandRulers)
    };
    // The glyph strip a ruler pass binds.
    struct GlyphBinding {
        std::shared_ptr<dvv::gpu::GpuImage> image;  // null: the glyph dummy
        dvv::gpu::RecordFunction upload;  // fills `image`; empty when it holds the strip already
    };
    // uploadGlyphStrip of `strip`; on an error the empty binding, with one warning per strip.
    // The strip that failed last is not tried again: the empty binding at once.
    [[nodiscard]] GlyphBinding uploadGlyphs(
        const std::shared_ptr<const dvv::gpu::GlyphStrip>& strip);
    // What record() recorded.
    struct Recording {
        dvv::gpu::RecordFunction commands;  // empty: nothing dispatched
        bool uploads = false;               // the commands carry the binding's upload
    };
    // The picture pass over the drawn part of P when `picture` and that part is not empty, then
    // the ruler pass over the non-empty parts of the bands `rulers` names, binding `glyphs.image`
    // (or the glyph dummy) and recording `glyphs.upload` before it (only when a band part is
    // dispatched: Recording::uploads); empty commands when nothing is dispatched. The grid prepass
    // rebuilds the line table over P before the picture pass when `build_lines`, which the caller
    // sets only when a grid level is drawn; else the picture pass reads the table in place. A due
    // picture pass sets RecordedInfo::version to `version` (frame()'s snapshot; render() passes
    // none) with the rest of the record.
    [[nodiscard]] Recording record(const Targets& targets, dvv::platform::PixelSize extent,
                                   const FrameSet& frames, const Drawn& drawn, bool picture,
                                   bool build_lines, RulerPass rulers, GlyphBinding glyphs,
                                   std::optional<std::uint64_t> version);
    // The display image of a lease, or null when the lease is not a display view (one
    // DISPLAY_IMAGE_FORMAT plane in GENERAL). A display view bound otherwise logs one Error per
    // `epoch` (FrameSet::epoch). Presenter thread only. The image is valid while the lease (or a
    // share of it) lives.
    [[nodiscard]] dvv::gpu::GpuImage* displayImageOf(const dvv::media::ReadLease& lease,
                                                     std::uint64_t epoch);

    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::uint64_t epoch_;
    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline_;
    std::shared_ptr<dvv::gpu::ComputePipeline> lines_pipeline_;
    std::shared_ptr<dvv::gpu::ComputePipeline> ruler_pipeline_;
    std::shared_ptr<dvv::gpu::GpuImage> dummy_;
    std::shared_ptr<dvv::gpu::GpuImage> glyph_dummy_;  // 1x1 RGBA8, bound without numbers
    // frame() only: the strip glyph_image_ holds, as uploaded by a recorded frame (a dropped
    // frame releases both).
    std::shared_ptr<const dvv::gpu::GlyphStrip> glyph_source_;
    std::shared_ptr<dvv::gpu::GpuImage> glyph_image_;
    // The last strip whose upload failed (warned once, not tried again); held, so its address is
    // not reused.
    std::shared_ptr<const dvv::gpu::GlyphStrip> glyph_failed_;
    Targets targets_;  // frame() only
    dvv::platform::PixelSize extent_{};
    std::optional<PictureInputs> picture_inputs_;  // of the last frame() record of each pass
    std::optional<RulerInputs> ruler_inputs_;
    // The inputs of the line table in targets_.lines: set by each due picture pass of frame() that
    // reads the table (rebuilt or in place), cleared by one that does not (P or its drawn part
    // empty, no grid level drawn).
    std::optional<LineInputs> line_inputs_;
    bool logged_srgb_ = false;
    // Presenter thread only (no lock): the epoch whose Error is logged.
    std::optional<std::uint64_t> reported_binding_epoch_;
    mutable std::mutex recorded_mutex_;
    RecordedInfo recorded_;
};

// The viewport's presenter: dvv::gpu::createPresenter (presenter_vulkan.h) with a frame provider
// that takes one display snapshot per frame and records through `renderer`. `fail_for_test`
// replaces each record function by one that throws (the injected "presenter" fault).
[[nodiscard]] dvv::Result<std::unique_ptr<dvv::gpu::Presenter>> createViewportPresenter(
    ViewportRenderer& renderer, std::shared_ptr<DisplayState> display,
    dvv::gpu::PresenterOptions options, bool fail_for_test);

}  // namespace rawreel
