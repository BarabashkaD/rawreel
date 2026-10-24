#include "gpu_support.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <span>
#include <string>
#include <utility>

#include <vulkan/vulkan_core.h>

#include "dvv/base/lifetime.h"
#include "dvv/base/task.h"
#include "dvv/gpu/command_recorder.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/resources.h"
#include "dvv/registry/capabilities.h"

namespace rawreel::testing {

namespace {

dvv::Task<dvv::Result<std::uint64_t>> submitTask(dvv::gpu::ExecutionContext& context,
                                                 dvv::IExecutor& executor,
                                                 dvv::gpu::RecordFunction record) {
    co_return co_await context.submit(executor, std::move(record));
}

}  // namespace

GpuTestEnvironment makeGpuTestEnvironment() {
    GpuTestEnvironment environment;
    auto registry = dvv::gpu::GpuRegistry::create({});
    REQUIRE_MESSAGE(registry, (registry ? std::string() : registry.error().describe()));
    environment.registry = std::move(*registry);
    auto cpu = dvv::ExecutorFactory::makeThreadPool(4);
    REQUIRE(cpu);
    environment.cpu = std::move(*cpu);
    auto io = dvv::ExecutorFactory::makeThreadPool(4);
    REQUIRE(io);
    environment.io = std::move(*io);
    environment.components = dvv::ComponentRegistry::create(dvv::Capabilities{});
    REQUIRE(environment.components);
    return environment;
}

std::int64_t liveCountOf(std::string_view type) {
    std::int64_t live = 0;
    for (const dvv::TypeStats& stats : dvv::LifetimeRegistry::instance().typeStats()) {
        if (stats.name.ends_with(type)) {
            live += stats.live;
        }
    }
    return live;
}

dvv::Result<std::uint64_t> submitAndWait(dvv::gpu::ExecutionContext& context,
                                         dvv::IExecutor& executor,
                                         dvv::gpu::RecordFunction record) {
    return dvv::syncWait(submitTask(context, executor, std::move(record)));
}

dvv::Result<std::vector<std::byte>> readImage(dvv::gpu::ExecutionContext& context,
                                              dvv::IExecutor& executor,
                                              const std::shared_ptr<dvv::gpu::GpuImage>& image,
                                              std::uint32_t bytes_per_texel) {
    if (!image) {
        return dvv::makeError(dvv::Errc::Internal, "readImage: no image");
    }
    const std::uint64_t size =
        std::uint64_t{image->width()} * image->height() * std::uint64_t{bytes_per_texel};
    DVV_TRY(std::shared_ptr<dvv::gpu::GpuBuffer> readback,
            context.createBuffer({.size = size,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  .memory = dvv::gpu::MemoryUsage::HostReadback,
                                  .name = "rawreel test readback"}));
    DVV_TRY(
        const std::uint64_t submitted,
        submitAndWait(context, executor, [image, readback](dvv::gpu::CommandRecorder& recorder) {
            recorder.copyImageToBuffer(*image, *readback);
        }));
    static_cast<void>(submitted);
    DVV_TRY_VOID(readback->invalidate());
    const std::span<const std::byte> mapped = readback->mapped();
    if (mapped.size() < size) {
        return dvv::makeError(dvv::Errc::Internal, "readImage: the readback buffer is too small");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::copy_n(mapped.begin(), bytes.size(), bytes.begin());
    return bytes;
}

}  // namespace rawreel::testing
