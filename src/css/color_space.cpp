#include "css/color_space.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace htmlayout::css::colorspace {

namespace {

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

constexpr double kPi = 3.14159265358979323846;

Vec3 mul(const Mat3& m, const Vec3& v) {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

Mat3 inverse(const Mat3& m) {
    double a = m[0][0], b = m[0][1], c = m[0][2];
    double d = m[1][0], e = m[1][1], f = m[1][2];
    double g = m[2][0], h = m[2][1], i = m[2][2];
    double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    double det = a * A + b * B + c * C;
    double s = 1.0 / det;
    return {Vec3{A * s, -(b * i - c * h) * s, (b * f - c * e) * s},
            Vec3{B * s, (a * i - c * g) * s, -(a * f - c * d) * s},
            Vec3{C * s, -(a * h - b * g) * s, (a * e - b * d) * s}};
}

// Matrices from the CSS Color 4 sample code (§18). Inverses are computed
// rather than transcribed so each round trip is exact to double precision.
const Mat3 kLinSRGBToXYZ = {Vec3{506752.0 / 1228815, 87881.0 / 245763, 12673.0 / 70218},
                            Vec3{87098.0 / 409605, 175762.0 / 245763, 12673.0 / 175545},
                            Vec3{7918.0 / 409605, 87881.0 / 737289, 1001167.0 / 1053270}};
const Mat3 kLinP3ToXYZ = {Vec3{608311.0 / 1250200, 189793.0 / 714400, 198249.0 / 1000160},
                          Vec3{35783.0 / 156275, 247089.0 / 357200, 198249.0 / 2500400},
                          Vec3{0.0, 32229.0 / 714400, 5220557.0 / 5000800}};
const Mat3 kD65ToD50 = {Vec3{1.0479297925449969, 0.022946870601609652, -0.05019226628920524},
                        Vec3{0.02962780877005599, 0.9904344267538799, -0.017073799063418826},
                        Vec3{-0.009243040646204504, 0.015055191490298152, 0.7518742814281371}};
const Mat3 kXYZToLMS = {Vec3{0.8190224379967030, 0.3619062600528904, -0.1288737815209879},
                        Vec3{0.0329836539323885, 0.9292868615863434, 0.0361446663506424},
                        Vec3{0.0481771893596242, 0.2642395317527308, 0.6335478284694309}};
const Mat3 kLMSToOKLab = {Vec3{0.2104542683093140, 0.7936177747023054, -0.0040720430116193},
                          Vec3{1.9779985324311684, -2.4285922420485799, 0.4505937096174110},
                          Vec3{0.0259040424655478, 0.7827717124575296, -0.8086757549230774}};

const Mat3& xyzToLinSRGB() { static const Mat3 m = inverse(kLinSRGBToXYZ); return m; }
const Mat3& xyzToLinP3() { static const Mat3 m = inverse(kLinP3ToXYZ); return m; }
const Mat3& d50ToD65() { static const Mat3 m = inverse(kD65ToD50); return m; }
const Mat3& lmsToXYZ() { static const Mat3 m = inverse(kXYZToLMS); return m; }
const Mat3& oklabToLMS() { static const Mat3 m = inverse(kLMSToOKLab); return m; }

const Vec3 kD50White = {0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};

// sRGB transfer function, extended sign-symmetrically past [0, 1].
double linearize(double c) {
    double a = std::fabs(c);
    if (a <= 0.04045) return c / 12.92;
    return std::copysign(std::pow((a + 0.055) / 1.055, 2.4), c);
}
double gammaEncode(double c) {
    double a = std::fabs(c);
    if (a > 0.0031308) return std::copysign(1.055 * std::pow(a, 1.0 / 2.4) - 0.055, c);
    return 12.92 * c;
}

double normHue(double h) {
    if (!std::isfinite(h)) return 0;
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    return h;
}

Vec3 hslToSRGB(double h, double s, double l) {
    h = normHue(h);
    auto f = [&](double n) {
        double k = std::fmod(n + h / 30.0, 12.0);
        double a = s * std::min(l, 1.0 - l);
        return l - a * std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0}));
    };
    return {f(0), f(8), f(4)};
}

