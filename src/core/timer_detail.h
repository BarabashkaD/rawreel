#pragma once

// The part of the thread timer both OS variants share (timer_win32.cpp, timer_posix.cpp): the
// pending deadline and callback.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "core/timer.h"
#include "dvv/base/move_only_function.h"

namespace rawreel {
namespace detail {

// What both platform variants share: the pending deadline and callback, guarded by mutex_.
class ThreadTimerBase : public ITimer {
public:
    void schedule(Clock::time_point when, dvv::MoveOnlyFunction<void()> callback) override {
        dvv::MoveOnlyFunction<void()> replaced;
        {
            std::lock_guard lock(mutex_);
            replaced = std::exchange(callback_, std::move(callback));
            deadline_ = when;
            ++generation_;
        }
        wake();
    }  // `replaced` is destroyed here, outside the lock

    void cancel() override {
        if (std::this_thread::get_id() == threadId()) {
            throw std::logic_error("ITimer::cancel() called from inside a timer callback");
        }
        dvv::MoveOnlyFunction<void()> dropped;
        {
            std::unique_lock lock(mutex_);
            dropped = std::exchange(callback_, {});
            deadline_.reset();
            ++generation_;
            // Binding (design §4.3): a running callback has returned before cancel() does.
            idle_.wait(lock, [this] { return !running_; });
        }
        wake();
    }

protected:
    virtual void wake() noexcept = 0;  // makes the thread read the deadline again
    [[nodiscard]] virtual std::thread::id threadId() const noexcept = 0;

    // Called by the timer thread with `lock` held; returns with it held. Runs the callback outside
    // the lock when the deadline has passed. True when the deadline was due.
    bool fireIfDue(std::unique_lock<std::mutex>& lock) {
        if (!deadline_ || Clock::now() < *deadline_) {
            return false;
        }
        dvv::MoveOnlyFunction<void()> callback = std::exchange(callback_, {});
        deadline_.reset();
        running_ = true;
        lock.unlock();
        if (callback) {
            callback();
        }
        callback = {};  // destroyed before running_ is cleared
        lock.lock();
        running_ = false;
        idle_.notify_all();
        return true;
    }

    std::mutex mutex_;
    std::condition_variable idle_;
    std::optional<Clock::time_point> deadline_;
    dvv::MoveOnlyFunction<void()> callback_;
    std::uint64_t generation_ = 0;
    bool running_ = false;
    bool stop_ = false;  // Windows only
};

}  // namespace detail
}  // namespace rawreel
