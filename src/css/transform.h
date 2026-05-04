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

// 4x4 transformation matrix in column-major form, matching CSS Transforms 2
// and Skia's SkM44. Element layout: m[col*4 + row]:
//   m[ 0]  m[ 4]  m[ 8]  m[12]      column 0..3
//   m[ 1]  m[ 5]  m[ 9]  m[13]
//   m[ 2]  m[ 6]  m[10]  m[14]
//   m[ 3]  m[ 7]  m[11]  m[15]
// Multiplies vectors as (x,y,z,w)^T on the right.
struct Matrix3D {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Matrix3D identity() { return Matrix3D{}; }
    bool isIdentity() const;

    // result = lhs * rhs. Standard column-major matrix multiply.
    Matrix3D operator*(const Matrix3D& r) const;

    // Apply to a 4D point (x,y,z,w). Caller can use w=1 for points or do their own.
    void mapPoint4(float x, float y, float z, float w,
                   float& ox, float& oy, float& oz, float& ow) const;

    // Apply to a 3D point with w=1. Returns post-divide screen coords (x/w, y/w).
    void project2D(float x, float y, float z,
                   float& ox, float& oy) const;

    // True if the matrix has only 2D-affine components (third row/col = identity,
    // no perspective). Useful to fast-path the existing 2D rendering pipeline.
    bool is2D() const;

    // If is2D() returns true, fill out a Matrix2D representation.
    Matrix2D to2D() const;
};

// Parse a CSS `transform` value into a 2D matrix. Supports the 2D function set
// only — translate/translateX/translateY (px and %), scale/scaleX/scaleY,
// rotate, skew/skewX/skewY (deg/rad/turn/grad), and matrix(a,b,c,d,e,f).
// Multiple functions compose left-to-right.
//
// Percentages in translate() resolve against (refW, refH) — per CSS spec, the
// element's own border-box size.
//
// Returns the identity matrix for "none", empty strings, or parse failure.
Matrix2D parseTransform(std::string_view val, float refW, float refH);

// Parse a CSS `transform` value into a 4x4 matrix. Supports the full 2D set
// above PLUS 3D functions: translateZ, translate3d, scaleZ, scale3d,
// rotateX, rotateY, rotateZ, rotate3d, perspective, matrix3d.
//
// Percentages in translate/translate3d resolve against (refW, refH); z values
// must be lengths (no percentages permitted by spec).
Matrix3D parseTransform3D(std::string_view val, float refW, float refH);

// Parse a CSS `transform-origin` value into pixel offsets relative to the
// border-box top-left corner. Supports px values, percentages (resolved
// against refW/refH), and the keywords left/center/right/top/bottom.
// Defaults to (refW/2, refH/2) if the value is empty.
void parseTransformOrigin(std::string_view val, float refW, float refH,
                           float& ox, float& oy);

// Parse a CSS `transform-origin` value with optional third (z) component.
// z defaults to 0; percentages are not permitted on z (treated as px).
void parseTransformOrigin3D(std::string_view val, float refW, float refH,
                             float& ox, float& oy, float& oz);

// Parse a CSS `perspective` value (length or "none"). Returns 0 for none.
// Used to build the perspective matrix applied to a perspective container's
// children: P = T(po_x,po_y,0) * persp(d) * T(-po_x,-po_y,0), where
// persp(d) is identity except m[11] = -1/d.
float parsePerspective(std::string_view val);

// Build a CSS perspective matrix for distance d. Returns identity if d<=0.
Matrix3D makePerspectiveMatrix(float d);

} // namespace htmlayout::css
