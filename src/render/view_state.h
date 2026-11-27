#pragma once

// One view's parameters (GUI): the view, the overlay, the cursor and the alpha toggle, each set
// on its own, and the snapshot a presenter draws (the feed's frames with them). Thread-safe and
// non-blocking.

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "core/inspector.h"
#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "dvv/base/move_only_function.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "render/frame_feed.h"
#include "render/view_layout.h"

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
    bool dark = false;               // the theme: background and strip colours
    std::uint32_t cursorColour = 0;  // the lit tick and the marker: 0xRRGGBB, sRGB-encoded
    // The ruler labels' glyphs; null: no numbers drawn. Compared by pointer: a new strip is a new
    // object.
    std::shared_ptr<const dvv::gpu::GlyphStrip> glyphs{};
    LastValue lastValue = LastValue::Pixel;
    LabelSpacing labelSpacing{};
    bool cursorTagTextBlack = false;  // the cursor tag's text: black, else white
    std::uint32_t checkerSize = 0;    // the alpha checkerboard's square; the pass draws at least 1
    // The padding marks (ViewParams::paddingMarks): the disc across and its dark rim; the tiles
    // of the check are markSize, at least 2 (markRim + 1), so a mark lies in its 3 x 3 tiles.
    std::uint32_t markSize = 0;
    std::uint32_t markRim = 0;
    bool operator==(const OverlayParams&) const = default;
};
struct InspectParams {  // the inspect layers; on = false draws today's picture
    bool on = false;
    PanelKind panels = PanelKind::Picture;
    bool numbers = false;
    // The value strip's glyphs; compared by pointer, as OverlayParams::glyphs.
    std::shared_ptr<const dvv::gpu::GlyphStrip> glyphs{};
    ValueMetrics metrics{};                      // the values' layout (LayerBlock::metrics)
    std::array<std::uint32_t, 4> letterCells{};  // per component: its letter's cell in the strip
    std::optional<dvv::media::ImagePixel> probe{};
    bool operator==(const InspectParams&) const = default;
};
struct ImageRect {  // image pixels
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const ImageRect&) const = default;
};
// The markers over a view's picture: the pin's box (a dot at a low zoom) and the outline of the
// image region another view shows (the Zoom window's visible pixels). None: today's picture.
struct MarkerParams {
    std::optional<dvv::media::ImagePixel> pin{};
    std::optional<ImageRect> outline{};
    bool operator==(const MarkerParams&) const = default;
};
// The image rects one panel's markers frame: the pin's cell in that panel (markerCells of
// render/layer_block.h: the pixel, or a subsampled plane's sample block) and the outline.
struct MarkerCells {
    std::optional<ImageRect> pin{};
    std::optional<ImageRect> outline{};
    bool operator==(const MarkerCells&) const = default;
};
// The cells of `markers` in a panel that draws each image pixel as its own cell: the pin's pixel
// and the outline.
[[nodiscard]] inline MarkerCells pixelCells(const MarkerParams& markers) noexcept {
    MarkerCells cells{.pin = std::nullopt, .outline = markers.outline};
    if (markers.pin) {
        cells.pin = ImageRect{.x = markers.pin->x, .y = markers.pin->y, .width = 1, .height = 1};
    }
    return cells;
}
// Whether a marker is set: the picture then draws with picture.slang plus the marker pass (markers
// alone), or with the inspect pipeline when a layer is on.
[[nodiscard]] inline bool markersOn(const MarkerParams& markers) noexcept {
    return markers.pin.has_value() || markers.outline.has_value();
}
[[nodiscard]] inline bool markersOn(const MarkerCells& cells) noexcept {
    return cells.pin.has_value() || cells.outline.has_value();
}
struct ViewParams {
    // Resolved by the renderer against its extent and the frame size.
    dvv::media::ViewRequest view{};
    OverlayParams overlay{};
    std::optional<dvv::media::PointD> cursor{};  // backing pixels in the whole viewport; none: off
    bool alpha = false;  // the picture blended over a checkerboard by its alpha, else opaque
    InspectParams inspect{};
    MarkerParams markers{};
    // The padding check (level 1, needs the Raw view): a Picture panel marks every sample whose
    // own stored word has padding bits set, and the readback carries their count.
    bool paddingMarks = false;
    bool operator==(const ViewParams&) const = default;
};
struct ViewSnapshot {  // what one presenter frame draws
    FeedSnapshot feed;
    ViewParams params;
};

class ViewState {
public:
    // Each setter changes its own part of the params; it notifies only when the value changed.
    void setView(dvv::media::ViewRequest view);
    void setOverlay(const OverlayParams& overlay);
    void setCursor(std::optional<dvv::media::PointD> cursor);
    void setAlpha(bool on);
    void setInspect(const InspectParams& inspect);
    void setMarkers(const MarkerParams& markers);
    void setPaddingMarks(bool on);
    // A setter that changed its value calls the callback while holding
    // callback_mutex_ (never mutex_). Binding: once setOnChanged({}) returns, no callback is
    // running and none will run. The callback must not block (the viewport calls
    // Presenter::requestFrame) and must not reset its own callback from inside it (a deadlock).
    void setOnChanged(dvv::MoveOnlyFunction<void()> callback);
    [[nodiscard]] ViewParams params() const;

private:
    void notify();

    mutable std::mutex mutex_;
    ViewParams params_{};
    std::mutex callback_mutex_;
    dvv::MoveOnlyFunction<void()> on_changed_;
};

}  // namespace rawreel
