// Status subscriptions (spec §6.2): one overwriting notification per subscription until take(),
// take() clears before it reads, independent subscribers, and the destructor's binding (no
// callback after it returns), also while publishes run and after the Session is gone. "ryw:"
// pins read-your-writes (spec §6.3): a seek is the published target from the moment seek() returns.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/session.h"
#include "core/session_types.h"
#include "session_fixture.h"
#include "test_source_provider.h"

using rawreel::SessionStatus;
using rawreel::StatusSubscription;
using rawreel::testing::closeGate;
using rawreel::testing::ExecutorHold;
using rawreel::testing::releaseGate;
using rawreel::testing::SessionFixture;
using rawreel::testing::WAIT_TIMEOUT;
using namespace std::chrono_literals;

namespace {

constexpr const char* A = "synthetic:gradient?w=32&h=16&fmt=yuv420p&frames=40&fps=25";
constexpr std::uint32_t W = 8;  // PlayerConfig::window: frames ready ahead below index 32

// Counts the callbacks of one subscription; waitCount() is the only way to observe a new count.
class CallCounter {
public:
    void call() {
        {
            std::lock_guard lock(mutex_);
            count_.fetch_add(1);
        }
        changed_.notify_all();
    }
    [[nodiscard]] std::size_t count() const { return count_.load(); }
    void reset() { count_.store(0); }
    [[nodiscard]] bool waitCount(std::size_t n, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return count_.load() >= n; });
    }
    // The subscription callback: only counts (it runs on the Session executor).
    [[nodiscard]] auto callback() {
        return [this] { call(); };
    }

private:
    std::atomic<std::size_t> count_{0};
    std::mutex mutex_;
    std::condition_variable changed_;
};

// Records every published snapshot: its callback calls take() itself (re-arming the flag), so it
// runs once per publish and reads the snapshot that publish stored. arm() names the token.
class PublishRecorder {
public:
    void arm(StatusSubscription* sub) { sub_.store(sub); }
    [[nodiscard]] auto callback() {
        return [this] {
            if (StatusSubscription* sub = sub_.load()) {
                SessionStatus status = sub->take();
                {
                    std::lock_guard lock(mutex_);
                    records_.push_back(std::move(status));
                }
                changed_.notify_all();
            }
        };
    }
    [[nodiscard]] bool waitCount(std::size_t n) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, WAIT_TIMEOUT, [&] { return records_.size() >= n; });
    }
    [[nodiscard]] bool waitFor(const std::function<bool(const std::vector<SessionStatus>&)>& done) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, WAIT_TIMEOUT, [&] { return done(records_); });
    }
    [[nodiscard]] std::vector<SessionStatus> records() {
        std::lock_guard lock(mutex_);
        return records_;
    }
    [[nodiscard]] std::vector<std::int64_t> targets() {
        std::lock_guard lock(mutex_);
        std::vector<std::int64_t> targets;
        targets.reserve(records_.size());
        for (const SessionStatus& status : records_) {
            targets.push_back(status.player.target);
        }
        return targets;
    }
    void clear() {
        std::lock_guard lock(mutex_);
        records_.clear();
    }

private:
    std::atomic<StatusSubscription*> sub_{nullptr};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<SessionStatus> records_;
};

// A callback that blocks inside enterAndWait() until open() (or WAIT_TIMEOUT).
class Gate {
public:
    void enterAndWait() {
        std::unique_lock lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        (void)changed_.wait_for(lock, WAIT_TIMEOUT, [&] { return open_; });
    }
    [[nodiscard]] bool waitEntered() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, WAIT_TIMEOUT, [&] { return entered_; });
    }
    void open() {
        {
            std::lock_guard lock(mutex_);
            open_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_ = false;
    bool open_ = false;
};

// Opens A paused at frame 0 and waits until its window is ready and nothing is in flight.
void openSettled(SessionFixture& f) {
    f.open(0, A);
    REQUIRE(f.sink->waitShown(0));
    REQUIRE(f.settle(W));
}

}  // namespace

