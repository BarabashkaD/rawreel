#pragma once

// Helpers for rawreel's GPU tests (rawreel_gpu_tests). One GpuTestEnvironment per test case.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/executors.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/gpu/submission.h"
#include "dvv/registry/component_registry.h"

namespace rawreel::testing {

struct GpuTestEnvironment {                           // members are released in reverse order
    std::shared_ptr<dvv::gpu::GpuRegistry> registry;  // headless; validation from the environment
    std::shared_ptr<dvv::ThreadPool> cpu;             // 4 threads
    std::shared_ptr<dvv::ThreadPool> io;              // 4 threads
    std::shared_ptr<dvv::ComponentRegistry> components;
};
// REQUIREs every step (GpuRegistry::create({}), the pools,
// ComponentRegistry::create(Capabilities{})).
[[nodiscard]] GpuTestEnvironment makeGpuTestEnvironment();
// Live managed objects whose type name ends with `type` (sum over LifetimeRegistry::typeStats()).
[[nodiscard]] std::int64_t liveCountOf(std::string_view type);
// One submission of `record`, awaited with syncWait; resumes on `executor`.
[[nodiscard]] dvv::Result<std::uint64_t> submitAndWait(dvv::gpu::ExecutionContext& context,
                                                       dvv::IExecutor& executor,
                                                       dvv::gpu::RecordFunction record);
// The whole image copied into a HostReadback buffer (copyImageToBuffer, then invalidate), tightly
// packed, `bytes_per_texel` per texel.
[[nodiscard]] dvv::Result<std::vector<std::byte>> readImage(
    dvv::gpu::ExecutionContext& context, dvv::IExecutor& executor,
    const std::shared_ptr<dvv::gpu::GpuImage>& image, std::uint32_t bytes_per_texel);

}  // namespace rawreel::testing
