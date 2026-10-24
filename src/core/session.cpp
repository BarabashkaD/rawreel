#include "core/session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/player.h"
#include "dvv/base/executors.h"
#include "dvv/base/log.h"
#include "dvv/base/uri.h"
#include "dvv/base/when_all.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_info.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/media/display.h"
#include "dvv/media/pixel_format.h"

namespace rawreel {
namespace detail {

struct Stream {
    std::shared_ptr<dvv::media::ISource> source;  // released on a device loss (design v2 D7)
    dvv::media::SourceInfo info;
    dvv::media::ResolvedColour colour;
    dvv::media::OriginId origin = 0;  // in the current stack's store
    dvv::media::ViewSpec view{};      // stack.viewFor(info.desc)
};

// One job of the open FIFO (design v2 §5.3 step 3). Exactly one runs at a time; a job that finds
// no stack installed builds one first.
struct OpenJob {
    std::uint32_t slot = 0;
    std::optional<dvv::media::SourceRequest> request;  // empty: a stack-only job
    std::optional<std::int64_t> start;                 // design v2 §4.3 "Start position"
    bool rebuild = false;                              // reopen() (§5.3 step 3)
};

struct LastStream {  // the stream forgotten by the last releaseStack()
    std::string resolvedUri;
    std::int64_t position = 0;
};

// The requests of one stream set on one stack (design §5.2); `generation` changes when the
// out-of-memory rule restarts the window.
struct Tag {
    std::uint64_t streamSet = 0;
    std::uint64_t epoch = 0;
    std::uint64_t generation = 0;
    bool operator==(const Tag&) const = default;
};

struct RequestSlot {
    dvv::media::OriginId origin = 0;
    dvv::media::ViewSpec view{};
};

using Leases = std::array<std::shared_ptr<const dvv::media::ReadLease>, MAX_STREAMS>;
using OpenResult =
    dvv::Result<std::variant<std::unique_ptr<dvv::media::ISource>, dvv::media::Ambiguous>>;

class SessionCore;
// The detached task bodies (defined after the class; they use only its public members).
dvv::Task<void> openSource(std::shared_ptr<SessionCore> core, OpenJob job,
                           std::uint64_t stream_set);
dvv::Task<void> buildStack(std::shared_ptr<SessionCore> core, OpenJob job,
                           std::uint64_t stream_set);
dvv::Task<void> requestFrames(std::shared_ptr<SessionCore> core,
                              std::shared_ptr<dvv::media::FrameStore> store, std::int64_t index,
                              Tag tag, std::vector<RequestSlot> slots);

class SessionCore final : public std::enable_shared_from_this<SessionCore> {
public:
    SessionCore(SessionOptions options, std::shared_ptr<dvv::SerialExecutor> executor,
                std::unique_ptr<dvv::media::SourceFactory> factory) noexcept
        : options_(std::move(options)),
          executor_(std::move(executor)),
          factory_(std::move(factory)) {}

    // --- any thread ---
    [[nodiscard]] dvv::IExecutor& executor() const noexcept { return *executor_; }
    [[nodiscard]] bool stopped() const noexcept { return stopped_.load(std::memory_order_acquire); }
    [[nodiscard]] SessionStatus status() const {
        std::lock_guard lock(status_mutex_);
        return status_;
    }

    // --- Session executor only, from here on ---
    [[nodiscard]] bool stopping() const noexcept { return stopping_; }
    [[nodiscard]] dvv::media::SourceFactory& factory() const noexcept { return *factory_; }
    [[nodiscard]] dvv::IExecutor& io() const noexcept { return *options_.io; }
    // Only buildStack calls it, on `io`, while its job runs: never twice at once.
    [[nodiscard]] dvv::Result<MediaStack> makeStack() {
        return options_.makeStack(options_.device);
    }

    // Every installed stack gets the next epoch (the first is 1); epochs never go back (D7).
    void installStack(MediaStack stack) {
        stack_ = std::move(stack);
        ++stack_epoch_;
        emitStackChanged();
    }

    void open(std::uint32_t slot, dvv::media::SourceRequest request,
              std::optional<std::int64_t> start) {
        opens_.push_back(OpenJob{slot, std::move(request), start, false});
        startNextOpen();
    }
    void reopen() {
        if (rebuild_running_ || std::ranges::any_of(opens_, &OpenJob::rebuild)) {
            DVV_LOG_INFO("session: Reopen ignored: a rebuild is already queued or running");
            return;
        }
        opens_.push_back(OpenJob{.slot = 0, .request = {}, .start = {}, .rebuild = true});
        startNextOpen();
    }
    void reportDeviceLost(std::uint64_t epoch) {
        deviceLost(
            epoch,
            dvv::makeError(dvv::Errc::DeviceLost, "the presenter reported a lost device").error());
    }

    // The end of buildStack, on the executor (§5.3 step 3).
    void stackBuilt(OpenJob job, std::uint64_t stream_set, dvv::Result<MediaStack> stack) {
        if (stopping_) {
            finishJob();
            return;  // `stack` is released here, on the executor
        }
        if (!stack || !stack->store || !stack->viewFor) {
            const dvv::Error cause =
                stack
                    ? dvv::makeError(dvv::Errc::Internal, "makeStack returned an incomplete stack")
                          .error()
                    : stack.error();
            DVV_LOG_WARN("session: the GPU could not be started: {}", cause.describe());
            emitError(cause.wrap(
                dvv::Errc::DeviceLost,
                std::format("the GPU could not be started: {}. Reopen to retry.", cause.message)));
            finishJob();
            return;
        }
        installStack(std::move(*stack));
        if (job.request && stream_set == stream_set_) {
            spawn(openSource(shared_from_this(), std::move(job), stream_set_), false);
            return;  // the job ends in openDone
        }
        finishJob();  // a stack-only job, or closeStreams() ran meanwhile: the request is dropped
    }

