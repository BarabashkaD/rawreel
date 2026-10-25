#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/drop.h"
#include "core/format_choice.h"
#include "core/session.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/task.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/pixel_format.h"
#include "session_fixture.h"
#include "test_source_provider.h"

using rawreel::SessionStatus;
using rawreel::testing::closeGate;
using rawreel::testing::gateWaiters;
using rawreel::testing::releaseGate;
using rawreel::testing::SessionEvent;
using rawreel::testing::SessionFixture;
using rawreel::testing::ShownFrame;
using Kind = rawreel::testing::SessionEvent::Kind;
using namespace std::chrono_literals;

namespace {

constexpr const char* A = "synthetic:gradient?w=32&h=16&fmt=yuv420p&frames=40&fps=25";
constexpr const char* B = "synthetic:noise?w=32&h=16&fmt=nv12&frames=40&fps=25";
constexpr const char* C = "synthetic:noise?w=64&h=16&fmt=nv12&frames=40&fps=25";
constexpr std::int64_t LENGTH = 40;
constexpr std::uint32_t W = 8;  // the host budget gives the full window

// Frames that can be ready ahead of `position` in a 40-frame stream without loop.
std::uint32_t ahead(std::int64_t position, std::uint32_t window = W) {
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(LENGTH - 1 - position, 0, static_cast<std::int64_t>(window)));
}

bool playing(const SessionStatus& s) {
    return s.player.playing;
}

// The index sequence of the shows.
std::vector<std::int64_t> indices(const std::vector<ShownFrame>& history) {
    std::vector<std::int64_t> result;
    result.reserve(history.size());
    for (const ShownFrame& shown : history) {
        result.push_back(shown.index);
    }
    return result;
}

// Shows recorded after the event with sequence number `seq`.
std::vector<ShownFrame> showsAfter(const std::vector<ShownFrame>& history, std::uint64_t seq) {
    std::vector<ShownFrame> result;
    for (const ShownFrame& shown : history) {
        if (shown.seq > seq) {
            result.push_back(shown);
        }
    }
    return result;
}

// Waits for the n-th streamsChanged and returns it.
SessionEvent waitStreamsChanged(SessionFixture& f, std::size_t n) {
    REQUIRE(f.events->waitFor([n](const std::vector<SessionEvent>& events) {
        return rawreel::testing::countKind(events, Kind::StreamsChanged) >= n;
    }));
    return f.events->of(Kind::StreamsChanged).at(n - 1);
}

// Waits for the first show after `seq` and returns it.
ShownFrame firstShowAfter(SessionFixture& f, std::uint64_t seq) {
    REQUIRE(f.sink->waitFor([seq](const std::vector<ShownFrame>& history) {
        return !showsAfter(history, seq).empty();
    }));
    return showsAfter(f.sink->history(), seq).front();
}

// Plays with tick() until the player stops advancing; returns false on a timeout.
bool playUntilStopped(SessionFixture& f) {
    for (int round = 0; round < 2 * LENGTH; ++round) {
        if (!f.waitStatus([](const SessionStatus& s) { return s.inflight == 0; })) {
            return false;
        }
        const SessionStatus before = f.session->status();
        if (!before.player.playing) {
            return true;
        }
        f.tick();
        if (!f.waitStatus([&before](const SessionStatus& s) {
                return s.player.position != before.player.position || !s.player.playing;
            })) {
            return false;
        }
    }
    return false;
}

std::size_t errorCount(SessionFixture& f) {
    return f.events->of(Kind::Error).size();
}

// Waits until count threads wait at the gate (at most 5 s).
bool waitGateWaiters(std::string_view gate, std::size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (gateWaiters(gate) != count) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

// Polls `predicate` every millisecond, at most `timeout`.
bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

// --- device loss and reopen (Task 6) ---
// Headless stacks are host-only: every stackChanged has an expired context, so the cases match on
// the epoch: epoch 0 is a loss (or a released stack), epoch N >= 1 an installed stack.

using EventPredicate = std::function<bool(const SessionEvent&)>;

EventPredicate isLoss() {
    return [](const SessionEvent& e) { return e.kind == Kind::StackChanged && e.epoch == 0; };
}
EventPredicate isLive(std::uint64_t epoch) {
    return [epoch](const SessionEvent& e) {
        return e.kind == Kind::StackChanged && e.epoch == epoch && epoch > 0;
    };
}
EventPredicate isAnyLive() {
    return [](const SessionEvent& e) { return e.kind == Kind::StackChanged && e.epoch > 0; };
}
EventPredicate isNoStreams() {
    return
        [](const SessionEvent& e) { return e.kind == Kind::StreamsChanged && e.streams.empty(); };
}
// A streamsChanged with one stream whose resolved URI contains `part`.
EventPredicate holds(std::string_view part) {
    return [part = std::string(part)](const SessionEvent& e) {
        return e.kind == Kind::StreamsChanged && e.streams.size() == 1 &&
               e.streams[0].resolvedUri.find(part) != std::string::npos;
    };
}

// The first event after `after` (a seq) that matches.
std::optional<SessionEvent> findEvent(const std::vector<SessionEvent>& events,
                                      const EventPredicate& predicate, std::uint64_t after = 0) {
    for (const SessionEvent& event : events) {
        if (event.seq > after && predicate(event)) {
            return event;
        }
    }
    return std::nullopt;
}
// The last event that matches.
std::optional<SessionEvent> findLast(const std::vector<SessionEvent>& events,
                                     const EventPredicate& predicate) {
    std::optional<SessionEvent> last;
    for (const SessionEvent& event : events) {
        if (predicate(event)) {
            last = event;
        }
    }
    return last;
}
std::size_t countIf(const std::vector<SessionEvent>& events, const EventPredicate& predicate,
                    std::uint64_t after = 0) {
    return static_cast<std::size_t>(std::ranges::count_if(
        events, [&](const SessionEvent& e) { return e.seq > after && predicate(e); }));
}

// Waits for an event after `after` that matches and returns it.
SessionEvent waitEvent(SessionFixture& f, const EventPredicate& predicate,
                       std::uint64_t after = 0) {
    REQUIRE(f.events->waitFor([&](const std::vector<SessionEvent>& events) {
        return findEvent(events, predicate, after).has_value();
    }));
    return findEvent(f.events->events(), predicate, after).value_or(SessionEvent{});
}

// Waits for the n-th error and returns it.
SessionEvent waitError(SessionFixture& f, std::size_t n) {
    REQUIRE(f.events->waitFor([n](const std::vector<SessionEvent>& events) {
        return rawreel::testing::countKind(events, Kind::Error) >= n;
    }));
    return f.events->of(Kind::Error).at(n - 1);
}

// Waits for a show of `index` with `epoch` after `after` (a seq) and returns it.
ShownFrame waitShow(SessionFixture& f, std::int64_t index, std::uint64_t epoch,
                    std::uint64_t after = 0) {
    const auto matches = [=](const ShownFrame& s) {
        return s.seq > after && s.index == index && s.epoch == epoch;
    };
    REQUIRE(f.sink->waitFor([&](const std::vector<ShownFrame>& history) {
        return std::ranges::any_of(history, matches);
    }));
    const std::vector<ShownFrame> history = f.sink->history();
    return *std::ranges::find_if(history, matches);
}

bool isDeviceLost(const SessionEvent& e) {
    return e.kind == Kind::Error && e.error &&
           e.error.value_or(dvv::Error{}).code == dvv::Errc::DeviceLost;
}

}  // namespace

