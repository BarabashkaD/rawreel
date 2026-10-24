#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>

#include "core/timer.h"

using rawreel::Clock;
using namespace std::chrono_literals;

namespace {

std::shared_ptr<rawreel::ITimer> makeTimer() {
    auto made = rawreel::makeThreadTimer();
    REQUIRE(made);
    return std::move(*made);
}

// Waits until `flag` is set (at most `timeout`).
bool waitFlag(const std::atomic<bool>& flag, Clock::duration timeout = 2s) {
    const auto deadline = Clock::now() + timeout;
    while (!flag.load()) {
        if (Clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

}  // namespace

// Its own suite and CTest entry (rawreel.core_timer), so the race checks repeat it.
TEST_SUITE("timer") {
    TEST_CASE("timer: steadyClock is the steady clock") {
        const std::shared_ptr<rawreel::IClock> clock = rawreel::steadyClock();
        const Clock::time_point first = clock->now();
        const Clock::time_point second = clock->now();
        CHECK(first <= second);
        const Clock::time_point real = Clock::now();
        CHECK(real - second < 1s);
        CHECK(second - real < 1s);
    }

    TEST_CASE("timer: fires once after the deadline") {
        const auto timer = makeTimer();
        std::atomic<int> calls{0};
        std::promise<Clock::time_point> fired;
        std::future<Clock::time_point> when = fired.get_future();
        const Clock::time_point deadline = Clock::now() + 30ms;
        timer->schedule(deadline, [&] {
            if (calls.fetch_add(1) == 0) {
                fired.set_value(Clock::now());
            }
        });
        REQUIRE(when.wait_for(2s) == std::future_status::ready);
        const Clock::time_point at = when.get();
        CHECK(at >= deadline);
        CHECK(at < deadline + 1s);
        timer->cancel();
        CHECK(calls.load() == 1);
    }

    TEST_CASE("timer: schedule replaces the pending deadline") {
        const auto timer = makeTimer();
        std::atomic<bool> a{false};
        std::atomic<bool> b{false};
        timer->schedule(Clock::now() + 10s, [&] { a = true; });
        timer->schedule(Clock::now() + 20ms, [&] { b = true; });
        CHECK(waitFlag(b));
        timer->cancel();
        CHECK_FALSE(a.load());
    }

    TEST_CASE("timer: cancel drops the pending callback") {
        const auto timer = makeTimer();
        std::atomic<int> a{0};
        std::atomic<bool> b{false};
        timer->schedule(Clock::now() + 50ms, [&] { ++a; });
        timer->cancel();
        timer->schedule(Clock::now() + 60ms, [&] { b = true; });
        CHECK(waitFlag(b));
        timer->cancel();
        CHECK(a.load() == 0);
    }

    TEST_CASE("timer: cancel waits for a running callback") {
        const auto timer = makeTimer();
        std::atomic<bool> entered{false};
        std::atomic<bool> returned{false};
        std::latch release(1);
        timer->schedule(Clock::now(), [&] {
            entered = true;
            release.wait();
            returned = true;
        });
        REQUIRE(waitFlag(entered));
        std::atomic<bool> passed{false};
        std::atomic<bool> cancelled{false};
        std::thread canceller([&] {
            timer->cancel();
            passed = returned.load();
            cancelled = true;
        });
        std::this_thread::sleep_for(
            20ms);  // give cancel() the chance to return early (it must not)
        CHECK_FALSE(cancelled.load());
        release.count_down();
        canceller.join();
        CHECK(passed.load());
    }

    TEST_CASE("timer: cancel inside a callback throws") {
        const auto timer = makeTimer();
        rawreel::ITimer* raw = timer.get();
        std::promise<bool> result;
        std::future<bool> threw = result.get_future();
        timer->schedule(Clock::now(), [raw, &result] {
            try {
                raw->cancel();
                result.set_value(false);
            } catch (const std::logic_error&) {
                result.set_value(true);
            }
        });
        REQUIRE(threw.wait_for(2s) == std::future_status::ready);
        CHECK(threw.get());
    }

    TEST_CASE("timer: a past deadline fires at once") {
        const auto timer = makeTimer();
        std::atomic<bool> fired{false};
        timer->schedule(Clock::now() - 1s, [&] { fired = true; });
        CHECK(waitFlag(fired, 1s));
    }

    TEST_CASE("timer: destruction cancels") {
        auto timer = makeTimer();
        const auto fired = std::make_shared<std::atomic<bool>>(false);
        timer->schedule(Clock::now() + 50ms, [fired] { *fired = true; });
        timer.reset();  // the thread is joined, so the callback can never run
        CHECK_FALSE(fired->load());
    }
}