    void closeStreams() {
        last_stream_.reset();  // a later reopen() rebuilds only the stack
        opens_.clear();
        ++stream_set_;  // a running open and every request become stale
        detachStreams();
        ready_.clear();
        player_.reset();
        oom_retried_.clear();
        oom_window_.reset();
        options_.sink->clear();
        emitStreamsChanged();
    }
    void play() {
        if (player_) {
            player_->play(clock().now());
        }
    }
    void pause() {
        if (player_) {
            player_->pause();
        }
    }
    void togglePlay() {
        if (player_) {
            if (player_->status(clock().now()).playing) {
                player_->pause();
            } else {
                player_->play(clock().now());
            }
        }
    }
    void stop() {
        if (player_) {
            player_->stop();
        }
    }
    void setLoop(bool loop) {
        loop_ = loop;
        if (player_) {
            player_->setLoop(loop);
        }
    }
    void step(std::int64_t delta) {
        if (player_) {
            player_->step(delta);
        }
    }
    void seek(std::int64_t index) {
        if (player_) {
            player_->seek(index);
        }
    }
    void logDiagnostics() const {
        if (!stack_.store) {
            DVV_LOG_INFO("store: none");
            return;
        }
        const std::vector<dvv::media::LeaseInfo> leases = stack_.store->outstandingLeases();
        for (const std::string& line : diagnosticsLines(stack_.store->stats(), leases)) {
            DVV_LOG_INFO("{}", line);
        }
    }

    void onTick() {
        scheduled_.reset();  // the deadline fired; pump() schedules the next one
        if (!stopping_ && player_) {
            player_->tick(clock().now());
        }
        pump();
    }

    void pump() {
        // The backstop trigger (§5.3): a lost context found here, e.g. when a store error did not
        // carry DeviceLost as its outer code. The host-only stack has no context.
        if (!stopping_ && stack_.context && stack_.context->lost()) {
            deviceLost(stack_epoch_,
                       dvv::makeError(dvv::Errc::DeviceLost, "the device was lost").error());
        }
        if (!stopping_ && player_ && stack_.store) {
            for (const std::int64_t index : player_->takeRequests()) {
                startRequest(index);
            }
            if (const std::optional<std::int64_t> index = player_->takeShow()) {
                show(*index);
            }
            std::erase_if(ready_,
                          [this](const auto& entry) { return !player_->keeps(entry.first); });
            updateBottleneck(clock().now());
            scheduleTimer(player_->nextDeadline());
        } else {
            scheduleTimer(std::nullopt);
        }
        publishStatus();
    }

    // A detached task (design §5.2 step 5): counted until it has finished on the executor.
    void spawn(dvv::Task<void> body, bool request);
    void taskFinished(bool request) {
        --tasks_;
        if (request) {
            --request_tasks_;
        }
        if (tasks_ == 0 && drain_waiter_) {
            // Through the executor, not inline: the finishing task's frame is still on the stack.
            executor_->post([handle = std::exchange(drain_waiter_, {})] { handle.resume(); });
        }
        pump();
    }

    // The end of an open job with a request (openSource is spawned only for those).
    void openDone(OpenJob job, std::uint64_t stream_set, OpenResult opened) {
        open_running_ = false;
        rebuild_running_ = false;
        if (stopping_) {
            return;
        }
        if (stream_set == stream_set_ && job.request) {
            dvv::media::SourceRequest& request = *job.request;
            if (!opened) {
                dvv::Error error = std::move(opened).error();
                if (needsFormat(request, error)) {
                    std::string reason = error.message;
                    emitFormatNeeded(job.slot, std::move(request), {}, std::move(reason));
                } else {
                    emitError(std::move(error));
                }
            } else if (auto* ambiguous = std::get_if<dvv::media::Ambiguous>(&*opened)) {
                std::string reason =
                    std::format("the format of '{}' is ambiguous", dvv::formatUri(request.uri));
                emitFormatNeeded(job.slot, std::move(request), std::move(*ambiguous),
                                 std::move(reason));
            } else {
                attachSource(job.slot,
                             std::shared_ptr<dvv::media::ISource>(std::move(std::get<0>(*opened))),
                             job.start);
            }
        }  // else closeStreams() or a loss ran meanwhile: the source dies here, nothing is reported
        startNextOpen();
    }

    void requestDone(std::int64_t index, const Tag& tag,
                     std::vector<dvv::Result<dvv::media::ReadLease>> results) {
        if (stopping_ || !player_ || tag != currentTag()) {
            return;  // stale: the leases are released here, on the executor
        }
        for (std::size_t slot = 0; slot < results.size(); ++slot) {
            if (!results[slot]) {
                requestFailed(index, slot, tag, std::move(results[slot]).error());
                return;
            }
        }
        const bool kept = player_->frameReady(index, clock().now());  // P2
        if (kept && !ready_.contains(index)) {
            Leases leases{};
            for (std::size_t slot = 0; slot < results.size() && slot < leases.size(); ++slot) {
                leases[slot] =
                    std::make_shared<const dvv::media::ReadLease>(std::move(*results[slot]));
            }
            ready_.emplace(index, std::move(leases));
        }
    }

