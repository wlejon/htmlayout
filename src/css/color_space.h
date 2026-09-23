#pragma once
// Internal: CSS Color 4/5 colour spaces, conversion between them, and the
// color-mix() interpolation. Not part of the public API — color.h is.
#include <optional>
#include <string_view>

namespace htmlayout::css::colorspace {

enum class Space {
    SRGB,        // gamma-encoded sRGB, components 0..1
    SRGBLinear,
    DisplayP3,
    A98RGB,
    ProPhotoRGB, // D50
    Rec2020,
    XYZD50,
    XYZD65,
    Lab,         // CIE Lab (D50): L 0..100
    LCH,         // L 0..100, C, H degrees
    OKLab,       // L 0..1
    OKLCH,       // L 0..1, C, H degrees
    HSL,         // H degrees, S 0..1, L 0..1
    HWB,         // H degrees, W 0..1, B 0..1
};

// A colour in some space. Missing (`none`) components are flagged; their
// stored value is 0, which is what they resolve to when nothing fills them.
struct ColorVal {
    Space space = Space::SRGB;
    double c[3] = {0, 0, 0};
    bool missing[3] = {false, false, false};
    double alpha = 1.0;
    bool alphaMissing = false;
};

// Lower-case space name as used by color() and color-mix()'s `in <space>`.
std::optional<Space> spaceFromName(std::string_view name);
bool isPolar(Space s);
// Index of the hue component in a polar space (-1 otherwise).
int hueIndex(Space s);

// Convert to `target`, carrying missing components forward to analogous
// components (CSS Color 4 §12.2) and marking achromatic hues missing.
ColorVal convert(const ColorVal& in, Space target);

enum class HueMethod { Shorter, Longer, Increasing, Decreasing };

// color-mix(): both colours are already normalised weights (p1 + p2 == 1);
// the returned colour is in `space`, with `alphaMult` applied.
ColorVal mix(const ColorVal& a, const ColorVal& b, Space space, HueMethod hue,
             double p2, double alphaMult);

// Final step: gamma-encoded sRGB, components 0..1. A colour outside the sRGB
// gamut is brought into it with the CSS Color 4 §13.2 gamut-mapping algorithm
// (OKLCH chroma reduction by binary search, stopping within deltaEOK 0.02 of
// the clipped colour); L >= 1 maps to white and L <= 0 to black.
void toOutputSRGB(const ColorVal& in, double out[3], double& alpha);

} // namespace htmlayout::css::colorspace
