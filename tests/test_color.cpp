// CSS Color 4/5 colour functions and color-mix().

#include "test_color.h"
#include "test_helpers.h"
#include "css/color.h"
#include "css/color_space.h"
#include "css/parser.h"
#include "css/properties.h"
#include <cstdlib>
#include <string>

using namespace htmlayout::css;

namespace {

bool near(const Color& c, int r, int g, int b, int a = 255, int tol = 1) {
    return std::abs(c.r - r) <= tol && std::abs(c.g - g) <= tol &&
           std::abs(c.b - b) <= tol && std::abs(c.a - a) <= tol;
}

void checkColor(const char* css, int r, int g, int b, int a = 255, int tol = 1) {
    Color c;
    bool ok = tryParseColor(css, c);
    std::string name = std::string(css) + " = rgba(" + std::to_string(r) + "," +
                       std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a) + ")";
    if (ok && !near(c, r, g, b, a, tol)) {
        name += " [got " + std::to_string(c.r) + "," + std::to_string(c.g) + "," +
                std::to_string(c.b) + "," + std::to_string(c.a) + "]";
    }
    check(ok && near(c, r, g, b, a, tol), name.c_str());
}

void checkInvalid(const char* css) {
    Color c;
    check(!tryParseColor(css, c), (std::string("invalid: ") + css).c_str());
}

void checkSame(const char* a, const char* b, int tol = 1) {
    Color ca, cb;
    bool ok = tryParseColor(a, ca) && tryParseColor(b, cb);
    check(ok && near(ca, cb.r, cb.g, cb.b, cb.a, tol),
          (std::string(a) + " == " + b).c_str());
}

void testLegacyAndModernSyntax() {
    printf("--- Color: rgb()/hsl()/hwb() syntax ---\n");
    checkColor("rgb(255 0 0 / 50%)", 255, 0, 0, 128);
    checkColor("rgb(255 0 0 / 0.25)", 255, 0, 0, 64);
    checkColor("rgb(none 128 0)", 0, 128, 0);
    checkColor("rgb(255 0 0 / none)", 255, 0, 0, 0);
    checkColor("rgba(0, 0, 0, 2)", 0, 0, 0, 255);
    checkColor("rgb(300 -20 0)", 255, 0, 0);
    checkColor("rgb(1e2 0 0)", 100, 0, 0);
    checkColor("RGB(0 0 255)", 0, 0, 255);
    checkColor("rgba(10 20 30)", 10, 20, 30);
    checkColor("hsl(120deg 100% 50%)", 0, 255, 0);
    checkColor("hsl(0.5turn 100% 50%)", 0, 255, 255);
    checkColor("hsl(120 100 50)", 0, 255, 0);
    checkColor("hsl(none 0% 50% / 0.5)", 128, 128, 128, 128);
    checkColor("hsl(-120 100% 50%)", 0, 0, 255);
    checkColor("hsla(240, 100%, 50%, 50%)", 0, 0, 255, 128);
    checkColor("hwb(0 0% 0%)", 255, 0, 0);
    checkColor("hwb(0 50% 50%)", 128, 128, 128);
    checkColor("hwb(120 0% 50%)", 0, 128, 0);
    checkColor("hwb(0 80% 40%)", 170, 170, 170);
    checkInvalid("rgb(none, 0, 0)");
    checkInvalid("rgb(1, 2 3)");
    checkInvalid("rgb(1 2)");
    checkInvalid("rgb(1 2 3 4)");
    checkInvalid("rgb(1 2 3 /)");
    checkInvalid("hwb(0, 0%, 0%)");
    checkInvalid("hsl(10px 50% 50%)");
    checkInvalid("#ggg");
    checkInvalid("#12345");
    checkInvalid("rgb(1 2 3");
}

