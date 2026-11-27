// FrameFeed: versions, the overwritten count per subscriber, lease release, the roles of the
// subscriptions and the binding subscription handshake; FrameSet::share.
#include <doctest/doctest.h>

#include <algorithm>
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

#include "core/frame_set.h"
#include "dvv/base/executors.h"
#include "dvv/media/display.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/types.h"
#include "render/frame_feed.h"

using rawreel::FeedSnapshot;
using rawreel::FeedSubscription;
using rawreel::FrameFeed;
using rawreel::FrameSet;
using rawreel::ViewRole;
using rawreel::ViewRoles;

namespace {

FrameSet frameSet(std::int64_t index) {
    return FrameSet{.index = index, .count = 1, .leases = {}, .epoch = 0};
}

constexpr ViewRoles DISPLAY = ViewRoles::of(ViewRole::Display);
constexpr ViewRoles RAW = ViewRoles::of(ViewRole::Raw);

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
        FrameSet set = ::frameSet(index);
        set.leases[static_cast<std::size_t>(ViewRole::Display)][0] = adopt(1, index);
        REQUIRE(static_cast<bool>(set.lease(ViewRole::Display)));
        return set;
    }
    // The lease of a newly adopted frame of `origin` at `index` (another role's lease).
    [[nodiscard]] dvv::media::ReadLease adopt(dvv::media::OriginId origin,
                                              std::int64_t index) const {
        const dvv::media::ViewSpec view{.format = dvv::media::displayFormat(),
                                        .layout = dvv::media::LayoutPolicy::Tight,
                                        .domain = store_->hostDomain()};
        auto lease = store_->adopt(dvv::media::FrameKey{.origin = origin, .index = index},
                                   std::make_shared<HostTestFrame>(view));
        REQUIRE(lease.has_value());
        return std::move(*lease);
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

TEST_CASE("frame feed: versions and snapshots") {
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription subscription = feed->subscribe(DISPLAY, {});
    CHECK(subscription.snapshot().version == 0);
    feed->show(frameSet(7));
    CHECK(subscription.snapshot().version == 1);
    const FeedSnapshot snapshot = subscription.snapshot();
    CHECK(snapshot.frames.index == 7);
    feed->clear();
    const FeedSnapshot cleared = subscription.snapshot();
    CHECK(cleared.version == 2);
    CHECK(cleared.frames.index == -1);
    CHECK(cleared.frames.count == 0);
}

TEST_CASE("frame feed: overwritten per subscriber") {
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription state = feed->subscribe(DISPLAY, {});
    const FeedSubscription other = feed->subscribe(DISPLAY, {});
    feed->show(frameSet(1));
    feed->show(frameSet(2));
    CHECK(state.stats().overwritten == 1);
    static_cast<void>(state.snapshot());
    feed->show(frameSet(3));
    CHECK(state.stats().overwritten == 1);
    feed->clear();
    feed->show(frameSet(4));
    CHECK(state.stats().overwritten == 1);
    // The other subscriber read none of them: 2 and 3 were replaced unread; clear() is no read
    // but leaves nothing to overwrite.
    CHECK(other.stats().overwritten == 2);
}

TEST_CASE("frame feed: leases are released outside the feed") {
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription subscription = feed->subscribe(DISPLAY, {});
    feed->show(host.frameSet(1));
    CHECK(host.outstanding().size() == 1);
    {
        [[maybe_unused]] const FeedSnapshot snapshot = subscription.snapshot();
        CHECK(host.outstanding().size() == 2);  // the snapshot holds a share of its own
    }
    CHECK(host.outstanding().size() == 1);
    feed->show(host.frameSet(2));
    const std::vector<dvv::media::LeaseInfo> shown = host.outstanding();
    REQUIRE(shown.size() == 1);
    CHECK(shown.front().key.index == 2);  // the next show dropped the first
    feed->clear();
    CHECK(host.outstanding().empty());
}

TEST_CASE("frame feed: show, snapshot and clear on two threads") {
    // Race case (TSan at the CI checkpoint): show() and clear() drop their shares outside the
    // feed's mutex; snapshot() takes a share under it (lock order: feed, then store).
    constexpr int ROUNDS = 10000;
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription subscription = feed->subscribe(DISPLAY, {});
    {
        const FrameSet shown = host.frameSet(1);
        std::latch start(2);
        std::jthread writer([&feed, &shown, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                feed->show(shown.share());
                feed->clear();
            }
        });
        std::jthread reader([&subscription, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                [[maybe_unused]] const FeedSnapshot snapshot = subscription.snapshot();
            }
        });
    }
    feed->clear();
    CHECK(subscription.snapshot().version == std::uint64_t{(2 * ROUNDS) + 1});
    CHECK(host.outstanding().empty());
}

