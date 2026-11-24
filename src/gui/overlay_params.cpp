#include "gui/overlay_params.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rawreel {

namespace {

// pt x per_dip in backing pixels, rounded half up, at least 1.
std::uint32_t backingOf(double pt, double per_dip) {
    const double backing = std::floor(pt * per_dip + 0.5);
    return backing >= 1 ? static_cast<std::uint32_t>(backing) : 1U;
}

// The colour word of the shaders: r | g << 8 | b << 16 | a << 24, a = opacity in % to 0-255,
// rounded half up.
std::uint32_t packedColour(Rgb8 colour, std::uint32_t opacity) {
    const std::uint32_t alpha = (std::min(opacity, 100U) * 255U + 50U) / 100U;
    return std::uint32_t{colour.r} | std::uint32_t{colour.g} << 8U |
           std::uint32_t{colour.b} << 16U | alpha << 24U;
}

// The ruler ticks' lengths per level from the strip's inner edge, pt.
constexpr std::array<double, GRID_LEVELS> TICK_LENGTH_PT{9, 5, 3};
constexpr double TICK_WIDTH_PT = 0.75;
constexpr double MARKER_DEPTH_PT = 3;
// How far the lit tick reaches past the tick band, pt.
constexpr double LIT_REACH_PT = 4;
// An estimate of one digit's advance, times the font size.
constexpr double DIGIT_ADVANCE_EM = 0.6;

}  // namespace

OverlayConversion convertOverlay(const GridConfig& grid, const RulerConfig& rulers,
                                 const OverlayScale& scale, bool dark) {
    const double per_dip = scale.display.backingPerDip;
    OverlayParams params;
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        const GridLevelConfig& level = grid.levels[i];
        assert(std::has_single_bit(level.step));  // the precondition: stepsValid()
        params.levels[i] = {.stepLog2 = static_cast<std::uint32_t>(std::countr_zero(level.step)),
                            .minimumSpacing = minimumSpacingBacking(level.minimumSpacing, per_dip),
                            .thickness = backingOf(level.thickness, per_dip),
                            .colour = packedColour(level.colour, level.opacity),
                            .shown = level.show,
                            .adaptive = level.adaptive,
                            .halo = level.halo};
        params.tickLength[i] = backingOf(TICK_LENGTH_PT[i], per_dip);
    }
    params.numbers = rulers.numbers;
    params.cursorColour = (static_cast<std::uint32_t>(rulers.cursorColour.r) << 16U) |
                          (static_cast<std::uint32_t>(rulers.cursorColour.g) << 8U) |
                          static_cast<std::uint32_t>(rulers.cursorColour.b);
    const RulerStrip strip = rulerStrip(rulers, scale.fontDip, scale.display);
    params.strips = {.top = strip.backing, .left = strip.backing};
    params.tickBand = strip.tickBand;
    params.tickWidth = backingOf(TICK_WIDTH_PT, per_dip);
    params.markerDepth = backingOf(MARKER_DEPTH_PT, per_dip);
    // With numbers off the strip is the tick band rounded up to whole window units, so the lit
    // tick fills the strip.
    params.litDepth = std::min(strip.backing, strip.tickBand + backingOf(LIT_REACH_PT, per_dip));
    params.digitAdvance = static_cast<float>(DIGIT_ADVANCE_EM * scale.fontDip * per_dip);
    params.dark = dark;
    return {.params = params, .strip = strip};
}

}  // namespace rawreel