    // Binding order (design §5.5, Session part).
    [[nodiscard]] dvv::Task<void> shutdownOnExecutor() {
        if (stopping_) {
            co_return;  // a second shutdown(): the first one finishes the work
        }
        stopping_ = true;
        options_.timer->cancel();  // blocks only while a callback is posting a tick
        scheduled_.reset();
        co_await dvv::resumeOn(
            *executor_);  // FIFO: ticks posted before cancel() returned run first
        opens_.clear();
        ++stream_set_;  // running opens and requests become stale
        ready_.clear();
        options_.sink->clear();
        publishStatus();
        if (tasks_ > 0) {
            co_await DrainAwaiter{this};
        }
        MediaStack stack = std::exchange(stack_, MediaStack{});
        for (const Stream& stream : streams_) {
            if (stack.store && stream.origin != 0) {
                stack.store->detach(stream.origin);
            }
        }
        streams_.clear();
        player_.reset();
        stack = MediaStack{};  // the Session's context reference is released here, on the executor
        factory_.reset();
        publishStatus();
        stopped_.store(true, std::memory_order_release);
    }

private:
    struct DrainAwaiter {
        SessionCore* core;
        [[nodiscard]] bool await_ready() const noexcept { return core->tasks_ == 0; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            core->drain_waiter_ = handle;
        }
        void await_resume() const noexcept {}
    };

    [[nodiscard]] IClock& clock() const noexcept { return *options_.clock; }
    [[nodiscard]] Tag currentTag() const noexcept {
        return {stream_set_, stack_epoch_, generation_};
    }

    // Every stack build after create() runs as a step of an open job (design v2 §5.3 step 3), so
    // two makeStack calls never overlap and opens stay in call order.
    void startNextOpen() {
        while (!open_running_ && !opens_.empty() && !stopping_) {
            OpenJob job = std::move(opens_.front());
            opens_.pop_front();
            if (job.slot >= MAX_STREAMS) {  // D2: slot 0 only
                emitError(dvv::Errc::Unsupported, "one stream in v1.0");
                continue;
            }
            if (job.rebuild) {
                if (stack_.store) {
                    releaseStack();  // D25: no error; the FIFO is kept
                }
                job.request.reset();
                job.start.reset();
                if (last_stream_) {
                    dvv::Result<dvv::Uri> uri = dvv::parseUri(last_stream_->resolvedUri);
                    if (uri) {
                        job.request = dvv::media::SourceRequest{.uri = std::move(*uri), .hint = {}};
                        job.start = last_stream_->position;
                    } else {
                        emitError(uri.error().wrap(uri.error().code,
                                                   "the last stream cannot be reopened"));
                    }
                }
            }
            open_running_ = true;
            rebuild_running_ = job.rebuild;
            if (!stack_.store) {
                spawn(buildStack(shared_from_this(), std::move(job), stream_set_), false);
            } else if (job.request) {
                spawn(openSource(shared_from_this(), std::move(job), stream_set_), false);
            } else {
                open_running_ =
                    false;  // nothing to do (not reachable: a rebuild released the stack)
                rebuild_running_ = false;
            }
        }
    }

    void finishJob() {
        open_running_ = false;
        rebuild_running_ = false;
        startNextOpen();
    }

    // Design v2 §5.3 steps 1-2: pause, forget the stream (kept as last_stream_), release the stack
    // and report DeviceLost. Nothing is rebuilt until the user acts (reopen() or open()).
    void deviceLost(std::uint64_t epoch, const dvv::Error& cause) {
        if (stopping_ || !stack_.store || epoch != stack_epoch_) {
            return;  // another stack's report, no stack, or this loss is handled already
        }
        DVV_LOG_WARN("session: the GPU device was lost (stack {}): {}", epoch, cause.describe());
        // Read before pause(), which drops a target that came from playback.
        const std::int64_t target = player_ ? player_->target() : -1;
        if (player_) {
            player_->pause();
        }
        publishStatus();  // the snapshot read during the error event shows the kept position
        const std::size_t cancelled = opens_.size() + (open_running_ ? 1U : 0U);
        opens_.clear();
        rebuild_running_ = false;  // a running job is dropped by the stream_set_ bump below
        if (cancelled > 0) {
            DVV_LOG_INFO("session: {} opens cancelled: GPU device lost", cancelled);
        }
        releaseStack(target);
        emitError(
            cause.wrap(dvv::Errc::DeviceLost,
                       std::format("GPU device lost: {}. Reopen to continue.", cause.message)));
    }

