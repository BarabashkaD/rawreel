#pragma once

// Renders the test pattern (design M1b §4.6) for a Presenter's frame provider.

#include <memory>

#include "dvv/base/error.h"
#include "dvv/gpu/compute_pipeline.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/presenter.h"
#include "dvv/gpu/resources.h"
#include "dvv/platform/native_window.h"

namespace rawreel {

class TestPatternRenderer {  // not thread-safe; used by one presenter's provider
    // Only create() constructs a renderer.
    struct Key {
        explicit Key() = default;
    };

public:
    // The compute pipeline for test_pattern.slang (16 bytes of push constants).
    [[nodiscard]] static dvv::Result<std::unique_ptr<TestPatternRenderer>> create(
        std::shared_ptr<dvv::gpu::ExecutionContext> context);

    TestPatternRenderer(Key key, std::shared_ptr<dvv::gpu::ExecutionContext> context,
                        std::shared_ptr<dvv::gpu::ComputePipeline> pipeline) noexcept;

    // Re-renders when the extent changed or !info.previousFrameCompleted; otherwise returns the
    // same image and no commands.
    [[nodiscard]] dvv::Result<dvv::gpu::PresentFrame> frame(const dvv::gpu::PresentFrameInfo& info);

private:
    std::shared_ptr<dvv::gpu::ExecutionContext> context_;
    std::shared_ptr<dvv::gpu::ComputePipeline> pipeline_;
    dvv::platform::PixelSize extent_{};
    std::shared_ptr<dvv::gpu::GpuBuffer> buffer_;  // width * height RGBA8 values
    std::shared_ptr<dvv::gpu::GpuImage> image_;
};

}  // namespace rawreel
