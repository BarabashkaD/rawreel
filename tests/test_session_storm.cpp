// A seeded storm of Session commands on the fake clock with slow sources (M4 Task 11-pre):
// about 200 random steps, seeks, opens, closes, plays, ticks and gate
// changes without waiting for their results, then a liveness check, shutdown and the leak check.
// Runs in every preset, vs2026-asan included.

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

#include "core/session.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/task.h"
#include "session_fixture.h"
#include "test_source_provider.h"

using rawreel::testing::closeGate;
using rawreel::testing::releaseGate;
using rawreel::testing::SessionEvent;
using rawreel::testing::SessionFixture;
using rawreel::testing::ShownFrame;
using namespace std::chrono_literals;

namespace {

constexpr std::uint32_t SEED = 20261025;
constexpr int OPERATIONS = 200;
constexpr std::size_t LENGTH = 40;  // the frames of every URI below
constexpr std::array<std::string_view, 3> GATES{"storm-a", "storm-b", "storm-c"};
// Plain, slow (frame and open gates) and failing sources, in two sizes and three formats.
constexpr std::array<std::string_view, 6> URIS{
    "synthetic:gradient?w=32&h=16&fmt=yuv420p&frames=40&fps=25",
    "slow:gradient?index=3&gate=storm-a",
    "slow:noise?w=64&h=16&fmt=nv12&index=1&gate=storm-b",
    "slow:gradient?opengate=storm-c",
    "slow:noise?fmt=nv12&index=20&gate=storm-c",
    "fail:gradient?index=5&errc=io&times=0",
};
// The two opens after the storm use URIs the storm never opens (width 48 and 56), so their own
// streamsChanged tells them apart from any open the storm left in the Session queue.
constexpr std::string_view FINAL_URI = "synthetic:gradient?w=48&h=16&fmt=yuv420p&frames=40&fps=25";
constexpr std::uint32_t FINAL_WIDTH = 48;
constexpr std::string_view GATED_URI = "slow:gradient?w=56&h=16&index=3&gate=storm-a";
constexpr std::uint32_t GATED_WIDTH = 56;

// The seq of the first streamsChanged after the event count `after` with one stream of `width`
// pixels, or nothing within 10 s.
std::optional<std::uint64_t> waitStreams(SessionFixture& f, std::size_t after,
                                         std::uint32_t width) {
    const auto matches = [after, width](const std::vector<SessionEvent>& events) {
        for (std::size_t i = after; i < events.size(); ++i) {
            const SessionEvent& e = events[i];
            if (e.kind == SessionEvent::Kind::StreamsChanged && e.streams.size() == 1 &&
                e.streams[0].info.desc.width == width) {
                return std::optional<std::uint64_t>(e.seq);
            }
        }
        return std::optional<std::uint64_t>();
    };
    if (!f.events->waitFor(
            [&matches](const std::vector<SessionEvent>& events) {
                return matches(events).has_value();
            },
            10s)) {
        return std::nullopt;
    }
    return matches(f.events->events());
}

}  // namespace