    // §5.3 step 1 without the FIFO and the error; also a rebuild of a healthy stack (D25).
    // `target`: the player's target read before an earlier pause() (-1: read it here).
    void releaseStack(std::int64_t target = -1) {
        if (player_) {
            // A show the player already counted reaches the sink first (kept == last shown).
            if (const std::optional<std::int64_t> index = player_->takeShow()) {
                show(*index);
            }
        }
        if (player_ && !streams_.empty()) {
            if (target < 0) {
                target = player_->target();
            }
            player_->pause();
            // The last shown index; before the first show of a stream set (after an attach or an
            // out-of-memory retry the position is -1) the pending target, never -1.
            std::int64_t position = player_->status(clock().now()).position;
            if (position < 0) {
                position = std::max<std::int64_t>(target, 0);
            }
            last_stream_ = LastStream{streams_.front().info.resolvedUri, position};
        }
        ready_.clear();
        options_.sink->clear();
        ++stream_set_;  // in-flight results and a running open become stale
        for (const Stream& stream : streams_) {
            if (stream.origin != 0) {
                stack_.store->detach(stream.origin);
            }
        }
        streams_.clear();
        player_.reset();
        oom_retried_.clear();
        oom_window_.reset();
        emitStreamsChanged();  // {}
        if (!stopping_) {
            options_.events->stackChanged({}, 0);  // GUI: destroys presenter and renderer
        }
        MediaStack old = std::exchange(stack_, MediaStack{});
        old = MediaStack{};  // released here, on the executor (old-context release rule)
    }

    void startRequest(std::int64_t index) {
        std::vector<RequestSlot> slots;
        slots.reserve(streams_.size());
        for (const Stream& stream : streams_) {
            slots.push_back({stream.origin, stream.view});
        }
        spawn(
            requestFrames(shared_from_this(), stack_.store, index, currentTag(), std::move(slots)),
            true);
    }

    void show(std::int64_t index) {
        const auto entry = ready_.find(index);
        if (entry == ready_.end()) {
            DVV_LOG_ERROR("session: frame {} was due but is not ready", index);
            return;
        }
        FrameSet frames;
        frames.index = index;
        frames.count = static_cast<std::uint32_t>(streams_.size());
        frames.views = entry->second;
        frames.epoch = stack_epoch_;
        ++shown_;
        options_.sink->show(std::move(frames));
    }

    void scheduleTimer(std::optional<Clock::time_point> deadline) {
        if (deadline == scheduled_) {
            return;
        }
        scheduled_ = deadline;
        if (!deadline) {
            options_.timer->cancel();
            return;
        }
        options_.timer->schedule(*deadline, [weak = weak_from_this()] {
            // Timer thread: it only posts (design §5.1). shutdown() cancels the timer before the
            // core can go away, so this never holds the last reference.
            if (const std::shared_ptr<SessionCore> core = weak.lock()) {
                try {
                    core->executor().post([core] { core->onTick(); });
                } catch (const std::exception& error) {
                    DVV_LOG_ERROR("session: a timer tick was lost: {}", error.what());
                }
            }
        });
    }

    void requestFailed(std::int64_t index, std::size_t slot, const Tag& tag,
                       const dvv::Error& error) {
        if (error.code == dvv::Errc::DeviceLost) {  // before the out-of-memory rule (§5.3)
            deviceLost(tag.epoch, error);
            return;
        }
        if (error.code == dvv::Errc::OutOfMemory && oom_retried_.insert(index).second) {
            // Design §5.2: halve the window and request the index again, once. Results of the old
            // window carry the old generation and are dropped. The restart keeps a pending seek,
            // step or start target (else the position), so that target is requested again.
            oom_window_ = std::max<std::uint32_t>(2, config_.window / 2);
            ++generation_;
            resetPlayer(std::max<std::int64_t>(player_->target(), 0), true);
            const Clock::time_point now = clock().now();
            for (const auto& [ready, leases] : ready_) {
                if (player_->keeps(ready)) {
                    static_cast<void>(player_->frameReady(ready, now));
                }
            }
            return;
        }
        player_->frameFailed(index);
        const std::string where =
            slot < streams_.size()
                ? std::format("frame {} of '{}'", index, streams_[slot].info.resolvedUri)
                : std::format("frame {}", index);
        emitError(error.wrap(error.code, where));
    }

    // D21 (one stream in v1.0): any size, format or colour replaces the stream in slot 0.
    void attachSource(std::uint32_t slot, std::shared_ptr<dvv::media::ISource> source,
                      std::optional<std::int64_t> start) {
        static_cast<void>(slot);  // slot 0: startNextOpen() refused every other slot
        if (!stack_.store) {
            // Not reachable: an open job builds a stack first, and a loss makes its open stale.
            DVV_LOG_ERROR("session: an open finished without a stack; its source is dropped");
            return;
        }
        dvv::Result<dvv::media::OriginId> attached = stack_.store->attach(source);
        if (!attached) {
            emitError(std::move(attached).error());
            return;
        }
        Stream stream;
        stream.info = source->info();
        stream.colour = dvv::media::resolveColour(stream.info.desc);
        stream.origin = *attached;
        stream.view = stack_.viewFor(stream.info.desc);
        stream.source = std::move(source);
        detachStreams();
        streams_.push_back(std::move(stream));
        streamsReplaced(start);
    }

    void streamsReplaced(std::optional<std::int64_t> start) {
        ++stream_set_;
        ready_.clear();
        oom_retried_.clear();
        oom_window_.reset();
        resetPlayer(start.value_or(0), true);
        options_.sink->clear();
        emitStreamsChanged();
    }

    // Design v2 §4.3 "Start position"; also the out-of-memory retry.
    void resetPlayer(std::int64_t position, bool keep_playing) {
        const Clock::time_point now = clock().now();
        const bool was_playing = player_ && player_->status(now).playing;
        config_ = makeConfig();
        std::int64_t first = std::max<std::int64_t>(position, 0);
        if (config_.length && *config_.length > 0) {
            first = std::min(first, *config_.length - 1);
        }
        if (!player_) {
            player_.emplace(config_);
        }
        player_->reset(config_, first);
        player_->setLoop(loop_);
        if (keep_playing && was_playing) {
            player_->play(now);
        }
    }

