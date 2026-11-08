#include "core/delayed_source.h"

#include <chrono>
#include <cstdint>
#include <utility>

#include "dvv/base/task.h"

namespace rawreel {

void ProduceDelay::stop() noexcept {
    {
        const std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    wake_.notify_all();
}

void ProduceDelay::waitFor(std::uint32_t ms) {
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stopped_; });
}

namespace {

class DelayedSource final : public dvv::media::ISource {
public:
    DelayedSource(std::unique_ptr<dvv::media::ISource> inner, std::shared_ptr<ProduceDelay> delay,
                  std::shared_ptr<dvv::IExecutor> io)
        : inner_(std::move(inner)), delay_(std::move(delay)), io_(std::move(io)) {}

    [[nodiscard]] const dvv::media::SourceInfo& info() const noexcept override {
        return inner_->info();
    }

    [[nodiscard]] dvv::Task<dvv::Result<dvv::media::FrameRef>> produce(
        std::int64_t index, dvv::media::ProduceContext& ctx) override {
        const std::uint32_t ms = delay_->get();
        if (ms != 0) {
            co_await dvv::resumeOn(*io_);  // the wait blocks: not on the caller's thread
            delay_->waitFor(ms);
        }
        co_return co_await inner_->produce(index, ctx);
    }

private:
    std::unique_ptr<dvv::media::ISource> inner_;
    std::shared_ptr<ProduceDelay> delay_;
    std::shared_ptr<dvv::IExecutor> io_;
};

}  // namespace

std::unique_ptr<dvv::media::ISource> delayedSource(std::unique_ptr<dvv::media::ISource> inner,
                                                   std::shared_ptr<ProduceDelay> delay,
                                                   std::shared_ptr<dvv::IExecutor> io) {
    return std::make_unique<DelayedSource>(std::move(inner), std::move(delay), std::move(io));
}

}  // namespace rawreel
