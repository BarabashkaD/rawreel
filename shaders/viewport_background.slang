// The viewport background, which both passes write wherever they draw neither picture nor ruler:
// P's border around the image and the reserved space beside the rulers are the same value.

module viewport_background;

// The theme's grey, sRGB-encoded (BACKGROUND_DARK and BACKGROUND_LIGHT of
// tests/composite_reference.h).
public float viewportBackground(bool dark)
{
    return dark ? 0.12 : 0.85;
}
