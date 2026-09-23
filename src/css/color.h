#pragma once
#include <string>
#include <cstdint>

namespace htmlayout::css {

// Resolved RGBA color
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

// Parse a CSS color value. Supports:
// - Named colors (red, blue, transparent, currentcolor, etc.)
// - #hex (#RGB, #RRGGBB, #RGBA, #RRGGBBAA)
// - rgb()/rgba(), hsl()/hsla() in legacy comma and modern space syntax,
//   hwb(), lab(), lch(), oklab(), oklch(), including `none` components and
//   `/ alpha`
// - color(<space> c1 c2 c3 [/ a]) for srgb, srgb-linear, display-p3, xyz,
//   xyz-d50, xyz-d65
// - color-mix([in <space> [<hue-method> hue]], <color> [p%], <color> [p%])
// Colours outside sRGB are clipped per channel. calc() in components and the
// relative colour syntax (`from`) are not supported.
// Returns {0,0,0,0} for unrecognized values.
Color parseColor(const std::string& value);

// As above; `currentcolor` (including inside color-mix) resolves to
// `currentColor` instead of opaque black.
Color parseColor(const std::string& value, const Color& currentColor);

// Returns false for an unrecognized value (so a valid `transparent` is
// distinguishable from a parse failure).
bool tryParseColor(const std::string& value, Color& out,
                   const Color& currentColor = Color{0, 0, 0, 255});

} // namespace htmlayout::css
