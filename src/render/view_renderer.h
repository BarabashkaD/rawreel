#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "core/frame_set.h"
#include "dvv/base/error.h"
#include "dvv/base/move_only_function.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/gpu_fwd.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/gpu/submission.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/layer_block.h"
#include "render/pass_regions.h"
#include "render/picture_constants.h"
#include "render/picture_pass.h"
#include "render/ruler_constants.h"
#include "render/ruler_pass.h"
#include "render/view_layout.h"
#include "render/view_state.h"

namespace rawreel {

// The most components a probe reads.
inline constexpr std::size_t PROBE_COMPONENTS = 4;

// The pinned pixel's values, read from the Raw view's bytes: per component k < count its code
// (dvv::media::codeOf of the word) and its whole stored word, padding bits included
// (dvv::media::readStoredWord), the same rule as dvv-media's unpack.
struct ProbeSample {
    dvv::media::ImagePixel pixel{};
    std::uint8_t count = 0;  // the format's components, at most 4
    std::array<std::uint32_t, PROBE_COMPONENTS> codes{}, words{};
    bool operator==(const ProbeSample&) const = default;
};

// What one readback-carrying record read back, tagged with the FrameSet it recorded (index,
// epoch and streamSet, from the same snapshot).
struct ViewReadback {
    std::int64_t index = -1;
    std::uint64_t epoch = 0;
    std::uint64_t streamSet = 0;
    std::optional<std::uint64_t> paddingSamples{};  // the padding check's counter (none yet)
    std::optional<ProbeSample> probe{};
    bool operator==(const ViewReadback&) const = default;
};

// What the GUI does with a readback, against the epoch and stream set it shows.
enum class ReadbackUse : std::uint8_t {
    Show,  // the same epoch and stream set
    Hold,  // newer than the GUI's status: kept, checked again when the status catches up
    Drop,  // older
};
// A newer epoch holds and an older one drops whatever the stream sets; within one epoch the
// stream set decides the same way.
[[nodiscard]] ReadbackUse readbackUse(const ViewReadback& r, std::uint64_t epoch,
                                      std::uint64_t stream_set) noexcept;

// The renderer's readback buffer: READBACK_SLOTS slots of READBACK_SLOT_BYTES, the slot of a
// record its readback serial % READBACK_SLOTS. Bytes 0-7 of a slot hold the padding counter,
// bytes 8 * (k + 1) onward component k's 8-byte window of the Raw view, from its stored word's
// byteOffset & ~3.
inline constexpr std::uint32_t READBACK_SLOTS = 4;
inline constexpr std::uint64_t READBACK_SLOT_BYTES = 64;
inline constexpr std::uint64_t READBACK_WINDOW_BYTES = 8;

// The markers one panel's last due picture pass drew: the cells they frame and the output
// pixels they cover (markerRects, the rects in the order it gives them).
struct PanelMarkers {
    MarkerCells cells{};
    std::vector<dvv::media::PixelRect> rects{};
    bool operator==(const PanelMarkers&) const = default;
};

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
    std::uint32_t pictureFlags = 0;    // the last recorded picture pass's PictureConstants::flags
    std::uint64_t rulerRecords = 0;    // ruler passes recorded
    std::uint64_t linePasses = 0;      // grid prepasses recorded (the line table built)
    std::uint64_t glyphUploads = 0;    // glyph strip uploads recorded
    std::uint64_t glyphFailures = 0;   // glyph strip uploads tried that failed
    std::uint64_t valueStripUploads = 0;   // value strip uploads recorded
    std::uint64_t valueStripFailures = 0;  // value strip uploads tried that failed
    std::uint32_t panels = 1;              // the panels of the last record (panelGrid)
    std::uint64_t layerUploads = 0;        // layer blocks made (each an immutable buffer)
    // Whether panel 0's last due picture pass drew with the inspect pipeline (a layer on but the
    // markers alone, which keep picture.slang and draw in the marker pass).
    bool inspectPipeline = false;
    // The rects the marker pass of panel 0's last due picture pass dispatched; empty: none.
    std::vector<dvv::media::PixelRect> markerParts{};
    // The cells the markers of panel 0's last due picture pass framed.
    MarkerCells markers{};
    // Per panel of the last record (one entry each): the markers its last due picture pass drew;
    // empty while no panel draws one.
    std::vector<PanelMarkers> panelMarkers{};
    // The ViewSnapshot version of the last frame() record whose picture pass was due (render()
    // leaves it).
    std::uint64_t version = 0;
    // The readback slot of the last record that carried a readback (0 before the first).
    std::uint32_t readbackSlot = 0;
    // The rects the last due picture pass of panel 0 dispatched: its drawn part, or after a change
    // in the markers only their rects (PicturePass::Due::parts); empty when it dispatched nothing.
    std::vector<dvv::media::PixelRect> pictureParts{};
    // Picture passes that dispatched only the marker rects (not counted in pictureRecords).
    std::uint64_t markerRecords = 0;
    std::uint64_t paddingPasses = 0;  // padding checks recorded (level 1)
    bool operator==(const RecordedInfo&) const = default;
};

