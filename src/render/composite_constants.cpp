#include "render/composite_constants.h"

namespace rawreel {

CompositeConstants compositeConstants(const CompositeParams& params, const ViewTransform& view,
                                      bool skip_encode) {
    const bool drawn = view.zoom() > 0.0;
    return CompositeConstants{
        .viewportW = view.viewport().width,
        .viewportH = view.viewport().height,
        .imageW = drawn ? view.image().width : 0,
        .imageH = drawn ? view.image().height : 0,
        .zoom = static_cast<float>(view.zoom()),
        .originX = static_cast<float>(view.origin().x),
        .originY = static_cast<float>(view.origin().y),
        .flags = (params.dark ? COMPOSITE_DARK : 0U) | (skip_encode ? COMPOSITE_SKIP_ENCODE : 0U)};
}

}  // namespace rawreel