    // D11: the window from the tier budget of the stream's display view, capped by the
    // out-of-memory window.
    [[nodiscard]] PlayerConfig makeConfig() const {
        PlayerConfig config;
        config.inflight = options_.inflight;
        if (streams_.empty()) {
            return config;
        }
        const Stream& stream = streams_.front();
        config.fps = stream.info.fps;
        config.length = stream.info.frameCount;
        std::uint64_t budget = 0;
        if (stack_.store) {
            const dvv::media::StoreStats stats = stack_.store->stats();
            const auto tier = std::ranges::find_if(stats.tiers, [&](const auto& candidate) {
                return candidate.domain == stream.view.domain;
            });
            if (tier != stats.tiers.end()) {
                budget = tier->budget;
            } else if (!stats.tiers.empty()) {
                budget = stats.tiers.front().budget;  // the host tier
            }
        }
        constexpr std::uint64_t STREAMS = 1;
        const std::uint64_t frame = std::max<std::uint64_t>(
            2ULL * 8ULL * stream.info.desc.width * stream.info.desc.height * STREAMS, 1);
        std::uint64_t window = std::clamp<std::uint64_t>(budget / frame, 2, 8);
        if (oom_window_) {
            window = std::min<std::uint64_t>(window, *oom_window_);
        }
        config.window = static_cast<std::uint32_t>(window);
        return config;
    }

    // D20: once per second of the Session clock.
    void updateBottleneck(Clock::time_point now) {
        if (stats_time_ && now - *stats_time_ < std::chrono::seconds(1)) {
            return;
        }
        dvv::media::StoreStats stats = stack_.store->stats();
        if (stats_before_) {
            bottleneck_ = bottleneckStage(*stats_before_, stats);
        }
        stats_before_ = std::move(stats);
        stats_time_ = now;
    }

    void publishStatus() {
        SessionStatus status;
        if (player_) {
            status.player = player_->status(clock().now());
            const PlayerStatus& player = status.player;
            if (player.playing && player.actualFps < 0.95 * player.targetFps) {
                status.bottleneck = bottleneck_;
            }
            if (player.position >= 0) {
                const bool wrap = player.length && *player.length > 0 && loop_;
                for (const auto& entry : ready_) {
                    std::int64_t distance = entry.first - player.position;
                    if (wrap) {
                        distance = ((distance % *player.length) + *player.length) % *player.length;
                    }
                    if (distance >= 1 && distance <= static_cast<std::int64_t>(config_.window)) {
                        ++status.readyAhead;
                    }
                }
            }
        }
        status.streams = static_cast<std::uint32_t>(streams_.size());
        status.epoch = stack_.store ? stack_epoch_ : 0;  // 0 like stackChanged({}, 0)
        status.gpuAvailable = stack_.store != nullptr;
        status.inflight = request_tasks_;
        std::lock_guard lock(status_mutex_);
        status_ = std::move(status);
    }

    void detachStreams() {
        for (const Stream& stream : streams_) {
            if (stack_.store && stream.origin != 0) {
                stack_.store->detach(stream.origin);
            }
        }
        streams_.clear();
    }

    [[nodiscard]] static bool needsFormat(const dvv::media::SourceRequest& request,
                                          const dvv::Error& error) {
        return request.uri.scheme == "file" && !request.uri.param("fmt").has_value() &&
               (error.code == dvv::Errc::Unsupported || error.code == dvv::Errc::Format);
    }

    // Events: only on the executor, never while stopping.
    void emitError(dvv::Error error) {
        if (!stopping_) {
            options_.events->error(std::move(error));
        }
    }
    void emitError(dvv::Errc code, std::string message) {
        emitError(dvv::makeError(code, std::move(message)).error());
    }
    void emitFormatNeeded(std::uint32_t slot, dvv::media::SourceRequest request,
                          dvv::media::Ambiguous candidates, std::string reason) {
        if (!stopping_) {
            options_.events->formatNeeded(slot, std::move(request), std::move(candidates),
                                          std::move(reason));
        }
    }
    void emitStreamsChanged() {
        if (stopping_) {
            return;
        }
        std::vector<StreamInfo> streams;
        streams.reserve(streams_.size());
        for (std::size_t slot = 0; slot < streams_.size(); ++slot) {
            const Stream& stream = streams_[slot];
            streams.push_back(StreamInfo{.slot = static_cast<std::uint32_t>(slot),
                                         .resolvedUri = stream.info.resolvedUri,
                                         .info = stream.info,
                                         .colour = stream.colour});
        }
        options_.events->streamsChanged(std::move(streams));
    }
    void emitStackChanged() {
        if (!stopping_) {
            options_.events->stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext>(stack_.context),
                                          stack_epoch_);
        }
    }

    SessionOptions options_;
    std::shared_ptr<dvv::SerialExecutor> executor_;
    std::unique_ptr<dvv::media::SourceFactory> factory_;

