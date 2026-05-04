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

// Length without percentage (z-axis lengths in 3D transforms). Skips unit.
float parseLengthPx(std::string_view s, size_t& pos) {
    float v = parseNumber(s, pos);
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos])))
        ++pos;
    if (pos < s.size() && s[pos] == '%') ++pos;
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

// ---- Matrix3D ----

bool Matrix3D::isIdentity() const {
    static constexpr float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int i = 0; i < 16; ++i)
        if (m[i] != I[i]) return false;
    return true;
}

Matrix3D Matrix3D::operator*(const Matrix3D& r) const {
    Matrix3D out;
    // out[col][row] = sum_k lhs[k][row] * rhs[col][k]
    for (int c = 0; c < 4; ++c) {
        for (int rIdx = 0; rIdx < 4; ++rIdx) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) {
                v += m[k*4 + rIdx] * r.m[c*4 + k];
            }
            out.m[c*4 + rIdx] = v;
        }
    }
    return out;
}

void Matrix3D::mapPoint4(float x, float y, float z, float w,
                          float& ox, float& oy, float& oz, float& ow) const {
    ox = m[0]*x + m[4]*y + m[ 8]*z + m[12]*w;
    oy = m[1]*x + m[5]*y + m[ 9]*z + m[13]*w;
    oz = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
    ow = m[3]*x + m[7]*y + m[11]*z + m[15]*w;
}

void Matrix3D::project2D(float x, float y, float z,
                          float& ox, float& oy) const {
    float xx, yy, zz, ww;
    mapPoint4(x, y, z, 1.0f, xx, yy, zz, ww);
    if (std::abs(ww) < 1e-9f) ww = (ww < 0 ? -1e-9f : 1e-9f);
    ox = xx / ww;
    oy = yy / ww;
}

bool Matrix3D::is2D() const {
    // 2D affine layout (column-major): col2 = (0,0,1,0); col0/1 third row=0;
    // col3 third row=0; col0/1/3 fourth row=0; col3 fourth=1.
    if (m[2] != 0 || m[6] != 0 || m[14] != 0) return false;
    if (m[8] != 0 || m[9] != 0 || m[10] != 1 || m[11] != 0) return false;
    if (m[3] != 0 || m[7] != 0 || m[15] != 1) return false;
    return true;
}

Matrix2D Matrix3D::to2D() const {
    Matrix2D r;
    r.a = m[0]; r.b = m[1];
    r.c = m[4]; r.d = m[5];
    r.e = m[12]; r.f = m[13];
    return r;
}

// Helpers for building primitive 4x4 transforms (column-major).
namespace {

Matrix3D mat3D_translate(float tx, float ty, float tz) {
    Matrix3D m;
    m.m[12] = tx; m.m[13] = ty; m.m[14] = tz;
    return m;
}
Matrix3D mat3D_scale(float sx, float sy, float sz) {
    Matrix3D m;
    m.m[0] = sx; m.m[5] = sy; m.m[10] = sz;
    return m;
}
Matrix3D mat3D_rotateZ(float rad) {
    Matrix3D m;
    float c = std::cos(rad), s = std::sin(rad);
    m.m[0] = c;  m.m[1] = s;
    m.m[4] = -s; m.m[5] = c;
    return m;
}
Matrix3D mat3D_rotateX(float rad) {
    Matrix3D m;
    float c = std::cos(rad), s = std::sin(rad);
    m.m[5] = c;  m.m[6] = s;
    m.m[9] = -s; m.m[10] = c;
    return m;
}
Matrix3D mat3D_rotateY(float rad) {
    Matrix3D m;
    float c = std::cos(rad), s = std::sin(rad);
    m.m[0] = c;  m.m[2] = -s;
    m.m[8] = s;  m.m[10] = c;
    return m;
}
Matrix3D mat3D_rotate3d(float ax, float ay, float az, float rad) {
    // Normalize axis
    float len = std::sqrt(ax*ax + ay*ay + az*az);
    if (len < 1e-9f) return Matrix3D::identity();
    ax /= len; ay /= len; az /= len;
    float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
    Matrix3D m;
    m.m[0]  = t*ax*ax + c;       m.m[1]  = t*ax*ay + s*az;    m.m[2]  = t*ax*az - s*ay;
    m.m[4]  = t*ax*ay - s*az;    m.m[5]  = t*ay*ay + c;       m.m[6]  = t*ay*az + s*ax;
    m.m[8]  = t*ax*az + s*ay;    m.m[9]  = t*ay*az - s*ax;    m.m[10] = t*az*az + c;
    return m;
}
Matrix3D mat3D_skewX(float rad) {
    Matrix3D m;
    m.m[4] = std::tan(rad);
    return m;
}
Matrix3D mat3D_skewY(float rad) {
    Matrix3D m;
    m.m[1] = std::tan(rad);
    return m;
}
Matrix3D mat3D_perspective(float d) {
    Matrix3D m;
    if (d > 0) m.m[11] = -1.0f / d;
    return m;
}

} // namespace

Matrix3D makePerspectiveMatrix(float d) {
    return mat3D_perspective(d);
}

float parsePerspective(std::string_view val) {
    if (val.empty() || val == "none") return 0.0f;
    size_t pos = 0;
    float v = parseLengthPx(val, pos);
    return v > 0 ? v : 0.0f;
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
        // else: unknown function — skip silently (3D functions etc.)

        skipToCloseParen(val, pos);
        result = result * m;
    }
    return result;
}

