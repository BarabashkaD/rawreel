// TestPatternRenderer on a real device: the shader's pixels equal the CPU reference bit for bit,
// and frame() records commands only when the presenter needs a new picture.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "dvv/base/error.h"
#include "dvv/base/executors.h"
#include "dvv/base/task.h"
#include "dvv/gpu/command_recorder.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/gpu/presenter.h"
#include "dvv/gpu/resources.h"
#include "dvv/gpu/submission.h"
#include "dvv/platform/native_window.h"
#include "test_pattern.h"
#include "test_pattern_renderer.h"

using dvv::gpu::CommandRecorder;
using dvv::gpu::ExecutionContext;
using dvv::gpu::PresentFrame;
using dvv::gpu::PresentFrameInfo;
using dvv::gpu::RecordFunction;
using dvv::platform::PixelSize;
using rawreel::TestPatternRenderer;

namespace {

dvv::Task<dvv::Result<std::uint64_t>> submitTask(ExecutionContext& context,
                                                 dvv::IExecutor& executor, RecordFunction record) {
    co_return co_await context.submit(executor, std::move(record));
}

// A registry configured from the environment (DVV_VK_VALIDATION, DVV_VK_VALIDATION_REQUIRED), a
// context on the best device and a one-thread executor for the submissions. At the end the context
// is released first, then no validation message may have been counted.
class RendererTest {
public:
    RendererTest() {
        auto registry = dvv::gpu::GpuRegistry::create();
        if (!registry) {
            FAIL(registry.error().describe());
        }
        registry_ = std::move(*registry);
        // The dereference stays in the checked branch: clang-tidy does not know that FAIL throws.
        if (const auto device = registry_->bestDevice(); device) {
            auto context = registry_->createContext(*device);
            if (!context) {
                FAIL(context.error().describe());
            }
            context_ = std::move(*context);
        } else {
            FAIL("no usable Vulkan device");
        }
        executor_ = dvv::ExecutorFactory::makeThreadPool(1).value();
    }
    RendererTest(const RendererTest&) = delete;
    RendererTest& operator=(const RendererTest&) = delete;
    RendererTest(RendererTest&&) = delete;
    RendererTest& operator=(RendererTest&&) = delete;
    ~RendererTest() {
        context_.reset();
        executor_.reset();
        CHECK(registry_->validationMessageCount() == 0);
    }

    [[nodiscard]] const std::shared_ptr<ExecutionContext>& context() const noexcept {
        return context_;
    }
    [[nodiscard]] dvv::Result<std::uint64_t> submitAndWait(RecordFunction record) {
        return dvv::syncWait(submitTask(*context_, *executor_, std::move(record)));
    }

private:
    std::shared_ptr<dvv::gpu::GpuRegistry> registry_;
    std::shared_ptr<ExecutionContext> context_;
    std::shared_ptr<dvv::ThreadPool> executor_;
};

PresentFrameInfo frameInfo(PixelSize extent, std::uint64_t number, bool previous_completed) {
    return {.extent = extent,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .frameNumber = number,
            .previousFrameCompleted = previous_completed};
}

}  // namespace

TEST_CASE_FIXTURE(RendererTest, "the shader draws testPatternPixel exactly") {
    const auto renderer = TestPatternRenderer::create(context()).value();
    for (const PixelSize extent : {PixelSize{1, 1}, PixelSize{33, 17}, PixelSize{640, 360}}) {
        CAPTURE(extent.width);
        CAPTURE(extent.height);
        auto frame = renderer->frame(frameInfo(extent, 0, false));
        REQUIRE(frame.has_value());
        REQUIRE(static_cast<bool>(frame->record));
        REQUIRE(frame->image != nullptr);
        CHECK(frame->image->width() == extent.width);
        CHECK(frame->image->height() == extent.height);

        const std::uint64_t size = std::uint64_t{extent.width} * extent.height * 4;
        const auto readback = context()
                                  ->createBuffer({.size = size,
                                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                  .memory = dvv::gpu::MemoryUsage::HostReadback,
                                                  .name = "test pattern readback"})
                                  .value();
        REQUIRE(submitAndWait([&frame, &readback](CommandRecorder& recorder) {
                    frame->record(recorder);
                    recorder.copyImageToBuffer(*frame->image, *readback);
                }).has_value());

        REQUIRE(readback->invalidate().has_value());
        std::vector<std::uint32_t> pixels(size / sizeof(std::uint32_t));
        std::memcpy(pixels.data(), readback->mapped().data(), size);
        std::size_t mismatches = 0;
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                const std::uint32_t wanted =
                    rawreel::testPatternPixel(x, y, extent.width, extent.height);
                mismatches += pixels[(std::size_t{y} * extent.width) + x] != wanted ? 1 : 0;
            }
        }
        CHECK(mismatches == 0);
    }
}

TEST_CASE_FIXTURE(RendererTest, "frame() renders only when needed") {
    const auto renderer = TestPatternRenderer::create(context()).value();
    const PixelSize extent{64, 48};

    auto first = renderer->frame(frameInfo(extent, 0, false));
    REQUIRE(first.has_value());
    REQUIRE(static_cast<bool>(first->record));
    REQUIRE(first->image != nullptr);
    REQUIRE(submitAndWait(std::move(first->record)).has_value());

    // The previous frame completed at the same size: the image is still valid.
    auto same = renderer->frame(frameInfo(extent, 1, true));
    REQUIRE(same.has_value());
    CHECK_FALSE(static_cast<bool>(same->record));
    CHECK(same->image == first->image);

    // The previous frame was dropped: its commands may not have run, so render again.
    auto dropped = renderer->frame(frameInfo(extent, 2, false));
    REQUIRE(dropped.has_value());
    REQUIRE(static_cast<bool>(dropped->record));
    CHECK(dropped->image == first->image);
    REQUIRE(submitAndWait(std::move(dropped->record)).has_value());

    // A new size: new resources, rendered at once.
    const PixelSize resized{80, 30};
    auto bigger = renderer->frame(frameInfo(resized, 3, true));
    REQUIRE(bigger.has_value());
    REQUIRE(static_cast<bool>(bigger->record));
    REQUIRE(bigger->image != nullptr);
    CHECK(bigger->image != first->image);
    CHECK(bigger->image->width() == resized.width);
    CHECK(bigger->image->height() == resized.height);
    REQUIRE(submitAndWait(std::move(bigger->record)).has_value());
}
