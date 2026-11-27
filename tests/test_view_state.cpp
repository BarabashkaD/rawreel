// ViewState: the separate view, overlay, cursor and alpha setters, each notifying only on a
// change, and the binding setOnChanged handshake.
#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <latch>
#include <optional>
#include <thread>

#include "dvv/media/view.h"
#include "render/view_state.h"

using dvv::media::PointD;
using dvv::media::ViewMode;
using dvv::media::ViewRequest;
using rawreel::OverlayParams;
using rawreel::ViewParams;
using rawreel::ViewState;

TEST_CASE("view state: each setter changes only its own part") {
    ViewState state;
    const ViewRequest zoom{.mode = ViewMode::Zoom, .zoom = 2.0, .centre = PointD{.x = 3, .y = 4}};
    state.setView(zoom);
    state.setOverlay(OverlayParams{.dark = true});
    state.setCursor(PointD{.x = 10.5, .y = 20.5});
    const ViewParams params = state.params();
    CHECK(params.view == zoom);
    CHECK(params.overlay == OverlayParams{.dark = true});
    CHECK(params.cursor == PointD{.x = 10.5, .y = 20.5});
    state.setView(ViewRequest{});
    state.setCursor(std::nullopt);
    CHECK(state.params() == ViewParams{.overlay = OverlayParams{.dark = true}});
}

TEST_CASE("view state: a setter with an unchanged value does not notify") {
    ViewState state;
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

TEST_CASE("view state: setAlpha notifies once") {
    ViewState state;
    int calls = 0;
    state.setOnChanged([&calls] { ++calls; });
    state.setAlpha(false);  // unchanged: the default is off
    CHECK(calls == 0);
    state.setAlpha(true);
    CHECK(calls == 1);
    const ViewParams params = state.params();
    CHECK(params.alpha);
    CHECK(params == ViewParams{.alpha = true});  // only its own part
    state.setAlpha(true);
    CHECK(calls == 1);
    state.setAlpha(false);
    CHECK(calls == 2);
    CHECK_FALSE(state.params().alpha);
    state.setOnChanged({});
}

TEST_CASE("view state: the setters on two threads") {
    // Race case (TSan): the GUI thread sets the params while the presenter thread reads them. The
    // callback runs outside the state's mutex: it reads the params itself, which a notify() under
    // that mutex would deadlock on. Each read is one cut of the writes view, overlay, cursor,
    // alpha of round i, seen through each part's parity (before the first round: the parity of -1).
    constexpr int ROUNDS = 10000;
    ViewState state;
    std::atomic<int> calls{0};
    bool inconsistent = false;  // the reader thread only, read after it joined
    state.setOnChanged([&state, &calls] {
        static_cast<void>(state.params());
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
                state.setAlpha(i % 2 == 0);
            }
        });
        std::jthread reader([&state, &start, &inconsistent] {
            start.arrive_and_wait();
            for (int i = 0; i < ROUNDS; ++i) {
                const ViewParams params = state.params();
                const int view =
                    params.view.mode == ViewMode::Zoom ? static_cast<int>(params.view.zoom) - 1 : 1;
                const int overlay = params.overlay.dark ? 0 : 1;
                const int cursor = params.cursor ? static_cast<int>(params.cursor->x) : 1;
                const int alpha = params.alpha ? 0 : 1;
                // A cut after view i, overlay i, cursor i or alpha i: in write order the parts
                // change round at most once, from the view's round to the round before.
                const int changes = static_cast<int>(overlay != view) +
                                    static_cast<int>(cursor != overlay) +
                                    static_cast<int>(alpha != cursor);
                if ((params.cursor && params.cursor->x != params.cursor->y) || changes > 1) {
                    inconsistent = true;
                }
            }
        });
    }
    state.setOnChanged({});
    CHECK(calls.load() == 4 * ROUNDS);
    CHECK_FALSE(inconsistent);
    CHECK(state.params() == ViewParams{.view = ViewRequest{.mode = ViewMode::Zoom, .zoom = 2.0},
                                       .overlay = OverlayParams{.dark = false},
                                       .cursor = PointD{.x = 1, .y = 1},
                                       .alpha = false});
}

TEST_CASE("view state: no callback after setOnChanged({})") {
    // Race case (Debug x10 in Task 7, Windows ASan x10 at the checkpoint, TSan once Linux returns).
    constexpr int CHANGES = 20000;
    ViewState state;
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
            for (int i = 0; i < CHANGES; ++i) {
                state.setCursor(PointD{.x = static_cast<double>(i), .y = 0});
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
    CHECK(state.params().cursor == PointD{.x = CHANGES - 1, .y = 0});
}

TEST_CASE("view state: setMarkers notifies once per change") {
    ViewState state;
    int calls = 0;
    state.setOnChanged([&calls] { ++calls; });
    state.setMarkers(rawreel::MarkerParams{});  // unchanged: no markers by default
    CHECK(calls == 0);
    const rawreel::MarkerParams pin{.pin = dvv::media::ImagePixel{.x = 3, .y = 4}};
    state.setMarkers(pin);
    state.setMarkers(pin);
    CHECK(calls == 1);
    CHECK(state.params().markers == pin);
    CHECK(rawreel::markersOn(state.params().markers));
    rawreel::MarkerParams outline = pin;
    outline.outline = rawreel::ImageRect{.x = 1, .y = 2, .width = 5, .height = 6};
    state.setMarkers(outline);
    CHECK(calls == 2);
    CHECK(state.params() == ViewParams{.markers = outline});
    state.setMarkers({});
    CHECK(calls == 3);
    CHECK(!rawreel::markersOn(state.params().markers));
    state.setOnChanged({});
}