TEST_CASE("frame feed: no callback after the subscription is gone") {
    // Race case (Debug x10 in Task 7, Windows ASan x10 at the checkpoint, TSan once Linux returns).
    constexpr int SHOWS = 20000;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription reader = feed->subscribe(DISPLAY, {});
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> violations{0};
    std::atomic<bool> cleared{false};
    std::atomic<bool> done{false};
    std::optional<FeedSubscription> subscription =
        feed->subscribe(DISPLAY, [&calls, &violations, &cleared] {
            calls.fetch_add(1, std::memory_order_relaxed);
            if (cleared.load(std::memory_order_acquire)) {
                violations.fetch_add(1, std::memory_order_relaxed);
            }
        });
    {
        std::jthread producer([&feed, &done] {
            for (int i = 0; i < SHOWS; ++i) {
                feed->show(frameSet(i));
            }
            done.store(true, std::memory_order_release);
        });
        // `done` only bounds the wait when no callback runs at all.
        while (calls.load(std::memory_order_relaxed) <= 100 &&
               !done.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        subscription.reset();
        cleared.store(true, std::memory_order_release);
    }
    CHECK(violations.load() == 0);
    CHECK(calls.load() > 100);
    CHECK(reader.snapshot().frames.index == SHOWS - 1);
}

TEST_CASE("frame feed: a subscription sees only its roles") {
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription display = feed->subscribe(DISPLAY, {});
    const FeedSubscription raw = feed->subscribe(RAW, {});
    CHECK(static_cast<bool>(display));
    CHECK_FALSE(static_cast<bool>(FeedSubscription{}));
    FrameSet set = host.frameSet(5);
    set.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = host.adopt(2, 5);
    feed->show(std::move(set));
    REQUIRE(host.outstanding().size() == 2);

    const FeedSnapshot shown = display.snapshot();
    CHECK(shown.frames.index == 5);
    CHECK(static_cast<bool>(shown.frames.lease(ViewRole::Display)));
    CHECK_FALSE(static_cast<bool>(shown.frames.lease(ViewRole::Raw)));
    const FeedSnapshot unconverted = raw.snapshot();
    CHECK(unconverted.frames.index == 5);
    CHECK_FALSE(static_cast<bool>(unconverted.frames.lease(ViewRole::Display)));
    CHECK(static_cast<bool>(unconverted.frames.lease(ViewRole::Raw)));
    CHECK(host.outstanding().size() == 4);  // one share per role asked
    // Each snapshot marks the set read for its own subscriber only.
    feed->show(frameSet(6));
    CHECK(display.stats().overwritten == 0);
    CHECK(raw.stats().overwritten == 0);
}

TEST_CASE("frame feed: the roles callback hears every union change") {
    const auto feed = std::make_shared<FrameFeed>();
    std::vector<ViewRoles> heard;
    // push_back may throw; a throw would terminate the test, which is the right outcome here.
    feed->setRolesChanged([&heard](ViewRoles roles) noexcept {  // NOLINT(bugprone-exception-escape)
        heard.push_back(roles);
    });
    REQUIRE(heard.size() == 1);  // installing it reports the current union
    CHECK(heard.back() == DISPLAY);
    CHECK(feed->roles() == DISPLAY);
    {
        const FeedSubscription display = feed->subscribe(DISPLAY, {});
        CHECK(heard.size() == 1);  // {Display}: the union stays Display
        std::optional<FeedSubscription> raw = feed->subscribe(RAW, {});
        REQUIRE(heard.size() == 2);
        CHECK(heard.back() == (DISPLAY | RAW));
        CHECK(feed->roles() == (DISPLAY | RAW));
        raw.reset();
        REQUIRE(heard.size() == 3);
        CHECK(heard.back() == DISPLAY);
    }
    CHECK(heard.size() == 3);  // the Display subscription leaves the union as it is
    CHECK(feed->roles() == DISPLAY);
    feed->setRolesChanged({});
    CHECK(heard.size() == 3);  // removing the callback reports nothing
}

TEST_CASE("frame feed: installing the roles callback reports the roles subscribed before it") {
    const auto feed = std::make_shared<FrameFeed>();
    // A view that subscribed Raw before the callback existed (a setting restored at start).
    const FeedSubscription raw = feed->subscribe(DISPLAY | RAW, {});
    CHECK(raw.roles() == (DISPLAY | RAW));
    CHECK(FeedSubscription{}.roles() == ViewRoles{});
    CHECK(feed->roles() == (DISPLAY | RAW));
    std::vector<ViewRoles> heard;
    // push_back may throw; a throw would terminate the test, which is the right outcome here.
    feed->setRolesChanged([&heard](ViewRoles roles) noexcept {  // NOLINT(bugprone-exception-escape)
        heard.push_back(roles);
    });
    REQUIRE(heard.size() == 1);
    CHECK(heard.back() == (DISPLAY | RAW));
    feed->setRolesChanged({});
    CHECK(heard.size() == 1);
}

TEST_CASE("frame feed: a dropped role leaves the held set") {
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription display = feed->subscribe(DISPLAY, {});
    std::optional<FeedSubscription> raw = feed->subscribe(RAW, {});
    FrameSet set = host.frameSet(8);
    set.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = host.adopt(2, 8);
    feed->show(std::move(set));
    const auto raw_leases = [&host] {
        const std::vector<dvv::media::LeaseInfo> leases = host.outstanding();
        return std::ranges::count_if(
            leases, [](const dvv::media::LeaseInfo& info) { return info.key.origin == 2; });
    };
    CHECK(raw_leases() == 1);
    raw.reset();
    CHECK(raw_leases() == 0);  // the paused frame holds no Raw lease any more
    CHECK(host.outstanding().size() == 1);
    CHECK(display.snapshot().frames.index == 8);
}

TEST_CASE("frame feed: a raw subscription made and dropped against show and snapshot") {
    // Race case (TSan at the CI checkpoint): the roles union changes while frames are shown and
    // read; a dropped Raw subscription leaves no Raw lease behind.
    constexpr int ROUNDS = 2000;
    constexpr int MAX_SHOWS = 1000000;  // bounds the show loop should the other thread stall
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription display = feed->subscribe(DISPLAY, {});
    FrameSet shown = host.frameSet(3);
    shown.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = host.adopt(2, 3);
    const auto raw_leases = [&host] {
        const std::vector<dvv::media::LeaseInfo> leases = host.outstanding();
        return std::ranges::count_if(
            leases, [](const dvv::media::LeaseInfo& info) { return info.key.origin == 2; });
    };
    {
        std::atomic<bool> done{false};
        std::latch start(2);
        std::jthread subscriber([&feed, &done, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                std::optional<FeedSubscription> raw = feed->subscribe(RAW, {});
                raw.reset();
            }
            done = true;
        });
        std::jthread shower([&feed, &display, &shown, &done, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < MAX_SHOWS && !done.load(); ++i) {
                feed->show(shown.share());
                [[maybe_unused]] const FeedSnapshot snapshot = display.snapshot();
            }
        });
    }
    CHECK(raw_leases() == 1);  // the test's own
    CHECK(display.snapshot().frames.index == 3);
    shown = FrameSet{};
    feed->clear();
    CHECK(host.outstanding().empty());
}

