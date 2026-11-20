#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <ratio>
#include <thread>

#include <windows.h>

#include "core/timer.h"
#include "core/timer_detail.h"
#include "dvv/base/error.h"

namespace rawreel {
namespace {

using detail::ThreadTimerBase;

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
    bool stop_ = false;   // guarded by mutex_
    std::thread thread_;  // last: starts after the handles exist
};

}  // namespace

dvv::Result<std::shared_ptr<ITimer>> makeThreadTimer() {
    return WaitableThreadTimer::create();
}

}  // namespace rawreel