void testLabFamily() {
    printf("--- Color: lab()/lch()/oklab()/oklch() ---\n");
    checkColor("lab(100 0 0)", 255, 255, 255);
    checkColor("lab(0 0 0)", 0, 0, 0);
    checkColor("lab(54.29 80.82 69.9)", 255, 0, 0, 255, 2);
    checkColor("lch(54.29 106.84 40.85)", 255, 0, 0, 255, 2);
    checkColor("lch(54.29 106.84 40.85 / 50%)", 255, 0, 0, 128, 2);
    checkColor("oklab(1 0 0)", 255, 255, 255);
    checkColor("oklab(0.628 0.2249 0.1258)", 255, 0, 0, 255, 2);
    checkColor("oklch(0.628 0.2577 29.23)", 255, 0, 0, 255, 2);
    checkColor("oklch(62.8% 64.4% 29.23deg)", 255, 0, 0, 255, 2);
    checkColor("oklch(0.452 0.313 264.05)", 0, 0, 255, 255, 2);
    checkColor("oklch(0.866 0.295 142.5)", 0, 255, 0, 255, 2);
    checkSame("lab(50% 0 0)", "lab(50 0 0)");
    checkSame("lab(50 100% -100%)", "lab(50 125 -125)");
    checkSame("lch(50 100% 30)", "lch(50 150 30)");
    checkSame("oklab(50% 100% 0)", "oklab(0.5 0.4 0)");
    checkSame("oklch(0.7 0.1 none)", "oklch(0.7 0.1 0)");
    checkSame("oklch(none 0.1 30)", "oklch(0 0.1 30)");
    checkSame("lab(150 0 0)", "lab(100 0 0)");            // L clamps
    checkSame("oklch(0.5 -0.1 30)", "oklch(0.5 0 30)");   // negative chroma clamps
    checkSame("lch(50 30 1.5708rad)", "lch(50 30 90)");
    checkSame("LAB(50 10 10)", "lab(50 10 10)");
    // Out of gamut: gamut-mapped (see testGamutMapping).
    {
        Color c;
        tryParseColor("lab(100 200 0)", c);
        check(c.r == 255 && c.a == 255, "lab out-of-gamut red maps to 255");
        tryParseColor("oklch(0.9 0.4 145)", c);
        check(c.g == 255 && c.a == 255, "oklch out-of-gamut light green maps to 255");
    }
    checkInvalid("lab(50 0)");
    checkInvalid("lab(50, 0, 0)");
    checkInvalid("oklch(0.5 0.1 30 /)");
    checkInvalid("lch(50 30 10px)");
}

void testColorFunction() {
    printf("--- Color: color() ---\n");
    checkColor("color(srgb 1 0 0)", 255, 0, 0);
    checkColor("color(srgb 100% 50% 0% / 0.5)", 255, 128, 0, 128);
    checkColor("color(srgb-linear 0.5 0.5 0.5)", 188, 188, 188);
    // Outside sRGB: gamut-mapped (CSS Color 4 §13.2), not clipped to 255,0,0.
    checkColor("color(display-p3 1 0 0)", 255, 11, 12, 255, 2);
    checkColor("color(display-p3 0.5 0.5 0.5)", 128, 128, 128);
    checkColor("color(xyz 0.9505 1 1.089)", 255, 255, 255);
    checkColor("color(xyz-d50 0.9643 1 0.8251)", 255, 255, 255);
    checkInvalid("color(lab 50 0 0)");
    checkInvalid("color(nope 1 0 0)");
}