    MediaStack stack_;
    std::uint64_t stack_epoch_ = 0;
    std::vector<Stream> streams_;
    std::uint64_t stream_set_ = 0;
    std::uint64_t generation_ = 0;
    std::optional<PlayerLogic> player_;
    PlayerConfig config_{};
    bool loop_ = false;
    std::map<std::int64_t, Leases> ready_;
    std::set<std::int64_t> oom_retried_;
    std::optional<std::uint32_t> oom_window_;
    std::deque<OpenJob> opens_;
    bool open_running_ = false;
    bool rebuild_running_ = false;  // the running job is a rebuild job
    std::optional<LastStream> last_stream_;
    std::uint32_t tasks_ = 0;          // every detached task
    std::uint32_t request_tasks_ = 0;  // SessionStatus::inflight
    std::coroutine_handle<> drain_waiter_;
    std::optional<Clock::time_point> scheduled_;
    std::uint64_t shown_ = 0;
    std::string bottleneck_;
    std::optional<dvv::media::StoreStats> stats_before_;  // the bottleneck's last snapshot
    std::optional<Clock::time_point> stats_time_;
    bool stopping_ = false;
    std::atomic<bool> stopped_{false};

    mutable std::mutex status_mutex_;
    SessionStatus status_;
};

namespace {

// Fire-and-forget coroutine (like M1b's FrameJob): starts at once, frees itself at the end.
struct Detached {
    struct promise_type {
        Detached get_return_object() const noexcept { return {}; }
        [[nodiscard]] std::suspend_never initial_suspend() const noexcept { return {}; }
        [[nodiscard]] std::suspend_never final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        // Only an allocation failure of resumeOn below gets here: the counters would never
        // reach zero and shutdown() would hang, so stop the process instead.
        void unhandled_exception() const noexcept { std::terminate(); }
    };
};

Detached runDetached(std::shared_ptr<SessionCore> core, dvv::Task<void> body, bool request) {
    try {
        co_await std::move(body);
    } catch (const std::exception& error) {
        DVV_LOG_ERROR("session: a task failed: {}", error.what());
    } catch (...) {
        DVV_LOG_ERROR("session: a task failed with an unknown exception");
    }
    // The bookkeeping runs on the executor; resumeOn never resumes inline.
    co_await dvv::resumeOn(core->executor());
    core->taskFinished(request);
}

}  // namespace

void SessionCore::spawn(dvv::Task<void> body, bool request) {
    ++tasks_;
    if (request) {
        ++request_tasks_;
    }
    runDetached(shared_from_this(), std::move(body), request);
}

dvv::Task<void> openSource(std::shared_ptr<SessionCore> core, OpenJob job,
                           std::uint64_t stream_set) {
    // Spawned only for a job with a request; `job` lives in this frame.
    OpenResult opened = dvv::makeError(dvv::Errc::Internal, "an open job without a request");
    if (job.request) {
        try {
            opened = co_await core->factory().open(*job.request);
        } catch (const std::exception& error) {
            opened = dvv::makeError(dvv::Errc::Internal,
                                    std::format("opening '{}' failed: {}",
                                                dvv::formatUri(job.request->uri), error.what()));
        }
    }
    co_await dvv::resumeOn(core->executor());  // the factory may resume on the I/O pool
    core->openDone(std::move(job), stream_set, std::move(opened));
}

dvv::Task<void> buildStack(std::shared_ptr<SessionCore> core, OpenJob job,
                           std::uint64_t stream_set) {
    // Context and store creation block (device creation, pipeline cache file), so they run on the
    // I/O pool and the Session executor never blocks (design v2 §5.1, §5.3 step 3).
    co_await dvv::resumeOn(core->io());
    dvv::Result<MediaStack> stack = dvv::makeError(dvv::Errc::Internal, "makeStack did not run");
    try {
        stack = core->makeStack();
    } catch (const std::exception& error) {
        stack =
            dvv::makeError(dvv::Errc::Internal, std::format("makeStack failed: {}", error.what()));
    }
    co_await dvv::resumeOn(core->executor());
    core->stackBuilt(std::move(job), stream_set, std::move(stack));
}

dvv::Task<void> requestFrames(std::shared_ptr<SessionCore> core,
                              std::shared_ptr<dvv::media::FrameStore> store, std::int64_t index,
                              Tag tag, std::vector<RequestSlot> slots) {
    std::vector<dvv::Result<dvv::media::ReadLease>> results;
    try {
        std::vector<dvv::Task<dvv::Result<dvv::media::ReadLease>>> acquires;
        acquires.reserve(slots.size());
        for (const RequestSlot& slot : slots) {
            acquires.push_back(store->acquire({slot.origin, index}, slot.view, core->executor()));
        }
        // Every acquire resumes on the executor (resume_on), so whenAll does too.
        results = co_await dvv::whenAll(std::move(acquires));
    } catch (const std::exception& error) {
        // Thrown either before any suspension (on the executor, inside pump) or by whenAll after
        // every child resumed on the executor: either way this runs on the executor.
        results.clear();
        results.push_back(
            dvv::makeError(dvv::Errc::Internal, std::format("frame {}: {}", index, error.what())));
    }
    core->requestDone(index, tag, std::move(results));
}

}  // namespace detail

