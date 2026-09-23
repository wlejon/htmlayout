#include "css/color.h"
#include "css/color_calc.h"
#include "css/color_space.h"
#include "../from_chars_compat.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <optional>
#include <span>
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

// CSS Color 4 §6.2 system colours, keyed lowercase, as {light, dark}: the
// values a browser uses with no OS theme to follow (Chromium's), the dark
// ones for an element whose used colour scheme is dark.
struct SystemColor {
    Color light, dark;
};

const std::unordered_map<std::string, SystemColor>& systemColors() {
    static const std::unordered_map<std::string, SystemColor> colors = [] {
        std::unordered_map<std::string, SystemColor> m = {
            {"accentcolor",       {{0, 117, 255, 255},   {153, 200, 255, 255}}},
            {"accentcolortext",   {{255, 255, 255, 255}, {0, 0, 0, 255}}},
            {"activetext",        {{255, 0, 0, 255},     {255, 158, 158, 255}}},
            {"buttonborder",      {{118, 118, 118, 255}, {107, 107, 107, 255}}},
            {"buttonface",        {{239, 239, 239, 255}, {107, 107, 107, 255}}},
            {"buttontext",        {{0, 0, 0, 255},       {255, 255, 255, 255}}},
            {"canvas",            {{255, 255, 255, 255}, {18, 18, 18, 255}}},
            {"canvastext",        {{0, 0, 0, 255},       {255, 255, 255, 255}}},
            {"field",             {{255, 255, 255, 255}, {59, 59, 59, 255}}},
            {"fieldtext",         {{0, 0, 0, 255},       {255, 255, 255, 255}}},
            {"graytext",          {{109, 109, 109, 255}, {142, 142, 142, 255}}},
            {"highlight",         {{181, 213, 255, 255}, {153, 200, 255, 255}}},
            {"highlighttext",     {{0, 0, 0, 255},       {0, 0, 0, 255}}},
            {"linktext",          {{0, 0, 238, 255},     {158, 158, 255, 255}}},
            {"mark",              {{255, 255, 0, 255},   {102, 77, 0, 255}}},
            {"marktext",          {{0, 0, 0, 255},       {255, 255, 255, 255}}},
            {"selecteditem",      {{0, 117, 255, 255},   {153, 200, 255, 255}}},
            {"selecteditemtext",  {{255, 255, 255, 255}, {0, 0, 0, 255}}},
            {"visitedtext",       {{85, 26, 139, 255},   {208, 173, 240, 255}}},
        };
        // The deprecated keywords (§6.2.1) are aliases for these.
        static const std::pair<const char*, const char*> kDeprecated[] = {
            {"activeborder", "buttonborder"},     {"activecaption", "canvas"},
            {"appworkspace", "canvas"},           {"background", "canvas"},
            {"buttonhighlight", "buttonface"},    {"buttonshadow", "buttonface"},
            {"captiontext", "canvastext"},        {"inactiveborder", "buttonborder"},
            {"inactivecaption", "canvas"},        {"inactivecaptiontext", "graytext"},
            {"infobackground", "canvas"},         {"infotext", "canvastext"},
            {"menu", "canvas"},                   {"menutext", "canvastext"},
            {"scrollbar", "canvas"},              {"threeddarkshadow", "buttonborder"},
            {"threedface", "buttonface"},         {"threedhighlight", "buttonborder"},
            {"threedlightshadow", "buttonborder"}, {"threedshadow", "buttonborder"},
            {"window", "canvas"},                 {"windowframe", "buttonborder"},
            {"windowtext", "canvastext"},
        };
        for (const auto& [alias, target] : kDeprecated) {
            SystemColor c = m.at(target);
            m.emplace(alias, c);
        }
        return m;
    }();
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

// Numbers, percentages, angles, `none`, a channel keyword of the relative
// syntax, or a math function; which of these a component accepts is checked
// when it is resolved.
bool isComponentTok(const Tok& t) {
    return t.kind == Tok::Num || t.kind == Tok::Pct || t.kind == Tok::Dim ||
           t.kind == Tok::Ident ||
           (t.kind == Tok::Func && colorcalc::isMathFunction(t.s));
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

struct Ctx {
    Color current{0, 0, 0, 255};
    bool dark = false;
    // The origin colour's channels, inside a relative colour function.
    std::span<const colorcalc::Channel> channels;
};

// A component token with channel keywords and math functions evaluated.
struct Resolved {
    bool none = false;
    colorcalc::Value val;
};

std::optional<Resolved> resolve(const Tok* t, const Ctx& ctx) {
    using colorcalc::Type;
    switch (t->kind) {
        case Tok::Num: return Resolved{false, {t->v, Type::Number}};
        case Tok::Pct: return Resolved{false, {t->v, Type::Percent}};
        case Tok::Dim: {
            double v = t->v;
            if (t->s == "deg") {}
            else if (t->s == "rad") v = v * 180.0 / 3.14159265358979323846;
            else if (t->s == "grad") v = v * 0.9;
            else if (t->s == "turn") v = v * 360.0;
            else return std::nullopt;
            return Resolved{false, {v, Type::Angle}};
        }
        case Tok::Ident:
            if (t->s == "none") return Resolved{true, {}};
            for (const auto& c : ctx.channels)
                if (c.name == t->s) return Resolved{false, {c.value, Type::Number}};
            return std::nullopt;
        case Tok::Func: {
            auto v = colorcalc::evaluate(t->s, t->inner, ctx.channels);
            if (!v) return std::nullopt;
            return Resolved{false, *v};
        }
        default: return std::nullopt;
    }
}

// A <number> | <percentage> | none component. `pct100` is the value 100%
// maps to (0 = percentages not allowed). Numbers are scaled by `numScale`.
bool component(const Tok* t, const Ctx& ctx, double pct100, double numScale,
               ColorVal& out, int idx) {
    auto r = resolve(t, ctx);
    if (!r) return false;
    if (r->none) { out.missing[idx] = true; out.c[idx] = 0; return true; }
    double v;
    if (r->val.type == colorcalc::Type::Number) v = r->val.v * numScale;
    else if (r->val.type == colorcalc::Type::Percent && pct100 != 0) v = r->val.v / 100.0 * pct100;
    else return false;
    if (std::isnan(v)) v = 0;
    out.c[idx] = v;
    return true;
}

bool hueComponent(const Tok* t, const Ctx& ctx, ColorVal& out, int idx) {
    auto r = resolve(t, ctx);
    if (!r) return false;
    if (r->none) { out.missing[idx] = true; out.c[idx] = 0; return true; }
    if (r->val.type == colorcalc::Type::Percent) return false;
    double deg = r->val.v;
    // A literal infinite hue is a parse error; one computed by calc() is
    // clamped per CSS Values 4 and so ends up as 0 after normalisation.
    if (!std::isfinite(deg)) {
        if (t->kind != Tok::Func) return false;
        deg = 0;
    }
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    out.c[idx] = deg;
    return true;
}

// An omitted alpha is `fallback` (opaque, or the origin's in relative syntax).
bool alphaComponent(const Tok* t, const Ctx& ctx, ColorVal& out,
                    double fallback = 1.0, bool fallbackMissing = false) {
    if (!t) {
        out.alpha = fallback;
        out.alphaMissing = fallbackMissing;
        return true;
    }
    auto r = resolve(t, ctx);
    if (!r) return false;
    if (r->none) { out.alphaMissing = true; out.alpha = 0; return true; }
    double a;
    if (r->val.type == colorcalc::Type::Number) a = r->val.v;
    else if (r->val.type == colorcalc::Type::Percent) a = r->val.v / 100.0;
    else return false;
    out.alpha = std::isnan(a) ? 0.0 : std::clamp(a, 0.0, 1.0);
    return true;
}

void clampComp(ColorVal& v, int i, double lo, double hi) {
    if (!v.missing[i]) v.c[i] = std::clamp(v.c[i], lo, hi);
}

// The relative colour syntax's channel keywords for `v` (already converted to
// the target space): their names, and values in the units the function's
// components take as bare numbers.
std::vector<colorcalc::Channel> channelsOf(const ColorVal& v, std::string_view fn) {
    auto c = [&](int i) { return v.missing[i] ? 0.0 : v.c[i]; };
    double a = v.alphaMissing ? 0.0 : v.alpha;
    const char* names[3];
    double vals[3] = {c(0), c(1), c(2)};
    switch (v.space) {
        case Space::SRGB:
            if (fn == "color") { names[0] = "r"; names[1] = "g"; names[2] = "b"; break; }
            names[0] = "r"; names[1] = "g"; names[2] = "b";
            for (double& x : vals) x *= 255.0;
            break;
        case Space::HSL:
            names[0] = "h"; names[1] = "s"; names[2] = "l";
            vals[1] *= 100.0; vals[2] *= 100.0;
            break;
        case Space::HWB:
            names[0] = "h"; names[1] = "w"; names[2] = "b";
            vals[1] *= 100.0; vals[2] *= 100.0;
            break;
        case Space::Lab: case Space::OKLab:
            names[0] = "l"; names[1] = "a"; names[2] = "b";
            break;
        case Space::LCH: case Space::OKLCH:
            names[0] = "l"; names[1] = "c"; names[2] = "h";
            break;
        case Space::XYZD50: case Space::XYZD65:
            names[0] = "x"; names[1] = "y"; names[2] = "z";
            break;
        default:
            names[0] = "r"; names[1] = "g"; names[2] = "b";
            break;
    }
    return {{names[0], vals[0]}, {names[1], vals[1]}, {names[2], vals[2]}, {"alpha", a}};
}

std::optional<ColorVal> parseColorVal(std::string_view s, const Ctx& ctx, int depth);

// Relative colour syntax (css-color-5 §4): `fn(from <color> ...)`. On a match,
// fills `origin` (converted to `target`), `chans` and `inner` (the context
// to parse the components in) and returns the index of the first token after
// the origin; returns 0 when `toks` does not start with `from`, and nullopt
// when it does but the origin is invalid. The caller converts the origin to
// its space and parses the components with channelsOf() in scope.
std::optional<size_t> relativeOrigin(const std::vector<Tok>& toks, const Ctx& ctx, int depth,
                                     ColorVal& origin) {
    if (toks.empty() || toks[0].kind != Tok::Ident || toks[0].s != "from") return 0;
    if (toks.size() < 2) return std::nullopt;
    const Tok& o = toks[1];
    if (o.kind != Tok::Ident && o.kind != Tok::Hash && o.kind != Tok::Func) return std::nullopt;
    Ctx outer = ctx;
    outer.channels = {};
    auto v = parseColorVal(o.raw, outer, depth + 1);
    if (!v) return std::nullopt;
    origin = *v;
    return 2;
}

std::optional<ColorVal> parseColorVal(std::string_view s, const Ctx& ctx, int depth);

std::optional<ColorVal> parseColorFunc(const Tok& f, const Ctx& ctx, int depth) {
    auto toks = lex(f.inner);
    if (!toks) return std::nullopt;
    const std::string& name = f.s;
    ColorVal v;

    std::optional<Space> space;
    if (name == "rgb" || name == "rgba") space = Space::SRGB;
    else if (name == "hsl" || name == "hsla") space = Space::HSL;
    else if (name == "hwb") space = Space::HWB;
    else if (name == "lab") space = Space::Lab;
    else if (name == "lch") space = Space::LCH;
    else if (name == "oklab") space = Space::OKLab;
    else if (name == "oklch") space = Space::OKLCH;

    if (space || name == "color") {
        // Relative syntax: the origin, then (for color()) the space name.
        ColorVal origin;
        auto rel = relativeOrigin(*toks, ctx, depth, origin);
        if (!rel) return std::nullopt;
        size_t at = *rel;
        if (name == "color") {
            if (at >= toks->size() || (*toks)[at].kind != Tok::Ident) return std::nullopt;
            auto sp = colorspace::spaceFromName((*toks)[at].s);
            if (!sp || colorspace::isPolar(*sp) || *sp == Space::Lab || *sp == Space::OKLab)
                return std::nullopt;
            space = sp;
            at++;
        }
        bool relative = at >= 2 && (*toks)[0].kind == Tok::Ident && (*toks)[0].s == "from";
        Ctx inner = ctx;
        std::vector<colorcalc::Channel> chans;
        double alphaFallback = 1.0;
        bool alphaFallbackMissing = false;
        if (relative) {
            origin = colorspace::convert(origin, *space);
            chans = channelsOf(origin, name);
            inner.channels = chans;
            alphaFallback = origin.alpha;
            alphaFallbackMissing = origin.alphaMissing;
        } else {
            inner.channels = {};
        }
        bool legacyOk = !relative && (*space == Space::SRGB || *space == Space::HSL) &&
                        name != "color";
        auto a = splitArgs(*toks, at, legacyOk);
        if (!a) return std::nullopt;
        v.space = *space;
        if (!alphaComponent(a->alpha, inner, v, alphaFallback, alphaFallbackMissing))
            return std::nullopt;

        if (name == "color") {
            for (int i = 0; i < 3; i++)
                if (!component(a->comp[i], inner, 1.0, 1.0, v, i)) return std::nullopt;
            return v;
        }
        switch (*space) {
            case Space::SRGB:
                for (int i = 0; i < 3; i++) {
                    if (!component(a->comp[i], inner, 1.0, 1.0 / 255.0, v, i)) return std::nullopt;
                    clampComp(v, i, 0.0, 1.0);  // clamped at parsed-value time
                }
                return v;
            case Space::HSL: case Space::HWB:
                // Bare numbers are accepted for s/l (w/b), meaning the same as %.
                if (!hueComponent(a->comp[0], inner, v, 0) ||
                    !component(a->comp[1], inner, 1.0, 0.01, v, 1) ||
                    !component(a->comp[2], inner, 1.0, 0.01, v, 2)) return std::nullopt;
                if (*space == Space::HSL) {
                    clampComp(v, 1, 0.0, 1.0);
                    clampComp(v, 2, 0.0, 1.0);
                } else {
                    clampComp(v, 1, 0.0, 1e9);
                    clampComp(v, 2, 0.0, 1e9);
                }
                return v;
            default: {
                bool ok = *space == Space::OKLab || *space == Space::OKLCH;
                bool polar = colorspace::isPolar(*space);
                double lMax = ok ? 1.0 : 100.0;
                if (!component(a->comp[0], inner, lMax, 1.0, v, 0)) return std::nullopt;
                clampComp(v, 0, 0.0, lMax);
                if (polar) {
                    if (!component(a->comp[1], inner, ok ? 0.4 : 150.0, 1.0, v, 1) ||
                        !hueComponent(a->comp[2], inner, v, 2)) return std::nullopt;
                    clampComp(v, 1, 0.0, 1e9);
                } else {
                    double ab = ok ? 0.4 : 125.0;
                    if (!component(a->comp[1], inner, ab, 1.0, v, 1) ||
                        !component(a->comp[2], inner, ab, 1.0, v, 2)) return std::nullopt;
                }
                return v;
            }
        }
    }
    if (name == "light-dark") {
        // light-dark(<color>, <color>): the first under a light used colour
        // scheme, the second under a dark one. Both must be valid.
        std::vector<const Tok*> parts;
        for (size_t i = 0; i < toks->size(); i++) {
            const Tok& t = (*toks)[i];
            if (i % 2 == 1) {
                if (t.kind != Tok::Comma) return std::nullopt;
            } else {
                parts.push_back(&t);
            }
        }
        if (parts.size() != 2 || toks->size() != 3) return std::nullopt;
        std::optional<ColorVal> c[2];
        for (int k = 0; k < 2; k++) {
            const Tok* t = parts[k];
            if (t->kind != Tok::Ident && t->kind != Tok::Hash && t->kind != Tok::Func)
                return std::nullopt;
            c[k] = parseColorVal(t->raw, ctx, depth + 1);
            if (!c[k]) return std::nullopt;
        }
        return ctx.dark ? *c[1] : *c[0];
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
                bool math = t->kind == Tok::Func && colorcalc::isMathFunction(t->s);
                if (t->kind == Tok::Pct || math) {
                    double p = t->v;
                    if (math) {
                        auto r = colorcalc::evaluate(t->s, t->inner, {});
                        if (!r || r->type != colorcalc::Type::Percent) return std::nullopt;
                        p = std::clamp(r->v, 0.0, 100.0);  // calc() clamps to the range
                    }
                    if (pct[k] || p < 0 || p > 100) return std::nullopt;
                    pct[k] = p / 100.0;
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
        if (it != names.end()) return fromBytes(it->second);
        auto& sys = systemColors();
        auto st = sys.find(t.s);
        if (st == sys.end()) return std::nullopt;
        return fromBytes(ctx.dark ? st->second.dark : st->second.light);
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

bool tryParseColor(const std::string& value, Color& out, const ColorContext& context) {
    std::string lower = value;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    Ctx ctx;
    ctx.current = context.currentColor;
    ctx.dark = context.scheme == ColorScheme::Dark;
    auto v = parseColorVal(lower, ctx, 0);
    if (!v) return false;
    double rgb[3], a;
    colorspace::toOutputSRGB(*v, rgb, a);
    out = {toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2]), toByte(a)};
    return true;
}

bool tryParseColor(const std::string& value, Color& out, const Color& currentColor) {
    return tryParseColor(value, out, ColorContext{currentColor, ColorScheme::Light});
}

Color parseColor(const std::string& value, const ColorContext& context) {
    Color c{0, 0, 0, 0};
    if (!tryParseColor(value, c, context)) return {0, 0, 0, 0};
    return c;
}

Color parseColor(const std::string& value, const Color& currentColor) {
    return parseColor(value, ColorContext{currentColor, ColorScheme::Light});
}

ColorScheme usedColorScheme(std::string_view colorSchemeValue, ColorScheme preferred) {
    // color-scheme: normal | [ light | dark | <custom-ident> ]+ && only?
    // Unknown idents are ignored; with no supported scheme listed (`normal`
    // included) the element uses the UA default, light. Otherwise the preferred
    // scheme when it is listed, else the first listed one.
    bool light = false, dark = false;
    std::optional<ColorScheme> first;
    size_t i = 0, n = colorSchemeValue.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(colorSchemeValue[i]))) i++;
        size_t s = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(colorSchemeValue[i]))) i++;
        std::string word(colorSchemeValue.substr(s, i - s));
        for (auto& c : word) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (word == "light") {
            light = true;
            if (!first) first = ColorScheme::Light;
        } else if (word == "dark") {
            dark = true;
            if (!first) first = ColorScheme::Dark;
        }
    }
    if (!first) return ColorScheme::Light;
    if ((preferred == ColorScheme::Dark && dark) || (preferred == ColorScheme::Light && light))
        return preferred;
    return *first;
}

Color parseColor(const std::string& value) {
    return parseColor(value, Color{0, 0, 0, 255});
}

} // namespace htmlayout::css
