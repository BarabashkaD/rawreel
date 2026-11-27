#pragma once

// Which output pixels each pass of the view renderer draws: the picture pass and the two
// bands of the ruler pass split the output, so each pass reruns alone. A change that moves only
// the cursor (its marker, the lit tick) redraws only the ruler pixels of each band, as nothing
// else in a band depends on the cursor.

#include <array>

#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel {

struct PassRegions {                       // output pixels
    dvv::media::PixelRect picture{};       // P = pictureRegion(extent, strips): the view's viewport
    dvv::media::PixelRect pictureDrawn{};  // the part of P the picture pass dispatches
    std::array<dvv::media::PixelRect, 2> bands{};  // the ruler pass: [0] the top band, [1] the left
    dvv::media::RulerRects rulers{};
    // The part of each band a cursor-only change redraws: [0] rulers.top with the corner left of
    // it, [1] rulers.left; the rest of a band is background, which changes only with the rects.
    std::array<dvv::media::PixelRect, 2> bandRulers{};
    bool operator==(const PassRegions&) const = default;
};

// rulers = rulerRects(extent, strips, view) and H = rulers.hugged. With a strip not 0:
// pictureDrawn {H.x, H.y, P.x + P.width - H.x, P.y + P.height - H.y} (the part of P from H's
// top-left), bands[0] {0, 0, extent.width, H.y} and bands[1] {0, H.y, H.x, extent.height - H.y}:
// every output pixel lies in exactly one of the three. bandRulers[0] {H.x - P.x, H.y - P.y,
// P.x + H.width, P.y} (rulers.top and rulers.corner together) and bandRulers[1] = rulers.left.
// Zero strips: pictureDrawn = picture, the bands and bandRulers empty. `view` is resolved against
// P's size.
[[nodiscard]] PassRegions passRegions(dvv::platform::PixelSize extent,
                                      dvv::media::ViewStrips strips,
                                      const dvv::media::ViewTransform& view) noexcept;

}  // namespace rawreel
