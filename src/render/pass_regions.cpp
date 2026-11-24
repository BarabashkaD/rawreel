#include "render/pass_regions.h"

#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel {

PassRegions passRegions(dvv::platform::PixelSize extent, dvv::media::ViewStrips strips,
                        const dvv::media::ViewTransform& view) noexcept {
    PassRegions regions{.picture = dvv::media::pictureRegion(extent, strips),
                        .rulers = dvv::media::rulerRects(extent, strips, view)};
    if (strips.top == 0 && strips.left == 0) {
        regions.pictureDrawn = regions.picture;
        return regions;
    }
    // H lies in P, which lies in the output, so no subtraction underflows.
    const dvv::media::PixelRect& p = regions.picture;
    const dvv::media::PixelRect& h = regions.rulers.hugged;
    regions.pictureDrawn = {
        .x = h.x, .y = h.y, .width = p.x + p.width - h.x, .height = p.y + p.height - h.y};
    regions.bands[0] = {.x = 0, .y = 0, .width = extent.width, .height = h.y};
    regions.bands[1] = {.x = 0, .y = h.y, .width = h.x, .height = extent.height - h.y};
    // The corner ends where the top ruler begins, and both span the top strip's rows.
    regions.bandRulers[0] = {.x = h.x - p.x, .y = h.y - p.y, .width = p.x + h.width, .height = p.y};
    regions.bandRulers[1] = regions.rulers.left;
    return regions;
}

}  // namespace rawreel
