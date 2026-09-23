#pragma once
#include <string>
#include <cstdint>
#include <string_view>

namespace htmlayout::css {

// Resolved RGBA color
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

enum class ColorScheme { Light, Dark };

// What a colour value is resolved against: `currentcolor` (including inside
// color-mix(), light-dark() and a relative colour's origin) and the element's
// used colour scheme, which picks light-dark()'s branch.
struct ColorContext {
    Color currentColor{0, 0, 0, 255};
    ColorScheme scheme = ColorScheme::Light;
};

// Parse a CSS color value. Supports:
// - Named colors (red, blue, transparent, currentcolor, etc.)
// - #hex (#RGB, #RRGGBB, #RGBA, #RRGGBBAA)
// - rgb()/rgba(), hsl()/hsla() in legacy comma and modern space syntax,
//   hwb(), lab(), lch(), oklab(), oklch(), including `none` components and
//   `/ alpha`
// - color(<space> c1 c2 c3 [/ a]) for srgb, srgb-linear, display-p3,
//   a98-rgb, prophoto-rgb, rec2020, xyz, xyz-d50, xyz-d65
// - the relative colour syntax, `fn(from <color> ...)`, for every function
//   above, with the origin's channel keywords (r g b, h s l, h w b, l a b,
//   l c h, x y z, alpha) usable as components
// - calc(), min(), max(), clamp(), abs() and sign() in any component, with
//   channel keywords as operands
// - color-mix([in <space> [<hue-method> hue]], <color> [p%], <color> [p%])
// - light-dark(<color>, <color>), chosen by ColorContext::scheme
// Colours outside sRGB are gamut-mapped into it with the CSS Color 4 §13.2
// algorithm (OKLCH chroma reduction). System colours are not supported.
// Returns {0,0,0,0} for unrecognized values.
Color parseColor(const std::string& value);

// As above; `currentcolor` resolves to `currentColor` instead of opaque black.
Color parseColor(const std::string& value, const Color& currentColor);
Color parseColor(const std::string& value, const ColorContext& context);

// Returns false for an unrecognized value (so a valid `transparent` is
// distinguishable from a parse failure).
bool tryParseColor(const std::string& value, Color& out,
                   const Color& currentColor = Color{0, 0, 0, 255});
bool tryParseColor(const std::string& value, Color& out, const ColorContext& context);

// The used colour scheme of an element from its computed `color-scheme` value
// and the user's preference (MediaContext::colorScheme): `normal`, or a list
// naming neither `light` nor `dark`, is light; otherwise the preference when
// the list names it, else the first scheme listed.
ColorScheme usedColorScheme(std::string_view colorSchemeValue,
                            ColorScheme preferred = ColorScheme::Light);

} // namespace htmlayout::css
