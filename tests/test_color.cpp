// CSS Color 4/5 colour functions and color-mix().

#include "test_color.h"
#include "test_helpers.h"
#include "css/color.h"
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
    // Out of gamut: clipped per channel.
    {
        Color c;
        tryParseColor("lab(100 200 0)", c);
        check(c.r == 255 && c.a == 255, "lab out-of-gamut red clips to 255");
        tryParseColor("oklch(0.9 0.4 145)", c);
        check(c.g == 255 && c.a == 255, "oklch out-of-gamut green clips to 255");
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
    checkColor("color(display-p3 1 0 0)", 255, 0, 0);
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
    testColorIntegration();
}
