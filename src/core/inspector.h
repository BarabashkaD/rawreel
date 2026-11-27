#pragma once

// The pixel inspector's model and pure rules (pixel inspector design §3, §4): the pin, the probe,
// the layout and mode, the whole-number zoom, the numbers-fit zoom and the Cell rule. wx-free.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "core/overlay.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel {

enum class InspectorLayout : std::uint8_t { Structure, Planes };
enum class InspectorMode : std::uint8_t { Follow, Cell };

// The clip's pixel grid: the components come from formatDesc(format).
struct ClipGrid {
    dvv::platform::PixelSize image{};
    dvv::media::FormatId format = 0;
    bool operator==(const ClipGrid&) const = default;
};

// The block of image pixels that share the chroma sample of `pixel`: the block of the most
// subsampled component, clipped to the image. {x, y, 1, 1} without subsampling.
[[nodiscard]] dvv::media::PixelBlock chromaBlockAt(const ClipGrid& clip,
                                                   dvv::media::ImagePixel pixel) noexcept;

class Inspector {
public:
    [[nodiscard]] std::optional<dvv::media::ImagePixel> pin() const noexcept { return pin_; }
    [[nodiscard]] std::optional<dvv::media::ImagePixel> probe() const noexcept { return probe_; }
    [[nodiscard]] bool live() const noexcept { return live_; }
    [[nodiscard]] InspectorMode mode() const noexcept { return mode_; }
    [[nodiscard]] InspectorLayout layout() const noexcept { return layout_; }
    // The probe when live and set, else the pin.
    [[nodiscard]] std::optional<dvv::media::ImagePixel> shown() const noexcept;

    void setPin(std::optional<dvv::media::ImagePixel> pin) noexcept { pin_ = pin; }
    // A nullopt keeps the last probe.
    void setProbe(std::optional<dvv::media::ImagePixel> probe) noexcept {
        if (probe.has_value()) {
            probe_ = probe;
        }
    }
    void setLive(bool live) noexcept { live_ = live; }
    void setMode(InspectorMode mode) noexcept { mode_ = mode; }
    void setLayout(InspectorLayout layout) noexcept { layout_ = layout; }

    // Moves the pin by (dx, dy) pixels, clamped into the image; with `block`, by whole samples of
    // the most subsampled component (the pin keeps its offset in its block, clipped to the new
    // block). False without a pin.
    bool movePin(int dx, int dy, bool block, const ClipGrid& clip);
    // A new clip: the pin and the probe are kept only inside the new image.
    void clipChanged(dvv::platform::PixelSize image);

private:
    std::optional<dvv::media::ImagePixel> pin_, probe_;
    bool live_ = false;
    InspectorMode mode_ = InspectorMode::Follow;
    InspectorLayout layout_ = InspectorLayout::Structure;
};

// The Zoom window's zoom is a whole number in [minZoom, WHOLE_ZOOM_MAX] on every input path: it
// never zooms out past MIN_ZOOM_CELLS x MIN_ZOOM_CELLS major grid cells per panel, and never shows
// a Fit view.
inline constexpr std::uint32_t WHOLE_ZOOM_MAX = 256;
inline constexpr std::uint32_t MIN_ZOOM_CELLS = 3;
// Used only by the zoom in/out keys and Ctrl+wheel notches.
inline constexpr std::array<std::uint32_t, 16> WHOLE_ZOOM_STEPS{1,  2,  3,  4,  6,  8,   12,  16,
                                                                24, 32, 48, 64, 96, 128, 192, 256};

// The largest whole zoom in [1, 256] at which `image` shows whole in each of `pictures` (backing
// pixels): the smallest floor(min(picture.width / image.width, picture.height / image.height));
// 1 with no picture or an empty image.
[[nodiscard]] std::uint32_t wholeFitZoom(std::span<const dvv::platform::PixelSize> pictures,
                                         dvv::platform::PixelSize image) noexcept;