// Returns hue NaN when achromatic.
Vec3 srgbToHSL(const Vec3& rgb) {
    double r = rgb[0], g = rgb[1], b = rgb[2];
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double l = (mx + mn) / 2, d = mx - mn;
    double h = NAN, s = 0;
    if (d != 0) {
        s = (l == 0 || l == 1) ? 0 : (mx - l) / std::min(l, 1 - l);
        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h *= 60;
        if (s < 0) { h += 180; s = std::fabs(s); }
        h = normHue(h);
    }
    return {h, s, l};
}

Vec3 hwbToSRGB(double h, double w, double bl) {
    if (w + bl >= 1) {
        double gray = w / (w + bl);
        return {gray, gray, gray};
    }
    Vec3 rgb = hslToSRGB(h, 1, 0.5);
    for (double& c : rgb) c = c * (1 - w - bl) + w;
    return rgb;
}

Vec3 labToXYZD50(const Vec3& lab) {
    constexpr double kappa = 24389.0 / 27, eps = 216.0 / 24389;
    double f1 = (lab[0] + 16) / 116, f0 = lab[1] / 500 + f1, f2 = f1 - lab[2] / 200;
    double x = f0 * f0 * f0 > eps ? f0 * f0 * f0 : (116 * f0 - 16) / kappa;
    double y = lab[0] > kappa * eps ? f1 * f1 * f1 : lab[0] / kappa;
    double z = f2 * f2 * f2 > eps ? f2 * f2 * f2 : (116 * f2 - 16) / kappa;
    return {x * kD50White[0], y * kD50White[1], z * kD50White[2]};
}
Vec3 xyzD50ToLab(const Vec3& xyz) {
    constexpr double kappa = 24389.0 / 27, eps = 216.0 / 24389;
    double f[3];
    for (int i = 0; i < 3; i++) {
        double v = xyz[i] / kD50White[i];
        f[i] = v > eps ? std::cbrt(v) : (kappa * v + 16) / 116;
    }
    return {116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])};
}

Vec3 polarToRect(const Vec3& lch) {
    double hr = normHue(lch[2]) * kPi / 180;
    return {lch[0], lch[1] * std::cos(hr), lch[1] * std::sin(hr)};
}
Vec3 rectToPolar(const Vec3& lab) {
    double c = std::sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
    double h = normHue(std::atan2(lab[2], lab[1]) * 180 / kPi);
    return {lab[0], c, h};
}

Vec3 oklabToXYZ(const Vec3& lab) {
    Vec3 lms = mul(oklabToLMS(), lab);
    for (double& v : lms) v = v * v * v;
    return mul(lmsToXYZ(), lms);
}
Vec3 xyzToOKLab(const Vec3& xyz) {
    Vec3 lms = mul(kXYZToLMS, xyz);
    for (double& v : lms) v = std::cbrt(v);
    return mul(kLMSToOKLab, lms);
}

bool isSRGBFamily(Space s) { return s == Space::SRGB || s == Space::HSL || s == Space::HWB; }

Vec3 values(const ColorVal& v) {
    Vec3 r;
    for (int i = 0; i < 3; i++) r[i] = v.missing[i] ? 0.0 : v.c[i];
    return r;
}

Vec3 toSRGB(Space s, const Vec3& c) {
    switch (s) {
        case Space::HSL: return hslToSRGB(c[0], c[1], c[2]);
        case Space::HWB: return hwbToSRGB(c[0], c[1], c[2]);
        default: return c;
    }
}

Vec3 toXYZD65(Space s, const Vec3& c) {
    switch (s) {
        case Space::SRGB: case Space::HSL: case Space::HWB: {
            Vec3 rgb = toSRGB(s, c);
            for (double& v : rgb) v = linearize(v);
            return mul(kLinSRGBToXYZ, rgb);
        }
        case Space::SRGBLinear: return mul(kLinSRGBToXYZ, c);
        case Space::DisplayP3: {
            Vec3 rgb = c;
            for (double& v : rgb) v = linearize(v);
            return mul(kLinP3ToXYZ, rgb);
        }
        case Space::XYZD65: return c;
        case Space::XYZD50: return mul(d50ToD65(), c);
        case Space::Lab: return mul(d50ToD65(), labToXYZD50(c));
        case Space::LCH: return mul(d50ToD65(), labToXYZD50(polarToRect(c)));
        case Space::OKLab: return oklabToXYZ(c);
        case Space::OKLCH: return oklabToXYZ(polarToRect(c));
    }
    return c;
}

