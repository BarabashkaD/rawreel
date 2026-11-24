#pragma once

// The viewport's shared state (design M3 v2 §4.4, D23): the latest FrameSet (the Session's
// IDisplaySink) and the display parameters (GUI): the view, the overlay and the cursor, each set
// on its own. Thread-safe and non-blocking.

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

#include "core/overlay.h"
#include "core/session_types.h"
#include "dvv/base/move_only_function.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"

namespace rawreel {

struct GridLevelParams {  // one grid level, backing pixels
    std::uint32_t stepLog2 = 0;
    float minimumSpacing = 0;
    std::uint32_t thickness = 1;
    std::uint32_t colour = 0;  // r | g << 8 | b << 16 | a << 24, sRGB-encoded; a = opacity
    bool shown = false;        // the level's checkbox (level 0: the grid's on/off)
    bool adaptive = false;
    bool halo = false;
    bool operator==(const GridLevelParams&) const = default;
};
struct OverlayParams {  // backing pixels; OverlayParams{} draws no grid and no strips
    std::array<GridLevelParams, GRID_LEVELS> levels{};
    bool numbers = false;
    dvv::media::ViewStrips strips{};
    std::uint32_t tickBand = 0;
    std::uint32_t tickWidth = 1;
    std::uint32_t markerDepth = 0;
    std::uint32_t litDepth = 0;  // the lit tick's depth from the inner edge, at most the strip
    std::array<std::uint32_t, GRID_LEVELS> tickLength{};
    float digitAdvance = 0;          // an estimate of one digit's advance, backing pixels
    bool dark = false;               // the theme: background and strip colours
    std::uint32_t cursorColour = 0;  // the lit tick and the marker: 0xRRGGBB, sRGB-encoded
    bool operator==(const OverlayParams&) const = default;
};
struct DisplayParams {
    // Resolved by the renderer against its extent and the frame size.
    dvv::media::ViewRequest view{};
    OverlayParams overlay{};
    std::optional<dvv::media::PointD> cursor{};  // backing pixels in the whole viewport; none: off
    bool operator==(const DisplayParams&) const = default;
};
struct DisplaySnapshot {  // one consistent read, taken under one lock
    FrameSet frames;      // a share of the shown FrameSet, with leases of its own
    DisplayParams params;
    std::uint64_t version = 0;  // +1 for every show() and clear(); the setters leave it
};
struct DisplayStats {
    std::uint64_t overwritten = 0;  // FrameSets replaced before any snapshot() read them
};

class DisplayState final : public IDisplaySink {
public:
    void show(FrameSet frames) override;
    void clear() override;  // drops every lease it holds
    // Each setter changes its own part of the params; it notifies only when the value changed.
    void setView(dvv::media::ViewRequest view);
    void setOverlay(const OverlayParams& overlay);
    void setCursor(std::optional<dvv::media::PointD> cursor);
    // show(), clear() and a setter that changed its value call the callback while holding
    // callback_mutex_ (never mutex_). Binding: once setOnChanged({}) returns, no callback is
    // running and none will run. The callback must not block (the viewport calls
    // Presenter::requestFrame).
    void setOnChanged(dvv::MoveOnlyFunction<void()> callback);
    [[nodiscard]] DisplaySnapshot snapshot() const;
    [[nodiscard]] DisplayStats stats() const;

private:
    void notify();

    // Lock order: display state, then store. snapshot() takes lease shares under mutex_ (a share
    // takes the store's mutex); show() and clear() drop the leases they replace outside mutex_
    // (a release may free the frame and the store).
    mutable std::mutex mutex_;
    FrameSet frames_;
    DisplayParams params_{};
    std::uint64_t version_ = 0;
    mutable bool read_ = true;  // the current FrameSet was returned by snapshot()
    DisplayStats stats_;
    std::mutex callback_mutex_;
    dvv::MoveOnlyFunction<void()> on_changed_;
};

}  // namespace rawreel
