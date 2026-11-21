#include <condition_variable>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include "core/timer.h"
#include "core/timer_detail.h"
#include "dvv/base/error.h"

namespace rawreel {
namespace {

using detail::ThreadTimerBase;

class CvThreadTimer final : public ThreadTimerBase {
public:
    CvThreadTimer() : thread_([this](const std::stop_token& stop) { run(stop); }) {}
    CvThreadTimer(const CvThreadTimer&) = delete;
    CvThreadTimer& operator=(const CvThreadTimer&) = delete;
    CvThreadTimer(CvThreadTimer&&) = delete;
    CvThreadTimer& operator=(CvThreadTimer&&) = delete;
    ~CvThreadTimer() override {
        try {
            cancel();
            // NOLINTNEXTLINE(bugprone-empty-catch): cancel() throws only on the timer's own thread.
        } catch (...) {
        }
        thread_.request_stop();  // wakes the stop_token waits; the jthread joins below
    }

private:
    void wake() noexcept override { changed_.notify_all(); }
    [[nodiscard]] std::thread::id threadId() const noexcept override { return thread_.get_id(); }

    void run(const std::stop_token& stop) {
        std::unique_lock lock(mutex_);
        while (!stop.stop_requested()) {
            if (fireIfDue(lock)) {
                continue;
            }
            const std::uint64_t seen = generation_;
            const auto changed = [&] { return generation_ != seen; };
            if (deadline_) {
                const Clock::time_point due = *deadline_;
                changed_.wait_until(lock, stop, due, changed);
            } else {
                changed_.wait(lock, stop, changed);
            }
        }
    }

    std::condition_variable_any changed_;
    std::jthread thread_;  // last: destroyed (joined) first
};

}  // namespace

dvv::Result<std::shared_ptr<ITimer>> makeThreadTimer() {
    try {
        return std::shared_ptr<ITimer>(std::make_shared<CvThreadTimer>());
    } catch (const std::exception& error) {
        return dvv::makeError(dvv::Errc::Internal,
                              std::format("the timer thread did not start: {}", error.what()));
    }
}

}  // namespace rawreel
