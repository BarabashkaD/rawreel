#include "render/viewport_renderer.h"

#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <variant>

#include "core/view.h"
#include "dvv/base/log.h"
#include "dvv/gpu/command_recorder.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "rawreel_shaders/composite.h"
#include "render/composite_reference.h"

namespace rawreel {

namespace {

// The display image of a lease, or null when the lease is not a display view (design D3: one
// RGBA16F plane in GENERAL). The caller's epoch check is what separates stacks: an old and a new
// context on the same GPU share the DeviceId, and every generation is FIRST_GENERATION in v1.0
// (design v2 D7). The device/generation comparison only rejects an image of another physical
// device.
std::shared_ptr<dvv::gpu::GpuImage> displayImageOf(
    const std::shared_ptr<const dvv::media::ReadLease>& lease,
    const dvv::gpu::ExecutionContext& context) {
    if (!lease || !*lease) {
        return nullptr;
    }
    const auto* binding = std::get_if<dvv::media::VkImageB>(&lease->binding());
    if (binding == nullptr || binding->planeCount != 1 || !binding->planes[0]) {
        return nullptr;
    }
    const std::shared_ptr<dvv::gpu::GpuImage>& image = binding->planes[0];
    if (image->format() != VK_FORMAT_R16G16B16A16_SFLOAT ||
        image->deviceId() != context.deviceId() || image->generation() != context.generation()) {
        return nullptr;
    }
    return image;
}

}  // namespace

dvv::Result<std::unique_ptr<ViewportRenderer>> ViewportRenderer::create(
    std::shared_ptr<dvv::gpu::ExecutionContext> context, std::uint64_t epoch) {
    DVV_TRY(
        std::shared_ptr<dvv::gpu::ComputePipeline> pipeline,
        context->createComputePipeline(
            rawreel_shaders::composite(), static_cast<std::uint32_t>(sizeof(CompositeConstants)),
            dvv::gpu::ComputeBindings{.storageImages = MAX_STREAMS + 1}));
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuImage> dummy,
            context->createImage({.width = 1,
                                  .height = 1,
                                  .format = VK_FORMAT_R16G16B16A16_SFLOAT,
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
    return context_->createImage(
        {.width = extent.width,
         .height = extent.height,
         .format = VK_FORMAT_R8G8B8A8_UNORM,
         .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
         .name = "viewport composite"});
}

dvv::gpu::RecordFunction ViewportRenderer::recordComposite(
    const std::shared_ptr<dvv::gpu::GpuImage>& output, dvv::platform::PixelSize extent,
    const FrameSet& frames, const CompositeParams& params, bool skip_encode) {
    std::shared_ptr<dvv::gpu::GpuImage> image;
    if (frames.epoch == epoch_ && frames.count >= 1 && frames.count <= MAX_STREAMS) {
        image = displayImageOf(frames.views[0], *context_);  // null: background only
    }
    const dvv::platform::PixelSize image_size =
        image ? dvv::platform::PixelSize{image->width(), image->height()}
              : dvv::platform::PixelSize{};
    const CompositeConstants constants =
        compositeConstants(params, extent, image_size, skip_encode);
    {
        std::lock_guard lock(recorded_mutex_);
        recorded_ = RecordedInfo{.epoch = epoch_,
                                 .index = image ? frames.index : -1,
                                 .extent = extent,
                                 .zoom = ViewTransform::fit(extent, image_size).zoom()};
    }
    const std::uint32_t groups_x = (extent.width + 7) / 8;
    const std::uint32_t groups_y = (extent.height + 7) / 8;
    // Everything the GPU touches stays alive until the submission has completed (M1a resource
    // rule): pipeline, output, the bound image, and the FrameSet's leases, which keep the display
    // image in the store. The presenter releases the function on its own thread.
    return [pipeline = pipeline_, output, bound0 = image ? image : dummy_, leases = frames.views,
            constants, groups_x, groups_y](dvv::gpu::CommandRecorder& recorder) {
        static_cast<void>(leases);  // held only for their lifetime
        const std::array<dvv::gpu::GpuImage*, MAX_STREAMS + 1> bound{bound0.get(), output.get()};
        recorder.dispatch(*pipeline, std::as_bytes(std::span(&constants, 1)), bound, groups_x,
                          groups_y);
    };
}

dvv::Result<dvv::gpu::PresentFrame> ViewportRenderer::frame(const dvv::gpu::PresentFrameInfo& info,
                                                            const DisplaySnapshot& snapshot) {
    const bool skip_encode = isSrgbFormat(info.format);
    if (skip_encode && !logged_srgb_) {
        logged_srgb_ = true;
        DVV_LOG_WARN("viewport: the swapchain format is sRGB; the compositor skips its own encode");
    }
    bool record =
        !info.previousFrameCompleted || snapshot.version != version_ || skip_encode != skip_encode_;
    if (!output_ || info.extent != extent_) {
        DVV_TRY(output_, makeOutput(info.extent));
        extent_ = info.extent;
        record = true;
    }
    if (!record) {
        return dvv::gpu::PresentFrame{.record = {}, .image = output_};
    }
    dvv::gpu::RecordFunction commands =
        recordComposite(output_, info.extent, snapshot.frames, snapshot.params, skip_encode);
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

bool isSrgbFormat(VkFormat format) noexcept {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

}  // namespace rawreel