// Presenter-thread object, one per presenter (design §4.4, §5.3 GUI side): the picture pass
// (picture_pass.h) and the ruler pass (ruler_pass.h) over the panels of the view (view_layout.h),
// each panel with its own pass inputs, into one RGBA8 output image (STORAGE | TRANSFER_SRC) and
// the picture pass's line table. Created on the GUI thread, used on the presenter thread
// (frame()), destroyed on the GUI thread after the presenter. frame() and render() are never
// called concurrently; lastRecorded() may be called from any thread.
class ViewRenderer {
    struct Private {
        explicit Private() = default;
    };

public:
    // `epoch`: the Session stackEpoch of `context` (ISessionEvents::stackChanged). Errors of
    // the free createComputePipeline, createImage and createBuffer (the readback buffer, made
    // once per renderer, so a new context after a loss has its own) pass through (DeviceLost on
    // a lost context).
    [[nodiscard]] static dvv::Result<std::unique_ptr<ViewRenderer>> create(
        std::shared_ptr<dvv::gpu::ExecutionContext> context, std::uint64_t epoch);
    ViewRenderer(Private key, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                 std::uint64_t epoch, PicturePass picture, RulerPass rulers,
                 std::shared_ptr<dvv::gpu::GpuBuffer> readback) noexcept;