TEST_SUITE("session") {
    TEST_CASE("session: a seeded storm of steps, seeks, opens and closes leaks nothing") {
        for (const std::string_view gate : GATES) {
            closeGate(gate);
        }
        INFO("storm seed " << SEED);  // printed with every failure below
        // A fixed seed on purpose: a failing storm is replayed with the same commands.
        std::mt19937 random(SEED);  // NOLINT(bugprone-random-generator-seed)
        const auto pick = [&random](std::size_t n) {
            return static_cast<std::size_t>(random()) % n;
        };
        std::size_t errors = 0;
        {
            SessionFixture f;
            for (int op = 0; op < OPERATIONS; ++op) {
                switch (pick(10)) {
                    case 0:
                    case 1: {
                        const auto delta = static_cast<std::int64_t>(pick(3)) + 1;
                        f.session->step(pick(2) == 0 ? delta : -delta);
                        break;
                    }
                    case 2:
                    case 3: f.session->seek(static_cast<std::int64_t>(pick(LENGTH))); break;
                    case 4: {
                        std::optional<std::int64_t> start;
                        if (pick(2) == 0) {
                            start = static_cast<std::int64_t>(pick(LENGTH));
                        }
                        f.open(0, URIS.at(pick(URIS.size())), start);
                        break;
                    }
                    case 5: f.session->closeStreams(); break;
                    case 6:
                        if (pick(2) == 0) {
                            f.session->play();
                        } else {
                            f.session->pause();
                        }
                        break;
                    case 7: f.tick(); break;
                    case 8: releaseGate(GATES.at(pick(GATES.size()))); break;
                    default: closeGate(GATES.at(pick(GATES.size()))); break;
                }
                if (pick(4) == 0) {
                    std::this_thread::sleep_for(1ms);
                }
            }

            // Still alive: with every gate open, a last open shows its start frame.
            for (const std::string_view gate : GATES) {
                releaseGate(gate);
            }
            f.session->pause();
            // A show after this open's own streamsChanged: neither a frame the storm left on
            // screen nor a queued storm open or seek can meet it.
            const std::size_t final_events = f.events->events().size();
            f.open(0, FINAL_URI, 5);
            const std::optional<std::uint64_t> opened = waitStreams(f, final_events, FINAL_WIDTH);
            REQUIRE_MESSAGE(opened.has_value(), "no streamsChanged for the final open");
            // value_or: clang-tidy cannot see that REQUIRE ends the test; the 0 is never used.
            const std::uint64_t opened_seq = opened.value_or(0);
            CHECK(f.sink->waitFor(
                [seq = opened_seq](const std::vector<ShownFrame>& history) {
                    return !history.empty() && history.back().seq > seq &&
                           history.back().index == 5 && history.back().indicesMatch;
                },
                10s));
            for (const ShownFrame& shown : f.sink->history()) {
                CHECK_MESSAGE(shown.indicesMatch, "show ", shown.seq, " of index ", shown.index);
            }

            // Shutdown with a read in flight behind a closed gate, released while it runs.
            // Waited for after the gated open's own streamsChanged, and still there 20 ms later:
            // only the read of frame 3 behind the closed gate stays in flight that long.
            closeGate(GATES.front());
            const std::size_t gated_events = f.events->events().size();
            f.open(0, GATED_URI, 3);
            // CHECK, not REQUIRE: the shutdown below must still release the gate.
            CHECK_MESSAGE(waitStreams(f, gated_events, GATED_WIDTH).has_value(),
                          "no streamsChanged for the gated open");
            CHECK(f.waitStatus(
                [](const rawreel::SessionStatus& status) { return status.inflight > 0; }, 10s));
            std::this_thread::sleep_for(20ms);
            CHECK(f.session->status().inflight > 0);
            std::atomic<bool> done{false};
            std::jthread shutter([&f, &done] {
                dvv::syncWait(f.session->shutdown());
                done = true;
            });
            std::this_thread::sleep_for(5ms);
            releaseGate(GATES.front());
            const auto finish = std::chrono::steady_clock::now() + 15s;
            while (!done.load() && std::chrono::steady_clock::now() < finish) {
                std::this_thread::sleep_for(1ms);
            }
            if (!done.load()) {
                // join() would block until the CTest timeout; end the process at the bound instead.
                std::fputs("HANG: shutdown\n", stderr);
                std::fflush(stderr);
                std::quick_exit(1);
            }
            shutter.join();
            f.shutDown = true;
            CHECK_FALSE(f.sink->holdsFrame());
            CHECK(f.session->status().inflight == 0);
            errors = f.events->of(SessionEvent::Kind::Error).size();
        }
        MESSAGE("storm seed " << SEED << ": " << errors << " error event(s)");
        // The fixture's destructor waited for liveCount() == 0 (at most 5 s).
        CHECK(dvv::LifetimeRegistry::instance().liveCount() == 0);
    }
}