// The Zoom window's zoom-out floor over the panels' picture regions `pictures` (backing pixels),
// in [1, 256]. The cell floor is the smallest whole zoom at which each picture shows at most
// MIN_ZOOM_CELLS cells of the major grid level (`major_step` image pixels, GridConfig::levels[0]
// .step, shown or not) on each axis: ceil(largest side / (MIN_ZOOM_CELLS * major_step)); a step of
// 0 counts as 1. A non-empty `image` of at most MIN_ZOOM_CELLS x MIN_ZOOM_CELLS major cells may
// zoom out until it shows whole: its floor is min(cell floor, wholeFitZoom(pictures, image)).
[[nodiscard]] std::uint32_t minZoom(std::span<const dvv::platform::PixelSize> pictures,
                                    std::uint32_t major_step,
                                    dvv::platform::PixelSize image) noexcept;
// The nearest whole number in [minimum, 256] (minimum clamped to [1, 256]); NaN gives the minimum.
// For a pinch and a requested zoom; a minimum of 1 rounds a zoom that is ruled already.
[[nodiscard]] std::uint32_t wholeZoom(double zoom, std::uint32_t minimum) noexcept;
// The next stop of WHOLE_ZOOM_STEPS strictly past `zoom` (in: larger, out: smaller), held in
// [minimum, 256]: out from above the minimum lands on it at the latest, and gives nullopt from
// the minimum or below; in gives nullopt at 256.
[[nodiscard]] std::optional<std::uint32_t> nextWholeZoom(std::uint32_t zoom, bool in,
                                                         std::uint32_t minimum) noexcept;

// The image point at the centre of the Zoom view that follows `pin`: the middle of its pixel.
[[nodiscard]] dvv::media::PointD followCentre(dvv::media::ImagePixel pin) noexcept;

// Cell mode: the grid level whose cell fills the picture, and the cell's centre (clipped to the
// image).
struct CellView {
    std::size_t level = 0;
    dvv::media::PointD centre{};
    bool operator==(const CellView&) const = default;
};
// The coarsest level with step * zoom <= min(picture); nullopt means Follow.
[[nodiscard]] std::optional<CellView> cellView(dvv::media::ImagePixel pin, const GridConfig& grid,
                                               std::uint32_t zoom, dvv::platform::PixelSize picture,
                                               dvv::platform::PixelSize image) noexcept;

// The metrics of a drawn value, in backing pixels: the glyph advance, the line height and the
// padding around the text. The value geometry of the whole feature.
struct ValueMetrics {
    std::uint32_t advance = 0, lineHeight = 0, padX = 0, padY = 0;
    bool operator==(const ValueMetrics&) const = default;
};
// The smallest whole zoom, in [1, 256], at which the values of `layout` fit their cells.
[[nodiscard]] std::uint32_t numbersFitZoom(const ValueMetrics& metrics, InspectorLayout layout,
                                           const ClipGrid& clip) noexcept;
// The ClipGrid overload above forwards to this one with the clip's pixel format.
[[nodiscard]] std::uint32_t numbersFitZoom(const ValueMetrics& metrics, InspectorLayout layout,
                                           const dvv::media::PixelFormatDesc& desc) noexcept;
// The zoom at which a cell of the major grid level fills the picture: floor(min(picture) / step),
// in [1, 256].
[[nodiscard]] std::uint32_t fitCellZoom(const GridConfig& grid,
                                        dvv::platform::PixelSize picture) noexcept;

enum class InspectorEvent : std::uint8_t {
    FirstPin,
    PinMoved,
    ProbeMoved,
    ModeChanged,
    Centre,
    Other
};
// True for every event but Other: a hand pan is never undone by an unrelated recompute such as a
// new frame or a toggle. A new panel picture (the layout, a resize) re-frames the shown pixel all
// the same (MainFrame::updateInspector, P9).
[[nodiscard]] bool recentres(InspectorEvent event) noexcept;

}  // namespace rawreel