    // The PresentFrame for the presenter. The picture pass draws
    // snapshot.feed.frames.lease(ViewRole::Display) over P =
    // dvv::media::pictureRegion(params.extent, snapshot.params.overlay.strips) with
    // resolveView(snapshot.params.view, P's size, display image size) (nothing when P is empty);
    // passRegions splits the output: the picture pass dispatches its pictureDrawn, the ruler pass
    // the two bands of rulerConstants around it (the rulers hugging the visible picture), with the
    // cursor mapped to the image pixel by dvv::media::cursorPixel (none outside P). No pixel
    // belongs to two dispatches, so a new frame redraws no ruler. Each pass records again only when
    // its own inputs changed (PictureInputs, RulerInputs: derived values, so a cursor move records
    // only the ruler pass, and a move inside one image pixel records nothing). When the ruler
    // constants differ from the last record's in the cursor fields only, the ruler pass dispatches
    // only the ruler pixels of the bands (PassRegions::bandRulers); any other change (view, P, H,
    // theme, settings, extent, encoding) dispatches the whole bands. Both record, each when it is
    // shown (its region or a band not empty), after a dropped frame
    // (!params.previousFrameCompleted) and into a new output image. Otherwise the same image and no
    // commands. When a grid level is drawn, a picture pass rebuilds the line table first only when
    // its LineInputs differ from the table's, or after a dropped frame, or into new targets; a new
    // FrameSet in the same view reads the table in place. A change in the markers only
    // (PictureInputs::equalButMarkers) dispatches only the old and new markerRects. FrameSets of
    // another epoch (or without a usable display view) draw the background only. A record function
    // with the picture pass holds a share of the FrameSet's leases until the submission has
    // completed. The ruler pass binds the glyph strip (overlay.glyphs) where a band has
    // RULER_NUMBERS, else the glyph dummy. The strip is the view's, one image every panel binds: a
    // due ruler pass with RULER_NUMBERS uploads it when it is another object than the one uploaded
    // last, once per frame, recorded before every panel's commands; a new output keeps the uploaded
    // strip. A dropped frame (!params.previousFrameCompleted) releases the uploaded strip, whose
    // upload may not have run, so it is never bound unfilled. The upload counts (and replaces the
    // uploaded strip, releasing the old one) only when the record carries it: a frame that records
    // nothing leaves the next due frame to upload. A strip that fails to upload logs one warning
    // and the bands draw without numbers; it is not tried again until overlay.glyphs is another
    // object. A due ruler pass releases the uploaded strip when overlay.glyphs is null or the
    // numbers are off. The pin: with inspect.on and inspect.probe inside a Raw view of this epoch
    // (a buffer of this device), the record also copies each component's 8-byte window of the
    // stored word at the pin into the next readback slot, holding a share of the Raw lease until it
    // completed, and the frame's `completed` decodes them for the readback handler, tagged with the
    // snapshot's FrameSet. A frame records the readback until it is delivered: when the FrameSet
    // (version, index, epoch, stream set) or the pin differs from the last readback the handler
    // got. So a dropped frame, a failed submission, a failed map, no handler or a handler that
    // threw records it again; a frame that draws nothing then records the copies alone. The padding
    // check (paddingMarks, a Picture panel with a Raw view of this epoch): a due picture pass that
    // draws first zeroes the counter of the frame's readback slot (the pin's too), clears the tile
    // map and runs padding_check, then draws the marks; `completed` delivers the count as
    // ViewReadback::paddingSamples. A redraw of the marker rects alone runs no check and delivers
    // no count; it draws the marks from the last check's tile map. Off, nothing runs and no
    // buffer is touched.
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> frame(
        const dvv::gpu::PresentFrameParams& params, const ViewSnapshot& snapshot);
    // Tests: always records both passes, into a new image of `extent`, and returns it (no sRGB
    // rule), as frame() draws them; uploads the glyph strip on every call unless it is the strip
    // that failed last (frame()'s uploaded strip stays). With a pin it always records the
    // readback and sets `completed` (tests call it after the submission).
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> render(dvv::platform::PixelSize extent,
                                                             const FrameSet& frames,
                                                             const ViewParams& params);
    // The function that gets each readback: called from PresentFrame::completed of a successful
    // readback-carrying record (on the presenter thread for frame(), never for a dropped frame
    // or a failed submission). It must not block long, wait for the GUI thread or call
    // setReadbackHandler (it runs under the handler's lock). Once this returns, the old handler
    // is not running and never runs again. Any thread.
    void setReadbackHandler(dvv::MoveOnlyFunction<void(ViewReadback)> handler);
    // The last record (frame() or render(), see RecordedInfo); RecordedInfo{} before the first.
    // Test use only (selftests); read under recorded_mutex_.
    [[nodiscard]] RecordedInfo lastRecorded() const;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] const std::shared_ptr<dvv::gpu::ExecutionContext>& context() const noexcept {
        return context_;
    }
    // The glyph strip image frame() uploaded last and every panel binds, null when none: STORAGE |
    // TRANSFER_DST | TRANSFER_SRC (tests read it back). Presenter thread only, between frames.
    [[nodiscard]] const std::shared_ptr<dvv::gpu::GpuImage>& glyphImage() const noexcept {
        return glyphs_.image;
    }

