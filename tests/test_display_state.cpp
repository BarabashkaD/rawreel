// DisplayState: versions, the separate view, overlay and cursor setters, the overwritten count,
// lease release and the binding setOnChanged handshake.
#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "core/session_types.h"
#include "dvv/base/executors.h"
#include "dvv/media/display.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/view.h"
#include "render/display_state.h"

using dvv::media::PointD;
using dvv::media::ViewMode;
using dvv::media::ViewRequest;
using rawreel::DisplayParams;
using rawreel::DisplaySnapshot;
using rawreel::DisplayState;
using rawreel::FrameSet;
using rawreel::OverlayParams;

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
    const rawreel::DisplaySnapshot snapshot = state.snapshot();
    CHECK(snapshot.frames.index == 7);
    state.clear();
    const rawreel::DisplaySnapshot cleared = state.snapshot();
    CHECK(cleared.version == 2);
    CHECK(cleared.frames.index == -1);
    CHECK(cleared.frames.count == 0);
}

TEST_CASE("display state: the setters do not bump the version") {
    DisplayState state;
    state.show(frameSet(7));
    CHECK(state.snapshot().version == 1);
    const ViewRequest zoom{.mode = ViewMode::Zoom, .zoom = 2.0, .centre = PointD{.x = 3, .y = 4}};
    state.setView(zoom);
    state.setOverlay(OverlayParams{.dark = true});
    state.setCursor(PointD{.x = 10.5, .y = 20.5});
    const rawreel::DisplaySnapshot snapshot = state.snapshot();
    CHECK(snapshot.version == 1);
    CHECK(snapshot.frames.index == 7);
    CHECK(snapshot.params.view == zoom);
    CHECK(snapshot.params.overlay == OverlayParams{.dark = true});
    CHECK(snapshot.params.cursor == PointD{.x = 10.5, .y = 20.5});
    // Each setter changes only its own part.
    state.setView(ViewRequest{});
    state.setCursor(std::nullopt);
    const rawreel::DisplaySnapshot changed = state.snapshot();
    CHECK(changed.params == DisplayParams{.overlay = OverlayParams{.dark = true}});
    state.clear();
    const rawreel::DisplaySnapshot cleared = state.snapshot();
    CHECK(cleared.version == 2);
    CHECK(cleared.params.overlay.dark);  // clear() keeps the params
}

TEST_CASE("display state: a setter with an unchanged value does not notify") {
    DisplayState state;
    int calls = 0;
    state.setOnChanged([&calls] { ++calls; });
    state.setView(ViewRequest{});
    state.setOverlay(OverlayParams{});
    state.setCursor(std::nullopt);
    CHECK(calls == 0);
    const ViewRequest zoom{.mode = ViewMode::Zoom, .zoom = 2.0};
    state.setView(zoom);
    state.setView(zoom);
    CHECK(calls == 1);
    OverlayParams overlay;
    overlay.levels[1].shown = true;
    state.setOverlay(overlay);
    state.setOverlay(overlay);
    CHECK(calls == 2);
    state.setCursor(PointD{.x = 1, .y = 2});
    state.setCursor(PointD{.x = 1, .y = 2});
    CHECK(calls == 3);
    state.setCursor(std::nullopt);
    CHECK(calls == 4);
    state.setOnChanged({});
}

TEST_CASE("display state: the setters and snapshot on two threads") {
    // Race case (TSan): the GUI thread sets the params while the presenter thread reads them. The
    // callback runs outside the state's mutex: it takes a snapshot itself, which a notify() under
    // that mutex would deadlock on. Each snapshot is one cut of the writes view, overlay, cursor
    // of round i, seen through each part's parity (before the first round: the parity of -1).
    constexpr int ROUNDS = 10000;
    DisplayState state;
    std::atomic<int> calls{0};
    bool bumped = false;        // the reader thread only, read after it joined
    bool inconsistent = false;  // the reader thread only, read after it joined
    state.setOnChanged([&state, &calls] {
        static_cast<void>(state.snapshot());
        calls.fetch_add(1, std::memory_order_relaxed);
    });
    {
        std::latch start(2);
        std::jthread writer([&state, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                const double at = i % 2;
                state.setView(ViewRequest{.mode = ViewMode::Zoom, .zoom = 1.0 + at});
                state.setOverlay(OverlayParams{.dark = i % 2 == 0});
                state.setCursor(PointD{.x = at, .y = at});
            }
        });
        std::jthread reader([&state, &start, &bumped, &inconsistent] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                const DisplaySnapshot snapshot = state.snapshot();
                if (snapshot.version != 0) {
                    bumped = true;
                }
                const DisplayParams& params = snapshot.params;
                const int view =
                    params.view.mode == ViewMode::Zoom ? static_cast<int>(params.view.zoom) - 1 : 1;
                const int overlay = params.overlay.dark ? 0 : 1;
                const int cursor = params.cursor ? static_cast<int>(params.cursor->x) : 1;
                // A cut after view i, overlay i or cursor i: the overlay is of the view's round,
                // else the overlay and the cursor are both of the round before.
                if ((params.cursor && params.cursor->x != params.cursor->y) ||
                    (overlay != view && cursor != overlay)) {
                    inconsistent = true;
                }
            }
        });
    }
    state.setOnChanged({});
    CHECK(calls.load() == 3 * ROUNDS);
    CHECK_FALSE(bumped);
    CHECK_FALSE(inconsistent);
    CHECK(state.snapshot().params ==
          DisplayParams{.view = ViewRequest{.mode = ViewMode::Zoom, .zoom = 2.0},
                        .overlay = OverlayParams{.dark = false},
                        .cursor = PointD{.x = 1, .y = 1}});
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
