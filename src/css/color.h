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
// - Named colors (red, blue, transparent, etc.)
// - #hex (#RGB, #RRGGBB, #RGBA, #RRGGBBAA)
// - rgb(r, g, b) / rgba(r, g, b, a)
// - hsl(h, s%, l%) / hsla(h, s%, l%, a)
// Returns {0,0,0,0} for unrecognized values.
Color parseColor(const std::string& value);

} // namespace htmlayout::css
