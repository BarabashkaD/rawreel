#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "core/player.h"

using rawreel::Clock;
using rawreel::PlayerConfig;
using rawreel::PlayerLogic;
using namespace std::chrono_literals;

namespace {

using Indices = std::vector<std::int64_t>;

const Clock::time_point T0 = Clock::time_point{} + std::chrono::hours(1);
constexpr auto P = 40ms;  // 25 fps

PlayerConfig config(std::int64_t length) {
    return {.length = length, .fps = {25, 1}, .window = 8, .inflight = 4};
}

// Stands in for the Session: records what the player requested and answers it.
struct Feeder {
    std::set<std::int64_t> pending;
    std::size_t requested = 0;  // every index take() returned

    // takeRequests(); an index must never be requested while it is still pending.
    Indices take(PlayerLogic& p) {
        Indices requests = p.takeRequests();
        requested += requests.size();
        for (const std::int64_t index : requests) {
            CHECK_MESSAGE(pending.insert(index).second, "requested twice: ", index);
        }
        return requests;
    }
    // Requests and answers until a round delivers nothing; indices in `except` stay pending.
    void fill(PlayerLogic& p, Clock::time_point now, const std::set<std::int64_t>& except = {}) {
        for (;;) {
            take(p);
            bool delivered = false;
            for (auto it = pending.begin(); it != pending.end();) {
                if (except.contains(*it)) {
                    ++it;
                    continue;
                }
                const std::int64_t index = *it;
                it = pending.erase(it);
                static_cast<void>(p.frameReady(index, now));
                delivered = true;
            }
            if (!delivered) {
                return;
            }
        }
    }
    bool ready(PlayerLogic& p, std::int64_t index, Clock::time_point now) {
        pending.erase(index);
        return p.frameReady(index, now);
    }
    void failed(PlayerLogic& p, std::int64_t index) {
        pending.erase(index);
        p.frameFailed(index);
    }
    void reset(PlayerLogic& p, const PlayerConfig& c, std::int64_t position) {
        pending.clear();
        p.reset(c, position);
    }
    // The Session drops the results of the old generation: nothing is pending any more.
    void invalidate(PlayerLogic& p) {
        pending.clear();
        p.invalidate();
    }
};

bool contains(const Indices& indices, std::int64_t index) {
    return std::ranges::find(indices, index) != indices.end();
}

// Case 1's start: reset at 0, show 0, play at T0 and tick `frames` frames.
void playFrames(PlayerLogic& p, Feeder& feeder, int frames) {
    feeder.reset(p, config(100), 0);
    CHECK(feeder.take(p) == Indices{0, 1, 2, 3});
    feeder.fill(p, T0);
    CHECK(p.takeShow() == 0);
    p.play(T0);
    CHECK(p.nextDeadline() == T0 + P);
    for (int k = 1; k <= frames; ++k) {
        feeder.fill(p, T0 + k * P - 1ms);
        p.tick(T0 + k * P);
        CHECK(p.takeShow() == k);
    }
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("player: play shows the next frames at 1/fps") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 30);
        const auto status = p.status(T0 + 30 * P);
        CHECK(status.shown == 31);
        CHECK(status.stalls == 0);
        CHECK(status.actualFps == 25);
        CHECK(status.targetFps == 25);
        CHECK(status.playing);
        CHECK(status.position == 30);
    }

    TEST_CASE("player: a missing frame stalls without skipping and re-anchors") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 0);
        feeder.fill(p, T0, {5});
        CHECK(p.takeShow() == 0);
        p.play(T0);
        for (int k = 1; k <= 4; ++k) {
            feeder.fill(p, T0 + k * P - 1ms, {5});
            p.tick(T0 + k * P);
            CHECK(p.takeShow() == k);
        }
        feeder.fill(p, T0 + 5 * P - 1ms, {5});
        p.tick(T0 + 5 * P);
        CHECK_FALSE(p.takeShow());
        CHECK(p.status(T0 + 5 * P).stalled);
        CHECK(p.status(T0 + 5 * P).stalls == 1);
        CHECK_FALSE(p.nextDeadline());
        p.tick(T0 + 6 * P);
        CHECK_FALSE(p.takeShow());
        CHECK(p.status(T0 + 6 * P).stalls == 1);
        const auto late = T0 + 6 * P + 10ms;
        CHECK(feeder.ready(p, 5, late));
        CHECK(p.takeShow() == 5);
        CHECK_FALSE(p.status(late).stalled);
        CHECK(p.nextDeadline() == late + P);
        p.tick(late + P - 1ms);
        CHECK_FALSE(p.takeShow());
        p.tick(late + P);
        CHECK(p.takeShow() == 6);
    }

    TEST_CASE("player: the end pauses or loops") {
        PlayerLogic p(config(10));
        Feeder feeder;
        feeder.reset(p, config(10), 8);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 8);
        p.play(T0);
        p.tick(T0 + P);
        CHECK(p.takeShow() == 9);
        p.tick(T0 + 2 * P);
        CHECK_FALSE(p.takeShow());
        CHECK_FALSE(p.status(T0 + 2 * P).playing);
        CHECK(p.status(T0 + 2 * P).position == 9);

        // Play at the last frame without loop restarts from the beginning.
        const auto t1 = T0 + 10s;
        p.play(t1);
        feeder.fill(p, t1);
        CHECK(p.takeShow() == 0);

        p.setLoop(true);
        feeder.reset(p, config(10), 8);
        CHECK(feeder.take(p) == Indices{8, 9, 0, 1});
        const auto t2 = T0 + 20s;
        feeder.fill(p, t2);
        CHECK(p.takeShow() == 8);
        p.play(t2);
        for (const std::int64_t expected : {9, 0, 1}) {
            feeder.fill(p, t2);
            const auto deadline = p.nextDeadline();
            REQUIRE(deadline);
            p.tick(deadline.value_or(rawreel::Clock::time_point{}));
            CHECK(p.takeShow() == expected);
        }
        CHECK(p.status(t2).loop);
    }

    TEST_CASE("player: step and seek pause and show the target when ready") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 3);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 3);
        p.play(T0);
        feeder.fill(p, T0);
        p.tick(T0 + P);
        CHECK(p.takeShow() == 4);

        p.seek(20);
        CHECK_FALSE(p.status(T0 + P).playing);
        CHECK_FALSE(p.takeShow());
        CHECK(p.status(T0 + P).position == 4);
        CHECK(feeder.take(p) == Indices{20, 21, 22, 23});
        CHECK(feeder.ready(p, 20, T0 + 2 * P));
        CHECK(p.takeShow() == 20);
        feeder.fill(p, T0 + 2 * P);

        p.step(-1);
        CHECK(feeder.take(p) == Indices{19, 18, 17, 16});
        CHECK(feeder.ready(p, 19, T0 + 3 * P));
        CHECK(p.takeShow() == 19);
        p.step(+1);
        CHECK(p.takeShow() == 20);

        feeder.reset(p, config(100), 50);
        p.seek(1000);
        CHECK(feeder.take(p) == Indices{99});
        feeder.reset(p, config(100), 50);
        p.seek(-5);
        CHECK(feeder.take(p) == Indices{0});
    }

    TEST_CASE("player: requests respect the cap, the window and the direction") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 50);
        CHECK(feeder.take(p) == Indices{50, 51, 52, 53});
        CHECK(feeder.take(p).empty());
        CHECK(feeder.ready(p, 50, T0));
        CHECK(feeder.take(p) == Indices{54});
        for (std::int64_t index = 51; index <= 54; ++index) {
            CHECK(feeder.ready(p, index, T0));
        }
        CHECK(feeder.take(p) == Indices{55, 56, 57, 58});

        feeder.reset(p, config(100), 2);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 2);
        p.step(-1);
        CHECK(feeder.take(p) == Indices{1, 0});

        feeder.reset(p, config(100), 97);
        CHECK(feeder.take(p) == Indices{97, 98, 99});
    }

    TEST_CASE("player: a failed frame pauses at the last shown frame") {
        PlayerLogic p(config(100));
        Feeder feeder;
        const std::set<std::int64_t> skip{12};
        feeder.reset(p, config(100), 10);
        feeder.fill(p, T0, skip);
        CHECK(p.takeShow() == 10);
        p.play(T0);
        feeder.fill(p, T0, skip);
        p.tick(T0 + P);
        CHECK(p.takeShow() == 11);
        feeder.fill(p, T0 + P, skip);
        p.tick(T0 + 2 * P);
        CHECK(p.status(T0 + 2 * P).stalled);

        feeder.failed(p, 12);
        const auto status = p.status(T0 + 2 * P);
        CHECK_FALSE(status.playing);
        CHECK_FALSE(status.stalled);
        CHECK(status.position == 11);
        CHECK_FALSE(p.takeShow());
        CHECK_FALSE(contains(feeder.take(p), 12));
        feeder.fill(p, T0 + 2 * P, skip);  // 12 would stay pending if it were requested
        CHECK_FALSE(feeder.pending.contains(12));

        p.seek(12);
        CHECK(contains(feeder.take(p), 12));
    }

    TEST_CASE("player: an unbounded stream has no end and ignores loop") {
        const PlayerConfig unbounded{
            .length = std::nullopt, .fps = {25, 1}, .window = 8, .inflight = 4};
        PlayerLogic p(unbounded);
        Feeder feeder;
        p.setLoop(true);
        CHECK_FALSE(p.status(T0).loop);
        feeder.reset(p, unbounded, 0);
        p.seek(1'000'000);
        const Indices requests = feeder.take(p);
        REQUIRE(requests.size() >= 2);
        CHECK(requests[0] == 1'000'000);
        CHECK(requests[1] == 1'000'001);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 1'000'000);
        p.play(T0);
        feeder.fill(p, T0);
        p.tick(T0 + P);
        CHECK(p.takeShow() == 1'000'001);
        feeder.fill(p, T0 + P);
        p.tick(T0 + 2 * P);
        CHECK(p.takeShow() == 1'000'002);
        CHECK_FALSE(p.status(T0 + 2 * P).length);
    }

    TEST_CASE("player: actualFps counts the last second") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 50);
        CHECK(p.status(T0 + 50 * P).actualFps == 25);
        p.pause();
        CHECK(p.status(T0 + 50 * P + 500ms).actualFps == 13);
    }

    TEST_CASE("player: reset drops readiness and outstanding marks") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 0);
        CHECK(feeder.take(p) == Indices{0, 1, 2, 3});
        CHECK(feeder.ready(p, 0, T0));
        feeder.reset(p, config(100), 0);
        CHECK_FALSE(p.takeShow());
        CHECK(feeder.take(p) == Indices{0, 1, 2, 3});
    }

    TEST_CASE("player: a looping short clip never stalls") {
        const PlayerConfig short_clip{.length = 4, .fps = {25, 1}, .window = 8, .inflight = 4};
        PlayerLogic p(short_clip);
        Feeder feeder;
        p.setLoop(true);
        feeder.reset(p, short_clip, 0);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 0);
        p.play(T0);
        for (int k = 1; k <= 20; ++k) {
            p.tick(T0 + k * P);
            CHECK(p.takeShow() == k % 4);
            CHECK(feeder.take(p).empty());
            for (std::int64_t index = 0; index < 4; ++index) {
                CHECK(p.keeps(index));
            }
        }
        CHECK(p.status(T0 + 20 * P).stalls == 0);
    }

    TEST_CASE("player: a step past either end wraps with loop and stops there without") {
        const PlayerConfig clip{.length = 20, .fps = {25, 1}, .window = 8, .inflight = 4};
        PlayerLogic p(clip);
        Feeder feeder;
        feeder.reset(p, clip, 19);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 19);
        p.step(1);
        CHECK(p.target() == 19);  // no loop: the last frame stays
        p.setLoop(true);
        p.step(1);
        CHECK(p.target() == 0);
        p.step(-1);
        CHECK(p.target() == 19);
        p.step(10);
        CHECK(p.target() == 9);
        p.step(-10);
        CHECK(p.target() == 19);
        p.setLoop(false);
        p.seek(0);
        p.step(-1);
        CHECK(p.target() == 0);  // no loop: the first frame stays
    }

    TEST_CASE("player: a failure ahead of the position pauses when it is due") {
        PlayerLogic p(config(100));
        Feeder feeder;
        const std::set<std::int64_t> skip{12};
        feeder.reset(p, config(100), 4);
        feeder.fill(p, T0, skip);
        CHECK(p.takeShow() == 4);
        p.play(T0);
        feeder.failed(p, 12);
        CHECK(p.status(T0).playing);
        for (int k = 1; k <= 7; ++k) {
            feeder.fill(p, T0 + k * P - 1ms, skip);
            p.tick(T0 + k * P);
            CHECK(p.takeShow() == 4 + k);
        }
        p.tick(T0 + 8 * P);
        CHECK_FALSE(p.takeShow());
        const auto status = p.status(T0 + 8 * P);
        CHECK_FALSE(status.playing);
        CHECK_FALSE(status.stalled);
        CHECK(status.position == 11);
        CHECK(status.stalls == 0);
    }

    // D11 "no catch-up": the deadline advances by one period from the previous deadline (no drift
    // after a slightly late tick); a tick later than one period shows one frame and re-anchors.
    TEST_CASE("player: the deadline advances one period and a late tick re-anchors") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 3);
        CHECK(p.nextDeadline() == T0 + 4 * P);

        feeder.fill(p, T0 + 4 * P - 2ms);
        p.tick(T0 + 4 * P - 1ms);  // early: nothing
        CHECK_FALSE(p.takeShow());
        CHECK(p.nextDeadline() == T0 + 4 * P);

        p.tick(T0 + 4 * P + 5ms);  // slightly late: the old deadline plus one period
        CHECK(p.takeShow() == 4);
        CHECK(p.nextDeadline() == T0 + 5 * P);

        feeder.fill(p, T0 + 5 * P);
        const auto late = T0 + 7 * P + 5ms;  // more than one period late
        p.tick(late);
        CHECK(p.takeShow() == 5);
        CHECK(p.nextDeadline() == late + P);
        p.tick(late);  // no burst
        CHECK_FALSE(p.takeShow());
        p.tick(late + P - 1ms);
        CHECK_FALSE(p.takeShow());
        p.tick(late + P);
        CHECK(p.takeShow() == 6);
        CHECK(p.status(late + P).stalls == 0);
    }

    TEST_CASE("player: keeps follows the direction and wraps when looping") {
        // Backward: after step(-1) the range reaches W behind the position and 2 ahead.
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 20);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 20);
        p.step(-1);
        CHECK_FALSE(p.takeShow());  // 19 is not ready yet
        CHECK(feeder.take(p) == Indices{19, 18, 17, 16});
        for (std::int64_t index = 19; index >= 12; --index) {
            CHECK_MESSAGE(feeder.ready(p, index, T0), "index ", index);
        }
        CHECK(p.takeShow() == 19);
        CHECK(p.keepRange() == std::pair<std::int64_t, std::int64_t>{19 - 8, 21});
        const Indices later = feeder.take(p);
        for (std::int64_t index = 12; index <= 19; ++index) {
            CHECK_FALSE_MESSAGE(contains(later, index), "requested again: ", index);
        }
        CHECK(later == Indices{11});

        // Looping: the range [16, 26] of a 20-frame clip wraps to 16..19 and 0..6.
        const PlayerConfig clip = config(20);
        PlayerLogic q(clip);
        Feeder loop_feeder;
        q.setLoop(true);
        loop_feeder.reset(q, clip, 18);
        // Explicit answers, not fill(): a wrong keep rule would make fill() re-request forever.
        CHECK(loop_feeder.take(q) == Indices{18, 19, 0, 1});
        for (const std::int64_t index : {18, 19, 0, 1}) {
            CHECK_MESSAGE(loop_feeder.ready(q, index, T0), "index ", index);
        }
        CHECK(q.takeShow() == 18);
        CHECK(q.keepRange() == std::pair<std::int64_t, std::int64_t>{16, 26});
        CHECK(q.keeps(16));
        CHECK(q.keeps(19));
        CHECK(q.keeps(0));
        CHECK(q.keeps(2));
        CHECK(q.keeps(6));
        CHECK_FALSE(q.keeps(7));
        CHECK_FALSE(q.keeps(10));
        CHECK_FALSE(q.keeps(15));
    }

    // The Session releases the leases of every index outside keeps() (P4), so an index that left
    // the keep range must not count as ready when it comes back.
    TEST_CASE("player: a frame that leaves the keep range is no longer ready") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 20);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 20);
        p.seek(5);  // 21..28 leave the range; 5 is not ready yet
        CHECK_FALSE(p.takeShow());
        CHECK_FALSE(p.keeps(25));
        p.seek(25);
        CHECK_FALSE(p.takeShow());  // released by the Session: it must be requested again
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 25);

        // Loop off drops the wrapped frames; loop on again must request them.
        const PlayerConfig clip = config(20);
        PlayerLogic q(clip);
        Feeder loop_feeder;
        q.setLoop(true);
        loop_feeder.reset(q, clip, 18);
        CHECK(loop_feeder.take(q) == Indices{18, 19, 0, 1});
        for (const std::int64_t index : {18, 19, 0, 1}) {
            CHECK(loop_feeder.ready(q, index, T0));
        }
        CHECK(q.takeShow() == 18);
        q.setLoop(false);
        CHECK_FALSE(q.keeps(0));
        q.setLoop(true);
        q.play(T0);
        q.tick(T0 + P);
        CHECK(q.takeShow() == 19);
        q.tick(T0 + 2 * P);
        CHECK_FALSE(q.takeShow());  // 0 was released while loop was off
        CHECK(q.status(T0 + 2 * P).stalled);
        CHECK(contains(loop_feeder.take(q), 0));
    }

    TEST_CASE("player: status target is the pending seek, else the position") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 0);
        feeder.take(p);
        CHECK(feeder.ready(p, 0, T0));
        CHECK(p.takeShow() == 0);
        p.seek(50);
        CHECK(p.status(T0).position == 0);
        CHECK(p.status(T0).target == 50);
        feeder.take(p);
        CHECK(feeder.ready(p, 50, T0));
        CHECK(p.takeShow() == 50);
        CHECK(p.status(T0).position == 50);
        CHECK(p.status(T0).target == 50);
    }

    TEST_CASE("player: invalidate in pause re-requests the shown frame") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 10);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 10);
        CHECK(p.status(T0).position == 10);
        feeder.invalidate(p);
        CHECK(p.status(T0).position == 10);
        const std::size_t before = feeder.requested;
        // The shown frame first, then the window (window 8) up to the cap (inflight 4).
        CHECK(feeder.take(p) == Indices{10, 11, 12, 13});
        CHECK(p.status(T0).position == 10);
        CHECK(feeder.ready(p, 10, T0));
        CHECK(p.takeShow() == 10);
        const auto status = p.status(T0);
        CHECK(status.position == 10);
        CHECK(status.target == 10);
        CHECK_FALSE(status.playing);
        CHECK(status.shown == 2);
        // In all, the shown frame and the window once each: 1 + 8 requests, no second show.
        feeder.fill(p, T0);
        CHECK(feeder.requested - before == 9);
        CHECK_FALSE(p.takeShow());
    }

    TEST_CASE("player: invalidate in playback then pause re-requests the shown frame") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 3);
        feeder.invalidate(p);
        CHECK_FALSE(contains(feeder.take(p), 3));
        p.pause();
        CHECK(p.status(T0 + 3 * P).target == 3);
        // The window holds every slot: the position goes out when the first slot is free.
        feeder.fill(p, T0 + 3 * P);
        CHECK(p.takeShow() == 3);
        CHECK(p.status(T0 + 3 * P).position == 3);
        // Shown with the new view: a second pause asks for nothing.
        p.pause();
        CHECK(feeder.take(p).empty());
        CHECK_FALSE(p.takeShow());
    }

    TEST_CASE("player: invalidate in playback then a stall and a pause re-request the position") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 3);
        feeder.invalidate(p);
        CHECK(contains(feeder.take(p), 4));
        p.tick(T0 + 4 * P);
        REQUIRE(p.status(T0 + 4 * P).stalled);
        CHECK(p.target() == 4);
        p.pause();
        CHECK(p.target() == 3);
        feeder.fill(p, T0 + 4 * P);
        CHECK(p.takeShow() == 3);
        CHECK(p.status(T0 + 4 * P).position == 3);
    }

    TEST_CASE("player: invalidate in playback then a failed due frame re-requests the position") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 3);
        feeder.invalidate(p);
        CHECK(contains(feeder.take(p), 4));
        p.tick(T0 + 4 * P);
        REQUIRE(p.status(T0 + 4 * P).stalled);
        feeder.failed(p, 4);
        CHECK_FALSE(p.status(T0 + 4 * P).playing);
        CHECK(p.target() == 3);
        CHECK(contains(feeder.take(p), 3));
        CHECK(feeder.ready(p, 3, T0 + 4 * P));
        CHECK(p.takeShow() == 3);
    }

    TEST_CASE("player: invalidate in playback then the end re-requests the last frame") {
        PlayerLogic p(config(5));
        Feeder feeder;
        feeder.reset(p, config(5), 0);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 0);
        p.play(T0);
        for (int k = 1; k <= 4; ++k) {
            feeder.fill(p, T0 + k * P - 1ms);
            p.tick(T0 + k * P);
            CHECK(p.takeShow() == k);
        }
        feeder.invalidate(p);
        CHECK(feeder.take(p).empty());
        p.tick(T0 + 5 * P);  // the end without loop pauses
        CHECK_FALSE(p.status(T0 + 5 * P).playing);
        CHECK(feeder.take(p) == Indices{4});
        CHECK(feeder.ready(p, 4, T0 + 5 * P));
        CHECK(p.takeShow() == 4);
    }

    TEST_CASE("player: play right after invalidate in pause plays on without a stall") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 10);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 10);
        feeder.invalidate(p);
        CHECK(feeder.take(p) == Indices{10, 11, 12, 13});
        p.play(T0);
        // The re-request of the shown frame is only read-ahead now: the first tick shows 11.
        CHECK(p.target() == 10);
        feeder.fill(p, T0 + P - 1ms, {10});
        CHECK_FALSE(p.takeShow());
        p.tick(T0 + P);
        CHECK(p.takeShow() == 11);
        const auto status = p.status(T0 + P);
        CHECK(status.playing);
        CHECK_FALSE(status.stalled);
        CHECK(status.stalls == 0);
    }

    TEST_CASE("player: a pause after play right after invalidate shows the arrived position") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 10);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 10);
        feeder.invalidate(p);
        CHECK(feeder.take(p) == Indices{10, 11, 12, 13});
        p.play(T0);
        CHECK(feeder.ready(p, 10, T0 + P - 1ms));
        CHECK_FALSE(p.takeShow());
        p.tick(T0 + P);  // 11 is not there: a stall
        REQUIRE(p.status(T0 + P).stalled);
        p.pause();
        // 10 arrived with the new view and stays ready: the pause shows it.
        CHECK(p.takeShow() == 10);
        CHECK(p.status(T0 + P).position == 10);
        CHECK_FALSE(contains(feeder.take(p), 10));
        feeder.fill(p, T0 + P);
        CHECK_FALSE(p.takeShow());
    }

    TEST_CASE("player: a failed due frame after play right after invalidate shows the position") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 10);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 10);
        feeder.invalidate(p);
        CHECK(feeder.take(p) == Indices{10, 11, 12, 13});
        p.play(T0);
        CHECK(feeder.ready(p, 10, T0 + P - 1ms));
        p.tick(T0 + P);
        REQUIRE(p.status(T0 + P).stalled);
        feeder.failed(p, 11);
        CHECK_FALSE(p.status(T0 + P).playing);
        CHECK(p.takeShow() == 10);
        CHECK(p.status(T0 + P).position == 10);
    }

    TEST_CASE("player: play right after invalidate at the end restarts from the beginning") {
        PlayerLogic p(config(5));
        Feeder feeder;
        feeder.reset(p, config(5), 0);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 0);
        p.play(T0);
        for (int k = 1; k <= 4; ++k) {
            feeder.fill(p, T0 + k * P - 1ms);
            p.tick(T0 + k * P);
            CHECK(p.takeShow() == k);
        }
        feeder.invalidate(p);
        p.tick(T0 + 5 * P);  // the end without loop pauses and re-requests the last frame
        CHECK_FALSE(p.status(T0 + 5 * P).playing);
        CHECK(feeder.take(p) == Indices{4});
        p.play(T0 + 5 * P);
        CHECK(p.target() == 0);
        feeder.fill(p, T0 + 5 * P);
        CHECK(p.takeShow() == 0);
        CHECK(p.status(T0 + 5 * P).playing);
    }

    TEST_CASE("player: invalidate in playback then a seek to the position re-requests it") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 3);
        feeder.invalidate(p);
        static_cast<void>(feeder.take(p));
        p.seek(3);
        CHECK(p.target() == 3);
        feeder.fill(p, T0 + 3 * P);
        CHECK(p.takeShow() == 3);
    }

    TEST_CASE("player: a failed re-request of the shown frame is not retried until the next set") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 10);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 10);
        feeder.invalidate(p);
        CHECK(contains(feeder.take(p), 10));
        feeder.failed(p, 10);
        CHECK(p.status(T0).position == 10);
        p.pause();
        CHECK_FALSE(contains(feeder.take(p), 10));  // no request loop
        // The next set asks for the shown frame again.
        feeder.invalidate(p);
        CHECK(contains(feeder.take(p), 10));
        CHECK(feeder.ready(p, 10, T0));
        CHECK(p.takeShow() == 10);
    }

    TEST_CASE("player: invalidate keeps a pending seek target") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 0);
        feeder.fill(p, T0);
        CHECK(p.takeShow() == 0);
        p.seek(50);
        CHECK(contains(feeder.take(p), 50));
        feeder.invalidate(p);
        CHECK(p.target() == 50);
        CHECK(contains(feeder.take(p), 50));
        CHECK(p.status(T0).position == 0);
        CHECK(feeder.ready(p, 50, T0));
        CHECK(p.takeShow() == 50);
        CHECK(p.status(T0).position == 50);
    }

    TEST_CASE("player: invalidate in playback keeps playing") {
        PlayerLogic p(config(100));
        Feeder feeder;
        playFrames(p, feeder, 3);
        p.setLoop(true);
        feeder.invalidate(p);
        const auto status = p.status(T0 + 3 * P);
        CHECK(status.playing);
        CHECK(status.loop);
        CHECK(status.position == 3);
        CHECK(p.nextDeadline() == T0 + 4 * P);
        const Indices requests = feeder.take(p);
        CHECK(contains(requests, 4));
        CHECK(contains(requests, 5));
        CHECK_FALSE(contains(requests, 3));  // the position is not requested again in playback
        CHECK_FALSE(contains(requests, 2));  // the direction stays forward
        CHECK(feeder.ready(p, 4, T0 + 4 * P - 1ms));
        p.tick(T0 + 4 * P);
        CHECK(p.takeShow() == 4);
        CHECK(p.status(T0 + 4 * P).stalls == 0);

        // Not ready at the next deadline: the player stalls and re-anchors on arrival.
        feeder.invalidate(p);
        static_cast<void>(feeder.take(p));
        p.tick(T0 + 5 * P);
        CHECK_FALSE(p.takeShow());
        CHECK(p.status(T0 + 5 * P).stalled);
        CHECK(p.status(T0 + 5 * P).stalls == 1);
        const auto late = T0 + 5 * P + 10ms;
        CHECK(feeder.ready(p, 5, late));
        CHECK(p.takeShow() == 5);
        CHECK_FALSE(p.status(late).stalled);
        CHECK(p.nextDeadline() == late + P);
        CHECK(p.status(late).playing);
    }

    TEST_CASE("player: invalidate while stalled keeps the stall and the due index") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 0);
        feeder.fill(p, T0, {5});
        CHECK(p.takeShow() == 0);
        p.play(T0);
        for (int k = 1; k <= 4; ++k) {
            feeder.fill(p, T0 + k * P - 1ms, {5});
            p.tick(T0 + k * P);
            CHECK(p.takeShow() == k);
        }
        p.tick(T0 + 5 * P);
        CHECK_FALSE(p.takeShow());
        REQUIRE(p.status(T0 + 5 * P).stalled);
        feeder.invalidate(p);
        CHECK(p.status(T0 + 5 * P).stalled);
        CHECK(p.status(T0 + 5 * P).playing);
        CHECK(p.target() == 5);
        CHECK(contains(feeder.take(p), 5));
        const auto late = T0 + 6 * P;
        CHECK(feeder.ready(p, 5, late));
        CHECK(p.takeShow() == 5);
        CHECK_FALSE(p.status(late).stalled);
        CHECK(p.nextDeadline() == late + P);
    }

    TEST_CASE("player: invalidate with no position and no target requests nothing") {
        PlayerLogic p(config(100));
        Feeder feeder;
        feeder.reset(p, config(100), 0);
        CHECK(contains(feeder.take(p), 0));
        feeder.failed(p, 0);
        REQUIRE(p.status(T0).position == -1);
        const std::int64_t target = p.target();
        feeder.invalidate(p);
        CHECK(feeder.take(p).empty());
        CHECK(p.target() == target);
        CHECK(p.status(T0).position == -1);
        // Nothing to play: a target of -1 would start playback and stall on it for good.
        p.play(T0);
        CHECK_FALSE(p.status(T0).playing);
        CHECK_FALSE(p.nextDeadline());
        // The next seek works.
        p.seek(7);
        CHECK(contains(feeder.take(p), 7));
        CHECK(feeder.ready(p, 7, T0));
        CHECK(p.takeShow() == 7);
    }
}
