// DisplayState (design M3 v2 §4.4, D23): versions, the overwritten count, lease release and the
// binding setOnChanged handshake.
#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "core/session_types.h"
#include "dvv/media/frame_store.h"
#include "render/display_state.h"

using rawreel::CompositeParams;
using rawreel::DisplayState;
using rawreel::FrameSet;

namespace {

FrameSet frameSet(std::int64_t index) {
    return FrameSet{.index = index, .count = 1, .views = {}, .epoch = 0};
}

}  // namespace

TEST_CASE("display state: versions and snapshots") {
    DisplayState state;
    CHECK(state.snapshot().version == 0);
    state.show(frameSet(7));
    CHECK(state.snapshot().version == 1);
    state.setParams(CompositeParams{.dark = true});
    CHECK(state.snapshot().version == 2);
    const rawreel::DisplaySnapshot snapshot = state.snapshot();
    CHECK(snapshot.frames.index == 7);
    CHECK(snapshot.params.dark);
    state.setParams(CompositeParams{.dark = true});  // equal params still count
    CHECK(state.snapshot().version == 3);
    state.clear();
    const rawreel::DisplaySnapshot cleared = state.snapshot();
    CHECK(cleared.version == 4);
    CHECK(cleared.frames.index == -1);
    CHECK(cleared.frames.count == 0);
    CHECK(cleared.params.dark);
}

TEST_CASE("display state: overwritten") {
    DisplayState state;
    state.show(frameSet(1));
    state.show(frameSet(2));
    CHECK(state.stats().overwritten == 1);
    static_cast<void>(state.snapshot());
    state.show(frameSet(3));
    CHECK(state.stats().overwritten == 1);
    state.clear();
    state.show(frameSet(4));
    CHECK(state.stats().overwritten == 1);
}

TEST_CASE("display state: leases are released outside the state") {
    DisplayState state;
    std::weak_ptr<const dvv::media::ReadLease> weak;
    {
        FrameSet set = frameSet(1);
        auto lease = std::make_shared<const dvv::media::ReadLease>();
        weak = lease;
        set.views[0] = std::move(lease);
        state.show(std::move(set));
    }
    CHECK_FALSE(weak.expired());
    state.show(frameSet(2));
    CHECK(weak.expired());

    {
        FrameSet set = frameSet(3);
        auto lease = std::make_shared<const dvv::media::ReadLease>();
        weak = lease;
        set.views[0] = std::move(lease);
        state.show(std::move(set));
    }
    CHECK_FALSE(weak.expired());
    state.clear();
    CHECK(weak.expired());
}

TEST_CASE("display state: no callback after setOnChanged({})") {
    // Race case (Debug x10 in Task 7, Windows ASan x10 at the checkpoint, TSan once Linux returns).
    constexpr int SHOWS = 20000;
    DisplayState state;
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> violations{0};
    std::atomic<bool> cleared{false};
    std::atomic<bool> done{false};
    state.setOnChanged([&calls, &violations, &cleared] {
        calls.fetch_add(1, std::memory_order_relaxed);
        if (cleared.load(std::memory_order_acquire)) {
            violations.fetch_add(1, std::memory_order_relaxed);
        }
    });
    {
        std::jthread producer([&state, &done] {
            for (int i = 0; i < SHOWS; ++i) {
                state.show(frameSet(i));
            }
            done.store(true, std::memory_order_release);
        });
        // `done` only bounds the wait when no callback runs at all.
        while (calls.load(std::memory_order_relaxed) <= 100 &&
               !done.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        state.setOnChanged({});
        cleared.store(true, std::memory_order_release);
    }
    CHECK(violations.load() == 0);
    CHECK(calls.load() > 100);
    CHECK(state.snapshot().frames.index == SHOWS - 1);
}
