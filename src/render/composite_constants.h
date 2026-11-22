#pragma once

// The host side of composite.slang's push constants: the flag bits and the constants of one
// composite.

#include <cstdint>

#include "core/view.h"
#include "render/display_state.h"

namespace rawreel {

inline constexpr std::uint32_t COMPOSITE_DARK = 1;         // flags bit0: dark background
inline constexpr std::uint32_t COMPOSITE_SKIP_ENCODE = 2;  // flags bit1: the swapchain is _SRGB

struct CompositeConstants {  // = PushConstants of composite.slang
    std::uint32_t viewportW = 0, viewportH = 0, imageW = 0, imageH = 0;
    float zoom = 0.0F, originX = 0.0F, originY = 0.0F;
    std::uint32_t flags = 0;
    bool operator==(const CompositeConstants&) const = default;
};
static_assert(sizeof(CompositeConstants) == 32);

// The constants of one composite for the resolved `view` (design §4.4): viewportW/H =
// view.viewport(); zoom/originX/originY = float(view.zoom()/origin()); imageW/H = view.image() when
// view.zoom() > 0, else 0 (no frame or an empty size: background only); flags = COMPOSITE_DARK when
// params.dark, | COMPOSITE_SKIP_ENCODE when skip_encode.
[[nodiscard]] CompositeConstants compositeConstants(const CompositeParams& params,
                                                    const ViewTransform& view, bool skip_encode);

}  // namespace rawreel