// Converts from sRGB (for the sRGB family) or XYZ-D65 (everything else).
// Sets `achromatic` when the target's hue came out powerless.
Vec3 fromHub(Space target, const Vec3& srgb, const Vec3& xyz, bool& achromatic) {
    achromatic = false;
    switch (target) {
        case Space::SRGB: return srgb;
        case Space::HSL: {
            Vec3 hsl = srgbToHSL(srgb);
            if (std::isnan(hsl[0]) || std::fabs(hsl[1]) < 1e-9) { achromatic = true; hsl[0] = 0; }
            return hsl;
        }
        case Space::HWB: {
            Vec3 hsl = srgbToHSL(srgb);
            double w = std::min({srgb[0], srgb[1], srgb[2]});
            double b = 1 - std::max({srgb[0], srgb[1], srgb[2]});
            if (std::isnan(hsl[0]) || w + b >= 1 - 1e-9) { achromatic = true; hsl[0] = 0; }
            return {hsl[0], w, b};
        }
        case Space::SRGBLinear: return mul(xyzToLinSRGB(), xyz);
        case Space::DisplayP3: {
            Vec3 rgb = mul(xyzToLinP3(), xyz);
            for (double& v : rgb) v = gammaEncode(v);
            return rgb;
        }
        case Space::XYZD65: return xyz;
        case Space::XYZD50: return mul(kD65ToD50, xyz);
        case Space::Lab: return xyzD50ToLab(mul(kD65ToD50, xyz));
        case Space::LCH: {
            Vec3 lch = rectToPolar(xyzD50ToLab(mul(kD65ToD50, xyz)));
            if (lch[1] < 0.0015) { achromatic = true; lch[2] = 0; }
            return lch;
        }
        case Space::OKLab: return xyzToOKLab(xyz);
        case Space::OKLCH: {
            Vec3 lch = rectToPolar(xyzToOKLab(xyz));
            if (lch[1] < 0.000004) { achromatic = true; lch[2] = 0; }
            return lch;
        }
    }
    return xyz;
}

// Analogous-component categories (CSS Color 4 §12.2).
enum class Cat { None, Red, Green, Blue, Lightness, Colorfulness, Hue, OppA, OppB };

Cat categoryOf(Space s, int i) {
    switch (s) {
        case Space::SRGB: case Space::SRGBLinear: case Space::DisplayP3:
        case Space::XYZD50: case Space::XYZD65:
            return i == 0 ? Cat::Red : i == 1 ? Cat::Green : Cat::Blue;
        case Space::Lab: case Space::OKLab:
            return i == 0 ? Cat::Lightness : i == 1 ? Cat::OppA : Cat::OppB;
        case Space::LCH: case Space::OKLCH:
            return i == 0 ? Cat::Lightness : i == 1 ? Cat::Colorfulness : Cat::Hue;
        case Space::HSL:
            return i == 0 ? Cat::Hue : i == 1 ? Cat::Colorfulness : Cat::Lightness;
        case Space::HWB:
            return i == 0 ? Cat::Hue : Cat::None;
    }
    return Cat::None;
}

} // namespace

std::optional<Space> spaceFromName(std::string_view n) {
    if (n == "srgb") return Space::SRGB;
    if (n == "srgb-linear") return Space::SRGBLinear;
    if (n == "display-p3") return Space::DisplayP3;
    if (n == "xyz" || n == "xyz-d65") return Space::XYZD65;
    if (n == "xyz-d50") return Space::XYZD50;
    if (n == "lab") return Space::Lab;
    if (n == "lch") return Space::LCH;
    if (n == "oklab") return Space::OKLab;
    if (n == "oklch") return Space::OKLCH;
    if (n == "hsl") return Space::HSL;
    if (n == "hwb") return Space::HWB;
    return std::nullopt;
}

bool isPolar(Space s) { return hueIndex(s) >= 0; }

