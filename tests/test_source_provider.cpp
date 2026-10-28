#include "test_source_provider.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/media/frame.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/reference.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"
#include "dvv/registry/capabilities.h"
#include "dvv/registry/component_info.h"
#include "dvv/registry/component_registry.h"
#include "dvv/registry/parameters.h"

namespace rawreel::testing {

namespace {

using dvv::Errc;
using dvv::Result;
using dvv::media::FormatId;

constexpr auto GATE_TIMEOUT = std::chrono::seconds(10);
constexpr std::array<std::string_view, 3> SCHEMES{"slow", "fail", "file"};
// A file it recognises and refuses, as the ffmpeg provider refuses a lossy codec (M4 GUI D4).
constexpr std::string_view REFUSED_SUFFIX = ".refused";

struct Gate {
    bool closed = false;
    std::size_t waiters = 0;
};

struct Gates {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::string, Gate, std::less<>> gates;
};

Gates& gates() {
    static Gates instance;
    return instance;
}

// The used shots of `fail:...&shots=K`, per URI text (process-wide).
struct Shots {
    std::mutex mutex;
    std::map<std::string, std::uint64_t, std::less<>> used;
};

Shots& shots() {
    static Shots instance;
    return instance;
}

// True when this call takes one of the `limit` shots of `uri`.
bool takeShot(const std::string& uri, std::uint64_t limit) {
    Shots& all = shots();
    std::lock_guard lock(all.mutex);
    std::uint64_t& used = all.used[uri];
    if (used >= limit) {
        return false;
    }
    ++used;
    return true;
}

// True when the gate is open (now or within the timeout).
bool waitGate(std::string_view name) {
    return passGate(name, std::chrono::duration_cast<std::chrono::milliseconds>(GATE_TIMEOUT));
}

enum class Kind : std::uint8_t { Slow, Fail };

struct Behaviour {
    Kind kind = Kind::Slow;
    std::optional<std::int64_t> index;
    std::string gate;      // slow: produce(index) waits
    std::string openGate;  // slow: open() waits
    Errc errc = Errc::Io;  // fail:
    std::uint64_t times = 0;
    std::optional<std::uint64_t> shots;  // fail: per-URI count (replaces times)
    std::string uri;                     // the shots key: formatUri(request.uri)
};

Result<std::uint64_t> number(const dvv::Uri& uri, std::string_view name, std::uint64_t fallback) {
    const std::optional<std::string_view> text = uri.param(name);
    if (!text) {
        return fallback;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (error != std::errc{} || end != text->data() + text->size()) {
        return dvv::makeError(Errc::Format, std::format("bad {} '{}'", name, *text));
    }
    return value;
}

class TestSource final : public dvv::media::ISource, public dvv::Managed<TestSource> {
public:
    TestSource(dvv::CreationKey<dvv::media::SourceFactory> key, dvv::media::SourceInfo info,
               Behaviour behaviour) noexcept
        : Managed(std::move(key)), info_(std::move(info)), behaviour_(std::move(behaviour)) {}

    [[nodiscard]] const dvv::media::SourceInfo& info() const noexcept override { return info_; }

    [[nodiscard]] dvv::Task<Result<dvv::media::FrameRef>> produce(
        std::int64_t index, dvv::media::ProduceContext& ctx) override {
        co_await dvv::resumeOn(ctx.io());
        if (behaviour_.index == index) {
            if (behaviour_.kind == Kind::Slow && !behaviour_.gate.empty() &&
                !waitGate(behaviour_.gate)) {
                co_return dvv::makeError(Errc::Internal,
                                         std::format("gate {} timed out", behaviour_.gate));
            }
            if (behaviour_.kind == Kind::Fail) {
                bool fails = false;
                if (behaviour_.shots) {
                    fails = takeShot(behaviour_.uri, *behaviour_.shots);
                } else {
                    const std::uint64_t call = calls_.fetch_add(1, std::memory_order_relaxed) + 1;
                    fails = behaviour_.times == 0 || call <= behaviour_.times;
                }
                if (fails) {
                    switch (behaviour_.errc) {
                        case Errc::DeviceLost:
                            co_return dvv::makeError(Errc::DeviceLost, "injected device loss");
                        case Errc::OutOfMemory:
                            co_return dvv::makeError(Errc::OutOfMemory, "injected out of memory");
                        default: co_return dvv::makeError(Errc::Io, "injected read error");
                    }
                }
            }
        }
        const std::int64_t frames = info_.frameCount.value_or(0);
        if (index < 0 || index >= frames) {
            co_return dvv::makeError(Errc::Internal,
                                     std::format("frame {} is outside [0, {})", index, frames));
        }
        const dvv::media::FrameDesc& desc = info_.desc;
        const dvv::media::PixelFormatDescriptor& format = dvv::media::descriptor(desc.format);
        dvv::media::CodeValues values;
        values.format = desc.format;
        values.width = desc.width;
        values.height = desc.height;
        for (std::size_t c = 0; c < format.componentCount; ++c) {
            const auto [cw, ch] =
                dvv::media::componentSize(desc.format, c, desc.width, desc.height);
            std::vector<std::uint16_t>& plane = values.components.at(c);
            plane.resize(static_cast<std::size_t>(cw) * ch);
            for (std::uint32_t y = 0; y < ch; ++y) {
                for (std::uint32_t x = 0; x < cw; ++x) {
                    plane[(static_cast<std::size_t>(y) * cw) + x] =
                        testCode(desc.format, c, x, y, index);
                }
            }
        }
        const std::vector<std::byte> bytes = co_try(dvv::media::pack(values));
        const std::shared_ptr<dvv::media::HostFrame> frame =
            co_try(ctx.allocateHost(desc, bytes.size(), 64, 0));
        const std::span<std::byte> data = frame->data();
        if (data.size() != bytes.size()) {
            co_return dvv::makeError(Errc::Internal,
                                     "test source: the host frame has another size");
        }
        std::ranges::copy(bytes, data.begin());
        co_return ctx.publish(frame);
    }

private:
    dvv::media::SourceInfo info_;
    Behaviour behaviour_;
    std::atomic<std::uint64_t> calls_{0};
};

class TestSourceProvider final : public dvv::media::ISourceProvider,
                                 public dvv::Managed<TestSourceProvider> {
public:
    TestSourceProvider(dvv::CreationKey<dvv::ComponentRegistry> key,
                       const dvv::ParameterValues& /*values*/) noexcept
        : Managed(std::move(key)) {}

    static dvv::ComponentInfo info() noexcept {
        return {
            .id = "test", .name = "Session test sources", .kind = dvv::Kind::Source, .version = 1};
    }
    static const dvv::ParameterSchema& schema() {
        static const dvv::ParameterSchema SCHEMA;
        return SCHEMA;
    }
    static std::expected<void, dvv::Unavailable> probe(const dvv::Capabilities& /*caps*/) {
        return {};
    }

    [[nodiscard]] std::span<const std::string_view> schemes() const noexcept override {
        return SCHEMES;
    }

    [[nodiscard]] dvv::media::Match match(const dvv::media::SourceRequest& request,
                                          std::span<const std::byte> /*head*/) const override {
        if (std::ranges::find(SCHEMES, request.uri.scheme) == SCHEMES.end()) {
            return {.confidence = 0, .guess = {}, .why = "not a test URI"};
        }
        if (request.uri.scheme == "file") {
            if (!request.uri.path.ends_with(REFUSED_SUFFIX)) {
                return {.confidence = 0, .guess = {}, .why = "not a refused test file"};
            }
            return {.confidence = 100, .guess = {}, .why = "refused test file"};
        }
        return {.confidence = 100, .guess = {}, .why = "test source"};
    }

    [[nodiscard]] dvv::Task<Result<std::unique_ptr<dvv::media::ISource>>> open(
        dvv::media::OpenTicket ticket, const dvv::media::SourceRequest& request,
        const dvv::media::FormatGuess& /*guess*/, const dvv::media::OpenContext& context) override {
        co_await dvv::resumeOn(context.io);  // never block the caller's thread
        if (request.uri.scheme == "file") {
            co_return dvv::makeError(Errc::Unsupported,
                                     "codec test is not supported (lossless codecs only)");
        }
        const dvv::Uri uri = co_try(dvv::parseUri("synthetic:" + request.uri.body));
        Behaviour behaviour;
        behaviour.kind = request.uri.scheme == "fail" ? Kind::Fail : Kind::Slow;
        if (uri.param("index")) {
            behaviour.index = static_cast<std::int64_t>(co_try(number(uri, "index", 0)));
        }
        behaviour.gate = std::string(uri.param("gate").value_or(""));
        behaviour.openGate = std::string(uri.param("opengate").value_or(""));
        const std::string_view errc = uri.param("errc").value_or("io");
        behaviour.errc = errc == "devicelost" ? Errc::DeviceLost
                         : errc == "oom"      ? Errc::OutOfMemory
                                              : Errc::Io;
        behaviour.times = co_try(number(uri, "times", 0));
        if (uri.param("shots")) {
            behaviour.shots = co_try(number(uri, "shots", 0));
        }
        behaviour.uri = dvv::formatUri(request.uri);

        const auto width = static_cast<std::uint32_t>(co_try(number(uri, "w", 32)));
        const auto height = static_cast<std::uint32_t>(co_try(number(uri, "h", 16)));
        const auto frames = static_cast<std::int64_t>(co_try(number(uri, "frames", 40)));
        const auto fps = static_cast<std::int64_t>(co_try(number(uri, "fps", 25)));
        const std::string_view format_name = uri.param("fmt").value_or("yuv420p");
        const std::optional<FormatId> format = dvv::media::findFormat(format_name);
        if (!format) {
            co_return dvv::makeError(Errc::Format,
                                     std::format("unknown pixel format '{}'", format_name));
        }
        if (!behaviour.openGate.empty() && !waitGate(behaviour.openGate)) {
            co_return dvv::makeError(Errc::Internal,
                                     std::format("gate {} timed out", behaviour.openGate));
        }

        dvv::media::SourceInfo info;
        info.desc = {.format = *format, .colour = 0, .width = width, .height = height};
        info.fps = {fps, 1};
        info.frameCount = frames;
        info.access = dvv::media::SourceAccess::RandomCheap;
        info.deterministic = true;
        info.resolvedUri = dvv::formatUri(request.uri);
        info.provider = "test";
        std::unique_ptr<dvv::media::ISource> source = std::make_unique<TestSource>(
            dvv::media::SourceFactory::issueKey<TestSource>(std::move(ticket)), std::move(info),
            std::move(behaviour));
        co_return std::move(source);
    }
};

}  // namespace

DVV_REGISTER(dvv::media::ISourceProvider, TestSourceProvider);

void closeGate(std::string_view name) {
    Gates& all = gates();
    std::lock_guard lock(all.mutex);
    all.gates[std::string(name)].closed = true;
}

void releaseGate(std::string_view name) {
    Gates& all = gates();
    std::lock_guard lock(all.mutex);
    all.gates[std::string(name)].closed = false;
    all.changed.notify_all();  // under the lock (the waiters' state lives in the map)
}

std::size_t gateWaiters(std::string_view name) {
    Gates& all = gates();
    std::lock_guard lock(all.mutex);
    const auto gate = all.gates.find(name);
    return gate == all.gates.end() ? 0 : gate->second.waiters;
}

bool passGate(std::string_view name, std::chrono::milliseconds limit) {
    Gates& all = gates();
    std::unique_lock lock(all.mutex);
    Gate& gate = all.gates[std::string(name)];
    ++gate.waiters;
    const bool passed = all.changed.wait_for(lock, limit, [&gate] { return !gate.closed; });
    --gate.waiters;
    return passed;
}

void resetShots() {
    Shots& all = shots();
    std::lock_guard lock(all.mutex);
    all.used.clear();
}

std::uint16_t testCode(FormatId format, std::size_t component, std::uint32_t x, std::uint32_t y,
                       std::int64_t index) {
    const dvv::media::PixelFormatDescriptor& descriptor = dvv::media::descriptor(format);
    const unsigned bits =
        component < descriptor.componentCount ? descriptor.components.at(component).bits : 16U;
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    const std::uint64_t value =
        (7ULL * x) + (13ULL * y) + (29ULL * component) + (3ULL * static_cast<std::uint64_t>(index));
    return static_cast<std::uint16_t>(value & mask);
}

}  // namespace rawreel::testing
