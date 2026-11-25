#pragma once

// The grid and ruler settings in backing pixels: the GUI's one conversion of their points (DIP),
// wx-free so the unit tests check it without wx.

#include "core/layout.h"
#include "core/overlay.h"
#include "render/display_state.h"

namespace rawreel {

struct OverlayScale {
    DisplayScale display;
    double fontDip = 0;  // the ruler label font's size, DIP
};

// The one conversion's result: the renderer's params and the ruler strip they hold, so the strip
// in window units (the GUI's layout) and in backing pixels (params.strips) come from one call.
struct OverlayConversion {
    OverlayParams params;
    RulerStrip strip;  // rulerStrip(rulers, fontDip, display); params.strips are its backing
};

// The settings in backing pixels for the renderer. A size in pt is pt x backingPerDip rounded half
// up, at least 1: the level thicknesses, tickWidth 0.75 pt, markerDepth 3 pt and the tick lengths
// 9 / 5 / 3 pt (major, minor, second minor). A level's minimumSpacing is minimumSpacingBacking();
// the strip is rulerStrip(rulers, fontDip, display), params.strips (top and left alike) its backing
// and params.tickBand its tickBand; params.litDepth is min(strip, tickBand + 4 pt), the
// whole strip with numbers off, where the strip is the tick band rounded up to whole window units
// (the 4 pt converted as every pt size above). A colour packs r | g << 8 | b << 16 | a << 24 with
// a = opacity x 255 / 100 rounded half up; `dark` is the theme. The label spacing is
// RULER_NUMBER_GAP_DIP, RULER_TAG_PAD_DIP, RULER_TAG_INSET_DIP and RULER_TAG_MARGIN_DIP each as a
// pt size above, at most 255; lastValue is the rulers'; cursorTagTextBlack is
// cursorTagTextBlack(the cursor colour); checkerSize, the alpha checkerboard's square, is 8 DIP
// as a pt size above. The glyph strip is the GUI's: params.glyphs stays null.
// Precondition: stepsValid() of the grid's steps (each one of GRID_STEP_VALUES, a power of two), as
// parseOverlayConfig() and the settings keep them; a level's stepLog2 is the log2 of its step.
[[nodiscard]] OverlayConversion convertOverlay(const GridConfig& grid, const RulerConfig& rulers,
                                               const OverlayScale& scale, bool dark);

}  // namespace rawreel
