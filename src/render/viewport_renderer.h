#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include <vulkan/vulkan_core.h>

#include "dvv/base/error.h"
#include "dvv/gpu/compute_pipeline.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/gpu/resources.h"
#include "dvv/platform/native_window.h"
#include "render/display_state.h"

namespace rawreel {

// What the last recorded composite shows (design M3 v2 §4.4; selftests `player` and `lost`).
struct RecordedInfo {
    std::uint64_t epoch = 0;            // the renderer's epoch; 0 before the first record
    std::int64_t index = -1;            // the composited FrameSet's index; -1: background only
    dvv::platform::PixelSize extent{};  // the output extent of that record
    double zoom = 0.0;  // ViewTransform::fit(extent, image size).zoom(); 0: no image
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
    // createComputePipeline and createImage pass through (DeviceLost on a lost context).
    [[nodiscard]] static dvv::Result<std::unique_ptr<ViewportRenderer>> create(
        std::shared_ptr<dvv::gpu::ExecutionContext> context, std::uint64_t epoch);
    ViewportRenderer(Private key, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                     std::uint64_t epoch, std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                     std::shared_ptr<dvv::gpu::GpuImage> dummy) noexcept;

    // The PresentFrame for the presenter: records the composite of snapshot.frames.views[0] with
    // ViewTransform::fit(info.extent, display image size) and snapshot.params. Records again only
    // when the snapshot version, the extent or the sRGB rule changed, or
    // !info.previousFrameCompleted; otherwise the same image and no commands. FrameSets of another
    // epoch (or without a usable display view) draw the background only. The record function holds
    // the FrameSet's leases until the submission has completed.
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> frame(const dvv::gpu::PresentFrameInfo& info,
                                                            const DisplaySnapshot& snapshot);
    // Tests: always records, into a new image of `extent`, and returns it (no sRGB rule).
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> render(dvv::platform::PixelSize extent,
                                                             const FrameSet& frames,
                                                             const CompositeParams& params);
    // The last recorded composite (frame() or render()); RecordedInfo{} before the first. Test use
    // only (selftests); read under recorded_mutex_.
    [[nodiscard]] RecordedInfo lastRecorded() const;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }

private:
    [[nodiscard]] dvv::Result<std::shared_ptr<dvv::gpu::GpuImage>> makeOutput(
        dvv::platform::PixelSize extent) const;
    [[nodiscard]] dvv::gpu::RecordFunction recordComposite(
        const std::shared_ptr<dvv::gpu::GpuImage>& output, dvv::platform::PixelSize extent,
        const FrameSet& frames, const CompositeParams& params, bool skip_encode);

    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::uint64_t epoch_;
    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline_;
    std::shared_ptr<dvv::gpu::GpuImage> dummy_;
    std::shared_ptr<dvv::gpu::GpuImage> output_;  // frame() only
    dvv::platform::PixelSize extent_{};
    std::uint64_t version_ = 0;
    bool skip_encode_ = false;
    bool logged_srgb_ = false;
    mutable std::mutex recorded_mutex_;
    RecordedInfo recorded_;
};

// B8G8R8A8_SRGB, R8G8B8A8_SRGB, A8B8G8R8_SRGB_PACK32.
[[nodiscard]] bool isSrgbFormat(VkFormat format) noexcept;

}  // namespace rawreel
