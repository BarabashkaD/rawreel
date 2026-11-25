// The viewport background, which both passes write wherever they draw neither picture nor ruler:
// P's border around the image and the reserved space beside the rulers are the same value.

module viewport_background;

// The theme's grey, sRGB-encoded (BACKGROUND_DARK and BACKGROUND_LIGHT of
// tests/composite_reference.h).
public float viewportBackground(bool dark)
{
    return dark ? 0.12 : 0.85;
}

// The alpha checkerboard's grey, sRGB-encoded: per theme the even squares (the one at the image
// origin) and the odd ones (CHECKER_* of tests/composite_reference.h).
public float checkerGrey(bool dark, bool odd)
{
    if (dark)
    {
        return odd ? 0.25 : 0.35;
    }
    return odd ? 0.70 : 1.0;
}