void testColorMix() {
    printf("--- Color: color-mix() ---\n");
    checkColor("color-mix(in srgb, red, blue)", 128, 0, 128);
    checkColor("color-mix(in srgb, red 25%, blue)", 64, 0, 191);
    checkColor("color-mix(in srgb, red, blue 75%)", 64, 0, 191);
    checkColor("color-mix(in srgb, 25% red, blue)", 64, 0, 191);
    checkInvalid("color-mix(in srgb, red 50%, blue 150%)");
    checkInvalid("color-mix(in srgb, red 0%, blue 0%)");
    checkInvalid("color-mix(in srgb, red)");
    checkInvalid("color-mix(in nope, red, blue)");
    checkInvalid("color-mix(in srgb longer hue, red, blue)");
    // Percentages summing under 100% scale alpha.
    checkColor("color-mix(in srgb, red 20%, blue 20%)", 128, 0, 128, 102);
    // Over 100% normalises without an alpha change.
    checkColor("color-mix(in srgb, red 100%, blue 100%)", 128, 0, 128);
    checkColor("color-mix(in srgb-linear, red, blue)", 188, 0, 188);
    // Premultiplied interpolation: transparent adds no colour.
    checkColor("color-mix(in srgb, red, transparent)", 255, 0, 0, 128);
    checkColor("color-mix(in oklab, red, transparent)", 255, 0, 0, 128);
    // Hue interpolation.
    checkColor("color-mix(in hsl, hsl(350 100% 50%), hsl(10 100% 50%))", 255, 0, 0);
    checkColor("color-mix(in hsl longer hue, hsl(350 100% 50%), hsl(10 100% 50%))", 0, 255, 255);
    checkColor("color-mix(in hsl increasing hue, hsl(350 100% 50%), hsl(10 100% 50%))", 255, 0, 0);
    checkColor("color-mix(in hsl decreasing hue, hsl(350 100% 50%), hsl(10 100% 50%))", 0, 255, 255);
    checkSame("color-mix(in oklch, oklch(0.6 0.1 350), oklch(0.6 0.1 10))", "oklch(0.6 0.1 0)");
    checkSame("color-mix(in lch, lch(60 40 350), lch(60 40 10))", "lch(60 40 0)");
    // Achromatic colours have a powerless (missing) hue in polar spaces.
    checkColor("color-mix(in hsl, white, red)", 223, 159, 159);
    checkSame("color-mix(in oklch, white, blue)", "color-mix(in oklch, oklch(1 0 none), blue)");
    checkSame("color-mix(in lch, black, blue)", "color-mix(in lch, lch(0 0 none), blue)");
    // `none` takes the other colour's component.
    checkSame("color-mix(in oklch, oklch(0.6 0.1 none), oklch(0.6 0.1 200))", "oklch(0.6 0.1 200)");
    checkSame("color-mix(in srgb, rgb(none 0 0), rgb(200 0 0))", "rgb(200 0 0)");
    // Missing components carry forward to analogous ones (hsl hue -> oklch hue).
    // Without the carry the hue would land near 85deg (orange); with it, green.
    {
        Color c;
        tryParseColor("color-mix(in oklch, hsl(none 100% 50%), hsl(120 100% 50%))", c);
        check(c.r < 100 && c.g > 150, "missing hsl hue carries forward into oklch");
    }
    // Same colour mixed with itself is itself, in every space.
    for (const char* sp : {"srgb", "srgb-linear", "display-p3", "lab", "lch", "oklab",
                           "oklch", "hsl", "hwb", "xyz", "xyz-d50", "xyz-d65"}) {
        std::string m = std::string("color-mix(in ") + sp + ", rebeccapurple, rebeccapurple 30%)";
        checkSame(m.c_str(), "rebeccapurple");
    }
    checkSame("color-mix(red, blue)", "color-mix(in oklab, red, blue)");
    checkSame("color-mix(in oklab, red, blue)", "oklab(0.54 0.0962 -0.0929)", 2);
    checkColor("color-mix(in srgb, color-mix(in srgb, red, blue), white)", 191, 128, 191);
    checkColor("color-mix(in srgb, #f00 40%, rgb(0 0 255 / 50%))", 146, 0, 109, 179);
    {
        Color c = parseColor("color-mix(in srgb, currentcolor, white)", Color{0, 0, 255, 255});
        check(near(c, 128, 128, 255), "color-mix with currentcolor uses the context colour");
        c = parseColor("currentcolor", Color{10, 20, 30, 255});
        check(near(c, 10, 20, 30), "currentcolor resolves to the context colour");
    }
}

