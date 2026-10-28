#pragma once

// The Session (design M3 D10, §4.3, §5): a wx-free actor that owns the streams, the media stack,
// the player and the jobs. Every command is thread-safe and non-blocking: it posts to the Session's
// SerialExecutor and returns; commands run in call order. Frames go to an IDisplaySink, discrete
// events to ISessionEvents (both called on the Session executor), continuous state is polled with
// status().

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/session_types.h"
#include "dvv/base/error.h"
#include "dvv/base/executor.h"
#include "dvv/base/task.h"
#include "dvv/gpu/device_id.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/source_factory.h"
#include "dvv/platform/file_reader.h"
#include "dvv/registry/component_registry.h"

namespace dvv::gpu {
class ExecutionContext;
class GpuRegistry;
}  // namespace dvv::gpu

namespace rawreel {

namespace detail {
class SessionCore;
}  // namespace detail

struct MediaStack {                                       // what device loss replaces (design D7)
    std::shared_ptr<dvv::gpu::ExecutionContext> context;  // null: host-only (tests)
    std::shared_ptr<dvv::media::FrameStore> store;
    // The display view of a stream (GPU: displayView(deviceDomain), host fixture: planesView(desc,
    // HostPlanes, host)); evaluated once per stream at attach.
    std::function<dvv::media::ViewSpec(const dvv::media::FrameDesc&)> viewFor;
};
using StackFactory =
    std::move_only_function<dvv::Result<MediaStack>(std::optional<dvv::gpu::DeviceId>)>;

struct SessionOptions {
    std::shared_ptr<dvv::ComponentRegistry> components;  // source providers
    std::shared_ptr<dvv::IExecutor> cpu, io;
    std::shared_ptr<dvv::gpu::GpuRegistry> gpus;  // null: host-only
    std::optional<dvv::gpu::DeviceId> device;     // null: the best device
    bool presentation = false;                    // contexts with presentation (GUI)
    std::uint32_t inflight = 4;                   // PlayerConfig::inflight (K, D11)
    // SourceRequest::fileReads of every open (the config file's io.fileReads).
    dvv::platform::FileReadMode fileReads = dvv::platform::FileReadMode::Auto;
    std::shared_ptr<IClock> clock;
    std::shared_ptr<ITimer> timer;
    std::shared_ptr<IDisplaySink> sink;
    std::shared_ptr<ISessionEvents> events;
    // Empty: the default — gpuStackFactory(gpus, cpu, io, presentation) when gpus is set, else the
    // host-only store. The first stack is made in create() on the caller's thread (a GPU startup
    // failure fails create()); every later stack is made on `io` as a step of an open job in the
    // open FIFO (design v2 §5.3 step 3), one at a time.
    StackFactory makeStack{};
};

// The stage with the most converter nanoseconds between two snapshots (design D20): "Produce" ->
// "disk", "HostToStaging" -> "copy", "Upload" -> "upload", "GpuUnpack" -> "unpack", "ToDisplay" ->
// "display"; "" when none of them grew.
[[nodiscard]] std::string bottleneckStage(const dvv::media::StoreStats& before,
                                          const dvv::media::StoreStats& after);
// Help -> Diagnostics text (design X13): one line of store counters, one per tier, one per
// converter, one per outstanding lease.
[[nodiscard]] std::vector<std::string> diagnosticsLines(
    const dvv::media::StoreStats& stats, std::span<const dvv::media::LeaseInfo> leases);

class Session {
public:
    // Errc::Internal when components, cpu, io, clock, timer, sink or events is missing or
    // inflight is 0; errors of the SerialExecutor, the SourceFactory and the first makeStack pass
    // through (the GUI turns them into the M1b startup failure, exit 4).
    [[nodiscard]] static dvv::Result<std::unique_ptr<Session>> create(SessionOptions options);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    // Expects a completed shutdown(); otherwise it logs a warning and runs it (syncWait). Never
    // destroy a Session on a thread of `cpu` or `io`.
    ~Session();

    // Commands: thread-safe, non-blocking, executed in call order on the Session executor;
    // refused once shutdown() has started.
    // Slot 0 only (D2, D21; slot >= 1 -> error Unsupported "one stream at a time"); replaces the
    // stream. Enqueues an open job; with no stack installed the job first builds one (§5.3).
    // `start`: the index to show after the attach (clamped into [0, length − 1]); none -> frame 0
    // (design v2 §4.3 "Start position").
    void open(std::uint32_t slot, dvv::media::SourceRequest request,
              std::optional<std::int64_t> start = {});
    // Enqueues a rebuild job (§5.3 step 3): it releases the current stack if there is one (without
    // an error event), builds a new one on `io` and reopens the attached stream, or else the last
    // stream, at its kept position, paused. Ignored with one DVV_LOG_INFO line when a rebuild job
    // is already queued or running.
    void reopen();
    // The presenter of stack `epoch` failed with DeviceLost (GUI). Ignored for another epoch or
    // while no stack is installed.
    void reportDeviceLost(std::uint64_t epoch);
    void closeStreams();
    void play();
    void pause();
    void togglePlay();
    void stop();
    void setLoop(bool loop);
    void step(std::int64_t delta);
    void seek(std::int64_t index);
    void logDiagnostics();  // Help -> Diagnostics: diagnosticsLines() to the log (Info)

    // Snapshot; any thread. `epoch` is the installed stack's epoch, 0 while no stack is installed
    // (after a device loss); `gpuAvailable` means a stack is installed.
    [[nodiscard]] SessionStatus status() const;
    // Stops the timer, refuses new commands, waits for every running task (requests, opens and
    // stack builds), clears the sink, detaches the stream and releases the stack on the executor.
    // Await it once; a second call returns at once.
    [[nodiscard]] dvv::Task<void> shutdown();

private:
    explicit Session(std::shared_ptr<detail::SessionCore> core) noexcept;
    template <class F>
    void command(F f);

    std::shared_ptr<detail::SessionCore> core_;
};

// The default GPU StackFactory. Device: the argument; else the first entry of rankedDevices()
// whose GpuInfo is usable() (and has optional.swapchain when `presentation`). Errors: an argument
// outside devices() -> Errc::Internal "GPU <n> does not exist"; none found -> Errc::Unsupported
// "no usable GPU device" (+ " that can present to a window" when `presentation`). Context:
// createContext(id, {defaultPipelineCacheFile("rawreel", info), or empty with a DVV_LOG_WARN line
// when that fails, presentation}); store: FrameStore::create({cpu, io, context}) with default
// budgets; viewFor: displayView(*store->deviceDomain()) for every FrameDesc. Errors of
// createContext and FrameStore::create pass through unchanged.
[[nodiscard]] StackFactory gpuStackFactory(std::shared_ptr<dvv::gpu::GpuRegistry> gpus,
                                           std::shared_ptr<dvv::IExecutor> cpu,
                                           std::shared_ptr<dvv::IExecutor> io, bool presentation);

}  // namespace rawreel