int hueIndex(Space s) {
    switch (s) {
        case Space::LCH: case Space::OKLCH: return 2;
        case Space::HSL: case Space::HWB: return 0;
        default: return -1;
    }
}

ColorVal convert(const ColorVal& in, Space target) {
    if (in.space == target) return in;
    Vec3 src = values(in);
    Vec3 srgb{}, xyz{};
    if (isSRGBFamily(in.space) && isSRGBFamily(target)) {
        srgb = toSRGB(in.space, src);
    } else {
        xyz = toXYZD65(in.space, src);
        if (isSRGBFamily(target)) {
            srgb = mul(xyzToLinSRGB(), xyz);
            for (double& v : srgb) v = gammaEncode(v);
        }
    }
    bool achromatic = false;
    Vec3 res = fromHub(target, srgb, xyz, achromatic);

    ColorVal out;
    out.space = target;
    out.alpha = in.alpha;
    out.alphaMissing = in.alphaMissing;
    for (int i = 0; i < 3; i++) out.c[i] = std::isfinite(res[i]) ? res[i] : 0.0;
    for (int i = 0; i < 3; i++) {
        if (!in.missing[i]) continue;
        Cat cat = categoryOf(in.space, i);
        if (cat == Cat::None) continue;
        for (int j = 0; j < 3; j++)
            if (categoryOf(target, j) == cat) { out.missing[j] = true; out.c[j] = 0; }
    }
    int hi = hueIndex(target);
    if (achromatic && hi >= 0) { out.missing[hi] = true; out.c[hi] = 0; }
    return out;
}

ColorVal mix(const ColorVal& a, const ColorVal& b, Space space, HueMethod method,
             double t, double alphaMult) {
    ColorVal A = convert(a, space), B = convert(b, space);
    ColorVal R;
    R.space = space;

    // A component missing in one colour takes the other's value.
    for (int i = 0; i < 3; i++) {
        if (A.missing[i] && B.missing[i]) { R.missing[i] = true; continue; }
        if (A.missing[i]) A.c[i] = B.c[i];
        if (B.missing[i]) B.c[i] = A.c[i];
    }
    double aa = A.alphaMissing ? (B.alphaMissing ? 1.0 : B.alpha) : A.alpha;
    double ba = B.alphaMissing ? aa : B.alpha;

    int hi = hueIndex(space);
    // Premultiply the non-hue components.
    for (int i = 0; i < 3; i++) {
        if (i == hi) continue;
        A.c[i] *= aa;
        B.c[i] *= ba;
    }
    if (hi >= 0 && !R.missing[hi]) {
        double h1 = normHue(A.c[hi]), h2 = normHue(B.c[hi]);
        double d = h2 - h1;
        switch (method) {
            case HueMethod::Shorter:
                if (d > 180) h1 += 360;
                else if (d < -180) h2 += 360;
                break;
            case HueMethod::Longer:
                if (d > 0 && d < 180) h1 += 360;
                else if (d > -180 && d <= 0) h2 += 360;
                break;
            case HueMethod::Increasing:
                if (h2 < h1) h2 += 360;
                break;
            case HueMethod::Decreasing:
                if (h1 < h2) h1 += 360;
                break;
        }
        A.c[hi] = h1;
        B.c[hi] = h2;
    }
    double ra = aa * (1 - t) + ba * t;
    for (int i = 0; i < 3; i++) {
        if (R.missing[i]) continue;
        double v = A.c[i] * (1 - t) + B.c[i] * t;
        if (i == hi) v = normHue(v);
        else if (ra != 0) v /= ra;
        R.c[i] = v;
    }
    R.alpha = ra * alphaMult;
    return R;
}

void toClippedSRGB(const ColorVal& in, double out[3], double& alpha) {
    ColorVal s = convert(in, Space::SRGB);
    for (int i = 0; i < 3; i++) {
        double v = s.missing[i] ? 0.0 : s.c[i];
        out[i] = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
    }
    double a = in.alphaMissing ? 0.0 : in.alpha;
    alpha = std::isfinite(a) ? std::clamp(a, 0.0, 1.0) : 0.0;
}

} // namespace htmlayout::css::colorspace