namespace {

dvv::Result<MediaStack> hostStack(const std::shared_ptr<dvv::IExecutor>& cpu,
                                  const std::shared_ptr<dvv::IExecutor>& io) {
    DVV_TRY(std::shared_ptr<dvv::media::FrameStore> store,
            dvv::media::FrameStore::create({.cpu = cpu, .io = io}));
    MediaStack stack;
    const dvv::media::Domain host = store->hostDomain();
    stack.viewFor = [host](const dvv::media::FrameDesc& desc) {
        return dvv::media::planesView(desc, dvv::media::Api::HostPlanes, host);
    };
    stack.store = std::move(store);
    return stack;
}

std::string_view stageName(std::string_view converter) noexcept {
    if (converter == "Produce") {
        return "disk";
    }
    if (converter == "HostToStaging") {
        return "copy";
    }
    if (converter == "Upload") {
        return "upload";
    }
    if (converter == "GpuUnpack") {
        return "unpack";
    }
    if (converter == "ToDisplay") {
        return "display";
    }
    return {};
}

std::string_view memKindName(dvv::media::MemKind kind) noexcept {
    switch (kind) {
        case dvv::media::MemKind::Host: return "Host";
        case dvv::media::MemKind::HostVisible: return "HostVisible";
        case dvv::media::MemKind::DeviceLocal: return "DeviceLocal";
    }
    return "?";
}

}  // namespace

std::string bottleneckStage(const dvv::media::StoreStats& before,
                            const dvv::media::StoreStats& after) {
    std::string_view best;
    std::uint64_t best_growth = 0;
    for (const dvv::media::ConverterStats& converter : after.converters) {
        const std::string_view stage = stageName(converter.name);
        if (stage.empty()) {
            continue;
        }
        std::uint64_t previous = 0;
        for (const dvv::media::ConverterStats& old : before.converters) {
            if (old.name == converter.name) {
                previous = old.nanoseconds;
                break;
            }
        }
        const std::uint64_t growth =
            converter.nanoseconds > previous ? converter.nanoseconds - previous : 0;
        if (growth > best_growth) {
            best_growth = growth;
            best = stage;
        }
    }
    return std::string(best);
}

std::vector<std::string> diagnosticsLines(const dvv::media::StoreStats& stats,
                                          std::span<const dvv::media::LeaseInfo> leases) {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "store: frames {}, leases {}, pins {}, hits {}, misses {}, creations {}, evictions {}, "
        "failures {}",
        stats.frames, stats.leases, stats.pins, stats.hits, stats.misses, stats.creations,
        stats.evictions, stats.failures));
    for (const dvv::media::TierStats& tier : stats.tiers) {
        lines.push_back(std::format("tier {}: {} / {} bytes, {} views",
                                    memKindName(tier.domain.kind), tier.bytes, tier.budget,
                                    tier.views));
    }
    for (const dvv::media::ConverterStats& converter : stats.converters) {
        lines.push_back(std::format("converter {}: {} runs, {} bytes, {:.1f} ms", converter.name,
                                    converter.runs, converter.bytes,
                                    static_cast<double>(converter.nanoseconds) / 1e6));
    }
    for (const dvv::media::LeaseInfo& lease : leases) {
        lines.push_back(std::format("lease {}:{} {} {} at {}:{}", lease.key.origin, lease.key.index,
                                    dvv::media::descriptor(lease.view.format).name,
                                    lease.kind == dvv::media::LeaseKind::Pin ? "Pin" : "Read",
                                    dvv::sourceFileName(lease.where), lease.where.line()));
    }
    return lines;
}

dvv::Result<std::unique_ptr<Session>> Session::create(SessionOptions options) {
    const auto missing = [](std::string_view member) {
        return dvv::makeError(dvv::Errc::Internal,
                              std::format("Session::create: {} is missing", member));
    };
    if (!options.components) {
        return missing("components");
    }
    if (!options.cpu) {
        return missing("cpu");
    }
    if (!options.io) {
        return missing("io");
    }
    if (!options.clock) {
        return missing("clock");
    }
    if (!options.timer) {
        return missing("timer");
    }
    if (!options.sink) {
        return missing("sink");
    }
    if (!options.events) {
        return missing("events");
    }
    if (options.inflight == 0) {
        return dvv::makeError(dvv::Errc::Internal, "Session::create: inflight must be at least 1");
    }
    DVV_TRY(std::shared_ptr<dvv::SerialExecutor> executor,
            dvv::ExecutorFactory::makeSerialExecutor(options.cpu));
    DVV_TRY(std::unique_ptr<dvv::media::SourceFactory> factory,
            dvv::media::SourceFactory::create(*options.components, options.io));
    if (!options.makeStack) {
        if (options.gpus) {
            options.makeStack =
                gpuStackFactory(options.gpus, options.cpu, options.io, options.presentation);
        } else {
            options.makeStack = [cpu = options.cpu,
                                 io = options.io](std::optional<dvv::gpu::DeviceId> /*device*/) {
                return hostStack(cpu, io);
            };
        }
    }
    // Plan choice: the first stack is made here, on the caller's thread (startup failures are
    // synchronous); stackChanged(…, 1) is still sent from the executor.
    DVV_TRY(MediaStack stack, options.makeStack(options.device));
    if (!stack.store || !stack.viewFor) {
        return dvv::makeError(dvv::Errc::Internal,
                              "Session::create: the media stack has no store or no viewFor");
    }
    auto core = std::make_shared<detail::SessionCore>(std::move(options), std::move(executor),
                                                      std::move(factory));
    core->executor().post([core, stack = std::move(stack)]() mutable {
        core->installStack(std::move(stack));
        core->pump();
    });
    return std::unique_ptr<Session>(new Session(std::move(core)));
}

