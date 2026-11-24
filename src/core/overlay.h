#pragma once

// The grid and ruler settings, their step rules and styles, and the pt-free rules that decide
// whether a grid level or a ruler is drawn at a zoom, wx-free. Sizes in points (pt) are the
// settings' unit; every rule here takes them already in backing pixels or converts them in
// minimumSpacingBacking, the one conversion of a minimum spacing.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/media/view_lines.h"

namespace rawreel {

inline constexpr std::size_t GRID_LEVELS = dvv::media::LINE_LEVELS;
// The index of a level in GridConfig::levels, coarsest first.
enum class GridLevel : std::uint8_t { Major, Minor, SecondMinor };
// The steps a level may have, in image pixels.
inline constexpr std::array<std::uint32_t, 8> GRID_STEP_VALUES{2, 4, 8, 16, 32, 64, 128, 256};

// The range of a grid level's number, both ends included.
struct GridRange {
    std::uint32_t minimum = 0;
    std::uint32_t maximum = 0;
    bool operator==(const GridRange&) const = default;
};
// The ranges of a level's numbers: opacity in %, thickness and minimum spacing in pt.
inline constexpr GridRange GRID_OPACITY_RANGE{.minimum = 0, .maximum = 100};
inline constexpr GridRange GRID_THICKNESS_RANGE{.minimum = 1, .maximum = 4};
inline constexpr GridRange GRID_MINIMUM_SPACING_RANGE{.minimum = 1, .maximum = 100};

struct Rgb8 {
    std::uint8_t r = 255, g = 255, b = 255;
    bool operator==(const Rgb8&) const = default;
};

struct GridLevelConfig {
    bool show = false;
    std::uint32_t step = 0;     // one of GRID_STEP_VALUES
    std::uint32_t opacity = 0;  // %, in GRID_OPACITY_RANGE
    Rgb8 colour{};
    std::uint32_t thickness = 1;  // pt, in GRID_THICKNESS_RANGE
    bool adaptive = false, halo = false;
    // pt, in GRID_MINIMUM_SPACING_RANGE; the level is hidden below it
    std::uint32_t minimumSpacing = 1;
    bool operator==(const GridLevelConfig&) const = default;
};

// Defaults: grid off with the minor level shown, the Plain style, steps 32 / 8 / 2, thickness
// 2 / 1 / 1 pt and minimum spacings 2 / 4 / 3 pt.
struct GridConfig {
    std::array<GridLevelConfig, GRID_LEVELS> levels{{
        {.show = false, .step = 32, .opacity = 85, .thickness = 2, .minimumSpacing = 2},
        {.show = true, .step = 8, .opacity = 50, .thickness = 1, .minimumSpacing = 4},
        {.show = false, .step = 2, .opacity = 25, .thickness = 1, .minimumSpacing = 3},
    }};
    bool operator==(const GridConfig&) const = default;
};

struct RulerConfig {
    bool show = false;
    bool numbers = true;
    // The lit tick's and the cursor marker's colour.
    Rgb8 cursorColour{.r = 0xff, .g = 0x3b, .b = 0x30};
    bool operator==(const RulerConfig&) const = default;
};

// Every step is one of GRID_STEP_VALUES, major >= 2 x minor and minor >= 2 x second minor.
[[nodiscard]] bool stepsValid(const std::array<std::uint32_t, GRID_LEVELS>& steps) noexcept;
// The GRID_STEP_VALUES, ascending, that keep stepsValid at `level` with the other two levels as
// they are.
[[nodiscard]] std::vector<std::uint32_t> validSteps(
    GridLevel level, const std::array<std::uint32_t, GRID_LEVELS>& steps);
// The step rule as the user reads it. "\xE2\x89\xA5" is U+2265 (greater-than or equal to) and
// "\xC3\x97" is U+00D7 (multiplication sign) in UTF-8.
inline constexpr std::string_view GRID_STEP_RULE =
    "major \xE2\x89\xA5 2 \xC3\x97 minor, minor \xE2\x89\xA5 2 \xC3\x97 second minor";

// The styles set the grid's looks and the rulers' cursor colour together.
enum class OverlayStyle : std::uint8_t { Plain, Adaptive, Coloured, Halo, Custom };
// Sets the colours, opacities, adaptive and halo of all three levels and the cursor colour; show,
// step, thickness, minimum spacing and the ruler toggles are kept. Plain: #ffffff 85 / 50 / 25 %.
// Adaptive: #ffffff 85 / 55 / 30 %, adaptive on all levels. Coloured: #ff9f1a, #2bd2ff, #ffffff
// 90 / 70 / 30 %. Halo: #ffffff 95 / 75 / 30 %, halo on major and minor. The cursor colour:
// #ff2dd2 with Coloured, else #ff3b30. Custom: no change.
void applyOverlayStyle(GridConfig& grid, RulerConfig& rulers, OverlayStyle style) noexcept;
// The style whose values the grid and the cursor colour have; Custom when none matches.
[[nodiscard]] OverlayStyle overlayStyleOf(const GridConfig& grid,
                                          const RulerConfig& rulers) noexcept;
// "Plain", "Adaptive", "Coloured", "Halo", "Custom".
[[nodiscard]] std::string_view overlayStyleName(OverlayStyle style) noexcept;

// The pt-free rules every user shares (the GUI's conversion, the renderer and the notes), so a
// note never disagrees with what is drawn. All values in backing pixels.

// float(minimum_pt x backing_per_dip): the one conversion of a minimum spacing.
[[nodiscard]] float minimumSpacingBacking(std::uint32_t minimum_pt,
                                          double backing_per_dip) noexcept;
// A ruler label's width: the decimal digits of `size` x digit_advance.
[[nodiscard]] float labelWidth(std::uint32_t size, float digit_advance) noexcept;
// A label fits when it is at most two major spacings wide (labels alternate between two lines).
[[nodiscard]] bool labelFits(float label_width, float major_spacing) noexcept;
// What decides whether one ruler shows its ticks, in backing pixels.
struct RulerFit {
    std::uint32_t majorStepLog2 = 0;  // log2 of the major level's step
    float zoom = 0;
    float minimumSpacing = 0;  // the major level's minimumSpacingBacking
    // With numbers on, the widest label on the ruler's axis (labelWidth); nullopt with numbers off.
    std::optional<float> labelWidth;
    bool operator==(const RulerFit&) const = default;
};
// One ruler's ticks are shown when the major level is visible,
// dvv::media::levelVisible(majorStepLog2, zoom, minimumSpacing), and, with numbers, its label
// fits: labelFits(*labelWidth, float(1 << majorStepLog2) x zoom).
[[nodiscard]] bool rulerTicksShown(const RulerFit& fit) noexcept;

// "hidden at this zoom (2 px at 100 % = 1 pt, needs 3 pt)" when the level of `step` (a power of
// two) is not visible at `zoom` with minimumSpacingBacking(minimum_pt, backing_per_dip), else "".
// The spacing step x zoom in pt rounded down to one decimal, a trailing ".0" dropped; the zoom as
// zoomText(ViewMode::Zoom, zoom).
[[nodiscard]] std::string levelHiddenNote(std::uint32_t step, double zoom, double backing_per_dip,
                                          std::uint32_t minimum_pt);
// The note on a level's line: levelHiddenNote of the level with its step and minimum spacing;
// for the minor levels, when that is "" and the major level is hidden by its spacing (then no grid
// line is drawn), "hidden at this zoom (the major lines are hidden)".
[[nodiscard]] std::string gridLevelNote(const GridConfig& grid, GridLevel level, double zoom,
                                        double backing_per_dip);
// Why the rulers are off at `zoom`, by rulerTicksShown with the major level's step and minimum
// spacing: "" with the rulers hidden or both rulers' ticks shown; "Rulers off at this zoom: major
// ticks closer than N pt", N the major level's minimum spacing, when the major spacing is too
// small (the same on both axes); else, with numbers, "Rulers off at this zoom: the numbers do not
// fit" or "Top ruler off at this zoom: the numbers do not fit" or "Left ruler off at this zoom:
// the numbers do not fit". label_widths: {top ruler, left ruler}, labelWidth of the image width
// and height.
[[nodiscard]] std::string rulerOffNote(const GridConfig& grid, const RulerConfig& rulers,
                                       double zoom, double backing_per_dip,
                                       std::array<float, 2> label_widths);

}  // namespace rawreel