Matrix3D parseTransform3D(std::string_view val, float refW, float refH) {
    Matrix3D result;
    if (val.empty() || val == "none") return result;

    size_t pos = 0;
    while (pos < val.size()) {
        while (pos < val.size() && (val[pos] == ' ' || val[pos] == '\t'))
            ++pos;
        if (pos >= val.size()) break;

        size_t nameStart = pos;
        while (pos < val.size() && val[pos] != '(' && val[pos] != ' ')
            ++pos;
        std::string_view func = val.substr(nameStart, pos - nameStart);
        if (pos >= val.size() || val[pos] != '(') break;
        ++pos; // past '('

        Matrix3D m;
        if (func == "translate") {
            float tx = parseLength(val, pos, refW);
            float ty = 0;
            if (!nextIsClose(val, pos))
                ty = parseLength(val, pos, refH);
            m = mat3D_translate(tx, ty, 0);
        } else if (func == "translateX") {
            m = mat3D_translate(parseLength(val, pos, refW), 0, 0);
        } else if (func == "translateY") {
            m = mat3D_translate(0, parseLength(val, pos, refH), 0);
        } else if (func == "translateZ") {
            m = mat3D_translate(0, 0, parseLengthPx(val, pos));
        } else if (func == "translate3d") {
            float tx = parseLength(val, pos, refW);
            float ty = parseLength(val, pos, refH);
            float tz = parseLengthPx(val, pos);
            m = mat3D_translate(tx, ty, tz);
        } else if (func == "scale") {
            float sx = parseNumber(val, pos);
            float sy = sx;
            if (!nextIsClose(val, pos))
                sy = parseNumber(val, pos);
            m = mat3D_scale(sx, sy, 1);
        } else if (func == "scaleX") {
            m = mat3D_scale(parseNumber(val, pos), 1, 1);
        } else if (func == "scaleY") {
            m = mat3D_scale(1, parseNumber(val, pos), 1);
        } else if (func == "scaleZ") {
            m = mat3D_scale(1, 1, parseNumber(val, pos));
        } else if (func == "scale3d") {
            float sx = parseNumber(val, pos);
            float sy = parseNumber(val, pos);
            float sz = parseNumber(val, pos);
            m = mat3D_scale(sx, sy, sz);
        } else if (func == "rotate" || func == "rotateZ") {
            m = mat3D_rotateZ(parseAngleRad(val, pos));
        } else if (func == "rotateX") {
            m = mat3D_rotateX(parseAngleRad(val, pos));
        } else if (func == "rotateY") {
            m = mat3D_rotateY(parseAngleRad(val, pos));
        } else if (func == "rotate3d") {
            float ax = parseNumber(val, pos);
            float ay = parseNumber(val, pos);
            float az = parseNumber(val, pos);
            float rad = parseAngleRad(val, pos);
            m = mat3D_rotate3d(ax, ay, az, rad);
        } else if (func == "skewX") {
            m = mat3D_skewX(parseAngleRad(val, pos));
        } else if (func == "skewY") {
            m = mat3D_skewY(parseAngleRad(val, pos));
        } else if (func == "skew") {
            float rx = parseAngleRad(val, pos);
            float ry = 0;
            if (!nextIsClose(val, pos))
                ry = parseAngleRad(val, pos);
            // Combined skew: skewX(rx) then skewY(ry).
            m = mat3D_skewY(ry) * mat3D_skewX(rx);
        } else if (func == "perspective") {
            m = mat3D_perspective(parseLengthPx(val, pos));
        } else if (func == "matrix") {
            // matrix(a,b,c,d,e,f) is the 2D shorthand.
            float a = parseNumber(val, pos);
            float b = parseNumber(val, pos);
            float c = parseNumber(val, pos);
            float d = parseNumber(val, pos);
            float e = parseNumber(val, pos);
            float f = parseNumber(val, pos);
            m.m[0]=a; m.m[1]=b; m.m[4]=c; m.m[5]=d; m.m[12]=e; m.m[13]=f;
        } else if (func == "matrix3d") {
            // matrix3d(m00, m01, ..., m33) — 16 numbers in column-major order
            // exactly matching our internal layout.
            for (int i = 0; i < 16; ++i)
                m.m[i] = parseNumber(val, pos);
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

void parseTransformOrigin3D(std::string_view val, float refW, float refH,
                             float& ox, float& oy, float& oz) {
    parseTransformOrigin(val, refW, refH, ox, oy);
    oz = 0.0f;
    if (val.empty()) return;
    // Skip past the first two tokens to find the optional third (z) token.
    size_t pos = 0;
    int tokens = 0;
    while (pos < val.size() && tokens < 2) {
        skipSep(val, pos);
        if (pos >= val.size()) break;
        // Skip a single token (keyword or number+unit).
        if (std::isalpha(static_cast<unsigned char>(val[pos]))) {
            while (pos < val.size() && std::isalpha(static_cast<unsigned char>(val[pos])))
                ++pos;
        } else {
            (void)parseNumber(val, pos);
            while (pos < val.size() && std::isalpha(static_cast<unsigned char>(val[pos])))
                ++pos;
            if (pos < val.size() && val[pos] == '%') ++pos;
        }
        ++tokens;
    }
    if (pos < val.size()) {
        skipSep(val, pos);
        if (pos < val.size())
            oz = parseLengthPx(val, pos);
    }
}

} // namespace htmlayout::css
