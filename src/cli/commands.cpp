#include "cli/commands.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cli/arguments.h"
#include "cli/output.h"
#include "core/config.h"
#include "dvv/base/error.h"
#include "dvv/base/executors.h"
#include "dvv/base/json.h"
#include "dvv/base/log.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/base/when_all.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/types.h"
#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_registry.h"

namespace rawreel::cli {

namespace {

namespace media = dvv::media;

// The prefix of the error the GPU setup reports when there is no Vulkan loader or no device that
// meets the requirements; --via auto then falls back to the CPU.
constexpr std::string_view NO_DEVICE = "no usable GPU device";

// The objects of one info, dump or bench run. The destructor tears them down in the shutdown order
// of design §5.7, on success and on every error path alike; the leases are locals of dump() and
// bench(), so they are gone before.
class Resources {
public:
    Resources() = default;
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    Resources(Resources&&) = delete;
    Resources& operator=(Resources&&) = delete;
    ~Resources() {
        try {
            shutdown();
        } catch (const std::exception& error) {
            std::fputs("rawreel-cli: shutdown failed: ", stderr);
            std::fputs(error.what(), stderr);
            std::fputs("\n", stderr);
        } catch (...) {
            std::fputs("rawreel-cli: shutdown failed\n", stderr);
        }
    }