void testColorCalc() {
    printf("--- Color: calc() in components ---\n");
    checkColor("rgb(calc(100 + 55) 0 0)", 155, 0, 0);
    checkColor("rgb(calc(255 / 2) 0 0)", 128, 0, 0);
    checkColor("rgb(calc(50%) 0 calc(25% * 2))", 128, 0, 128);
    checkColor("rgb(calc(255), 0, 0)", 255, 0, 0);          // legacy syntax too
    checkColor("rgb(min(255, 300) max(0, -5) clamp(0, 128, 255))", 255, 0, 128);
    checkColor("rgb(0 0 0 / calc(0.5))", 0, 0, 0, 128);
    checkColor("rgb(0 0 0 / calc(25% + 25%))", 0, 0, 0, 128);
    checkColor("rgb(calc(-10 * -25.5) 0 0)", 255, 0, 0);
    checkColor("rgb(calc((100 + 27.5) * 2) 0 0)", 255, 0, 0);
    checkColor("rgb(abs(-128) calc(sign(-3) * -255) 0)", 128, 255, 0);
    checkColor("hsl(calc(60deg * 2) 100% 50%)", 0, 255, 0);
    checkColor("hsl(calc(0.25turn + 30deg) calc(50% + 50%) 50%)", 0, 255, 0);
    checkColor("hsl(calc(120) 100% 50%)", 0, 255, 0);
    checkSame("oklch(calc(0.5 + 0.128) calc(0.2577) calc(29.23deg))", "oklch(0.628 0.2577 29.23)");
    checkSame("lab(calc(50%) calc(-10 * 2) 0)", "lab(50 -20 0)");
    checkSame("color(srgb calc(1 / 2) 0 calc(pi / pi))", "color(srgb 0.5 0 1)");
    checkColor("color-mix(in srgb, red calc(20% + 5%), blue)", 64, 0, 191);
    checkColor("RGB(CALC(255) 0 0)", 255, 0, 0);
    checkInvalid("rgb(calc(50% + 10) 0 0)");   // mixed types
    checkInvalid("rgb(calc(10px) 0 0)");        // a length is no colour component
    checkInvalid("rgb(calc(1 -1) 0 0)");        // '-' needs whitespace
    checkInvalid("rgb(calc(1 + ) 0 0)");
    checkInvalid("rgb(calc() 0 0)");
    checkInvalid("rgb(calc(5% * 5%) 0 0)");
    checkInvalid("rgb(calc(5 / 5%) 0 0)");
    checkInvalid("hsl(calc(10%) 50% 50%)");    // a hue is no percentage
    checkInvalid("rgb(r g b)");                 // channel keywords need `from`
    checkInvalid("rgb(foo(1) 0 0)");
    checkInvalid("color-mix(in srgb, red calc(20), blue)");
}

void testRelativeColor() {
    printf("--- Color: relative colour syntax ---\n");
    checkColor("rgb(from red r g b)", 255, 0, 0);
    checkColor("rgb(from #0000ff b g r)", 255, 0, 0);
    checkColor("rgb(from red calc(r / 2) g b)", 128, 0, 0);
    checkColor("rgb(from red r g b / 50%)", 255, 0, 0, 128);
    checkColor("rgb(from red r g b / calc(alpha / 2))", 255, 0, 0, 128);
    checkColor("rgb(from rgb(10 20 30 / 0.5) r g b)", 10, 20, 30, 128);  // origin alpha
    checkColor("rgba(from red 0 g 255)", 0, 0, 255);
    checkColor("rgb(from red none g b)", 0, 0, 0);
    checkColor("rgb(from hsl(120 100% 50%) r g b)", 0, 255, 0);
    checkColor("hsl(from red calc(h + 120) s l)", 0, 255, 0);
    checkColor("hsl(from red h s calc(l / 2))", 128, 0, 0);
    checkColor("hwb(from red h w b)", 255, 0, 0);
    checkColor("hwb(from red h 50 b)", 255, 128, 128);
    checkSame("lab(from red l a b)", "red");
    checkSame("lch(from red l c h)", "red");
    checkSame("oklab(from red l a b)", "red");
    checkSame("oklch(from red l c h)", "red");
    checkSame("oklch(from blue l 0 h)", "oklch(0.452 0 0)");
    checkSame("oklch(from oklch(0.6 0.1 30) l c calc(h + 180))", "oklch(0.6 0.1 210)");
    checkSame("lch(from lch(50 30 90) calc(l + 10) c h)", "lch(60 30 90)");
    checkColor("color(from red srgb r g b)", 255, 0, 0);
    checkColor("color(from red srgb calc(r * 0.5) g b)", 128, 0, 0);
    checkSame("color(from red display-p3 r g b)", "red");
    checkSame("color(from red xyz x y z)", "red");
    checkSame("color(from rebeccapurple rec2020 r g b)", "rebeccapurple");
    checkSame("color(from color(srgb 0.2 0.4 0.6) srgb-linear r g b)", "color(srgb 0.2 0.4 0.6)");
    // The origin may itself be relative, a color-mix(), or currentcolor.
    checkColor("rgb(from rgb(from red g r b) r g b)", 0, 255, 0);
    checkColor("rgb(from color-mix(in srgb, red, blue) r g b)", 128, 0, 128);
    {
        Color c = parseColor("rgb(from currentcolor b g r)", Color{0, 0, 200, 255});
        check(near(c, 200, 0, 0), "relative colour from currentcolor");
    }
    checkInvalid("rgb(from red r g)");
    checkInvalid("rgb(from red, r, g, b)");       // no legacy syntax
    checkInvalid("rgb(from nope r g b)");
    checkInvalid("rgb(from red x g b)");           // not an rgb() channel
    checkInvalid("hsl(from red h s b)");
    checkInvalid("lab(from red l c h)");
    checkInvalid("color(from red r g b)");         // color() needs its space
    checkInvalid("color(from red lab l a b)");
    checkInvalid("rgb(from)");
    checkInvalid("rgb(from red r g b / alpha / alpha)");
}