TEST_CASE("frame feed: setRoles changes a subscription's roles in place") {
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    std::vector<ViewRoles> heard;
    // push_back may throw; a throw would terminate the test, which is the right outcome here.
    feed->setRolesChanged([&heard](ViewRoles roles) noexcept {  // NOLINT(bugprone-exception-escape)
        heard.push_back(roles);
    });
    REQUIRE(heard.size() == 1);
    FeedSubscription view = feed->subscribe(DISPLAY, {});
    view.setRoles(DISPLAY | RAW);
    CHECK(view.roles() == (DISPLAY | RAW));
    CHECK(feed->roles() == (DISPLAY | RAW));
    REQUIRE(heard.size() == 2);
    CHECK(heard.back() == (DISPLAY | RAW));
    view.setRoles(DISPLAY | RAW);
    CHECK(heard.size() == 2);  // the same roles: the union does not change
    FrameSet set = host.frameSet(4);
    set.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = host.adopt(2, 4);
    feed->show(std::move(set));
    CHECK(static_cast<bool>(view.snapshot().frames.lease(ViewRole::Raw)));
    view.setRoles(DISPLAY);
    CHECK(view.roles() == DISPLAY);
    CHECK(feed->roles() == DISPLAY);
    REQUIRE(heard.size() == 3);
    CHECK(heard.back() == DISPLAY);
    CHECK(host.outstanding().size() == 1);  // the dropped role leaves the held set at once
    const FeedSnapshot snapshot = view.snapshot();
    CHECK(snapshot.frames.index == 4);
    CHECK_FALSE(static_cast<bool>(snapshot.frames.lease(ViewRole::Raw)));
    FeedSubscription empty;
    empty.setRoles(DISPLAY | RAW);  // an empty subscription stays empty
    CHECK(empty.roles() == ViewRoles{});
    CHECK(feed->roles() == DISPLAY);
    feed->setRolesChanged({});
}

