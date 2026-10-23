#include "test_pattern_renderer.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <vulkan/vulkan_core.h>

#include "dvv/gpu/command_recorder.h"
#include "rawreel_shaders/test_pattern.h"

namespace rawreel {

namespace {

// test_pattern.slang's push constants: the pixel buffer by device address and the extent.
struct TestPatternConstants {
    std::uint64_t pixels;
    std::uint32_t width;
    std::uint32_t height;
};
// Pipeline creation checks only the block's size: the offsets pin the order of its members.
static_assert(sizeof(TestPatternConstants) == 16);
static_assert(offsetof(TestPatternConstants, pixels) == 0 &&
              offsetof(TestPatternConstants, width) == 8 &&
              offsetof(TestPatternConstants, height) == 12);

constexpr std::uint32_t GROUP_SIZE = 8;  // [numthreads(8, 8, 1)] in test_pattern.slang

}  // namespace

dvv::Result<std::unique_ptr<TestPatternRenderer>> TestPatternRenderer::create(
    std::shared_ptr<dvv::gpu::ExecutionContext> context) {
    if (!context) {
        return dvv::makeError(dvv::Errc::Internal, "TestPatternRenderer: no execution context");
    }
    DVV_TRY(auto pipeline, context->createComputePipeline(rawreel_shaders::testPattern(),
                                                          sizeof(TestPatternConstants)));
    return std::make_unique<TestPatternRenderer>(Key{}, std::move(context), std::move(pipeline));
}

TestPatternRenderer::TestPatternRenderer(
    Key /*key*/, std::shared_ptr<dvv::gpu::ExecutionContext> context,
    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline) noexcept
    : context_(std::move(context)), pipeline_(std::move(pipeline)) {}

dvv::Result<dvv::gpu::PresentFrame> TestPatternRenderer::frame(
    const dvv::gpu::PresentFrameInfo& info) {
    const bool resized = !image_ || info.extent != extent_;
    if (resized) {
        const std::uint32_t width = info.extent.width;
        const std::uint32_t height = info.extent.height;
        // Both resources are replaced only when both could be created.
        DVV_TRY(auto buffer, context_->createBuffer({.size = std::uint64_t{width} * height * 4,
                                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                     .memory = dvv::gpu::MemoryUsage::DeviceOnly,
                                                     .name = "test pattern pixels"}));
        DVV_TRY(auto image, context_->createImage({.width = width,
                                                   .height = height,
                                                   .format = VK_FORMAT_R8G8B8A8_UNORM,
                                                   .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                                   .name = "test pattern"}));
        buffer_ = std::move(buffer);
        image_ = std::move(image);
        extent_ = info.extent;
    }
    if (!resized && info.previousFrameCompleted) {
        return dvv::gpu::PresentFrame{.record = {}, .image = image_};
    }
    const TestPatternConstants push{
        .pixels = buffer_->deviceAddress(), .width = extent_.width, .height = extent_.height};
    // The record function keeps the pipeline, the buffer and the image alive until the GPU is done.
    auto record = [pipeline = pipeline_, buffer = buffer_, image = image_,
                   push](dvv::gpu::CommandRecorder& recorder) {
        recorder.dispatch(*pipeline, std::as_bytes(std::span(&push, 1)),
                          (push.width + GROUP_SIZE - 1) / GROUP_SIZE,
                          (push.height + GROUP_SIZE - 1) / GROUP_SIZE);
        recorder.copyBufferToImage(*buffer, *image);
    };
    return dvv::gpu::PresentFrame{.record = std::move(record), .image = image_};
}

}  // namespace rawreel