void testLightDark() {
    printf("--- Color: light-dark() ---\n");
    checkColor("light-dark(red, blue)", 255, 0, 0);
    ColorContext dark{Color{0, 0, 0, 255}, ColorScheme::Dark};
    Color c;
    check(tryParseColor("light-dark(red, blue)", c, dark) && near(c, 0, 0, 255),
          "light-dark() under a dark scheme picks the second colour");
    check(tryParseColor("color-mix(in srgb, light-dark(red, blue), white)", c, dark) &&
              near(c, 128, 128, 255),
          "light-dark() inside color-mix()");
    check(tryParseColor("rgb(from light-dark(red, blue) r g b / 50%)", c, dark) &&
              near(c, 0, 0, 255, 128),
          "light-dark() as a relative colour's origin");
    c = parseColor("light-dark(currentcolor, white)", ColorContext{Color{1, 2, 3, 255}});
    check(near(c, 1, 2, 3), "light-dark() resolves currentcolor");
    checkInvalid("light-dark(red)");
    checkInvalid("light-dark(red, blue, green)");
    checkInvalid("light-dark(red, nope)");
    checkInvalid("light-dark(red blue)");

    check(usedColorScheme("normal") == ColorScheme::Light, "color-scheme normal is light");
    check(usedColorScheme("normal", ColorScheme::Dark) == ColorScheme::Light,
          "color-scheme normal stays light when dark is preferred");
    check(usedColorScheme("dark") == ColorScheme::Dark, "color-scheme dark");
    check(usedColorScheme("light dark", ColorScheme::Dark) == ColorScheme::Dark,
          "color-scheme light dark follows a dark preference");
    check(usedColorScheme("light dark", ColorScheme::Light) == ColorScheme::Light,
          "color-scheme light dark follows a light preference");
    check(usedColorScheme("dark light", ColorScheme::Light) == ColorScheme::Light,
          "a listed preference wins over list order");
    check(usedColorScheme("only dark") == ColorScheme::Dark, "color-scheme only dark");
    check(usedColorScheme("fancy") == ColorScheme::Light, "unknown schemes are ignored");
    check(usedColorScheme("fancy DARK") == ColorScheme::Dark, "scheme keywords are case-insensitive");

    auto s = parse("@supports (color: light-dark(red, blue)) { .x { color: red; } }");
    check(s.rules.size() == 1, "@supports accepts light-dark()");
    s = parse("@supports (color: rgb(from red r g calc(b + 1))) { .x { color: red; } }");
    check(s.rules.size() == 1, "@supports accepts relative colour syntax");
}

void testWideGamutSpaces() {
    printf("--- Color: a98-rgb / prophoto-rgb / rec2020 ---\n");
    checkColor("color(a98-rgb 1 1 1)", 255, 255, 255);
    checkColor("color(prophoto-rgb 1 1 1)", 255, 255, 255);
    checkColor("color(rec2020 1 1 1)", 255, 255, 255);
    checkColor("color(a98-rgb 0 0 0)", 0, 0, 0);
    checkColor("color(a98-rgb 0.5 0.5 0.5)", 128, 128, 128);
    checkColor("color(prophoto-rgb 0.5 0.5 0.5)", 146, 146, 146);
    checkColor("color(rec2020 0.5 0.5 0.5)", 139, 139, 139);
    checkColor("color(rec2020 50% 50% 50% / 0.5)", 139, 139, 139, 128);
    // In-gamut colours round-trip through each space.
    for (const char* sp : {"a98-rgb", "prophoto-rgb", "rec2020"}) {
        std::string m = std::string("color-mix(in ") + sp + ", rebeccapurple, rebeccapurple 30%)";
        checkSame(m.c_str(), "rebeccapurple");
        std::string r = std::string("color(from #3a7 ") + sp + " r g b)";
        checkSame(r.c_str(), "#3a7");
    }
}

