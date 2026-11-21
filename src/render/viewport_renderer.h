#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "dvv/base/error.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/presenter_vulkan.h"
#include "dvv/platform/pixel_size.h"
#include "render/display_state.h"

namespace rawreel {

// What the last recorded composite shows (design M3 v2 §4.4; selftests `player` and `lost`).
struct RecordedInfo {
    std::uint64_t epoch = 0;            // the renderer's epoch; 0 before the first record
    std::int64_t index = -1;            // the composited FrameSet's index; -1: background only
    dvv::platform::PixelSize extent{};  // the output extent of that record
    double zoom = 0.0;                  // the resolved view's zoom; 0: no image
    PointD origin{};                    // the resolved view's origin
    ViewMode mode = ViewMode::Fit;      // the resolved view's mode
    bool operator==(const RecordedInfo&) const = default;
};

// Presenter-thread object, one per presenter (design §4.4, §5.3 GUI side): the composite
// pipeline, its RGBA8 output image (STORAGE | TRANSFER_SRC) and one 1x1 RGBA16F dummy bound when
// there is no frame. Created on the GUI thread, used on the presenter thread (frame()), destroyed
// on the GUI thread after the presenter. frame() and render() are never called concurrently;
// lastRecorded() may be called from any thread.
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
                     std::shared_ptr<dvv::gpu::GpuImage> dummy) noexcept;

    // The PresentFrame for the presenter: records the composite of snapshot.frames.views[0] with
    // resolveView(snapshot.params.view, params.extent, display image size) and snapshot.params.
    // Records again only when the snapshot version, the extent or the sRGB rule changed, or
    // !params.previousFrameCompleted; otherwise the same image and no commands. FrameSets of
    // another epoch (or without a usable display view) draw the background only. The record
    // function holds a share of the FrameSet's leases until the submission has completed.
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> frame(
        const dvv::gpu::PresentFrameParams& params, const DisplaySnapshot& snapshot);
    // Tests: always records, into a new image of `extent`, and returns it (no sRGB rule).
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> render(dvv::platform::PixelSize extent,
                                                             const FrameSet& frames,
                                                             const CompositeParams& params);
    // The last recorded composite (frame() or render()); RecordedInfo{} before the first. Test use
    // only (selftests); read under recorded_mutex_.
    [[nodiscard]] RecordedInfo lastRecorded() const;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] const std::shared_ptr<dvv::gpu::ExecutionContext>& context() const noexcept {
        return context_;
    }

private:
    [[nodiscard]] dvv::Result<std::shared_ptr<dvv::gpu::GpuImage>> makeOutput(
        dvv::platform::PixelSize extent) const;
    [[nodiscard]] dvv::gpu::RecordFunction recordComposite(
        const std::shared_ptr<dvv::gpu::GpuImage>& output, dvv::platform::PixelSize extent,
        const FrameSet& frames, const CompositeParams& params, bool skip_encode);
    // The display image of a lease, or null when the lease is not a display view (one
    // DISPLAY_IMAGE_FORMAT plane in GENERAL). A display view bound otherwise logs one Error per
    // `epoch` (FrameSet::epoch). Presenter thread only. The image is valid while the lease (or a
    // share of it) lives.
    [[nodiscard]] dvv::gpu::GpuImage* displayImageOf(const dvv::media::ReadLease& lease,
                                                     std::uint64_t epoch);

    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::uint64_t epoch_;
    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline_;
    std::shared_ptr<dvv::gpu::GpuImage> dummy_;
    std::shared_ptr<dvv::gpu::GpuImage> output_;  // frame() only
    dvv::platform::PixelSize extent_{};
    std::uint64_t version_ = 0;
    bool skip_encode_ = false;
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
