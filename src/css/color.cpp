#include "css/color.h"
#include "css/color_space.h"
#include "../from_chars_compat.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

namespace htmlayout::css {

namespace {

// CSS named colors (Level 4)
const std::unordered_map<std::string, Color>& namedColors() {
    static const std::unordered_map<std::string, Color> colors = {
        {"transparent",         {0, 0, 0, 0}},
        {"black",               {0, 0, 0, 255}},
        {"white",               {255, 255, 255, 255}},
        {"red",                 {255, 0, 0, 255}},
        {"green",               {0, 128, 0, 255}},
        {"blue",                {0, 0, 255, 255}},
        {"yellow",              {255, 255, 0, 255}},
        {"cyan",                {0, 255, 255, 255}},
        {"aqua",                {0, 255, 255, 255}},
        {"magenta",             {255, 0, 255, 255}},
        {"fuchsia",             {255, 0, 255, 255}},
        {"silver",              {192, 192, 192, 255}},
        {"gray",                {128, 128, 128, 255}},
        {"grey",                {128, 128, 128, 255}},
        {"maroon",              {128, 0, 0, 255}},
        {"olive",               {128, 128, 0, 255}},
        {"lime",                {0, 255, 0, 255}},
        {"teal",                {0, 128, 128, 255}},
        {"navy",                {0, 0, 128, 255}},
        {"purple",              {128, 0, 128, 255}},
        {"orange",              {255, 165, 0, 255}},
        {"pink",                {255, 192, 203, 255}},
        {"brown",               {165, 42, 42, 255}},
        {"coral",               {255, 127, 80, 255}},
        {"crimson",             {220, 20, 60, 255}},
        {"darkblue",            {0, 0, 139, 255}},
        {"darkgreen",           {0, 100, 0, 255}},
        {"darkred",             {139, 0, 0, 255}},
        {"darkgray",            {169, 169, 169, 255}},
        {"darkgrey",            {169, 169, 169, 255}},
        {"darkcyan",            {0, 139, 139, 255}},
        {"darkmagenta",         {139, 0, 139, 255}},
        {"darkorange",          {255, 140, 0, 255}},
        {"darkviolet",          {148, 0, 211, 255}},
        {"deeppink",            {255, 20, 147, 255}},
        {"deepskyblue",         {0, 191, 255, 255}},
        {"dimgray",             {105, 105, 105, 255}},
        {"dimgrey",             {105, 105, 105, 255}},
        {"dodgerblue",          {30, 144, 255, 255}},
        {"firebrick",           {178, 34, 34, 255}},
        {"forestgreen",         {34, 139, 34, 255}},
        {"gold",                {255, 215, 0, 255}},
        {"goldenrod",           {218, 165, 32, 255}},
        {"greenyellow",         {173, 255, 47, 255}},
        {"hotpink",             {255, 105, 180, 255}},
        {"indianred",           {205, 92, 92, 255}},
        {"indigo",              {75, 0, 130, 255}},
        {"ivory",               {255, 255, 240, 255}},
        {"khaki",               {240, 230, 140, 255}},
        {"lavender",            {230, 230, 250, 255}},
        {"lawngreen",           {124, 252, 0, 255}},
        {"lightblue",           {173, 216, 230, 255}},
        {"lightcoral",          {240, 128, 128, 255}},
        {"lightcyan",           {224, 255, 255, 255}},
        {"lightgray",           {211, 211, 211, 255}},
        {"lightgrey",           {211, 211, 211, 255}},
        {"lightgreen",          {144, 238, 144, 255}},
        {"lightpink",           {255, 182, 193, 255}},
        {"lightyellow",         {255, 255, 224, 255}},
        {"limegreen",           {50, 205, 50, 255}},
        {"linen",               {250, 240, 230, 255}},
        {"mediumblue",          {0, 0, 205, 255}},
        {"midnightblue",        {25, 25, 112, 255}},
        {"mintcream",           {245, 255, 250, 255}},
        {"mistyrose",           {255, 228, 225, 255}},
        {"moccasin",            {255, 228, 181, 255}},
        {"oldlace",             {253, 245, 230, 255}},
        {"olivedrab",           {107, 142, 35, 255}},
        {"orangered",           {255, 69, 0, 255}},
        {"orchid",              {218, 112, 214, 255}},
        {"papayawhip",          {255, 239, 213, 255}},
        {"peachpuff",           {255, 218, 185, 255}},
        {"peru",                {205, 133, 63, 255}},
        {"plum",                {221, 160, 221, 255}},
        {"powderblue",          {176, 224, 230, 255}},
        {"rebeccapurple",       {102, 51, 153, 255}},
        {"rosybrown",           {188, 143, 143, 255}},
        {"royalblue",           {65, 105, 225, 255}},
        {"saddlebrown",         {139, 69, 19, 255}},
        {"salmon",              {250, 128, 114, 255}},
        {"sandybrown",          {244, 164, 96, 255}},
        {"seagreen",            {46, 139, 87, 255}},
        {"seashell",            {255, 245, 238, 255}},
        {"sienna",              {160, 82, 45, 255}},
        {"skyblue",             {135, 206, 235, 255}},
        {"slateblue",           {106, 90, 205, 255}},
        {"slategray",           {112, 128, 144, 255}},
        {"slategrey",           {112, 128, 144, 255}},
        {"snow",                {255, 250, 250, 255}},
        {"springgreen",         {0, 255, 127, 255}},
        {"steelblue",           {70, 130, 180, 255}},
        {"tan",                 {210, 180, 140, 255}},
        {"thistle",             {216, 191, 216, 255}},
        {"tomato",              {255, 99, 71, 255}},
        {"turquoise",           {64, 224, 208, 255}},
        {"violet",              {238, 130, 238, 255}},
        {"wheat",               {245, 222, 179, 255}},
        {"whitesmoke",          {245, 245, 245, 255}},
        {"yellowgreen",         {154, 205, 50, 255}},
        {"aliceblue",           {240, 248, 255, 255}},
        {"antiquewhite",        {250, 235, 215, 255}},
        {"azure",               {240, 255, 255, 255}},
        {"beige",               {245, 245, 220, 255}},
        {"bisque",              {255, 228, 196, 255}},
        {"blanchedalmond",      {255, 235, 205, 255}},
        {"blueviolet",          {138, 43, 226, 255}},
        {"burlywood",           {222, 184, 135, 255}},
        {"cadetblue",           {95, 158, 160, 255}},
        {"chartreuse",          {127, 255, 0, 255}},
        {"chocolate",           {210, 105, 30, 255}},
        {"cornflowerblue",      {100, 149, 237, 255}},
        {"cornsilk",            {255, 248, 220, 255}},
        {"darkgoldenrod",       {184, 134, 11, 255}},
        {"darkkhaki",           {189, 183, 107, 255}},
        {"darkolivegreen",      {85, 107, 47, 255}},
        {"darkorchid",          {153, 50, 204, 255}},
        {"darksalmon",          {233, 150, 122, 255}},
        {"darkseagreen",        {143, 188, 143, 255}},
        {"darkslateblue",       {72, 61, 139, 255}},
        {"darkslategray",       {47, 79, 79, 255}},
        {"darkslategrey",       {47, 79, 79, 255}},
        {"darkturquoise",       {0, 206, 209, 255}},
        {"floralwhite",         {255, 250, 240, 255}},
        {"gainsboro",           {220, 220, 220, 255}},
        {"ghostwhite",          {248, 248, 255, 255}},
        {"honeydew",            {240, 255, 240, 255}},
        {"lavenderblush",       {255, 240, 245, 255}},
        {"lemonchiffon",        {255, 250, 205, 255}},
        {"lightsalmon",         {255, 160, 122, 255}},
        {"lightseagreen",       {32, 178, 170, 255}},
        {"lightskyblue",        {135, 206, 250, 255}},
        {"lightslategray",      {119, 136, 153, 255}},
        {"lightslategrey",      {119, 136, 153, 255}},
        {"lightsteelblue",      {176, 196, 222, 255}},
        {"mediumaquamarine",    {102, 205, 170, 255}},
        {"mediumorchid",        {186, 85, 211, 255}},
        {"mediumpurple",        {147, 111, 219, 255}},
        {"mediumseagreen",      {60, 179, 113, 255}},
        {"mediumslateblue",     {123, 104, 238, 255}},
        {"mediumspringgreen",   {0, 250, 154, 255}},
        {"mediumturquoise",     {72, 209, 204, 255}},
        {"mediumvioletred",     {199, 21, 133, 255}},
        {"navajowhite",         {255, 222, 173, 255}},
        {"palegoldenrod",       {238, 232, 170, 255}},
        {"palegreen",           {152, 251, 152, 255}},
        {"paleturquoise",       {175, 238, 238, 255}},
        {"palevioletred",       {219, 112, 147, 255}},
        {"lightgoldenrodyellow",{250, 250, 210, 255}},
        {"currentcolor",        {0, 0, 0, 255}},  // fallback
    };
    return colors;
}

using colorspace::ColorVal;
using colorspace::Space;

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

ColorVal fromBytes(const Color& c) {
    ColorVal v;
    v.c[0] = c.r / 255.0;
    v.c[1] = c.g / 255.0;
    v.c[2] = c.b / 255.0;
    v.alpha = c.a / 255.0;
    return v;
}

// ---------------------------------------------------------------------------
// A small tokenizer for colour values (input already lower-cased).

struct Tok {
    enum Kind { Num, Pct, Dim, Ident, Func, Hash, Comma, Slash } kind;
    double v = 0;
    std::string s;      // unit (Dim), name (Ident/Func), digits (Hash)
    std::string inner;  // Func arguments
    std::string raw;    // source text, for nested colours
};

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || c == '-' || c == '_' || (unsigned char)c >= 0x80;
}
bool isIdentChar(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::optional<std::vector<Tok>> lex(std::string_view s) {
    std::vector<Tok> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') { i++; continue; }
        size_t start = i;
        if (c == ',') { out.push_back({Tok::Comma}); i++; continue; }
        if (c == '/') { out.push_back({Tok::Slash}); i++; continue; }
        if (c == '#') {
            i++;
            while (i < n && isIdentChar(s[i])) i++;
            Tok t{Tok::Hash};
            t.s = std::string(s.substr(start + 1, i - start - 1));
            t.raw = std::string(s.substr(start, i - start));
            out.push_back(std::move(t));
            continue;
        }
        auto startsNumber = [&](size_t p) {
            if (p < n && (s[p] == '+' || s[p] == '-')) p++;
            if (p < n && isDigit(s[p])) return true;
            return p + 1 < n && s[p] == '.' && isDigit(s[p + 1]);
        };
        if (startsNumber(i)) {
            size_t p = i;
            if (s[p] == '+' || s[p] == '-') p++;
            while (p < n && isDigit(s[p])) p++;
            if (p + 1 < n && s[p] == '.' && isDigit(s[p + 1])) {
                p++;
                while (p < n && isDigit(s[p])) p++;
            }
            if (p < n && s[p] == 'e') {
                size_t q = p + 1;
                if (q < n && (s[q] == '+' || s[q] == '-')) q++;
                if (q < n && isDigit(s[q])) {
                    p = q;
                    while (p < n && isDigit(s[p])) p++;
                }
            }
            const char* b = s.data() + i;
            if (*b == '+') b++;
            double v = 0;
            auto [ptr, ec] = htmlayout::from_chars_fp(b, s.data() + p, v);
            if (ec != std::errc() || ptr != s.data() + p) return std::nullopt;
            Tok t{Tok::Num};
            t.v = v;
            i = p;
            if (i < n && s[i] == '%') {
                t.kind = Tok::Pct;
                i++;
            } else if (i < n && isIdentStart(s[i])) {
                size_t u = i;
                while (i < n && isIdentChar(s[i])) i++;
                t.kind = Tok::Dim;
                t.s = std::string(s.substr(u, i - u));
            }
            t.raw = std::string(s.substr(start, i - start));
            out.push_back(std::move(t));
            continue;
        }
        if (isIdentStart(c)) {
            while (i < n && isIdentChar(s[i])) i++;
            Tok t{Tok::Ident};
            t.s = std::string(s.substr(start, i - start));
            if (i < n && s[i] == '(') {
                int depth = 0;
                size_t p = i;
                for (; p < n; p++) {
                    if (s[p] == '(') depth++;
                    else if (s[p] == ')' && --depth == 0) break;
                }
                if (p >= n) return std::nullopt;  // unbalanced
                t.kind = Tok::Func;
                t.inner = std::string(s.substr(i + 1, p - i - 1));
                i = p + 1;
            }
            t.raw = std::string(s.substr(start, i - start));
            out.push_back(std::move(t));
            continue;
        }
        return std::nullopt;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Component arguments: `a b c [/ alpha]` (modern) or `a, b, c[, alpha]`
// (legacy, rgb()/hsl() only; `none` is not allowed there).

struct Args {
    const Tok* comp[3] = {nullptr, nullptr, nullptr};
    const Tok* alpha = nullptr;
    bool legacy = false;
};

bool isComponentTok(const Tok& t) {
    return t.kind == Tok::Num || t.kind == Tok::Pct || t.kind == Tok::Dim ||
           (t.kind == Tok::Ident && t.s == "none");
}

std::optional<Args> splitArgs(const std::vector<Tok>& t, size_t from, bool allowLegacy) {
    Args a;
    size_t n = t.size() - from;
    bool hasComma = false;
    for (size_t i = from; i < t.size(); i++)
        if (t[i].kind == Tok::Comma) hasComma = true;
    if (hasComma) {
        if (!allowLegacy || (n != 5 && n != 7)) return std::nullopt;
        for (size_t k = 0; k < n; k++) {
            const Tok& x = t[from + k];
            if (k % 2 == 1) {
                if (x.kind != Tok::Comma) return std::nullopt;
            } else if (!isComponentTok(x) || x.kind == Tok::Ident) {
                return std::nullopt;
            }
        }
        a.legacy = true;
        for (int k = 0; k < 3; k++) a.comp[k] = &t[from + k * 2];
        if (n == 7) a.alpha = &t[from + 6];
        return a;
    }
    if (n != 3 && n != 5) return std::nullopt;
    for (int k = 0; k < 3; k++) {
        if (!isComponentTok(t[from + k])) return std::nullopt;
        a.comp[k] = &t[from + k];
    }
    if (n == 5) {
        if (t[from + 3].kind != Tok::Slash || !isComponentTok(t[from + 4])) return std::nullopt;
        a.alpha = &t[from + 4];
    }
    return a;
}

// A <number> | <percentage> | none component. `pct100` is the value 100%
// maps to (0 = percentages not allowed). Numbers are scaled by `numScale`.
bool component(const Tok* t, double pct100, double numScale, ColorVal& out, int idx) {
    if (t->kind == Tok::Ident) { out.missing[idx] = true; out.c[idx] = 0; return true; }
    if (t->kind == Tok::Num) { out.c[idx] = t->v * numScale; return true; }
    if (t->kind == Tok::Pct && pct100 != 0) { out.c[idx] = t->v / 100.0 * pct100; return true; }
    return false;
}

bool hueComponent(const Tok* t, ColorVal& out, int idx) {
    if (t->kind == Tok::Ident) { out.missing[idx] = true; out.c[idx] = 0; return true; }
    double deg;
    if (t->kind == Tok::Num) deg = t->v;
    else if (t->kind == Tok::Dim) {
        if (t->s == "deg") deg = t->v;
        else if (t->s == "rad") deg = t->v * 180.0 / 3.14159265358979323846;
        else if (t->s == "grad") deg = t->v * 0.9;
        else if (t->s == "turn") deg = t->v * 360.0;
        else return false;
    } else return false;
    if (!std::isfinite(deg)) return false;
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    out.c[idx] = deg;
    return true;
}

bool alphaComponent(const Tok* t, ColorVal& out) {
    if (!t) { out.alpha = 1.0; return true; }
    if (t->kind == Tok::Ident) { out.alphaMissing = true; out.alpha = 0; return true; }
    double a;
    if (t->kind == Tok::Num) a = t->v;
    else if (t->kind == Tok::Pct) a = t->v / 100.0;
    else return false;
    out.alpha = std::clamp(a, 0.0, 1.0);
    return true;
}

void clampComp(ColorVal& v, int i, double lo, double hi) {
    if (!v.missing[i]) v.c[i] = std::clamp(v.c[i], lo, hi);
}

struct Ctx {
    Color current{0, 0, 0, 255};
};

std::optional<ColorVal> parseColorVal(std::string_view s, const Ctx& ctx, int depth);

std::optional<ColorVal> parseColorFunc(const Tok& f, const Ctx& ctx, int depth) {
    auto toks = lex(f.inner);
    if (!toks) return std::nullopt;
    const std::string& name = f.s;
    ColorVal v;

    if (name == "rgb" || name == "rgba") {
        auto a = splitArgs(*toks, 0, true);
        if (!a) return std::nullopt;
        v.space = Space::SRGB;
        for (int i = 0; i < 3; i++)
            if (!component(a->comp[i], 1.0, 1.0 / 255.0, v, i)) return std::nullopt;
        if (!alphaComponent(a->alpha, v)) return std::nullopt;
        return v;
    }
    if (name == "hsl" || name == "hsla" || name == "hwb") {
        bool hwb = name == "hwb";
        auto a = splitArgs(*toks, 0, !hwb);
        if (!a) return std::nullopt;
        v.space = hwb ? Space::HWB : Space::HSL;
        // Bare numbers are accepted for s/l (w/b), meaning the same as %.
        if (!hueComponent(a->comp[0], v, 0) ||
            !component(a->comp[1], 1.0, 0.01, v, 1) ||
            !component(a->comp[2], 1.0, 0.01, v, 2) ||
            !alphaComponent(a->alpha, v)) return std::nullopt;
        if (!hwb) clampComp(v, 1, 0.0, 1e9);
        return v;
    }
    if (name == "lab" || name == "lch" || name == "oklab" || name == "oklch") {
        auto a = splitArgs(*toks, 0, false);
        if (!a) return std::nullopt;
        bool ok = name.rfind("ok", 0) == 0;
        bool polar = name.back() == 'h';
        v.space = ok ? (polar ? Space::OKLCH : Space::OKLab) : (polar ? Space::LCH : Space::Lab);
        double lMax = ok ? 1.0 : 100.0;
        if (!component(a->comp[0], lMax, 1.0, v, 0)) return std::nullopt;
        clampComp(v, 0, 0.0, lMax);
        if (polar) {
            if (!component(a->comp[1], ok ? 0.4 : 150.0, 1.0, v, 1) ||
                !hueComponent(a->comp[2], v, 2)) return std::nullopt;
            clampComp(v, 1, 0.0, 1e9);
        } else {
            double ab = ok ? 0.4 : 125.0;
            if (!component(a->comp[1], ab, 1.0, v, 1) ||
                !component(a->comp[2], ab, 1.0, v, 2)) return std::nullopt;
        }
        if (!alphaComponent(a->alpha, v)) return std::nullopt;
        return v;
    }
    if (name == "color") {
        if (toks->empty() || (*toks)[0].kind != Tok::Ident) return std::nullopt;
        auto sp = colorspace::spaceFromName((*toks)[0].s);
        if (!sp || colorspace::isPolar(*sp) || *sp == Space::Lab || *sp == Space::OKLab)
            return std::nullopt;
        auto a = splitArgs(*toks, 1, false);
        if (!a) return std::nullopt;
        v.space = *sp;
        for (int i = 0; i < 3; i++)
            if (!component(a->comp[i], 1.0, 1.0, v, i)) return std::nullopt;
        if (!alphaComponent(a->alpha, v)) return std::nullopt;
        return v;
    }
    if (name == "color-mix") {
        // Split on top-level commas.
        std::vector<std::vector<const Tok*>> groups(1);
        for (const Tok& t : *toks) {
            if (t.kind == Tok::Comma) groups.emplace_back();
            else groups.back().push_back(&t);
        }
        Space space = Space::OKLab;  // css-color-5: the interpolation method may be omitted
        auto hue = colorspace::HueMethod::Shorter;
        size_t first = 0;
        if (!groups[0].empty() && groups[0][0]->kind == Tok::Ident && groups[0][0]->s == "in") {
            const auto& g = groups[0];
            if (g.size() < 2 || g[1]->kind != Tok::Ident) return std::nullopt;
            auto sp = colorspace::spaceFromName(g[1]->s);
            if (!sp) return std::nullopt;
            space = *sp;
            if (g.size() == 4) {
                if (!colorspace::isPolar(space) || g[2]->kind != Tok::Ident ||
                    g[3]->kind != Tok::Ident || g[3]->s != "hue") return std::nullopt;
                const std::string& m = g[2]->s;
                if (m == "shorter") hue = colorspace::HueMethod::Shorter;
                else if (m == "longer") hue = colorspace::HueMethod::Longer;
                else if (m == "increasing") hue = colorspace::HueMethod::Increasing;
                else if (m == "decreasing") hue = colorspace::HueMethod::Decreasing;
                else return std::nullopt;
            } else if (g.size() != 2) {
                return std::nullopt;
            }
            first = 1;
        }
        if (groups.size() - first != 2) return std::nullopt;
        ColorVal col[2];
        std::optional<double> pct[2];
        for (int k = 0; k < 2; k++) {
            const auto& g = groups[first + k];
            const Tok* colorTok = nullptr;
            for (const Tok* t : g) {
                if (t->kind == Tok::Pct) {
                    if (pct[k] || t->v < 0 || t->v > 100) return std::nullopt;
                    pct[k] = t->v / 100.0;
                } else if (t->kind == Tok::Ident || t->kind == Tok::Hash || t->kind == Tok::Func) {
                    if (colorTok) return std::nullopt;
                    colorTok = t;
                } else {
                    return std::nullopt;
                }
            }
            if (!colorTok) return std::nullopt;
            auto c = parseColorVal(colorTok->raw, ctx, depth + 1);
            if (!c) return std::nullopt;
            col[k] = *c;
        }
        // Percentage normalisation (css-color-5 §3.1).
        double p1, p2, alphaMult = 1.0;
        if (!pct[0] && !pct[1]) { p1 = p2 = 0.5; }
        else if (!pct[1]) { p1 = *pct[0]; p2 = 1 - p1; }
        else if (!pct[0]) { p2 = *pct[1]; p1 = 1 - p2; }
        else {
            p1 = *pct[0];
            p2 = *pct[1];
            double sum = p1 + p2;
            if (sum <= 0) return std::nullopt;
            if (sum < 1) alphaMult = sum;
            p1 /= sum;
            p2 /= sum;
        }
        (void)p1;
        return colorspace::mix(col[0], col[1], space, hue, p2, alphaMult);
    }
    return std::nullopt;
}

std::optional<ColorVal> parseColorVal(std::string_view s, const Ctx& ctx, int depth) {
    if (depth > 16) return std::nullopt;
    auto toks = lex(s);
    if (!toks || toks->size() != 1) return std::nullopt;
    const Tok& t = (*toks)[0];
    if (t.kind == Tok::Ident) {
        if (t.s == "currentcolor") return fromBytes(ctx.current);
        auto& names = namedColors();
        auto it = names.find(t.s);
        if (it == names.end()) return std::nullopt;
        return fromBytes(it->second);
    }
    if (t.kind == Tok::Hash) {
        const std::string& h = t.s;
        int d[8];
        if (h.size() != 3 && h.size() != 4 && h.size() != 6 && h.size() != 8) return std::nullopt;
        for (size_t i = 0; i < h.size(); i++)
            if ((d[i] = hexDigit(h[i])) < 0) return std::nullopt;
        Color c;
        if (h.size() <= 4) {
            c = {uint8_t(d[0] * 17), uint8_t(d[1] * 17), uint8_t(d[2] * 17),
                 uint8_t(h.size() == 4 ? d[3] * 17 : 255)};
        } else {
            c = {uint8_t(d[0] * 16 + d[1]), uint8_t(d[2] * 16 + d[3]), uint8_t(d[4] * 16 + d[5]),
                 uint8_t(h.size() == 8 ? d[6] * 16 + d[7] : 255)};
        }
        return fromBytes(c);
    }
    if (t.kind == Tok::Func) return parseColorFunc(t, ctx, depth);
    return std::nullopt;
}

uint8_t toByte(double v) {
    return static_cast<uint8_t>(std::clamp(std::round(v * 255.0), 0.0, 255.0));
}

} // anonymous namespace

bool tryParseColor(const std::string& value, Color& out, const Color& currentColor) {
    std::string lower = value;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    Ctx ctx;
    ctx.current = currentColor;
    auto v = parseColorVal(lower, ctx, 0);
    if (!v) return false;
    double rgb[3], a;
    colorspace::toClippedSRGB(*v, rgb, a);
    out = {toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2]), toByte(a)};
    return true;
}

Color parseColor(const std::string& value, const Color& currentColor) {
    Color c{0, 0, 0, 0};
    if (!tryParseColor(value, c, currentColor)) return {0, 0, 0, 0};
    return c;
}

Color parseColor(const std::string& value) {
    return parseColor(value, Color{0, 0, 0, 255});
}

} // namespace htmlayout::css
