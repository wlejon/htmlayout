#pragma once
#include <string>
#include <string_view>

namespace htmlayout::css {

// 2D affine transformation matrix in column-major form:
//   | a  c  e |
//   | b  d  f |
//   | 0  0  1 |
// Maps point (x, y) to (a*x + c*y + e, b*x + d*y + f).
struct Matrix2D {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    bool isIdentity() const {
        return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0;
    }

    // Standard 2D affine composition: result = lhs * rhs.
    Matrix2D operator*(const Matrix2D& r) const {
        return { a*r.a + c*r.b, b*r.a + d*r.b,
                 a*r.c + c*r.d, b*r.c + d*r.d,
                 a*r.e + c*r.f + e, b*r.e + d*r.f + f };
    }

    // Invert. Returns false if the matrix is singular (determinant ≈ 0).
    bool invert(Matrix2D& out) const;
};

// Parse a CSS `transform` value. Supports translate/translateX/translateY
// (px and %), scale/scaleX/scaleY, rotate, skew/skewX/skewY (deg/rad/turn/grad),
// and matrix(a,b,c,d,e,f). Multiple functions compose left-to-right.
//
// Percentages in translate() resolve against (refW, refH) — per CSS spec, the
// element's own border-box size.
//
// Returns the identity matrix for "none", empty strings, or parse failure.
Matrix2D parseTransform(std::string_view val, float refW, float refH);

// Parse a CSS `transform-origin` value into pixel offsets relative to the
// border-box top-left corner. Supports px values, percentages (resolved
// against refW/refH), and the keywords left/center/right/top/bottom.
// Defaults to (refW/2, refH/2) if the value is empty.
void parseTransformOrigin(std::string_view val, float refW, float refH,
                           float& ox, float& oy);

} // namespace htmlayout::css
