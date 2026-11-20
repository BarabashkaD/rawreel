#include "render/viewport_renderer.h"

#include <array>
#include <cstddef>
#include <mutex>  // frame.h no longer brings it
#include <span>
#include <utility>

#include "core/view.h"
#include "dvv/base/log.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/media/display.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/frame_vulkan.h"
#include "rawreel_shaders/composite.h"
#include "render/composite_reference.h"

namespace rawreel {

static_assert(dvv::media::DISPLAY_IMAGE_FORMAT == VK_FORMAT_R16G16B16A16_SFLOAT,
              "shaders/composite.slang declares the display binding as rgba16f");

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
                dvv::gpu::ComputeBindings{.storageImages = MAX_STREAMS + 1}));
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> dummy,
            dvv::gpu::createImage(*context, {.width = 1,
                                             .height = 1,
                                             .format = dvv::media::DISPLAY_IMAGE_FORMAT,
                                             .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                             .name = "composite dummy"}));
    return std::make_unique<ViewportRenderer>(Private{}, std::move(context), epoch,
                                              std::move(pipeline), std::move(dummy));
}

ViewportRenderer::ViewportRenderer(Private /*key*/,
                                   std::shared_ptr<dvv::gpu::ExecutionContext> context,
                                   std::uint64_t epoch,
                                   std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
                                   std::shared_ptr<dvv::gpu::GpuImage> dummy) noexcept
    : context_(std::move(context)),
      epoch_(epoch),
      pipeline_(std::move(pipeline)),
      dummy_(std::move(dummy)) {}

dvv::Result<std::shared_ptr<dvv::gpu::GpuImage>> ViewportRenderer::makeOutput(
    dvv::platform::PixelSize extent) const {
    return dvv::gpu::createImage(
        *context_, {.width = extent.width,
                    .height = extent.height,
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    .name = "viewport composite"});
}

dvv::gpu::RecordFunction ViewportRenderer::recordComposite(
    const std::shared_ptr<dvv::gpu::GpuImage>& output, dvv::platform::PixelSize extent,
    const FrameSet& frames, const CompositeParams& params, bool skip_encode) {
    dvv::gpu::GpuImage* image = nullptr;
    if (frames.epoch == epoch_ && frames.count >= 1 && frames.count <= MAX_STREAMS) {
        image = displayImageOf(frames.views[0], frames.epoch);  // null: background only
    }
    const dvv::platform::PixelSize image_size =
        image != nullptr ? dvv::platform::PixelSize{image->width(), image->height()}
                         : dvv::platform::PixelSize{};
    const ViewTransform view = resolveView(params.view, extent, image_size);
    const CompositeConstants constants = compositeConstants(params, view, skip_encode);
    {
        std::lock_guard lock(recorded_mutex_);
        recorded_ = RecordedInfo{.epoch = epoch_,
                                 .index = image != nullptr ? frames.index : -1,
                                 .extent = extent,
                                 .zoom = view.zoom(),
                                 .origin = view.origin(),
                                 .mode = view.mode()};
    }
    const std::uint32_t groups_x = (extent.width + 7) / 8;
    const std::uint32_t groups_y = (extent.height + 7) / 8;
    // Everything the GPU touches stays alive until the submission has completed (M1a resource
    // rule): pipeline, output, the dummy, and a share of the FrameSet's leases, which keeps the
    // display image (`image`) in the store. The presenter releases the function on its own thread.
    return [pipeline = pipeline_, output, dummy = dummy_, image, leases = frames.share(), constants,
            groups_x, groups_y](dvv::gpu::CommandRecorder& recorder) {
        static_cast<void>(leases);  // held only for their lifetime
        const std::array<dvv::gpu::GpuImage*, MAX_STREAMS + 1> bound{
            image != nullptr ? image : dummy.get(), output.get()};
        recorder.dispatch(*pipeline, std::as_bytes(std::span(&constants, 1)), bound,
                          dvv::gpu::GroupCount{.x = groups_x, .y = groups_y});
    };
}

dvv::Result<dvv::gpu::PresentFrame> ViewportRenderer::frame(
    const dvv::gpu::PresentFrameParams& params, const DisplaySnapshot& snapshot) {
    const bool skip_encode = params.encoding == dvv::gpu::PresentEncoding::Srgb;
    if (skip_encode && !logged_srgb_) {
        logged_srgb_ = true;
        DVV_LOG_WARN(
            "viewport: the presenter applies the sRGB encode; the compositor skips its own");
    }
    bool record = !params.previousFrameCompleted || snapshot.version != version_ ||
                  skip_encode != skip_encode_;
    if (!output_ || params.extent != extent_) {
        DVV_TRY(output_, makeOutput(params.extent));
        extent_ = params.extent;
        record = true;
    }
    if (!record) {
        return dvv::gpu::PresentFrame{.record = {}, .image = output_};
    }
    dvv::gpu::RecordFunction commands =
        recordComposite(output_, params.extent, snapshot.frames, snapshot.params, skip_encode);
    version_ = snapshot.version;
    skip_encode_ = skip_encode;
    return dvv::gpu::PresentFrame{.record = std::move(commands), .image = output_};
}

dvv::Result<dvv::gpu::PresentFrame> ViewportRenderer::render(dvv::platform::PixelSize extent,
                                                             const FrameSet& frames,
                                                             const CompositeParams& params) {
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> output, makeOutput(extent));
    dvv::gpu::RecordFunction commands = recordComposite(output, extent, frames, params, false);
    return dvv::gpu::PresentFrame{.record = std::move(commands), .image = std::move(output)};
}

RecordedInfo ViewportRenderer::lastRecorded() const {
    std::lock_guard lock(recorded_mutex_);
    return recorded_;
}

}  // namespace rawreel
