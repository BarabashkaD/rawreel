// DisplayState (design M3 v2 §4.4, D23): versions, the overwritten count, lease release and the
// binding setOnChanged handshake.
#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "core/session_types.h"
#include "dvv/base/executors.h"
#include "dvv/media/display.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "render/display_state.h"

using rawreel::CompositeParams;
using rawreel::DisplayState;
using rawreel::FrameSet;

namespace {

FrameSet frameSet(std::int64_t index) {
    return FrameSet{.index = index, .count = 1, .views = {}, .epoch = 0};
}

// A frame with one native host view and no bytes: enough for the store to hand out leases.
class HostTestFrame final : public dvv::media::IFrame {
public:
    explicit HostTestFrame(const dvv::media::ViewSpec& view)
        : desc_{.format = view.format, .colour = view.colour, .width = 1, .height = 1},
          view_(view) {}
    [[nodiscard]] const dvv::media::FrameDesc& desc() const noexcept override { return desc_; }
    [[nodiscard]] std::span<const dvv::media::ViewSpec> nativeViews() const noexcept override {
        return {&view_, 1};
    }
    [[nodiscard]] dvv::media::Binding bind(const dvv::media::ViewSpec& /*native*/) const override {
        return dvv::media::HostPlanesB{};
    }
    [[nodiscard]] std::size_t bytes() const noexcept override { return 0; }

private:
    dvv::media::FrameDesc desc_;
    dvv::media::ViewSpec view_;
};

// A host-only store; each frameSet() adopts a new frame and puts its lease in the FrameSet.
class HostStore {
public:
    HostStore() {
        if (auto cpu = dvv::ExecutorFactory::makeThreadPool(1); cpu) {
            cpu_ = std::move(*cpu);
        }
        if (auto io = dvv::ExecutorFactory::makeThreadPool(1); io) {
            io_ = std::move(*io);
        }
        REQUIRE(cpu_ != nullptr);
        REQUIRE(io_ != nullptr);
        if (auto store = dvv::media::FrameStore::create({.cpu = cpu_, .io = io_}); store) {
            store_ = std::move(*store);
        }
        REQUIRE(store_ != nullptr);
    }

    // One stream at `index`; its view is the lease of a newly adopted frame.
    [[nodiscard]] FrameSet frameSet(std::int64_t index) const {
        const dvv::media::ViewSpec view{.format = dvv::media::displayFormat(),
                                        .layout = dvv::media::LayoutPolicy::Tight,
                                        .domain = store_->hostDomain()};
        FrameSet set = ::frameSet(index);
        if (auto lease = store_->adopt(dvv::media::FrameKey{.origin = 1, .index = index},
                                       std::make_shared<HostTestFrame>(view));
            lease) {
            set.views[0] = std::move(*lease);
        }
        REQUIRE(static_cast<bool>(set.views[0]));
        return set;
    }
    [[nodiscard]] std::vector<dvv::media::LeaseInfo> outstanding() const {
        return store_->outstandingLeases();
    }

private:
    std::shared_ptr<dvv::ThreadPool> cpu_;
    std::shared_ptr<dvv::ThreadPool> io_;
    std::shared_ptr<dvv::media::FrameStore> store_;
};

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
    const HostStore host;
    DisplayState state;
    state.show(host.frameSet(1));
    CHECK(host.outstanding().size() == 1);
    {
        [[maybe_unused]] const rawreel::DisplaySnapshot snapshot = state.snapshot();
        CHECK(host.outstanding().size() == 2);  // the snapshot holds a share of its own
    }
    CHECK(host.outstanding().size() == 1);
    state.show(host.frameSet(2));
    const std::vector<dvv::media::LeaseInfo> shown = host.outstanding();
    REQUIRE(shown.size() == 1);
    CHECK(shown.front().key.index == 2);  // the next show dropped the first
    state.clear();
    CHECK(host.outstanding().empty());
}

TEST_CASE("display state: show, snapshot and clear on two threads") {
    // Race case (TSan at the CI checkpoint): show() and clear() drop their shares outside the
    // state's mutex; snapshot() takes a share under it (lock order: display state, then store).
    constexpr int ROUNDS = 10000;
    const HostStore host;
    DisplayState state;
    {
        const FrameSet shown = host.frameSet(1);
        std::latch start(2);
        std::jthread writer([&state, &shown, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                state.show(shown.share());
                state.clear();
            }
        });
        std::jthread reader([&state, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                [[maybe_unused]] const rawreel::DisplaySnapshot snapshot = state.snapshot();
            }
        });
    }
    state.clear();
    CHECK(state.snapshot().version == std::uint64_t{(2 * ROUNDS) + 1});
    CHECK(host.outstanding().empty());
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