TEST_CASE("frame feed: setRoles against show and snapshot on two threads") {
    // Race case (TSan at the CI checkpoint): a view's padding check toggles its Raw role while its
    // presenter reads frames that are shown; the last {Display} leaves no Raw lease behind.
    constexpr int ROUNDS = 2000;
    constexpr int MAX_READS = 1000000;  // bounds the read loop should the other thread stall
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    FeedSubscription view = feed->subscribe(DISPLAY, {});
    FrameSet shown = host.frameSet(5);
    shown.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = host.adopt(2, 5);
    const auto raw_leases = [&host] {
        const std::vector<dvv::media::LeaseInfo> leases = host.outstanding();
        return std::ranges::count_if(
            leases, [](const dvv::media::LeaseInfo& info) { return info.key.origin == 2; });
    };
    std::atomic<int> wrong{0};  // snapshots without the shown frame; checked on this thread
    {
        std::atomic<bool> done{false};
        std::latch start(2);
        std::jthread toggler([&view, &done, &start] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                view.setRoles(DISPLAY | RAW);
                view.setRoles(DISPLAY);
            }
            done = true;
        });
        std::jthread presenter([&feed, &view, &shown, &done, &start, &wrong] {
            start.arrive_and_wait();
            for (int i = 0; i < MAX_READS && !done.load(); ++i) {
                feed->show(shown.share());
                if (view.snapshot().frames.index != 5) {
                    ++wrong;
                }
            }
        });
    }
    CHECK(wrong.load() == 0);
    CHECK(view.roles() == DISPLAY);
    CHECK(feed->roles() == DISPLAY);
    CHECK(raw_leases() == 1);  // the test's own
    shown = FrameSet{};
    CHECK(raw_leases() == 0);
    CHECK(view.snapshot().frames.index == 5);
    feed->clear();
    CHECK(host.outstanding().empty());
}

TEST_CASE("frame feed: show strips a role nobody subscribes") {
    const HostStore host;
    const auto feed = std::make_shared<FrameFeed>();
    const FeedSubscription display = feed->subscribe(DISPLAY, {});
    FrameSet set = host.frameSet(9);
    set.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = host.adopt(2, 9);
    feed->show(std::move(set));
    const std::vector<dvv::media::LeaseInfo> leases = host.outstanding();
    CHECK(std::ranges::count_if(
              leases, [](const dvv::media::LeaseInfo& info) { return info.key.origin == 2; }) == 0);
    CHECK(leases.size() == 1);
}

TEST_CASE("frame set: share copies only the asked roles") {
    static_assert(ViewRoles::all().has(ViewRole::Raw));
    static_assert(!ViewRoles::of(ViewRole::Display).has(ViewRole::Planes));
    static_assert(
        (ViewRoles::of(ViewRole::Display) | ViewRoles::of(ViewRole::Raw)).has(ViewRole::Raw));
    static_assert(ViewRoles::of(ViewRole::Planes) == ViewRoles{.bits = 2});

    const HostStore host;
    FrameSet set = host.frameSet(3);
    set.leases[static_cast<std::size_t>(ViewRole::Raw)][0] = set.lease(ViewRole::Display).share();
    set.streamSet = 9;
    set.epoch = 4;
    REQUIRE(host.outstanding().size() == 2);

    const FrameSet display = set.share(ViewRoles::of(ViewRole::Display));
    CHECK(display.index == 3);
    CHECK(display.epoch == 4);
    CHECK(display.streamSet == 9);
    CHECK(static_cast<bool>(display.lease(ViewRole::Display)));
    CHECK_FALSE(static_cast<bool>(display.lease(ViewRole::Planes)));
    CHECK_FALSE(static_cast<bool>(display.lease(ViewRole::Raw)));
    CHECK(host.outstanding().size() == 3);  // one share per asked role

    const FrameSet everything = set.share();
    CHECK(static_cast<bool>(everything.lease(ViewRole::Display)));
    CHECK_FALSE(static_cast<bool>(everything.lease(ViewRole::Planes)));
    CHECK(static_cast<bool>(everything.lease(ViewRole::Raw)));
    CHECK(host.outstanding().size() == 5);
}