Session::Session(std::shared_ptr<detail::SessionCore> core) noexcept : core_(std::move(core)) {}

template <class F>
void Session::command(F f) {
    core_->executor().post([core = core_, f = std::move(f)]() mutable {
        if (core->stopping()) {
            return;  // refused once shutdown() has started
        }
        f(*core);
        core->pump();
    });
}

namespace {
dvv::Task<void> shutdownCore(std::shared_ptr<detail::SessionCore> core) {
    co_await dvv::resumeOn(core->executor());
    co_await core->shutdownOnExecutor();
}
}  // namespace

dvv::Task<void> Session::shutdown() {
    return shutdownCore(core_);
}

Session::~Session() {
    if (core_ && !core_->stopped()) {
        DVV_LOG_WARN("session: destroyed without shutdown(); shutting down now");
        try {
            dvv::syncWait(shutdownCore(core_));
        } catch (const std::exception& error) {
            DVV_LOG_ERROR("session: shutdown failed: {}", error.what());
        }
    }
}

void Session::open(std::uint32_t slot, dvv::media::SourceRequest request,
                   std::optional<std::int64_t> start) {
    command([slot, request = std::move(request), start](detail::SessionCore& core) mutable {
        core.open(slot, std::move(request), start);
    });
}
void Session::closeStreams() {
    command([](detail::SessionCore& core) { core.closeStreams(); });
}
void Session::play() {
    command([](detail::SessionCore& core) { core.play(); });
}
void Session::pause() {
    command([](detail::SessionCore& core) { core.pause(); });
}
void Session::togglePlay() {
    command([](detail::SessionCore& core) { core.togglePlay(); });
}
void Session::stop() {
    command([](detail::SessionCore& core) { core.stop(); });
}
void Session::setLoop(bool loop) {
    command([loop](detail::SessionCore& core) { core.setLoop(loop); });
}
void Session::step(std::int64_t delta) {
    command([delta](detail::SessionCore& core) { core.step(delta); });
}
void Session::seek(std::int64_t index) {
    command([index](detail::SessionCore& core) { core.seek(index); });
}
void Session::logDiagnostics() {
    command([](detail::SessionCore& core) { core.logDiagnostics(); });
}
void Session::reopen() {
    command([](detail::SessionCore& core) { core.reopen(); });
}
void Session::reportDeviceLost(std::uint64_t epoch) {
    command([epoch](detail::SessionCore& core) { core.reportDeviceLost(epoch); });
}

StackFactory gpuStackFactory(std::shared_ptr<dvv::gpu::GpuRegistry> gpus,
                             std::shared_ptr<dvv::IExecutor> cpu,
                             std::shared_ptr<dvv::IExecutor> io, bool presentation) {
    // Captures no context: every call makes a new one (the old one is released by the Session).
    return [gpus = std::move(gpus), cpu = std::move(cpu), io = std::move(io),
            presentation](std::optional<dvv::gpu::DeviceId> device) -> dvv::Result<MediaStack> {
        if (!gpus) {
            return dvv::makeError(dvv::Errc::Internal, "gpuStackFactory: no GPU registry");
        }
        const std::span<const dvv::gpu::GpuInfo> devices = gpus->devices();
        std::optional<dvv::gpu::DeviceId> id;
        if (device) {
            if (device->index() >= devices.size()) {
                return dvv::makeError(dvv::Errc::Internal,
                                      std::format("GPU {} does not exist", device->index()));
            }
            id = device;
        } else {
            for (const dvv::gpu::DeviceId candidate : gpus->rankedDevices()) {
                const dvv::gpu::GpuInfo& info = devices[candidate.index()];
                if (info.usable() && (!presentation || info.optional.swapchain)) {
                    id = candidate;
                    break;
                }
            }
            if (!id) {
                return dvv::makeError(dvv::Errc::Unsupported,
                                      presentation
                                          ? "no usable GPU device that can present to a window"
                                          : "no usable GPU device");
            }
        }
        const dvv::gpu::GpuInfo& info = devices[id->index()];
        dvv::gpu::ContextOptions context_options;
        context_options.presentation = presentation;
        if (dvv::Result<std::filesystem::path> file =
                dvv::gpu::defaultPipelineCacheFile("rawreel", info)) {
            context_options.pipelineCacheFile = std::move(*file);
        } else {
            DVV_LOG_WARN("session: the pipeline cache stays in memory: {}",
                         file.error().describe());
        }
        DVV_TRY(std::shared_ptr<dvv::gpu::ExecutionContext> context,
                gpus->createContext(*id, context_options));
        // On failure the local context is released here, on the calling thread (`io` for every
        // stack after the first), which the old-context release rule allows.
        DVV_TRY(std::shared_ptr<dvv::media::FrameStore> store,
                dvv::media::FrameStore::create({.cpu = cpu, .io = io, .gpu = context}));
        const std::optional<dvv::media::Domain> domain = store->deviceDomain();
        if (!domain) {
            return dvv::makeError(dvv::Errc::Internal, "the GPU store has no device domain");
        }
        MediaStack stack;
        stack.context = std::move(context);
        stack.viewFor = [domain = *domain](const dvv::media::FrameDesc& /*desc*/) {
            return dvv::media::displayView(domain);
        };
        stack.store = std::move(store);
        return stack;
    };
}

SessionStatus Session::status() const {
    return core_->status();
}

}  // namespace rawreel