TEST_SUITE("session") {
    TEST_CASE("session: create checks its options") {
        SessionFixture f;
        const std::vector<std::function<void(rawreel::SessionOptions&)>> breakers{
            [](rawreel::SessionOptions& o) { o.components.reset(); },
            [](rawreel::SessionOptions& o) { o.cpu.reset(); },
            [](rawreel::SessionOptions& o) { o.io.reset(); },
            [](rawreel::SessionOptions& o) { o.clock.reset(); },
            [](rawreel::SessionOptions& o) { o.timer.reset(); },
            [](rawreel::SessionOptions& o) { o.sink.reset(); },
            [](rawreel::SessionOptions& o) { o.events.reset(); },
            [](rawreel::SessionOptions& o) { o.inflight = 0; },
        };
        for (std::size_t i = 0; i < breakers.size(); ++i) {
            rawreel::SessionOptions options = f.options();
            breakers[i](options);
            const auto made = rawreel::Session::create(std::move(options));
            CHECK_MESSAGE(!made, "breaker ", i);
            if (!made) {
                CHECK(made.error().code == dvv::Errc::Internal);
            }
        }
        // The fixture's own Session: one makeStack call with the default device, stackChanged
        // with an expired context and epoch 1, the host-only stack counts as available.
        REQUIRE(f.events->waitFor([](const std::vector<SessionEvent>& events) {
            return rawreel::testing::countKind(events, Kind::StackChanged) == 1;
        }));
        const SessionEvent stack = f.events->of(Kind::StackChanged).front();
        CHECK(stack.expired);
        CHECK(stack.epoch == 1);
        CHECK(f.waitStatus([](const SessionStatus& s) { return s.gpuAvailable && s.epoch == 1; }));
        {
            std::lock_guard lock(f.stackLog->mutex);
            CHECK(f.stackLog->calls == 1);
            REQUIRE(f.stackLog->devices.size() == 1);
            CHECK_FALSE(f.stackLog->devices.front().has_value());
        }
    }

    TEST_CASE("session: one stream shows its frames") {
        SessionFixture f;
        f.open(0, A);
        const SessionEvent changed = waitStreamsChanged(f, 1);
        REQUIRE(changed.streams.size() == 1);
        CHECK(changed.streams[0].slot == 0);
        CHECK(changed.streams[0].resolvedUri.starts_with("synthetic:gradient?"));
        CHECK(changed.streams[0].resolvedUri.find("size=32x16") != std::string::npos);
        CHECK(changed.streams[0].resolvedUri == changed.streams[0].info.resolvedUri);
        REQUIRE(f.sink->waitShown(0));
        CHECK(f.sink->history().front().count == 1);

        f.session->play();
        REQUIRE(f.waitStatus(playing));
        for (std::int64_t k = 1; k <= 10; ++k) {
            REQUIRE(f.settle(ahead(k - 1)));
            f.tick();
            REQUIRE(f.waitPosition(k));
        }
        const std::vector<ShownFrame> history = f.sink->history();
        CHECK(history.size() == 11);
        for (const ShownFrame& shown : history) {
            CHECK(shown.count == 1);
            CHECK(shown.indicesMatch);
        }
        const SessionStatus status = f.session->status();
        CHECK(status.gpuAvailable);
        CHECK(status.streams == 1);
    }

    TEST_CASE("session: play shows every index once, in order") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->play();
        REQUIRE(f.waitStatus(playing));
        for (std::int64_t k = 1; k <= 30; ++k) {
            REQUIRE(f.settle(ahead(k - 1)));
            f.tick();
            REQUIRE(f.waitPosition(k));
        }
        std::vector<std::int64_t> expected;
        for (std::int64_t k = 0; k <= 30; ++k) {
            expected.push_back(k);
        }
        CHECK(indices(f.sink->history()) == expected);
        CHECK(f.session->status().player.stalls == 0);
        CHECK(errorCount(f) == 0);
    }

    TEST_CASE("session: a slow frame stalls and re-anchors") {
        closeGate("g3");
        SessionFixture f;
        f.open(0, "slow:gradient?index=10&gate=g3");
        REQUIRE(f.sink->waitShown(0));
        f.session->play();
        REQUIRE(f.waitStatus(playing));
        for (std::int64_t k = 1; k <= 9; ++k) {
            const std::int64_t position = k - 1;
            std::uint32_t needed = 0;
            for (std::int64_t j = position + 1; j <= std::min<std::int64_t>(position + W, 39);
                 ++j) {
                needed += j == 10 ? 0 : 1;
            }
            REQUIRE(
                f.waitStatus([needed](const SessionStatus& s) { return s.readyAhead >= needed; }));
            f.tick();
            REQUIRE(f.waitPosition(k));
        }
        CHECK(waitGateWaiters("g3", 1));
        f.tick();
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.player.stalled; }));
        SessionStatus status = f.session->status();
        CHECK(status.player.stalls == 1);
        CHECK(status.player.position == 9);
        CHECK_FALSE(f.timer->pending().has_value());

        releaseGate("g3");  // 10 is shown at once, the fake clock unchanged
        REQUIRE(f.waitPosition(10));
        REQUIRE(f.timer->pending().has_value());
        f.timer->advance(39ms);
        CHECK(f.session->status().player.position == 10);
        f.timer->advance(1ms);  // one period after the late show, no burst
        REQUIRE(f.waitPosition(11));
        std::vector<std::int64_t> expected;
        for (std::int64_t k = 0; k <= 11; ++k) {
            expected.push_back(k);
        }
        CHECK(indices(f.sink->history()) == expected);
        status = f.session->status();
        CHECK(status.player.stalls == 1);
        CHECK_FALSE(status.player.stalled);
    }

    TEST_CASE("session: step, seek, home, end and loop") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        REQUIRE(f.settle(W));
        f.session->step(1);
        REQUIRE(f.waitPosition(1));
        f.session->step(-1);
        REQUIRE(f.waitPosition(0));
        f.session->seek(20);
        REQUIRE(f.waitPosition(20));
        f.session->seek(0);
        REQUIRE(f.waitPosition(0));
        f.session->seek(39);
        REQUIRE(f.waitPosition(39));
        CHECK_FALSE(f.session->status().player.playing);

        // The end without loop: playing up to 39 pauses there.
        f.session->seek(38);
        REQUIRE(f.waitPosition(38));
        REQUIRE(f.settle(1));
        f.session->play();
        REQUIRE(f.waitStatus(playing));
        f.tick();
        REQUIRE(f.waitPosition(39));
        f.tick();
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return !s.player.playing; }));
        CHECK(f.session->status().player.position == 39);

        // Task 4's rule: play() at the last frame without loop restarts from 0.
        f.session->play();
        REQUIRE(f.waitStatus(
            [](const SessionStatus& s) { return s.player.playing && s.player.position == 0; }));
        f.session->pause();
        f.session->seek(39);
        REQUIRE(f.waitStatus(
            [](const SessionStatus& s) { return !s.player.playing && s.player.position == 39; }));

        // With loop, 0 follows 39.
        f.session->setLoop(true);
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.player.loop; }));
        REQUIRE(f.settle(W));
        f.session->play();
        REQUIRE(f.waitStatus(playing));
        f.tick();
        REQUIRE(f.waitPosition(0));
        CHECK(f.session->status().player.playing);

        // Stepping while playing pauses.
        f.session->step(1);
        REQUIRE(f.waitStatus(
            [](const SessionStatus& s) { return !s.player.playing && s.player.position == 1; }));
        CHECK(errorCount(f) == 0);
    }

    TEST_CASE("session: slot 1 is refused and an open replaces the stream") {
        SessionFixture f;
        f.open(0, A);
        static_cast<void>(waitStreamsChanged(f, 1));
        REQUIRE(f.sink->waitShown(0));
        const dvv::media::OriginId origin_a = f.sink->history().back().origin;

        f.open(1, B);
        REQUIRE(f.events->waitFor([](const std::vector<SessionEvent>& events) {
            return rawreel::testing::countKind(events, Kind::Error) == 1;
        }));
        const SessionEvent refused = f.events->of(Kind::Error).front();
        REQUIRE(refused.error);
        CHECK(refused.error.value_or(dvv::Error{}).code == dvv::Errc::Unsupported);
        CHECK(refused.error.value_or(dvv::Error{}).message.find("one stream in v1.0") !=
              std::string::npos);
        CHECK(f.session->status().streams == 1);
        CHECK(f.events->of(Kind::StreamsChanged).size() == 1);

        // Another size and format replaces the stream.
        f.open(0, C);
        const SessionEvent replaced = waitStreamsChanged(f, 2);
        REQUIRE(replaced.streams.size() == 1);
        CHECK(replaced.streams[0].info.desc.width == 64);
        CHECK(replaced.streams[0].info.desc.height == 16);
        CHECK(replaced.streams[0].info.desc.format == dvv::media::findFormat("nv12"));
        const ShownFrame first_c = firstShowAfter(f, replaced.seq);
        CHECK(first_c.index == 0);
        CHECK(first_c.origin != origin_a);
        for (const ShownFrame& shown : showsAfter(f.sink->history(), replaced.seq)) {
            CHECK(shown.count == 1);
            CHECK(shown.origin == first_c.origin);
        }

        // Start position (design v2 §4.3).
        f.open(0, A, 7);
        const SessionEvent at_seven = waitStreamsChanged(f, 3);
        CHECK(firstShowAfter(f, at_seven.seq).index == 7);
        CHECK_FALSE(f.session->status().player.playing);
        f.open(0, A, 1000);
        const SessionEvent clamped = waitStreamsChanged(f, 4);
        CHECK(firstShowAfter(f, clamped.seq).index == 39);

        // A replaced stream that was playing keeps playing (play at 39 restarts from 0 first).
        f.session->play();
        REQUIRE(f.waitStatus(
            [](const SessionStatus& s) { return s.player.playing && s.player.position == 0; }));
        for (std::int64_t k = 1; k <= 2; ++k) {
            REQUIRE(f.settle(ahead(k - 1)));
            f.tick();
            REQUIRE(f.waitPosition(k));
        }
        f.open(0, C);
        const SessionEvent playing_c = waitStreamsChanged(f, 5);
        CHECK(firstShowAfter(f, playing_c.seq).index == 0);
        CHECK(f.session->status().player.playing);
        CHECK(errorCount(f) == 1);
    }

    TEST_CASE("session: a read error pauses at the last shown frame") {
        SessionFixture f;
        f.open(0, "fail:gradient?index=12&errc=io");
        REQUIRE(f.sink->waitShown(0));
        f.session->play();
        REQUIRE(f.waitStatus(playing));
        REQUIRE(playUntilStopped(f));
        const SessionStatus status = f.session->status();
        CHECK(status.player.position == 11);
        CHECK_FALSE(status.player.playing);
        const std::vector<SessionEvent> errors = f.events->of(Kind::Error);
        REQUIRE(errors.size() == 1);
        REQUIRE(errors[0].error);
        const dvv::Error error = errors[0].error.value_or(dvv::Error{});
        CHECK(error.message.find("frame 12") != std::string::npos);
        REQUIRE(error.cause);
        CHECK(error.cause->code == dvv::Errc::Io);
        const std::vector<std::int64_t> shown = indices(f.sink->history());
        CHECK(std::ranges::find(shown, 12) == shown.end());
    }

    TEST_CASE("session: an ambiguous raw file asks for the format") {
        const std::filesystem::path folder =
            std::filesystem::temp_directory_path() /
            ("rawreel-session-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder);
        const std::filesystem::path file = folder / "clip_33x17.yuv";
        {
            std::ofstream out(file, std::ios::binary);
            const std::string bytes(std::size_t{3} * 867, '\x40');
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        {
            SessionFixture f;
            const std::string uri = rawreel::fileUri(file);
            f.open(0, uri);
            REQUIRE(f.events->waitFor([](const std::vector<SessionEvent>& events) {
                return rawreel::testing::countKind(events, Kind::FormatNeeded) == 1;
            }));
            const SessionEvent needed = f.events->of(Kind::FormatNeeded).front();
            CHECK(needed.slot == 0);
            REQUIRE(needed.candidates.candidates.size() == 1);
            const dvv::media::Candidate& candidate = needed.candidates.candidates.front();
            CHECK(candidate.component == "raw");
            CHECK(candidate.guess.width == 33);
            CHECK(candidate.guess.height == 17);
            CHECK(candidate.guess.format == dvv::media::findFormat("yuv420p"));
            CHECK(f.events->of(Kind::StreamsChanged).empty());

            const auto chosen = rawreel::applyChoice(uri, rawreel::choiceFromCandidate(candidate));
            REQUIRE(chosen);
            f.open(0, *chosen);
            const SessionEvent changed = waitStreamsChanged(f, 1);
            REQUIRE(changed.streams.size() == 1);
            CHECK(changed.streams[0].info.frameCount == 3);
            REQUIRE(f.sink->waitShown(0));
            f.shutdown();
        }
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }

    // M3.1 U-IO2: every open reads files in SessionOptions::fileReads (the config file's mode).
    TEST_CASE("session: file sources read in the configured mode") {
        const std::filesystem::path folder =
            std::filesystem::temp_directory_path() /
            ("rawreel-session-reads-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder);
        const std::filesystem::path file = folder / "clip_33x17_yuv420p.yuv";
        {
            std::ofstream out(file, std::ios::binary);
            const std::string bytes(std::size_t{3} * 867, '\x40');
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
#if defined(_WIN32)
        constexpr std::string_view AUTO_READS = "unbuffered";  // NTFS temporary directory
#else
        constexpr std::string_view AUTO_READS = "buffered";
#endif
        for (const auto& [mode, reads] :
             {std::pair{dvv::platform::FileReadMode::Buffered, std::string_view("buffered")},
              std::pair{dvv::platform::FileReadMode::Auto, AUTO_READS}}) {
            CAPTURE(reads);
            SessionFixture f(mode);
            f.open(0, rawreel::fileUri(file));
            const SessionEvent changed = waitStreamsChanged(f, 1);
            REQUIRE(changed.streams.size() == 1);
            CHECK(changed.streams[0].info.reader == reads);
            REQUIRE(f.sink->waitShown(0));
            f.shutdown();
        }
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }

    TEST_CASE("session: shutdown with requests in flight") {
        closeGate("g11");
        {
            SessionFixture f;
            f.open(0, "slow:gradient?index=1&gate=g11");
            REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.inflight >= 1; }));
            REQUIRE(waitGateWaiters("g11", 1));

            std::atomic<bool> done{false};
            std::jthread shutter([&f, &done] {
                dvv::syncWait(f.session->shutdown());
                done = true;
            });
            releaseGate("g11");
            const auto finish = std::chrono::steady_clock::now() + 5s;
            while (!done.load() && std::chrono::steady_clock::now() < finish) {
                std::this_thread::sleep_for(1ms);
            }
            CHECK(done.load());
            shutter.join();
            f.shutDown = true;
            CHECK_FALSE(f.sink->holdsFrame());
            CHECK(f.session->status().inflight == 0);
            // Commands after shutdown are refused.
            f.session->play();
            f.open(0, A);
        }
        // The fixture's destructor waited for liveCount() == 0 (at most 5 s).
        CHECK(dvv::LifetimeRegistry::instance().liveCount() == 0);
    }

    TEST_CASE("session: out of memory halves the window and retries once") {
        SUBCASE("a first out of memory restarts with half the window") {
            SessionFixture f;
            f.open(0, "fail:gradient?index=5&errc=oom&times=1");
            REQUIRE(f.sink->waitShown(0));
            REQUIRE(f.settle(4));
            CHECK(f.session->status().readyAhead == 4);
            f.session->play();
            REQUIRE(f.waitStatus(playing));
            for (std::int64_t k = 1; k <= 5; ++k) {
                REQUIRE(f.settle(4));
                CHECK(f.session->status().readyAhead == 4);
                f.tick();
                REQUIRE(f.waitPosition(k));
            }
            CHECK(errorCount(f) == 0);
        }
        SUBCASE("a second out of memory is an error") {
            SessionFixture f;
            f.open(0, "fail:gradient?index=5&errc=oom&times=2");
            REQUIRE(f.sink->waitShown(0));
            f.session->play();
            REQUIRE(f.waitStatus(playing));
            REQUIRE(playUntilStopped(f));
            CHECK(f.session->status().player.position == 4);
            const std::vector<SessionEvent> errors = f.events->of(Kind::Error);
            REQUIRE(errors.size() == 1);
            REQUIRE(errors[0].error);
            CHECK(errors[0].error.value_or(dvv::Error{}).message.find("frame 5") !=
                  std::string::npos);
            REQUIRE(errors[0].error.value_or(dvv::Error{}).cause);
            CHECK(errors[0].error.value_or(dvv::Error{}).cause->code == dvv::Errc::OutOfMemory);
        }
        SUBCASE("the retry keeps a pending start position") {
            SessionFixture f;
            f.open(0, "fail:gradient?index=7&errc=oom&times=1", 7);
            const SessionEvent changed = waitStreamsChanged(f, 1);
            CHECK(firstShowAfter(f, changed.seq).index == 7);
            REQUIRE(f.settle(4));
            CHECK(f.session->status().player.position == 7);
            CHECK(f.session->status().readyAhead == 4);  // the halved window
            CHECK(errorCount(f) == 0);
        }
        SUBCASE("the retry keeps a pending seek target") {
            SessionFixture f;
            f.open(0, "fail:gradient?index=20&errc=oom&times=1");
            REQUIRE(f.sink->waitShown(0));
            REQUIRE(f.settle(W));
            f.session->seek(20);
            REQUIRE(f.waitPosition(20));
            REQUIRE(f.settle(4));
            CHECK(f.session->status().readyAhead == 4);
            CHECK(errorCount(f) == 0);
        }
    }

    TEST_CASE("session: opens are serialised") {
        SessionFixture f;
        closeGate("oa");
        closeGate("ob");
        f.open(0, "slow:gradient?opengate=oa");
        f.open(0, "slow:noise?opengate=ob");
        REQUIRE(waitGateWaiters("oa", 1));

        CHECK(gateWaiters("ob") == 0);  // the second open waits for the first
        releaseGate("ob");
        releaseGate("oa");
        const SessionEvent last = waitStreamsChanged(f, 2);
        REQUIRE(last.streams.size() == 1);
        CHECK(last.streams[0].resolvedUri.find("noise") != std::string::npos);
        const ShownFrame first = firstShowAfter(f, last.seq);
        REQUIRE(f.sink->waitFor([&](const std::vector<ShownFrame>& history) {
            return !history.empty() && history.back().origin == first.origin;
        }));
        CHECK(f.events->of(Kind::StreamsChanged).size() == 2);

        // closeStreams() while an open runs: the open's source is dropped silently.
        closeGate("oc");
        f.open(0, "slow:gradient?opengate=oc");
        REQUIRE(waitGateWaiters("oc", 1));

        f.session->closeStreams();
        const SessionEvent closed = waitStreamsChanged(f, 3);
        CHECK(closed.streams.empty());
        releaseGate("oc");
        // Opens are serialised, so A's open starts only after the stale open finished on the live
        // Session: everything it would report lies between `closed` and A's streamsChanged.
        f.open(0, A);
        const SessionEvent opened_a = waitStreamsChanged(f, 4);
        REQUIRE(opened_a.streams.size() == 1);
        CHECK(opened_a.streams[0].resolvedUri.starts_with("synthetic:gradient?"));
        const std::vector<SessionEvent> events = f.events->events();
        const auto at_a = std::ranges::find_if(
            events, [&opened_a](const SessionEvent& e) { return e.seq == opened_a.seq; });
        REQUIRE(at_a != events.end());
        REQUIRE(at_a != events.begin());
        CHECK(std::prev(at_a)->seq == closed.seq);  // no Error, FormatNeeded or streamsChanged
        const ShownFrame first_a = firstShowAfter(f, opened_a.seq);
        CHECK(first_a.index == 0);
        f.shutdown();
        CHECK(f.events->of(Kind::StreamsChanged).size() == 4);
        CHECK(f.session->status().streams == 0);
    }

    TEST_CASE("session: plays and shuts down on the real timer") {
        SessionFixture f{rawreel::testing::RealTime{}};
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->play();
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.player.position >= 10; }));
        f.session->pause();
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return !s.player.playing; }));
        const std::int64_t paused_at = f.session->status().player.position;
        const std::size_t shows = f.sink->history().size();
        std::this_thread::sleep_for(100ms);  // 2.5 periods: a tick after pause() would show
        CHECK(f.session->status().player.position == paused_at);
        CHECK(f.sink->history().size() == shows);
        std::vector<std::int64_t> expected;
        for (std::int64_t k = 0; k <= paused_at; ++k) {
            expected.push_back(k);
        }
        CHECK(indices(f.sink->history()) == expected);

        // Shut down while playing: the timer is cancelled, nothing is shown afterwards.
        f.session->play();
        REQUIRE(f.waitStatus(
            [paused_at](const SessionStatus& s) { return s.player.position > paused_at; }));
        f.shutdown();
        const std::size_t final_shows = f.sink->history().size();
        std::this_thread::sleep_for(100ms);
        CHECK(f.sink->history().size() == final_shows);
        CHECK_FALSE(f.sink->holdsFrame());
        CHECK(errorCount(f) == 0);
    }

    TEST_CASE("session: a device loss reports an error and reopen continues at the kept position") {
        SessionFixture f;
        // Index 12 lies beyond the first prefetch window, so the loss happens mid-play.
        f.open(0, "fail:gradient?w=32&h=16&fmt=yuv420p&frames=40&index=12&errc=devicelost&shots=1");
        const SessionEvent opened = waitStreamsChanged(f, 1);
        REQUIRE(opened.streams.size() == 1);
        const std::string uri = opened.streams[0].resolvedUri;
        REQUIRE(f.sink->waitShown(0));
        REQUIRE(
            f.waitStatus([](const SessionStatus& s) { return s.gpuAvailable && s.epoch == 1; }));
        f.session->play();
        REQUIRE(f.waitStatus(playing));
        for (int round = 0; round < 40 && errorCount(f) == 0; ++round) {
            const std::int64_t position = f.session->status().player.position;
            const std::uint32_t wanted = ahead(position);
            REQUIRE(f.waitStatus([wanted](const SessionStatus& s) {
                return (s.inflight == 0 && s.readyAhead >= wanted) || !s.gpuAvailable;
            }));
            if (errorCount(f) != 0 || !f.session->status().gpuAvailable) {
                break;
            }
            f.tick();
            REQUIRE(f.waitStatus([position](const SessionStatus& s) {
                return s.player.position != position || !s.gpuAvailable;
            }));
        }
        const SessionStatus played = f.session->status();
        INFO("position ", played.player.position, ", playing ", played.player.playing, ", shows ",
             f.sink->history().size(), ", events ", f.events->events().size());
        const SessionEvent error = waitError(f, 1);
        REQUIRE(error.error);
        CHECK(error.error.value_or(dvv::Error{}).code == dvv::Errc::DeviceLost);
        CHECK(error.error.value_or(dvv::Error{}).message.starts_with("GPU device lost:"));
        CHECK(error.error.value_or(dvv::Error{}).message.ends_with("Reopen to continue."));
        REQUIRE(error.status);
        const std::int64_t kept = error.status.value_or(SessionStatus{}).player.position;
        CHECK(kept >= 0);
        const std::vector<ShownFrame> before = f.sink->history();
        REQUIRE_FALSE(before.empty());
        const ShownFrame last = before.back();
        CHECK(last.epoch == 1);
        CHECK(kept == last.index);  // the kept position is the last index the sink received
        const std::vector<SessionEvent> events = f.events->events();
        const std::optional<SessionEvent> emptied = findEvent(events, isNoStreams(), last.seq);
        REQUIRE(emptied);
        const std::optional<SessionEvent> loss =
            findEvent(events, isLoss(), emptied.value_or(SessionEvent{}).seq);
        REQUIRE(loss);
        CHECK(loss.value_or(SessionEvent{}).seq < error.seq);
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return !s.gpuAvailable && s.epoch == 0 && s.streams == 0;
        }));
        CHECK(f.session->status().player.position == -1);
        CHECK(f.stackCalls() == 1);
        for (int k = 0; k < 3; ++k) {
            f.tick();
        }
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.inflight == 0; }));
        CHECK(showsAfter(f.sink->history(), loss.value_or(SessionEvent{}).seq).empty());

        f.session->reopen();
        const SessionEvent live = waitEvent(f, isLive(2), loss.value_or(SessionEvent{}).seq);
        const SessionEvent reopened = waitEvent(f, holds(""), live.seq);
        CHECK(f.stackCalls() == 2);
        CHECK(reopened.streams[0].resolvedUri == uri);
        REQUIRE(f.waitStatus([kept](const SessionStatus& s) {
            return s.epoch == 2 && s.player.position == kept && !s.player.playing;
        }));
        static_cast<void>(waitShow(f, kept, 2));

        f.session->play();
        REQUIRE(f.waitStatus(playing));
        for (int k = 1; k <= 15; ++k) {
            const std::int64_t position = f.session->status().player.position;
            REQUIRE(f.settle(ahead(position)));
            f.tick();
            REQUIRE(f.waitPosition(position + 1));
        }
        std::vector<std::int64_t> epoch2;
        for (const ShownFrame& shown : f.sink->history()) {
            if (shown.epoch == 2) {
                epoch2.push_back(shown.index);
            }
        }
        std::vector<std::int64_t> expected;
        for (std::int64_t k = kept; k <= kept + 15; ++k) {
            expected.push_back(k);
        }
        CHECK(epoch2 == expected);
        CHECK(std::ranges::find(epoch2, 12) != epoch2.end());  // the shot is used up

        f.session->reportDeviceLost(2);
        const SessionEvent second = waitError(f, 2);
        CHECK(isDeviceLost(second));
        CHECK(countIf(f.events->events(), isLoss()) == 2);
        REQUIRE(second.status);
        const std::int64_t kept2 = second.status.value_or(SessionStatus{}).player.position;
        CHECK(kept2 >= 0);
        f.session->reopen();
        static_cast<void>(waitEvent(f, isLive(3)));
        CHECK(f.stackCalls() == 3);
        static_cast<void>(waitShow(f, kept2, 3));
    }

    TEST_CASE("session: a loss before the first show keeps the pending start position") {
        // Verification note on Task 5: right after an attach (or an out-of-memory retry) the
        // player's position is -1 until the pending target is shown. A loss in that window keeps
        // the target, never -1.
        closeGate("s19");
        SessionFixture f;
        f.open(0, "slow:gradient?w=32&h=16&fmt=yuv420p&frames=40&index=7&gate=s19", 7);
        static_cast<void>(waitStreamsChanged(f, 1));
        REQUIRE(waitGateWaiters("s19", 1));
        REQUIRE(f.waitStatus(
            [](const SessionStatus& s) { return s.streams == 1 && s.player.position == -1; }));
        f.session->reportDeviceLost(1);
        CHECK(isDeviceLost(waitError(f, 1)));
        releaseGate("s19");
        f.session->reopen();
        const SessionEvent live = waitEvent(f, isLive(2));
        static_cast<void>(waitEvent(f, holds("gradient"), live.seq));
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.epoch == 2 && s.player.position == 7 && !s.player.playing;
        }));
        const ShownFrame first = firstShowAfter(f, live.seq);
        CHECK(first.index == 7);
        CHECK(first.epoch == 2);
        CHECK(errorCount(f) == 1);
    }

    TEST_CASE("session: a late result and a stale report of an old stack are ignored") {
        closeGate("s14");
        closeGate("s14b");
        SessionFixture f;
        // 1. Frame 3 of the first stream waits at s14.
        f.open(0, "slow:gradient?w=32&h=16&fmt=yuv420p&frames=40&index=3&gate=s14");
        REQUIRE(f.sink->waitShown(0));
        REQUIRE(waitGateWaiters("s14", 1));
        // 2. Loss of stack 1, reopen on stack 2 (the reopened frame 3 waits at s14 as well).
        f.session->reportDeviceLost(1);
        static_cast<void>(waitError(f, 1));
        const SessionEvent loss = waitEvent(f, isLoss());
        f.session->reopen();
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.epoch == 2; }));
        static_cast<void>(waitShow(f, 0, 2));
        // 3. Only C14 can supply a valid 3; it waits at s14b.
        f.open(0, "slow:noise?w=32&h=16&fmt=nv12&frames=40&index=3&gate=s14b");
        const SessionEvent c14 = waitEvent(f, holds("noise"));
        const ShownFrame c14_first = firstShowAfter(f, c14.seq);
        CHECK(c14_first.index == 0);
        CHECK(c14_first.epoch == 2);
        f.session->seek(3);
        // 4. Both stale frames 3 (epoch 1 and the replaced epoch-2 stream) arrive.
        releaseGate("s14");
        REQUIRE(f.waitStatus([](const SessionStatus& s) { return s.inflight == 1; }));
        // 5. Nothing stale was shown or reported.
        const std::vector<ShownFrame> after = showsAfter(f.sink->history(), c14.seq);
        CHECK(std::ranges::none_of(after, [](const ShownFrame& s) { return s.index == 3; }));
        CHECK(std::ranges::none_of(showsAfter(f.sink->history(), loss.seq),
                                   [](const ShownFrame& s) { return s.epoch == 1; }));
        CHECK(errorCount(f) == 1);
        CHECK(countIf(f.events->events(), isLoss()) == 1);
        // 6. C14's own frame 3.
        releaseGate("s14b");
        static_cast<void>(waitShow(f, 3, 2, c14.seq));
        // 7. A report of stack 1 is stale; the seek is a barrier (commands run in order).
        const std::uint64_t mark = f.sequence->load();
        f.session->reportDeviceLost(1);
        f.session->seek(1);
        static_cast<void>(waitShow(f, 1, 2, mark));
        // 8.
        CHECK(f.session->status().epoch == 2);
        CHECK(countIf(f.events->events(), isLoss()) == 1);
        CHECK(errorCount(f) == 1);
    }

    TEST_CASE("session: an open with no stack builds one first") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->reportDeviceLost(1);
        static_cast<void>(waitError(f, 1));
        f.open(0, B);
        const SessionEvent opened_b = waitEvent(f, holds("noise"));
        CHECK(f.stackCalls() == 2);
        const std::optional<SessionEvent> live = findEvent(f.events->events(), isLive(2));
        REQUIRE(live);
        CHECK(live.value_or(SessionEvent{}).seq < opened_b.seq);
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.player.position == 0 && !s.player.playing && s.epoch == 2;
        }));
        static_cast<void>(waitShow(f, 0, 2, opened_b.seq));
        CHECK(errorCount(f) == 1);
    }

    TEST_CASE("session: stack builds are serialised and a second reopen is coalesced") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->reportDeviceLost(1);
        static_cast<void>(waitError(f, 1));
        const SessionEvent loss = waitEvent(f, isLoss());
        closeGate(rawreel::testing::STACK_GATE);
        f.session->reopen();
        f.session->reopen();
        f.open(0, B);
        const bool waited = waitGateWaiters(rawreel::testing::STACK_GATE, 1);
        releaseGate(rawreel::testing::STACK_GATE);
        REQUIRE(waited);
        // B's streamsChanged, then B's frame 0 on stack 2 (A's frame 0 on stack 2 does not count).
        const SessionEvent opened_b = waitEvent(f, holds("noise"), loss.seq);
        static_cast<void>(waitShow(f, 0, 2, opened_b.seq));
        CHECK(f.stackCalls() == 2);
        CHECK(f.stackMaxRunning() == 1);
        const std::vector<SessionEvent> events = f.events->events();
        CHECK(countIf(events, isAnyLive(), loss.seq) == 1);
        const std::optional<SessionEvent> live = findEvent(events, isLive(2), loss.seq);
        REQUIRE(live);
        const std::optional<SessionEvent> opened_a =
            findEvent(events, holds("gradient"), live.value_or(SessionEvent{}).seq);
        REQUIRE(opened_a);
        CHECK(opened_a.value_or(SessionEvent{}).seq < opened_b.seq);
        CHECK(findLast(events, [](const SessionEvent& e) { return e.kind == Kind::StreamsChanged; })
                  .value_or(SessionEvent{})
                  .seq == opened_b.seq);
        const SessionStatus status = f.session->status();
        CHECK(status.epoch == 2);
        CHECK(status.streams == 1);
        CHECK(errorCount(f) == 1);
    }

    TEST_CASE("session: reopen with a healthy stack replaces it and keeps queued opens") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->seek(7);
        REQUIRE(f.sink->waitShown(7));
        const std::uint64_t mark = f.sequence->load();
        f.session->reopen();
        const SessionEvent reopened = waitEvent(f, holds("gradient"), mark);
        std::vector<SessionEvent> after;
        for (const SessionEvent& event : f.events->events()) {
            if (event.seq > mark && event.seq <= reopened.seq) {
                after.push_back(event);
            }
        }
        REQUIRE(after.size() == 4);
        CHECK(isNoStreams()(after[0]));
        CHECK(isLoss()(after[1]));
        CHECK(isLive(2)(after[2]));
        CHECK(after[3].seq == reopened.seq);
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.epoch == 2 && s.player.position == 7 && !s.player.playing;
        }));
        static_cast<void>(waitShow(f, 7, 2, reopened.seq));
        CHECK(errorCount(f) == 0);

        const std::uint64_t second = f.sequence->load();
        f.session->reopen();
        f.open(0, B);
        const SessionEvent opened_b = waitEvent(f, holds("noise"), second);
        const std::vector<SessionEvent> events = f.events->events();
        const std::optional<SessionEvent> live = findEvent(events, isLive(3), second);
        REQUIRE(live);
        const std::optional<SessionEvent> opened_a =
            findEvent(events, holds("gradient"), live.value_or(SessionEvent{}).seq);
        REQUIRE(opened_a);
        CHECK(opened_a.value_or(SessionEvent{}).seq < opened_b.seq);
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.epoch == 3 && s.streams == 1 && s.player.position == 0;
        }));
        static_cast<void>(waitShow(f, 0, 3, opened_b.seq));
        CHECK(errorCount(f) == 0);
    }

    TEST_CASE("session: reopen after closeStreams rebuilds only the stack") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->closeStreams();
        f.session->reopen();
        const SessionEvent closed = waitEvent(f, isNoStreams());
        const SessionEvent loss = waitEvent(f, isLoss(), closed.seq);
        static_cast<void>(waitEvent(f, isLive(2), loss.seq));
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.gpuAvailable && s.epoch == 2 && s.streams == 0;
        }));
        CHECK_FALSE(findEvent(f.events->events(), holds(""), closed.seq).has_value());
        CHECK(errorCount(f) == 0);
    }

    TEST_CASE("session: shutdown while a stack is being built") {
        {
            SessionFixture f;
            f.open(0, A);
            REQUIRE(f.sink->waitShown(0));
            f.session->reportDeviceLost(1);
            static_cast<void>(waitError(f, 1));
            closeGate(rawreel::testing::STACK_GATE);
            f.session->reopen();
            const bool building = waitGateWaiters(rawreel::testing::STACK_GATE, 1);
            if (!building) {
                releaseGate(rawreel::testing::STACK_GATE);
            }
            REQUIRE(building);

            const std::uint64_t clears = f.sink->clears();
            std::atomic<bool> done{false};
            std::jthread shutter([&f, &done] {
                dvv::syncWait(f.session->shutdown());
                done = true;
            });
            // shutdownOnExecutor clears the sink after it set stopping_, so stackBuilt sees it.
            const bool stopping = waitUntil([&f, clears] { return f.sink->clears() > clears; });
            releaseGate(rawreel::testing::STACK_GATE);
            CHECK(stopping);
            CHECK(waitUntil([&done] { return done.load(); }));
            shutter.join();
            f.shutDown = true;
            CHECK_FALSE(findEvent(f.events->events(), isLive(2)).has_value());
        }
        // The fixture's destructor waited for liveCount() == 0 (at most 5 s).
        CHECK(dvv::LifetimeRegistry::instance().liveCount() == 0);
    }

    TEST_CASE("session: a loss report without a stack or of another epoch is ignored") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->reportDeviceLost(7);
        f.session->seek(2);
        REQUIRE(f.sink->waitShown(2));
        CHECK(countIf(f.events->events(), isLoss()) == 0);
        CHECK(errorCount(f) == 0);
        CHECK(f.session->status().epoch == 1);

        f.session->reportDeviceLost(1);
        static_cast<void>(waitError(f, 1));
        f.session->reportDeviceLost(1);  // no stack: ignored
        f.open(0, B);
        const SessionEvent opened_b = waitEvent(f, holds("noise"));
        static_cast<void>(waitShow(f, 0, 2, opened_b.seq));
        CHECK(errorCount(f) == 1);
        CHECK(countIf(f.events->events(), isLoss()) == 1);
    }

    TEST_CASE("session: a failed stack build reports an error and a later reopen recovers") {
        SessionFixture f;
        f.open(0, A);
        REQUIRE(f.sink->waitShown(0));
        f.session->seek(5);
        REQUIRE(f.sink->waitShown(5));
        f.session->reportDeviceLost(1);
        CHECK(isDeviceLost(waitError(f, 1)));
        const SessionEvent loss = waitEvent(f, isLoss());
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return !s.gpuAvailable && s.epoch == 0 && s.streams == 0;
        }));

        // 1. The next makeStack fails: the job ends with a DeviceLost error and no stack.
        f.stackLog->failNext = 1;
        f.session->reopen();
        const SessionEvent failed = waitError(f, 2);
        REQUIRE(failed.error);
        const dvv::Error error = failed.error.value_or(dvv::Error{});
        CHECK(error.code == dvv::Errc::DeviceLost);
        CHECK(error.message ==
              "the GPU could not be started: injected stack failure. Reopen to retry.");
        REQUIRE(error.cause);
        CHECK(error.cause->code == dvv::Errc::Io);
        REQUIRE(failed.status);
        const SessionStatus during = failed.status.value_or(SessionStatus{});
        CHECK(during.epoch == 0);
        CHECK_FALSE(during.gpuAvailable);
        CHECK(f.stackLog->failNext.load() == 0);
        CHECK(f.stackCalls() == 1);  // completed calls: the failed one does not count
        const SessionStatus after = f.session->status();
        CHECK(after.epoch == 0);
        CHECK_FALSE(after.gpuAvailable);
        CHECK(after.streams == 0);
        CHECK_FALSE(findEvent(f.events->events(), isAnyLive(), loss.seq).has_value());

        // 2. The failed job ended, so the next reopen runs and restores A at the kept position.
        f.session->reopen();
        const SessionEvent live = waitEvent(f, isLive(2), loss.seq);
        const SessionEvent reopened = waitEvent(f, holds("gradient"), live.seq);
        REQUIRE(f.waitStatus([](const SessionStatus& s) {
            return s.gpuAvailable && s.epoch == 2 && s.player.position == 5 && !s.player.playing;
        }));
        static_cast<void>(waitShow(f, 5, 2, reopened.seq));
        CHECK(f.stackCalls() == 2);
        CHECK(errorCount(f) == 2);
    }

    TEST_CASE("session: bottleneckStage") {
        const auto stats = [](std::uint64_t produce, std::uint64_t unpack, std::uint64_t display,
                              std::uint64_t cpu_unpack) {
            dvv::media::StoreStats s;
            s.converters = {{.name = "Produce", .runs = 1, .bytes = 0, .nanoseconds = produce},
                            {.name = "CpuUnpack", .runs = 1, .bytes = 0, .nanoseconds = cpu_unpack},
                            {.name = "GpuUnpack", .runs = 1, .bytes = 0, .nanoseconds = unpack},
                            {.name = "ToDisplay", .runs = 1, .bytes = 0, .nanoseconds = display}};
            return s;
        };
        const dvv::media::StoreStats before = stats(1'000'000, 1'000'000, 1'000'000, 1'000'000);
        CHECK(rawreel::bottleneckStage(before, stats(3'000'000, 6'000'000, 1'000'000, 1'000'000)) ==
              "unpack");
        CHECK(rawreel::bottleneckStage(before, before).empty());
        CHECK(rawreel::bottleneckStage(before, stats(2'000'000, 2'000'000, 9'000'000, 1'000'000)) ==
              "display");
        CHECK(rawreel::bottleneckStage(before, stats(1'000'000, 1'000'000, 1'000'000, 9'000'000))
                  .empty());
        CHECK(rawreel::bottleneckStage({}, stats(5, 0, 0, 0)) == "disk");
    }

    TEST_CASE("session: diagnosticsLines") {
        dvv::media::StoreStats stats;
        stats.frames = 3;
        stats.leases = 1;
        stats.tiers = {{.domain = {}, .bytes = 1024, .budget = 4096, .views = 2}};
        stats.converters = {
            {.name = "Produce", .runs = 3, .bytes = 2048, .nanoseconds = 1'260'000}};
        dvv::media::LeaseInfo lease;
        lease.key = {.origin = 7, .index = 2};
        lease.view.format = dvv::media::findFormat("yuv420p").value_or(dvv::media::FormatId{});
        lease.kind = dvv::media::LeaseKind::Read;
        lease.where = std::source_location::current();
        const std::vector<dvv::media::LeaseInfo> leases{lease};
        const std::vector<std::string> lines = rawreel::diagnosticsLines(stats, leases);
        REQUIRE(lines.size() == 4);
        CHECK(lines[0].starts_with("store: frames 3, leases 1, pins 0"));
        CHECK(lines[1] == "tier Host: 1024 / 4096 bytes, 2 views");
        CHECK(lines[2] == "converter Produce: 3 runs, 2048 bytes, 1.3 ms");
        CHECK(lines[3].starts_with("lease 7:2 yuv420p Read at test_session.cpp:"));
    }
}
