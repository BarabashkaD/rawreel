#include "core/timer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "dvv/base/log.h"

namespace rawreel {
namespace {

class SteadyClock final : public IClock {
public:
    [[nodiscard]] Clock::time_point now() const override { return Clock::now(); }
};

// What both platform variants share: the pending deadline and callback, guarded by mutex_.
class ThreadTimerBase : public ITimer {
public:
    void schedule(Clock::time_point when, std::move_only_function<void()> callback) override {
        std::move_only_function<void()> replaced;
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
        std::move_only_function<void()> dropped;
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
        std::move_only_function<void()> callback = std::exchange(callback_, {});
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
    std::move_only_function<void()> callback_;
    std::uint64_t generation_ = 0;
    bool running_ = false;
    bool stop_ = false;  // Windows only
};

#if defined(_WIN32)

// A CREATE_WAITABLE_TIMER_HIGH_RESOLUTION timer (no process-wide timeBeginPeriod, design D11) plus
// an auto-reset event that schedule(), cancel() and the destructor set.
class WaitableThreadTimer final : public ThreadTimerBase {
    struct Private {
        explicit Private() = default;
    };

public:
    static dvv::Result<std::shared_ptr<ITimer>> create() {
        HANDLE timer = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer == nullptr) {
            return dvv::makeError(
                dvv::Errc::Internal,
                std::format("CreateWaitableTimerExW failed (error {})", GetLastError()));
        }
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) {
            const DWORD code = GetLastError();
            CloseHandle(timer);
            return dvv::makeError(dvv::Errc::Internal,
                                  std::format("CreateEventW failed (error {})", code));
        }
        try {
            return std::shared_ptr<ITimer>(
                std::make_shared<WaitableThreadTimer>(Private{}, timer, event));
        } catch (const std::exception& error) {
            // The constructor did not finish (the thread did not start), so no destructor runs.
            CloseHandle(event);
            CloseHandle(timer);
            return dvv::makeError(dvv::Errc::Internal,
                                  std::format("the timer thread did not start: {}", error.what()));
        }
    }

    WaitableThreadTimer(Private /*key*/, HANDLE timer, HANDLE event)
        : timer_(timer), event_(event), thread_([this] { run(); }) {}
    WaitableThreadTimer(const WaitableThreadTimer&) = delete;
    WaitableThreadTimer& operator=(const WaitableThreadTimer&) = delete;
    WaitableThreadTimer(WaitableThreadTimer&&) = delete;
    WaitableThreadTimer& operator=(WaitableThreadTimer&&) = delete;
    ~WaitableThreadTimer() override {
        if (thread_.joinable()) {
            cancel();
            {
                std::lock_guard lock(mutex_);
                stop_ = true;
            }
            wake();
            thread_.join();
        }
        CloseHandle(event_);
        CloseHandle(timer_);
    }

private:
    void wake() noexcept override { SetEvent(event_); }
    [[nodiscard]] std::thread::id threadId() const noexcept override { return thread_.get_id(); }

    void run() {
        std::unique_lock lock(mutex_);
        while (!stop_) {
            if (fireIfDue(lock)) {
                continue;
            }
            DWORD timeout = INFINITE;
            bool armed = false;
            if (deadline_) {
                using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
                const std::int64_t ticks = std::max<std::int64_t>(
                    std::chrono::ceil<Ticks>(*deadline_ - Clock::now()).count(), 1);
                LARGE_INTEGER due{};
                due.QuadPart = -ticks;  // relative, in 100 ns units
                armed = SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0) != FALSE;
                if (!armed) {  // never seen; keeps the timer working at millisecond precision
                    timeout =
                        static_cast<DWORD>(std::min<std::int64_t>(ticks / 10'000 + 1, 0x7FFFFFFF));
                }
            }
            lock.unlock();
            const std::array<HANDLE, 2> handles{event_, timer_};
            // A schedule()/cancel() between unlock and wait has set the event, so nothing is lost.
            WaitForMultipleObjects(armed ? 2U : 1U, handles.data(), FALSE, timeout);
            lock.lock();
        }
    }

    HANDLE timer_;
    HANDLE event_;
    std::thread thread_;  // last: starts after the handles exist
};

#else

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

#endif

}  // namespace

std::shared_ptr<IClock> steadyClock() {
    return std::make_shared<SteadyClock>();
}

dvv::Result<std::shared_ptr<ITimer>> makeThreadTimer() {
#if defined(_WIN32)
    return WaitableThreadTimer::create();
#else
    try {
        return std::shared_ptr<ITimer>(std::make_shared<CvThreadTimer>());
    } catch (const std::exception& error) {
        return dvv::makeError(dvv::Errc::Internal,
                              std::format("the timer thread did not start: {}", error.what()));
    }
#endif
}

}  // namespace rawreel
