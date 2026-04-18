#include "css/transform.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace htmlayout::css {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Skip spaces, tabs, and commas.
void skipSep(std::string_view s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == ','))
        ++pos;
}

// Parse a number. On return, pos points at the character after the number
// (not past the unit). Returns 0 on failure.
float parseNumber(std::string_view s, size_t& pos) {
    skipSep(s, pos);
    if (pos >= s.size()) return 0.0f;
    char* end = nullptr;
    // strtof needs a null-terminated string; std::string_view into a std::string
    // is null-terminated just past .size(), but we can't assume that here.
    // Copy the remaining tail into a small buffer.
    std::string tail(s.substr(pos));
    float v = std::strtof(tail.c_str(), &end);
    if (end == tail.c_str()) return 0.0f;
    pos += static_cast<size_t>(end - tail.c_str());
    return v;
}

// Parse a length: number with optional "px" or "%" suffix. Percentages resolve
// against `ref`.
float parseLength(std::string_view s, size_t& pos, float ref) {
    float v = parseNumber(s, pos);
    // Skip alphabetic unit (px, em, ...) — only px is treated as the identity
    // here; other units aren't meaningful in transforms.
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos])))
        ++pos;
    if (pos < s.size() && s[pos] == '%') {
        ++pos;
        return v * 0.01f * ref;
    }
    return v;
}

// Parse an angle value. Defaults to degrees if no unit.
float parseAngleRad(std::string_view s, size_t& pos) {
    float v = parseNumber(s, pos);
    std::string unit;
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos])))
        unit += s[pos++];
    if (unit == "rad")  return v;
    if (unit == "turn") return v * 2.0f * kPi;
    if (unit == "grad") return v * kPi / 200.0f;
    return v * kPi / 180.0f; // deg (default)
}

// After parsing a function's argument list, advance pos past the closing ')'.
void skipToCloseParen(std::string_view s, size_t& pos) {
    while (pos < s.size() && s[pos] != ')') ++pos;
    if (pos < s.size()) ++pos;
}

// Peek whether the next non-separator char is the closing ')'.
bool nextIsClose(std::string_view s, size_t pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == ','))
        ++pos;
    return pos < s.size() && s[pos] == ')';
}

} // namespace

bool Matrix2D::invert(Matrix2D& out) const {
    float det = a * d - b * c;
    if (std::abs(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    out.a =  d * inv;
    out.b = -b * inv;
    out.c = -c * inv;
    out.d =  a * inv;
    out.e = (c * f - d * e) * inv;
    out.f = (b * e - a * f) * inv;
    return true;
}

Matrix2D parseTransform(std::string_view val, float refW, float refH) {
    Matrix2D result;
    if (val.empty() || val == "none") return result;

    size_t pos = 0;
    while (pos < val.size()) {
        // Skip leading whitespace
        while (pos < val.size() && (val[pos] == ' ' || val[pos] == '\t'))
            ++pos;
        if (pos >= val.size()) break;

        // Parse function name
        size_t nameStart = pos;
        while (pos < val.size() && val[pos] != '(' && val[pos] != ' ')
            ++pos;
        std::string_view func = val.substr(nameStart, pos - nameStart);
        if (pos >= val.size() || val[pos] != '(') break;
        ++pos; // past '('

        Matrix2D m;
        if (func == "translate") {
            m.e = parseLength(val, pos, refW);
            if (!nextIsClose(val, pos))
                m.f = parseLength(val, pos, refH);
        } else if (func == "translateX") {
            m.e = parseLength(val, pos, refW);
        } else if (func == "translateY") {
            m.f = parseLength(val, pos, refH);
        } else if (func == "scale") {
            m.a = parseNumber(val, pos);
            m.d = m.a;
            if (!nextIsClose(val, pos))
                m.d = parseNumber(val, pos);
        } else if (func == "scaleX") {
            m.a = parseNumber(val, pos);
        } else if (func == "scaleY") {
            m.d = parseNumber(val, pos);
        } else if (func == "rotate") {
            float rad = parseAngleRad(val, pos);
            float cosA = std::cos(rad), sinA = std::sin(rad);
            m.a = cosA; m.c = -sinA;
            m.b = sinA; m.d =  cosA;
        } else if (func == "skewX") {
            m.c = std::tan(parseAngleRad(val, pos));
        } else if (func == "skewY") {
            m.b = std::tan(parseAngleRad(val, pos));
        } else if (func == "skew") {
            m.c = std::tan(parseAngleRad(val, pos));
            if (!nextIsClose(val, pos))
                m.b = std::tan(parseAngleRad(val, pos));
        } else if (func == "matrix") {
            m.a = parseNumber(val, pos);
            m.b = parseNumber(val, pos);
            m.c = parseNumber(val, pos);
            m.d = parseNumber(val, pos);
            m.e = parseNumber(val, pos);
            m.f = parseNumber(val, pos);
        }
        // else: unknown function — skip silently

        skipToCloseParen(val, pos);
        result = result * m;
    }
    return result;
}

void parseTransformOrigin(std::string_view val, float refW, float refH,
                           float& ox, float& oy) {
    ox = refW * 0.5f;
    oy = refH * 0.5f;
    if (val.empty()) return;

    // Parse up to two tokens. Each is either:
    //   - a keyword (left/center/right/top/bottom)
    //   - a length or percentage
    auto parseToken = [&](size_t& pos, bool isX, float& out) {
        skipSep(val, pos);
        if (pos >= val.size()) return false;

        // Try keyword
        size_t ks = pos;
        while (pos < val.size() && std::isalpha(static_cast<unsigned char>(val[pos])))
            ++pos;
        std::string_view kw = val.substr(ks, pos - ks);
        if (!kw.empty()) {
            if      (kw == "left"   || kw == "top")    out = 0.0f;
            else if (kw == "center")                   out = (isX ? refW : refH) * 0.5f;
            else if (kw == "right"  || kw == "bottom") out = (isX ? refW : refH);
            else { /* unknown keyword, ignore */ }
            return true;
        }

        // Fall back to numeric length
        out = parseLength(val, pos, isX ? refW : refH);
        return true;
    };

    size_t pos = 0;
    parseToken(pos, /*isX=*/true, ox);
    if (pos < val.size())
        parseToken(pos, /*isX=*/false, oy);
}

} // namespace htmlayout::css