private:
    // The images one extent draws into: the output and one grid prepass line table per panel.
    struct Targets {
        std::shared_ptr<dvv::gpu::GpuImage> output;
        std::vector<std::shared_ptr<dvv::gpu::GpuImage>> lines;
    };
    [[nodiscard]] dvv::Result<Targets> makeTargets(dvv::platform::PixelSize extent,
                                                   std::size_t panels) const;
    // The panels a frame draws: one Picture panel, or with the Planes panels on and a planes view
    // one per component of its format (at most MAX_PANELS).
    struct PanelPlan {
        PanelKind kind = PanelKind::Picture;
        std::uint32_t count = 1;
        dvv::media::FrameDesc desc{};  // the planes view's frame (Planes only)
    };
    [[nodiscard]] PanelPlan panelPlan(const FrameSet& frames, const ViewParams& params) const;
    // The layer block a panel last bound, its immutable buffer and its serial: the layer uploads
    // so far at its creation (RecordedInfo::layerUploads), never reused, so the picture pass's
    // identity of the block (PictureInputs::layers) and not its device address, which VMA reuses.
    struct LayerCache {
        std::optional<LayerBlock> block;
        std::shared_ptr<dvv::gpu::GpuBuffer> buffer;
        std::uint64_t serial = 0;
        // The serial of the block with its pin and outline fields cleared: kept while only
        // those fields change (PictureInputs::layersBase).
        std::uint64_t baseSerial = 0;
        std::shared_ptr<dvv::gpu::GpuBuffer>
            tiles;  // the padding check's tile map, reused while big enough
    };
    // What one panel's record of a snapshot draws: the display image (null: background only),
    // the regions of both passes, the resolved view and the constants of both passes.
    struct Drawn {
        dvv::gpu::GpuImage* image = nullptr;
        std::int64_t index = -1;
        PassRegions regions{};
        dvv::media::ViewTransform view{};
        PictureConstants picture{};
        std::array<RulerConstants, 2> rulers{};
        InspectBinding inspect{};  // layers null: today's pipeline
        std::uint64_t layers = 0;  // the bound layer block's serial (LayerCache::serial), 0: none
        std::uint64_t valueStrip = 0;  // the bound value strip's serial, 0: none
        std::uint64_t layersBase = 0;  // LayerCache::baseSerial of the bound block, 0: none
        MarkerCells markers{};         // the cells the bound block's markers frame
    };
    // The pass inputs of one panel (frame() only; render() has its own layer caches).
    struct PanelState {
        PicturePass::State picture;
        RulerPass::State rulers;
        LayerCache layers;
    };
    // `index` is the panel's place in `plan`; with a layer on, its layer block is bound from
    // `layers`, made again only when the block's bytes change, and the value strip `values` with
    // the values layer on. A Picture panel has a layer on only with the values on and a planes
    // view of this epoch (the codes), or with markersOn(params.markers) (LAYER_MARKERS only
    // without the values).
    [[nodiscard]] dvv::Result<Drawn> prepare(dvv::media::PixelRect panel, const FrameSet& frames,
                                             const ViewParams& params, bool skip_encode,
                                             const PanelPlan& plan, std::uint32_t index,
                                             LayerCache& layers,
                                             const PicturePass::ValueStrip& values);
    // What record() recorded.
    struct Recording {
        dvv::gpu::RecordFunction commands;  // empty: nothing dispatched
        bool rulers = false;                // a ruler pass is dispatched (it binds the strip)
    };
    // The picture pass over the drawn part of P when `picture` and that part is not empty, then
    // the ruler pass over the non-empty parts of the bands `rulers` names, binding `glyphs` (null:
    // the glyph dummy), whose upload the caller records before every panel's commands; empty
    // commands when nothing is dispatched. The grid prepass
    // rebuilds the line table over P before the picture pass when `build_lines`, which the caller
    // sets only when a grid level is drawn; else the picture pass reads the table in place. A due
    // picture pass sets RecordedInfo::version to `version` (frame()'s snapshot; render() passes
    // none) with the rest of the record.
    // `lines` is the panel's line table; panel `index` 0 of `panels` sets the record's layout
    // fields, every panel the counts. Non-empty `parts` (a change in the markers only) dispatch
    // only those rects and count in markerRecords, not pictureRecords.
    [[nodiscard]] Recording record(const std::shared_ptr<dvv::gpu::GpuImage>& output,
                                   const std::shared_ptr<dvv::gpu::GpuImage>& lines,
                                   dvv::platform::PixelSize extent, const FrameSet& frames,
                                   const Drawn& drawn, bool picture, bool build_lines,
                                   const std::vector<dvv::media::PixelRect>& parts,
                                   RulerPass::Part rulers,
                                   std::shared_ptr<dvv::gpu::GpuImage> glyphs,
                                   std::optional<std::uint64_t> version, std::uint32_t index,
                                   std::uint32_t panels);
    // The display image of a lease, or null when the lease is not a display view (one
    // DISPLAY_IMAGE_FORMAT plane in GENERAL). A display view bound otherwise logs one Error per
    // `epoch` (FrameSet::epoch). Presenter thread only. The image is valid while the lease (or a
    // share of it) lives.
    [[nodiscard]] dvv::gpu::GpuImage* displayImageOf(const dvv::media::ReadLease& lease,
                                                     std::uint64_t epoch);
    // Counts a failed glyph upload of `glyphs` (RecordedInfo::glyphFailures).
    void countFailure(const RulerPass::GlyphBinding& glyphs);
    // Counts a failed value strip upload (RecordedInfo::valueStripFailures).
    void countValueFailure(const PicturePass::StripUpload& upload);

    // The pin's stored words in the Raw view of a FrameSet, tagged with it (probe empty).
    struct ProbePlan {
        ViewReadback tags;
        dvv::media::ImagePixel pixel{};
        std::uint8_t count = 0;
        std::array<dvv::media::StoredWord, PROBE_COMPONENTS> words{};
        const dvv::gpu::GpuBuffer* raw = nullptr;  // valid while the Raw lease lives
    };
    // The plan for params.inspect.probe, none without a pin, a Raw buffer of this device and
    // epoch (TRANSFER_SRC, holding every window), or with the pin outside the frame.
    [[nodiscard]] std::optional<ProbePlan> probePlan(const FrameSet& frames,
                                                     const ViewParams& params) const;
    // The FrameSet and the pin of a readback.
    struct ReadbackKey {
        std::uint64_t version = 0;
        std::int64_t index = -1;
        std::uint64_t epoch = 0;
        std::uint64_t streamSet = 0;
        dvv::media::ImagePixel pixel{};
        bool operator==(const ReadbackKey&) const = default;
    };
    using Completion = dvv::MoveOnlyFunction<void(const dvv::Result<std::uint64_t>&) noexcept>;
    // The readback of `plan` into `slot`: the copies, which hold a share of the Raw lease,
    // and the completion that decodes the slot and calls the handler. `key` (frame() only): a
    // handler call that returned sets pin_delivered_ while pin_key_ is still `key`.
    struct ReadbackRecord {
        dvv::gpu::RecordFunction commands;
        Completion completed;
    };
    // `padding`: the record ran the padding check into the slot, so the completion reads its
    // counter too; no `plan`: no copies (the counter alone).
    [[nodiscard]] ReadbackRecord readbackRecord(const FrameSet& frames,
                                                const std::optional<ProbePlan>& plan, bool padding,
                                                std::uint64_t slot, std::optional<ReadbackKey> key);
    // The next readback slot (RecordedInfo::readbackSlot).
    [[nodiscard]] std::uint64_t nextSlot();
    // The Raw view's buffer of `frames`: none unless it is a buffer of this device, generation
    // and epoch.
    [[nodiscard]] const dvv::gpu::GpuBuffer* rawBufferOf(const FrameSet& frames) const;

    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::uint64_t epoch_;
    PicturePass picture_;
    RulerPass rulers_;
    std::array<PanelState, MAX_PANELS> panels_{};  // frame() only
    RulerPass::Glyphs glyphs_;                     // frame() only: the strip every panel binds
    PicturePass::ValueStrip value_strip_;  // frame() only: the value strip every panel binds
    std::uint64_t value_serial_ = 0;       // the last value strip upload's serial
    std::size_t last_panels_ = 0;          // frame() only: the panels of the last frame
    Targets targets_;                      // frame() only
    dvv::platform::PixelSize extent_{};
    bool logged_srgb_ = false;
    // Presenter thread only (no lock): the epoch whose Error is logged.
    std::optional<std::uint64_t> reported_binding_epoch_;
    std::atomic<bool> reported_raw_mismatch_{false};  // one warning per renderer
    mutable std::mutex recorded_mutex_;
    RecordedInfo recorded_;
    // HostReadback | TRANSFER_DST, READBACK_SLOTS slots; made once, with the renderer.
    std::shared_ptr<dvv::gpu::GpuBuffer> readback_;
    std::uint64_t readback_serial_ = 0;  // readback-carrying records so far
    // Under key_mutex_ (frame() and the completions): the pin frame() last drew (none: no pin),
    // and whether a handler call for that pin returned. A completion that runs after an unpin or
    // another pin delivers nothing, so the next pin records its readback again.
    std::mutex key_mutex_;
    std::optional<ReadbackKey> pin_key_;
    bool pin_delivered_ = false;
    std::mutex handler_mutex_;
    dvv::MoveOnlyFunction<void(ViewReadback)> handler_;  // under handler_mutex_
};

// The view's presenter: dvv::gpu::createPresenter (presenter_vulkan.h) with a frame provider
// that takes one `snapshot` per frame (on the presenter thread) and records through `renderer`.
// `fail_for_test` replaces each record function by one that throws (the injected "presenter"
// fault).
[[nodiscard]] dvv::Result<std::unique_ptr<dvv::gpu::Presenter>> createViewPresenter(
    ViewRenderer& renderer, dvv::MoveOnlyFunction<ViewSnapshot()> snapshot,
    dvv::gpu::PresenterOptions options, bool fail_for_test);

}  // namespace rawreel