    std::shared_ptr<dvv::ThreadPool> io, cpu;
    std::unique_ptr<dvv::ComponentRegistry> components;
    std::unique_ptr<media::SourceFactory> factory;
    std::unique_ptr<media::ISource> source;  // until dump attaches it
    std::shared_ptr<dvv::gpu::GpuRegistry> gpu;
    std::shared_ptr<dvv::gpu::ExecutionContext> context;
    std::shared_ptr<media::FrameStore> store;
    std::optional<media::OriginId> origin;

private:
    void shutdown() {
        // 1. detach the origin (2. the leases are already dropped).
        if (store && origin) {
            store->detach(*origin);
            origin.reset();
        }
        // 3. the source (info), the factory, then the store.
        source.reset();
        factory.reset();
        store.reset();
        // 4. drain the context, 5. then release it, the GPU registry and the component registry.
        if (context) {
            if (const dvv::Result<void> drained = context->drain(); !drained) {
                DVV_LOG_WARN("rawreel-cli: draining the GPU context failed: {}",
                             drained.error().describe());
            }
        }
        context.reset();
        gpu.reset();
        components.reset();
        // 6. the pools.
        cpu.reset();
        io.reset();
    }
};

void printJson(const dvv::json::Value& value) {
    std::println("{}", dvv::json::dump(value, 2));
}

// Prints `error` (stderr, or errorJson on stdout with --json) and returns `code`. `json_code`
// replaces errcName(error.code) in the JSON form.
int fail(const Arguments& arguments, const dvv::Error& error, int code,
         std::optional<std::string_view> json_code = std::nullopt) {
    if (arguments.json) {
        printJson(errorJson(json_code.value_or(dvv::errcName(error.code)), error.message));
    } else {
        std::println(stderr, "error: {}", error.describe());
    }
    return code;
}

int fail(const Arguments& arguments, const dvv::Result<void>& failed, int code) {
    return fail(arguments, failed.error(), code);
}

void printVersion(bool json) {
    struct Component {
        std::string_view name, key, version;
    };
    constexpr std::array<Component, 6> COMPONENTS{{
        {.name = "rawreel", .key = "rawreel", .version = RAWREEL_VERSION},
        {.name = "dvv-base", .key = "dvv_base", .version = RAWREEL_DVV_BASE_VERSION},
        {.name = "dvv-registry", .key = "dvv_registry", .version = RAWREEL_DVV_REGISTRY_VERSION},
        {.name = "dvv-platform", .key = "dvv_platform", .version = RAWREEL_DVV_PLATFORM_VERSION},
        {.name = "dvv-gpu", .key = "dvv_gpu", .version = RAWREEL_DVV_GPU_VERSION},
        {.name = "dvv-media", .key = "dvv_media", .version = RAWREEL_DVV_MEDIA_VERSION},
    }};
    if (json) {
        dvv::json::Value value = dvv::json::Value::object();
        for (const Component& component : COMPONENTS) {
            value[std::string(component.key)] = component.version;
        }
        printJson(value);
        return;
    }
    for (const Component& component : COMPONENTS) {
        std::println("{} {}", component.name, component.version);
    }
}

[[nodiscard]] bool isNoDevice(const dvv::Error& error) {
    return error.code == dvv::Errc::Unsupported && error.message.starts_with(NO_DEVICE);
}

// GpuRegistry::create() with the options from the environment, then a context on the best device.
// No loader (Unsupported) or no usable device: Errc::Unsupported "no usable GPU device: <reason>";
// other errors pass through.
[[nodiscard]] dvv::Result<void> setUpGpu(Resources& resources) {
    auto registry = dvv::gpu::GpuRegistry::create();
    if (!registry) {
        if (registry.error().code == dvv::Errc::Unsupported) {
            return dvv::makeError(dvv::Errc::Unsupported,
                                  std::format("{}: {}", NO_DEVICE, registry.error().message));
        }
        return std::unexpected(std::move(registry).error());
    }
    resources.gpu = std::move(*registry);
    const std::optional<dvv::gpu::DeviceId> best = resources.gpu->bestDevice();
    if (!best) {
        return dvv::makeError(dvv::Errc::Unsupported,
                              std::format("{}: no device meets the requirements", NO_DEVICE));
    }
    DVV_TRY(resources.context, resources.gpu->createContext(*best));
    return {};
}

int info(const Arguments& arguments, const media::ISource& source) {
    if (arguments.json) {
        printJson(infoJson(source.info()));
    } else {
        std::print("{}", infoText(source.info()));
    }
    return exit_code::SUCCESS;
}

int dump(const Arguments& arguments, Resources& resources) {
    const media::SourceInfo info = resources.source->info();

    // 1. The frame range (no overflow: frame >= 0 and count >= 1).
    const std::int64_t available =
        info.frameCount.value_or(std::numeric_limits<std::int64_t>::max());
    if (arguments.count > available || arguments.frame > available - arguments.count) {
        const std::uint64_t last = static_cast<std::uint64_t>(arguments.frame) +
                                   static_cast<std::uint64_t>(arguments.count - 1);
        const std::string frames =
            info.frameCount ? std::format("{} frames", available) : std::string("unbounded");
        const dvv::Error error{.code = dvv::Errc::Format,
                               .message = std::format("frames {}..{} are outside the source ({})",
                                                      arguments.frame, last, frames),
                               .where = std::source_location::current(),
                               .cause = nullptr};
        return fail(arguments, error, exit_code::BAD_ARGUMENTS, "Arguments");
    }

    // 2. --via.
    bool gpu = false;
    if (arguments.via != Via::Cpu) {
        const dvv::Result<void> setup = setUpGpu(resources);
        if (setup) {
            gpu = true;
        } else if (arguments.via == Via::Auto && isNoDevice(setup.error())) {
            DVV_LOG_DEBUG("rawreel-cli: {}; dumping via the CPU", setup.error().message);
            resources.context.reset();
            resources.gpu.reset();
        } else {
            return fail(arguments, setup, exitCodeFor(setup.error(), true));
        }
    }

    // 3. The store, with the context for --via gpu.
    media::FrameStoreOptions options;
    options.cpu = resources.cpu;
    options.io = resources.io;
    options.gpu = resources.context;
    auto store = media::FrameStore::create(std::move(options));
    if (!store) {
        return fail(arguments, store.error(), exitCodeFor(store.error(), gpu));
    }
    resources.store = std::move(*store);

    // 4. Attach the source.
    auto origin =
        resources.store->attach(std::shared_ptr<media::ISource>(std::move(resources.source)));
    if (!origin) {
        return fail(arguments, origin.error(), exitCodeFor(origin.error(), false));
    }
    resources.origin = *origin;

    // 5. The domain of the reads: host memory, or staging memory that only Readback reaches.
    media::Domain domain = resources.store->hostDomain();
    if (gpu) {
        const std::optional<media::Domain> staging = resources.store->stagingDomain();
        if (!staging) {
            return fail(arguments, dvv::makeError(dvv::Errc::Internal, "the store has no staging"),
                        exit_code::GPU_ERROR);
        }
        domain = *staging;
    }

    // 6. The output file.
    const auto cannot_write = [&arguments] {
        return dvv::makeError(dvv::Errc::Io, std::format("cannot write '{}'", arguments.out));
    };
    std::ofstream file(media::pathFromUtf8(arguments.out), std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(arguments, cannot_write(), exit_code::INPUT_ERROR);
    }

    // 7. The frames, one at a time; each lease is dropped after its planes are written.
    std::uint64_t bytes = 0;
    media::FormatId written = 0;
    const bool exact = arguments.view == View::Original;
    for (std::int64_t k = 0; k < arguments.count; ++k) {
        const media::FrameKey key{.origin = *origin, .index = arguments.frame + k};
        dvv::Result<media::OriginalRead> read = dvv::syncWait(resources.store->acquireOriginal(
            key, media::Api::HostPlanes, domain, *resources.cpu, exact));
        if (!read) {
            const bool gpu_stage = gpu && read.error().code == dvv::Errc::OutOfMemory;
            return fail(arguments, read.error(), exitCodeFor(read.error(), gpu_stage));
        }
        const media::ReadLease& lease = read->lease();
        const auto* const planes = std::get_if<media::HostPlanesB>(&lease.binding());
        if (planes == nullptr) {
            const dvv::Result<void> internal =
                dvv::makeError(dvv::Errc::Internal, "the read is not in host memory");
            return fail(arguments, internal, exitCodeFor(internal.error(), gpu));
        }
        for (std::size_t plane = 0; plane < planes->planeCount; ++plane) {
            const std::span<const std::byte> data = planes->planes.at(plane);
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
            bytes += data.size();
        }
        written = lease.view().format;
        if (!file) {
            return fail(arguments, cannot_write(), exit_code::INPUT_ERROR);
        }
    }

    // 8. Close and check.
    file.close();
    if (!file) {
        return fail(arguments, cannot_write(), exit_code::INPUT_ERROR);
    }

    // 9. The result.
    const std::string_view format = media::descriptor(written).name;
    const std::string_view via = gpu ? "gpu" : "cpu";
    if (arguments.json) {
        dvv::json::Value value = dvv::json::Value::object();
        value["command"] = "dump";
        value["uri"] = info.resolvedUri;
        value["first"] = arguments.frame;
        value["frames"] = arguments.count;
        value["bytes"] = bytes;
        value["view"] = exact ? "original" : "planes";
        value["via"] = via;
        value["format"] = format;
        value["out"] = arguments.out;
        value["converters"] = convertersJson(resources.store->stats());
        printJson(value);
    } else {
        std::println("wrote {} frames ({} bytes) of {} to {} via {}", arguments.count, bytes,
                     format, arguments.out, via);
    }
    return exit_code::SUCCESS;
}

// The tight size of one original frame; 0 when the geometry is invalid.
[[nodiscard]] std::uint64_t frameBytesOf(const media::FrameDesc& desc) {
    const auto geometry = media::geometry(desc.format, desc.width, desc.height);
    return geometry ? geometry->bytes : 0;
}

// The converters with runs > 0: Produce, HostToStaging, Upload and GpuUnpack in this order, then
// any others in the order of the stats.
constexpr std::array<std::string_view, 4> BENCH_ORDER{"Produce", "HostToStaging", "Upload",
                                                      "GpuUnpack"};

[[nodiscard]] std::vector<BenchStage> benchStages(const media::StoreStats& stats) {
    const auto rank = [](const media::ConverterStats& converter) {
        return std::ranges::distance(BENCH_ORDER.begin(),
                                     std::ranges::find(BENCH_ORDER, converter.name));
    };
    std::vector<media::ConverterStats> converters;
    std::ranges::copy_if(stats.converters, std::back_inserter(converters),
                         [](const media::ConverterStats& converter) { return converter.runs > 0; });
    std::ranges::stable_sort(converters, {}, rank);
    std::vector<BenchStage> stages;
    stages.reserve(converters.size());
    for (media::ConverterStats& converter : converters) {
        stages.push_back({.name = std::move(converter.name),
                          .runs = converter.runs,
                          .bytes = converter.bytes,
                          .nanoseconds = converter.nanoseconds});
    }
    return stages;
}

// Reads frames [0, --frames) as VkImage planes in the device domain (--read-only: the exact
// original as host planes, so only Produce runs), --inflight at a time (whenAll of the batch, then
// the next batch), and reports the wall time and the converter stats.
int bench(const Arguments& arguments, Resources& resources) {
    const media::SourceInfo info = resources.source->info();

    // 1. The frame count: no wrap-around, which would measure cache hits.
    if (info.frameCount && arguments.frames > *info.frameCount) {
        const dvv::Error error{
            .code = dvv::Errc::Format,
            .message = std::format("bench needs --frames <= {} (the source has {} frames)",
                                   *info.frameCount, *info.frameCount),
            .where = std::source_location::current(),
            .cause = nullptr};
        return fail(arguments, error, exit_code::BAD_ARGUMENTS, "Arguments");
    }

    // 2. The GPU, always.
    if (const dvv::Result<void> setup = setUpGpu(resources); !setup) {
        return fail(arguments, setup, exitCodeFor(setup.error(), true));
    }

    // 3. The store with the default budgets.
    media::FrameStoreOptions options;
    options.cpu = resources.cpu;
    options.io = resources.io;
    options.gpu = resources.context;
    auto store = media::FrameStore::create(std::move(options));
    if (!store) {
        return fail(arguments, store.error(), exitCodeFor(store.error(), true));
    }
    resources.store = std::move(*store);
    const std::optional<media::Domain> device = resources.store->deviceDomain();
    if (!device) {
        return fail(arguments, dvv::makeError(dvv::Errc::Internal, "the store has no device tier"),
                    exit_code::GPU_ERROR);
    }

    // 4. Attach the source.
    auto origin =
        resources.store->attach(std::shared_ptr<media::ISource>(std::move(resources.source)));
    if (!origin) {
        return fail(arguments, origin.error(), exitCodeFor(origin.error(), false));
    }
    resources.origin = *origin;

    // 5. The measured loop: batches of --inflight reads; the leases of a batch are dropped before
    // the next one starts.
    const auto start = std::chrono::steady_clock::now();
    for (std::int64_t first = 0; first < arguments.frames; first += arguments.inflight) {
        const std::int64_t end = std::min(first + arguments.inflight, arguments.frames);
        std::vector<dvv::Task<dvv::Result<media::OriginalRead>>> tasks;
        tasks.reserve(static_cast<std::size_t>(end - first));
        for (std::int64_t index = first; index < end; ++index) {
            const media::FrameKey key{.origin = *origin, .index = index};
            tasks.push_back(arguments.readOnly
                                ? resources.store->acquireOriginal(key, media::Api::HostPlanes,
                                                                   resources.store->hostDomain(),
                                                                   *resources.cpu, true)
                                : resources.store->acquireOriginal(key, media::Api::VkImage,
                                                                   *device, *resources.cpu));
        }
        const std::vector<dvv::Result<media::OriginalRead>> reads =
            dvv::syncWait(dvv::whenAll(std::move(tasks)));
        for (const dvv::Result<media::OriginalRead>& read : reads) {
            if (!read) {
                const dvv::Error& error = read.error();
                return fail(arguments, error,
                            exitCodeFor(error, error.code == dvv::Errc::OutOfMemory));
            }
        }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

    // 6. The report.
    const BenchReport report{.uri = info.resolvedUri,
                             .format = std::string(media::descriptor(info.desc.format).name),
                             .reader = info.reader,
                             .device = resources.context->deviceInfo().name,
                             .width = info.desc.width,
                             .height = info.desc.height,
                             .frames = arguments.frames,
                             .inflight = arguments.inflight,
                             .frameBytes = frameBytesOf(info.desc),
                             .seconds = elapsed.count(),
                             .stages = benchStages(resources.store->stats()),
                             .readOnly = arguments.readOnly};
    if (arguments.json) {
        printJson(benchJson(report));
    } else {
        std::print("{}", benchText(report));
    }
    return exit_code::SUCCESS;
}

// The common part of info, dump and bench: pools, registries, factory, and the source.
int runSource(const Arguments& arguments, Resources& resources) {
    // 1. The pools.
    auto io = dvv::ExecutorFactory::makeThreadPool(4);
    if (!io) {
        return fail(arguments, io.error(), exitCodeFor(io.error(), false));
    }
    resources.io = std::move(*io);
    auto cpu = dvv::ExecutorFactory::makeThreadPool(0);
    if (!cpu) {
        return fail(arguments, cpu.error(), exitCodeFor(cpu.error(), false));
    }
    resources.cpu = std::move(*cpu);

    // 2. and 3. The component registry and the source factory.
    resources.components = dvv::ComponentRegistry::create(dvv::Capabilities{});
    auto factory = media::SourceFactory::create(*resources.components, resources.io);
    if (!factory) {
        return fail(arguments, factory.error(), exitCodeFor(factory.error(), false));
    }
    resources.factory = std::move(*factory);

    // 4. The URI.
    auto uri = dvv::parseUri(arguments.uri);
    if (!uri) {
        return fail(arguments, uri.error(), exit_code::INPUT_ERROR);
    }

    // 5. Open the source, file reads as the config file says (M3.1 U-IO2).
    const LoadedConfig config = loadConfig(arguments.config);
    const media::SourceRequest request{
        .uri = std::move(*uri), .hint = std::nullopt, .fileReads = config.config.io.fileReads};
    auto opened = dvv::syncWait(resources.factory->open(request));
    if (!opened) {
        return fail(arguments, opened.error(), exitCodeFor(opened.error(), false));
    }
    if (const auto* const ambiguous = std::get_if<media::Ambiguous>(&*opened)) {
        if (arguments.json) {
            printJson(ambiguousJson(arguments.uri, *ambiguous));
        } else {
            std::print(stderr, "{}", ambiguousText(arguments.uri, *ambiguous));
        }
        return exit_code::UNSUPPORTED;
    }
    resources.source = std::move(std::get<std::unique_ptr<media::ISource>>(*opened));

    if (arguments.command == Command::Info) {
        return info(arguments, *resources.source);
    }
    if (arguments.command == Command::Bench) {
        return bench(arguments, resources);
    }
    return dump(arguments, resources);
}

}  // namespace

int run(const Arguments& arguments) {
    switch (arguments.command) {
        case Command::Help: std::print("{}", usageText()); return exit_code::SUCCESS;
        case Command::Version: printVersion(arguments.json); return exit_code::SUCCESS;
        case Command::Info:
        case Command::Dump:
        case Command::Bench: break;
    }
    Resources resources;
    return runSource(arguments, resources);
}

}  // namespace rawreel::cli