TEST_SUITE("session") {
    TEST_CASE("notify: one callback per burst until take") {
        SessionFixture f;
        openSettled(f);
        CallCounter counter;
        StatusSubscription sub = f.session->subscribeStatus(counter.callback());
        REQUIRE(sub);
        f.session->seek(5);
        f.session->seek(6);
        f.session->seek(7);
        REQUIRE(f.waitPosition(7));
        REQUIRE(counter.waitCount(1, WAIT_TIMEOUT));
        // Let the executor go idle: every later publish of this burst finds the flag set.
        REQUIRE(f.settle(0));
        std::this_thread::sleep_for(50ms);
        CHECK(counter.count() == 1);
        (void)sub.take();
        f.session->seek(8);
        REQUIRE(counter.waitCount(2, WAIT_TIMEOUT));
    }

    TEST_CASE("notify: take clears before it reads") {
        SessionFixture f;
        openSettled(f);
        CallCounter counter;
        StatusSubscription sub = f.session->subscribeStatus(counter.callback());
        f.session->seek(7);
        REQUIRE(f.waitPosition(7));
        REQUIRE(counter.waitCount(1, WAIT_TIMEOUT));
        REQUIRE(f.settle(W));  // nothing of the seek to 7 is left to publish
        CHECK(sub.take().player.target == 7);
        f.session->seek(9);
        REQUIRE(counter.waitCount(2, WAIT_TIMEOUT));
        CHECK(sub.take().player.target == 9);
    }

    TEST_CASE("notify: an unchanged snapshot calls nothing") {
        SessionFixture f;
        openSettled(f);  // idle from here on: nothing publishes until a command changes something
        PublishRecorder recorder;
        StatusSubscription sub = f.session->subscribeStatus(recorder.callback());
        recorder.arm(&sub);
        const auto before = f.session->status().revision;
        f.session->setLoop(false);  // already off: no change
        f.session->setLoop(false);
        f.session->setLoop(true);  // a command too, so it runs after the no-op ones (FIFO)
        REQUIRE(recorder.waitCount(1));
        // The first publish after the no-op commands is the loop change itself.
        const std::vector<SessionStatus> records = recorder.records();
        CHECK(records.front().player.loop);
        CHECK(records.front().revision == before + 1);
        recorder.arm(nullptr);
    }

    TEST_CASE("notify: the destructor waits for a running callback") {
        SessionFixture f;
        openSettled(f);
        Gate gate;
        std::atomic<int> calls{0};
        auto sub = std::make_unique<StatusSubscription>(f.session->subscribeStatus([&] {
            if (calls.fetch_add(1) == 0) {
                gate.enterAndWait();  // the first callback blocks: provably running
            }
        }));
        f.session->seek(5);
        REQUIRE(gate.waitEntered());
        std::atomic<bool> returned{false};
        std::thread destroyer([&] {
            sub.reset();
            returned.store(true);
        });
        std::this_thread::sleep_for(100ms);
        CHECK_FALSE(returned.load());  // still waiting for the blocked callback
        gate.open();
        destroyer.join();
        CHECK(returned.load());
        // Later changes call nothing.
        f.session->seek(10);
        REQUIRE(f.waitPosition(10));
        f.session->seek(12);
        REQUIRE(f.waitPosition(12));
        CHECK(calls.load() == 1);
    }

    TEST_CASE("notify: no callback after the destructor returned") {
        SessionFixture f;
        openSettled(f);
        // `blocker` is first in the list: its callback holds the fan-out after it has copied the
        // list with `victim` in it; `victim` is destroyed meanwhile.
        Gate gate;
        std::atomic<int> blocker_calls{0};
        StatusSubscription blocker = f.session->subscribeStatus([&] {
            if (blocker_calls.fetch_add(1) == 0) {
                gate.enterAndWait();
            }
        });
        std::atomic<bool> destroyed{false};
        std::atomic<bool> violated{false};
        auto victim = std::make_unique<StatusSubscription>(f.session->subscribeStatus([&] {
            if (destroyed.load()) {
                violated.store(true);
            }
        }));
        f.session->seek(5);
        REQUIRE(gate.waitEntered());
        victim.reset();
        destroyed.store(true);
        gate.open();
        f.session->seek(6);  // its snapshot is stored after the held fan-out has finished
        REQUIRE(f.waitPosition(6));
        CHECK_FALSE(violated.load());
    }

    TEST_CASE("notify: subscribers are independent") {
        SessionFixture f;
        openSettled(f);
        CallCounter a_counter;
        CallCounter b_counter;
        StatusSubscription a = f.session->subscribeStatus(a_counter.callback());
        StatusSubscription b = f.session->subscribeStatus(b_counter.callback());
        f.session->seek(5);
        REQUIRE(a_counter.waitCount(1, WAIT_TIMEOUT));
        REQUIRE(b_counter.waitCount(1, WAIT_TIMEOUT));
        (void)a.take();
        f.session->seek(6);
        REQUIRE(a_counter.waitCount(2, WAIT_TIMEOUT));
        CHECK(b_counter.count() == 1);  // b still pending
    }

    TEST_CASE("notify: destroying a token while publishes run") {
        SessionFixture f{rawreel::testing::RealTime{}};
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->setLoop(true);
        f.session->play();
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.player.playing; }));

        struct Probe {
            std::atomic<bool> destroyed{false};
            std::atomic<bool> violated{false};
        };
        constexpr int ITERATIONS = 500;
        std::mt19937 random(20261108);
        std::uniform_int_distribution<int> micros(0, 2000);
        std::vector<std::shared_ptr<Probe>> probes;
        probes.reserve(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            auto probe = std::make_shared<Probe>();
            probes.push_back(probe);
            {
                StatusSubscription sub = f.session->subscribeStatus([probe] {
                    if (probe->destroyed.load()) {
                        probe->violated.store(true);
                    }
                });
                // take() re-arms the flag, so the callback really races the destructor.
                const auto until =
                    std::chrono::steady_clock::now() + std::chrono::microseconds(micros(random));
                do {
                    (void)sub.take();
                } while (std::chrono::steady_clock::now() < until);
            }
            probe->destroyed.store(true);
        }
        std::this_thread::sleep_for(100ms);  // a late callback would show up here
        bool violated = false;
        for (const auto& probe : probes) {
            violated = violated || probe->violated.load();
        }
        CHECK_FALSE(violated);
    }

    TEST_CASE("notify: a token that outlives the session") {
        SessionFixture f;
        CallCounter counter;
        StatusSubscription sub = f.session->subscribeStatus(counter.callback());
        CHECK(sub);
        f.shutdown();
        f.session.reset();
        CHECK(sub.take().revision == 0);
    }  // the token's destructor finds no Session

    TEST_CASE("ryw: a seek is the target as soon as seek returns") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        REQUIRE(f.settle(1));
        ExecutorHold hold(f);
        f.session->seek(7);
        CHECK(f.session->status().player.target == 7);  // the executor has not run it
        hold.release();
        REQUIRE(f.waitPosition(7));
    }

    TEST_CASE("ryw: snapshots while seeks are queued show the newest seek") {
        SessionFixture f;
        openSettled(f);
        PublishRecorder recorder;
        StatusSubscription sub = f.session->subscribeStatus(recorder.callback());
        recorder.arm(&sub);
        {
            ExecutorHold hold(f);
            recorder.clear();  // the hold's own publish was recorded before it blocked
            f.session->seek(5);
            f.session->seek(7);
            hold.release();
            REQUIRE(f.waitPosition(7));
            REQUIRE(f.settle(1));
            const std::vector<std::int64_t> targets = recorder.targets();
            REQUIRE_FALSE(targets.empty());
            CHECK(std::ranges::all_of(targets, [](std::int64_t t) { return t == 7; }));
            recorder.arm(nullptr);
        }
    }

    TEST_CASE("ryw: a patched seek is notified when the executor snapshot equals it") {
        // Four frames, 3 gated: at position 0 with loop on, frames 1 and 2 are ready, 3 is
        // outstanding and every other index of the window is ready, outstanding or the position.
        // So seek(3) changes the target and nothing else: the executor's snapshot after it equals
        // the patched one.
        constexpr std::string_view GATE = "ryw-g3";
        closeGate(GATE);
        SessionFixture f;
        f.open(0, "slow:gradient?frames=4&index=3&gate=ryw-g3");
        REQUIRE(f.sink->waitShown(0));
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.player.position == 0 && s.inflight == 1 && s.readyAhead == 2;
        }));
        CallCounter counter;
        StatusSubscription s = f.session->subscribeStatus(counter.callback());
        (void)s.take();
        {
            ExecutorHold hold(f);  // its publish notifies s too (s is first in the list)
            f.session->seek(3);    // the patch: target 3
            CHECK(f.session->status().player.target == 3);
            (void)s.take();
            counter.reset();
            hold.release();
            REQUIRE(counter.waitCount(1, WAIT_TIMEOUT));
            CHECK(s.take().player.target == 3);
        }
        releaseGate(GATE);
        REQUIRE(f.waitPosition(3));
    }

    TEST_CASE("ryw: a seek refused during shutdown leaves no queued target") {
        SessionFixture f;
        openSettled(f);
        {
            ExecutorHold hold(f);
            std::thread stopper([&] { f.shutdown(); });
            std::this_thread::sleep_for(50ms);  // its first step is queued behind the hold
            f.session->seek(5);                 // patched, counted, queued after that step
            hold.release();
            stopper.join();
            CHECK(f.session->status().player.target == -1);
            CHECK(f.session->status().streams == 0);
        }
    }

    TEST_CASE("ryw: a step after queued seeks") {
        SessionFixture f;
        openSettled(f);
        PublishRecorder recorder;
        StatusSubscription sub = f.session->subscribeStatus(recorder.callback());
        recorder.arm(&sub);
        {
            ExecutorHold hold(f);
            recorder.clear();
            f.session->seek(5);
            f.session->seek(7);
            f.session->step(+1);
            hold.release();
            REQUIRE(f.waitPosition(8));
            REQUIRE(recorder.waitFor([](const std::vector<SessionStatus>& records) {
                return !records.empty() && records.back().player.position == 8;
            }));
            const std::vector<std::int64_t> targets = recorder.targets();
            REQUIRE_FALSE(targets.empty());
            // Only 7 then 8: never 5, never back to 7 after 8.
            const auto first8 = std::ranges::find(targets, 8);
            CHECK(first8 != targets.end());
            CHECK(std::all_of(targets.begin(), first8, [](std::int64_t t) { return t == 7; }));
            CHECK(std::all_of(first8, targets.end(), [](std::int64_t t) { return t == 8; }));
            recorder.arm(nullptr);
        }
    }

    TEST_CASE("ryw: the call-time clamp") {
        SessionFixture f;
        openSettled(f);
        ExecutorHold hold(f);
        f.session->seek(1000);
        CHECK(f.session->status().player.target == 39);
        f.session->seek(-5);
        CHECK(f.session->status().player.target == 0);
    }

    TEST_CASE("ryw: no patch without a stream") {
        SessionFixture f;
        REQUIRE(
            f.waitStatus([](const SessionStatus& s) { return s.gpuAvailable && s.revision >= 1; }));
        const std::uint64_t r = f.session->status().revision;
        f.session->seek(5);
        CHECK(f.session->status().player.target == -1);
        CHECK(f.session->status().revision == r);
    }

    TEST_CASE("notify: take on an empty token") {
        StatusSubscription empty;
        CHECK_FALSE(empty);
        CHECK(empty.take().revision == 0);
        StatusSubscription moved_to = std::move(empty);
        CHECK_FALSE(moved_to);
    }
}