void testGamutMapping() {
    printf("--- Color: gamut mapping ---\n");
    // In-gamut colours are untouched.
    checkColor("oklch(0.628 0.2577 29.23)", 255, 0, 0, 255, 2);
    checkColor("color(display-p3 0.5 0.5 0.5)", 128, 128, 128);
    // Lightness at or past the ends of the range is white / black, whatever
    // the chroma.
    checkColor("oklch(1 0.4 30)", 255, 255, 255);
    checkColor("oklch(0 0.4 30)", 0, 0, 0);
    checkColor("lab(100 200 0)", 255, 255, 255);
    // Out-of-gamut colours reduce chroma along constant OKLCH lightness and hue,
    // so they keep their lightness where per-channel clipping would not: a
    // clipped oklch(0.7 0.4 145) is rgb(0 255 0), far lighter (L 0.87).
    {
        Color mapped;
        check(tryParseColor("oklch(0.7 0.4 145)", mapped), "gamut-mapped colour parses");
        using namespace htmlayout::css::colorspace;
        ColorVal v;
        v.c[0] = mapped.r / 255.0;
        v.c[1] = mapped.g / 255.0;
        v.c[2] = mapped.b / 255.0;
        ColorVal lch = convert(v, Space::OKLCH);
        check(std::abs(lch.c[0] - 0.7) < 0.01, "gamut mapping keeps OKLCH lightness");
        check(std::abs(lch.c[2] - 145) < 5, "gamut mapping keeps OKLCH hue (within the JND)");
        check(lch.c[1] < 0.4, "gamut mapping reduces chroma");
        check(mapped.g > mapped.r && mapped.g > mapped.b && mapped.g < 255,
              "oklch(0.7 0.4 145) maps to a green darker than the clipped rgb(0 255 0)");
    }
    {
        Color c;
        // rec2020 green's OKLCH hue (~150) is bluer than sRGB green's (~142).
        check(tryParseColor("color(rec2020 0 1 0)", c) && near(c, 0, 242, 114, 255, 3),
              "rec2020 green maps to an sRGB green at its own hue");
        check(tryParseColor("color(display-p3 0 1 0)", c) && near(c, 0, 251, 41, 255, 3),
              "display-p3 green maps into sRGB");
        check(tryParseColor("color(srgb 1.5 0.5 0.5)", c) && c.r == 255,
              "color(srgb) past 1 is gamut-mapped");
    }
    // rgb()/hsl() clamp at parse time, so they never reach gamut mapping.
    checkColor("rgb(300 -20 0)", 255, 0, 0);
    checkColor("hsl(0 150% 50%)", 255, 0, 0);
}

void testColorIntegration() {
    printf("--- Color: parser integration ---\n");
    auto s = parse("@supports (color: oklch(0.5 0.1 30)) { .x { color: red; } }");
    check(s.rules.size() == 1, "@supports accepts oklch()");
    s = parse("@supports (color: rgb(0 0 0 / 0)) { .x { color: red; } }");
    check(s.rules.size() == 1, "@supports accepts a transparent rgb()");
    s = parse("@supports (color: lab(foo)) { .x { color: red; } }");
    check(s.rules.empty(), "@supports rejects a malformed lab()");
    auto r = expandShorthand("border", "1px solid oklch(0.5 0.1 30 / 50%)");
    bool found = false;
    for (auto& d : r)
        if (d.property == "border-top-color") found = d.value == "oklch(0.5 0.1 30 / 50%)";
    check(found, "border shorthand keeps an oklch() colour whole");
}

} // namespace

void testColor() {
    testLegacyAndModernSyntax();
    testLabFamily();
    testColorFunction();
    testColorMix();
    testColorCalc();
    testRelativeColor();
    testLightDark();
    testWideGamutSpaces();
    testGamutMapping();
    testColorIntegration();
}
