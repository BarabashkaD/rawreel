#include "core/overlay.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "core/view.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"

namespace rawreel {

namespace {

constexpr std::size_t MAJOR = static_cast<std::size_t>(GridLevel::Major);
constexpr std::size_t MINOR = static_cast<std::size_t>(GridLevel::Minor);
constexpr std::size_t SECOND_MINOR = static_cast<std::size_t>(GridLevel::SecondMinor);

// The values a style sets on one level.
struct StyleLevel {
    Rgb8 colour{};
    std::uint32_t opacity = 0;
    bool adaptive = false, halo = false;
};
// The values a style sets: per level, and the rulers' cursor colour.
struct Style {
    std::array<StyleLevel, GRID_LEVELS> levels;
    Rgb8 cursorColour;
};

constexpr Rgb8 WHITE{255, 255, 255};
constexpr Rgb8 RED{0xff, 0x3b, 0x30};
constexpr Rgb8 MAGENTA{0xff, 0x2d, 0xd2};
// Indexed by OverlayStyle; Custom has no entry.
constexpr std::array<Style, static_cast<std::size_t>(OverlayStyle::Custom)> STYLES{{
    // Plain
    {.levels = {{{WHITE, 85, false, false}, {WHITE, 50, false, false}, {WHITE, 25, false, false}}},
     .cursorColour = RED},
    // Adaptive
    {.levels = {{{WHITE, 85, true, false}, {WHITE, 55, true, false}, {WHITE, 30, true, false}}},
     .cursorColour = RED},
    // Coloured: a magenta cursor, since the major lines are orange
    {.levels = {{{Rgb8{255, 159, 26}, 90, false, false},
                 {Rgb8{43, 210, 255}, 70, false, false},
                 {WHITE, 30, false, false}}},
     .cursorColour = MAGENTA},
    // Halo
    {.levels = {{{WHITE, 95, false, true}, {WHITE, 75, false, true}, {WHITE, 30, false, false}}},
     .cursorColour = RED},
}};

bool matches(const GridConfig& grid, const RulerConfig& rulers, const Style& style) noexcept {
    if (rulers.cursorColour != style.cursorColour) {
        return false;
    }
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        const GridLevelConfig& level = grid.levels[i];
        const StyleLevel& want = style.levels[i];
        if (level.colour != want.colour || level.opacity != want.opacity ||
            level.adaptive != want.adaptive || level.halo != want.halo) {
            return false;
        }
    }
    return true;
}

// One decimal, a trailing ".0" dropped.
std::string oneDecimal(double value) {
    std::string text = std::format("{:.1f}", value);
    if (text.ends_with(".0")) {
        text.resize(text.size() - 2);
    }
    return text;
}

// The log2 of a step, which is a power of two.
std::uint32_t stepLog2(std::uint32_t step) noexcept {
    assert(std::has_single_bit(step));
    return static_cast<std::uint32_t>(std::countr_zero(step));
}

}  // namespace

bool stepsValid(const std::array<std::uint32_t, GRID_LEVELS>& steps) noexcept {
    const bool listed = std::ranges::all_of(
        steps, [](std::uint32_t step) { return std::ranges::contains(GRID_STEP_VALUES, step); });
    return listed && steps[MAJOR] >= 2 * steps[MINOR] && steps[MINOR] >= 2 * steps[SECOND_MINOR];
}

std::vector<std::uint32_t> validSteps(GridLevel level,
                                      const std::array<std::uint32_t, GRID_LEVELS>& steps) {
    std::vector<std::uint32_t> valid;
    for (const std::uint32_t value : GRID_STEP_VALUES) {
        auto candidate = steps;
        candidate[static_cast<std::size_t>(level)] = value;
        if (stepsValid(candidate)) {
            valid.push_back(value);
        }
    }
    return valid;
}

void applyOverlayStyle(GridConfig& grid, RulerConfig& rulers, OverlayStyle style) noexcept {
    if (style == OverlayStyle::Custom) {
        return;
    }
    const Style& values = STYLES[static_cast<std::size_t>(style)];
    for (std::size_t i = 0; i < GRID_LEVELS; ++i) {
        GridLevelConfig& level = grid.levels[i];
        level.colour = values.levels[i].colour;
        level.opacity = values.levels[i].opacity;
        level.adaptive = values.levels[i].adaptive;
        level.halo = values.levels[i].halo;
    }
    rulers.cursorColour = values.cursorColour;
}

OverlayStyle overlayStyleOf(const GridConfig& grid, const RulerConfig& rulers) noexcept {
    for (std::size_t i = 0; i < STYLES.size(); ++i) {
        if (matches(grid, rulers, STYLES[i])) {
            return static_cast<OverlayStyle>(i);
        }
    }
    return OverlayStyle::Custom;
}

std::string_view overlayStyleName(OverlayStyle style) noexcept {
    switch (style) {
        case OverlayStyle::Plain: return "Plain";
        case OverlayStyle::Adaptive: return "Adaptive";
        case OverlayStyle::Coloured: return "Coloured";
        case OverlayStyle::Halo: return "Halo";
        case OverlayStyle::Custom: break;
    }
    return "Custom";
}

float minimumSpacingBacking(std::uint32_t minimum_pt, double backing_per_dip) noexcept {
    return static_cast<float>(static_cast<double>(minimum_pt) * backing_per_dip);
}

std::string levelHiddenNote(std::uint32_t step, double zoom, double backing_per_dip,
                            std::uint32_t minimum_pt) {
    if (dvv::media::levelVisible(stepLog2(step), static_cast<float>(zoom),
                                 minimumSpacingBacking(minimum_pt, backing_per_dip))) {
        return {};
    }
    // Rounded down, so a hidden level never reads as reaching its minimum.
    const double spacing_pt =
        std::floor(static_cast<double>(step) * zoom / backing_per_dip * 10.0) / 10.0;
    return std::format("hidden at this zoom ({} px at {} = {} pt, needs {} pt)", step,
                       zoomText(dvv::media::ViewMode::Zoom, zoom), oneDecimal(spacing_pt),
                       minimum_pt);
}

std::string gridLevelNote(const GridConfig& grid, GridLevel level, double zoom,
                          double backing_per_dip) {
    const GridLevelConfig& shown = grid.levels[static_cast<std::size_t>(level)];
    std::string note = levelHiddenNote(shown.step, zoom, backing_per_dip, shown.minimumSpacing);
    if (!note.empty() || level == GridLevel::Major) {
        return note;
    }
    const GridLevelConfig& major = grid.levels[MAJOR];
    if (!levelHiddenNote(major.step, zoom, backing_per_dip, major.minimumSpacing).empty()) {
        return "hidden at this zoom (the major lines are hidden)";
    }
    return {};
}

std::string rulerOffNote(const GridConfig& grid, const RulerConfig& rulers, double zoom,
                         double backing_per_dip) {
    if (!rulers.show) {
        return {};
    }
    const GridLevelConfig& major = grid.levels[MAJOR];
    if (dvv::media::levelVisible(stepLog2(major.step), static_cast<float>(zoom),
                                 minimumSpacingBacking(major.minimumSpacing, backing_per_dip))) {
        return {};
    }
    return std::format("Rulers off at this zoom: major ticks closer than {} pt",
                       major.minimumSpacing);
}

}  // namespace rawreel
