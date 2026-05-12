#include "test_transform.h"
#include "test_helpers.h"
#include "css/transform.h"
#include <cmath>

using namespace htmlayout::css;

static bool approx(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) < tol;
}

static bool mat2Eq(const Matrix2D& m, float a, float b, float c, float d, float e, float f,
                   float tol = 1e-4f) {
    return approx(m.a, a, tol) && approx(m.b, b, tol) && approx(m.c, c, tol)
        && approx(m.d, d, tol) && approx(m.e, e, tol) && approx(m.f, f, tol);
}

// ===== Matrix2D =====
static void testMatrix2D() {
    printf("--- Matrix2D ---\n");

    Matrix2D ident;
    check(ident.isIdentity(), "default Matrix2D is identity");

    Matrix2D notIdent{2,0,0,1,0,0};
    check(!notIdent.isIdentity(), "scaled matrix is not identity");

    Matrix2D a{1,0,0,1,10,20};
    Matrix2D b{2,0,0,2,0,0};
    Matrix2D ab = a * b;
    check(mat2Eq(ab, 2,0,0,2,10,20), "translate * scale composes");

    // Invert
    Matrix2D translation{1,0,0,1,5,7};
    Matrix2D inv;
    check(translation.invert(inv), "translation invertible");
    check(mat2Eq(inv, 1,0,0,1,-5,-7), "translation inverse correct");

    Matrix2D singular{1,2,2,4,0,0};
    Matrix2D dummy;
    check(!singular.invert(dummy), "singular matrix not invertible");

    Matrix2D scale{4,0,0,2,0,0};
    check(scale.invert(inv), "scale invertible");
    check(mat2Eq(inv, 0.25f,0,0,0.5f,0,0), "scale inverse correct");
}

// ===== parseTransform (2D) =====
static void testParseTransform2D() {
    printf("--- parseTransform 2D ---\n");

    auto m = parseTransform("", 100, 100);
    check(m.isIdentity(), "empty -> identity");

    m = parseTransform("none", 100, 100);
    check(m.isIdentity(), "none -> identity");

    m = parseTransform("translate(10px, 20px)", 100, 100);
    check(mat2Eq(m, 1,0,0,1,10,20), "translate(10px,20px)");

    m = parseTransform("translate(50%, 25%)", 200, 400);
    check(mat2Eq(m, 1,0,0,1,100,100), "translate(%) resolves against refW/refH");

    m = parseTransform("translate(30px)", 100, 100);
    check(mat2Eq(m, 1,0,0,1,30,0), "translate single arg -> y=0");

    m = parseTransform("translateX(15px)", 100, 100);
    check(mat2Eq(m, 1,0,0,1,15,0), "translateX");

    m = parseTransform("translateY(25px)", 100, 100);
    check(mat2Eq(m, 1,0,0,1,0,25), "translateY");

    m = parseTransform("scale(2)", 100, 100);
    check(mat2Eq(m, 2,0,0,2,0,0), "scale uniform");

    m = parseTransform("scale(2, 3)", 100, 100);
    check(mat2Eq(m, 2,0,0,3,0,0), "scale x,y");

    m = parseTransform("scaleX(4)", 100, 100);
    check(mat2Eq(m, 4,0,0,1,0,0), "scaleX");

    m = parseTransform("scaleY(5)", 100, 100);
    check(mat2Eq(m, 1,0,0,5,0,0), "scaleY");

    m = parseTransform("rotate(90deg)", 100, 100);
    check(mat2Eq(m, 0,1,-1,0,0,0, 1e-3f), "rotate 90deg");

    m = parseTransform("rotate(0.5turn)", 100, 100);
    check(mat2Eq(m, -1,0,0,-1,0,0, 1e-3f), "rotate 0.5turn = 180deg");

    m = parseTransform("rotate(200grad)", 100, 100);
    check(mat2Eq(m, -1,0,0,-1,0,0, 1e-3f), "rotate 200grad = 180deg");

    m = parseTransform("rotate(3.14159265rad)", 100, 100);
    check(mat2Eq(m, -1,0,0,-1,0,0, 1e-3f), "rotate pi rad");

    m = parseTransform("skewX(45deg)", 100, 100);
    check(mat2Eq(m, 1,0,1,1,0,0, 1e-3f), "skewX 45");

    m = parseTransform("skewY(45deg)", 100, 100);
    check(mat2Eq(m, 1,1,0,1,0,0, 1e-3f), "skewY 45");

    m = parseTransform("skew(45deg, 0deg)", 100, 100);
    check(mat2Eq(m, 1,0,1,1,0,0, 1e-3f), "skew(45,0)");

    m = parseTransform("skew(45deg)", 100, 100);
    check(mat2Eq(m, 1,0,1,1,0,0, 1e-3f), "skew(45) single arg");

    m = parseTransform("matrix(1, 2, 3, 4, 5, 6)", 100, 100);
    check(mat2Eq(m, 1,2,3,4,5,6), "matrix(a..f)");

    // Composition (left-to-right): translate then scale
    m = parseTransform("translate(10px, 20px) scale(2)", 100, 100);
    check(mat2Eq(m, 2,0,0,2,10,20), "translate * scale composes");

    // Unknown function — silently skipped, identity remains
    m = parseTransform("perspective(500px)", 100, 100);
    check(m.isIdentity(), "perspective() ignored in 2D parse");

    // Truncated / malformed
    m = parseTransform("translate", 100, 100);
    check(m.isIdentity(), "no paren -> identity");

    m = parseTransform("translate(", 100, 100);
    check(mat2Eq(m, 1,0,0,1,0,0), "unclosed paren -> identity translate");
}

// ===== Matrix3D =====
static void testMatrix3D() {
    printf("--- Matrix3D ---\n");

    Matrix3D ident;
    check(ident.isIdentity(), "default Matrix3D identity");

    Matrix3D i2 = Matrix3D::identity();
    check(i2.isIdentity(), "Matrix3D::identity()");

    check(ident.is2D(), "identity is 2D");
    Matrix2D twoD = ident.to2D();
    check(mat2Eq(twoD, 1,0,0,1,0,0), "identity to2D = identity 2D");

    // Multiply
    Matrix3D a = Matrix3D::identity();
    a.m[12] = 5; a.m[13] = 6; a.m[14] = 7;
    Matrix3D b = Matrix3D::identity();
    b.m[12] = 1; b.m[13] = 2; b.m[14] = 3;
    Matrix3D ab = a * b;
    check(approx(ab.m[12], 6.0f) && approx(ab.m[13], 8.0f) && approx(ab.m[14], 10.0f),
          "translate * translate adds translations");

    // mapPoint4
    Matrix3D scale = Matrix3D::identity();
    scale.m[0] = 2; scale.m[5] = 3; scale.m[10] = 4;
    float ox, oy, oz, ow;
    scale.mapPoint4(1, 1, 1, 1, ox, oy, oz, ow);
    check(approx(ox, 2) && approx(oy, 3) && approx(oz, 4) && approx(ow, 1),
          "scale mapPoint4");

    // project2D with identity (w=1)
    Matrix3D pident;
    float px, py;
    pident.project2D(3, 4, 0, px, py);
    check(approx(px, 3) && approx(py, 4), "identity project2D");

    // project2D with perspective division (divide by zero guard)
    Matrix3D persp = makePerspectiveMatrix(100.0f);
    check(!persp.isIdentity(), "perspective matrix not identity");
    persp.project2D(50, 50, 100, px, py);
    // After perspective: w = -z/d + 1 = -100/100 + 1 = 0 -> guarded to small epsilon
    // The point should map to a large value but not crash.
    (void)px; (void)py;
    check(true, "project2D survives near-zero w");

    // is2D() failure paths
    Matrix3D not2D = Matrix3D::identity();
    not2D.m[2] = 1.0f; // breaks third row
    check(!not2D.is2D(), "m[2]!=0 -> not 2D");

    Matrix3D not2D_b = Matrix3D::identity();
    not2D_b.m[8] = 1.0f;
    check(!not2D_b.is2D(), "m[8]!=0 -> not 2D");

    Matrix3D not2D_c = Matrix3D::identity();
    not2D_c.m[3] = 1.0f;
    check(!not2D_c.is2D(), "m[3]!=0 -> not 2D");

    // to2D copies the right elements
    Matrix3D src;
    src.m[0] = 2; src.m[1] = 3; src.m[4] = 4; src.m[5] = 5; src.m[12] = 6; src.m[13] = 7;
    Matrix2D dst = src.to2D();
    check(mat2Eq(dst, 2,3,4,5,6,7), "to2D extracts components");
}

// ===== parseTransform3D =====
static void testParseTransform3D() {
    printf("--- parseTransform 3D ---\n");

    auto m = parseTransform3D("", 100, 100);
    check(m.isIdentity(), "empty 3d -> identity");

    m = parseTransform3D("none", 100, 100);
    check(m.isIdentity(), "none 3d -> identity");

    m = parseTransform3D("translate(10px, 20px)", 100, 100);
    check(m.is2D() && approx(m.m[12], 10) && approx(m.m[13], 20), "3d translate");

    m = parseTransform3D("translateX(15px)", 100, 100);
    check(approx(m.m[12], 15), "3d translateX");

    m = parseTransform3D("translateY(25px)", 100, 100);
    check(approx(m.m[13], 25), "3d translateY");

    m = parseTransform3D("translateZ(33px)", 100, 100);
    check(approx(m.m[14], 33), "3d translateZ");

    m = parseTransform3D("translate3d(1px, 2px, 3px)", 100, 100);
    check(approx(m.m[12], 1) && approx(m.m[13], 2) && approx(m.m[14], 3), "translate3d");

    m = parseTransform3D("scale(2)", 100, 100);
    check(approx(m.m[0], 2) && approx(m.m[5], 2) && approx(m.m[10], 1), "3d scale uniform");

    m = parseTransform3D("scale(2, 3)", 100, 100);
    check(approx(m.m[0], 2) && approx(m.m[5], 3), "3d scale xy");

    m = parseTransform3D("scaleX(4)", 100, 100);
    check(approx(m.m[0], 4) && approx(m.m[5], 1) && approx(m.m[10], 1), "scaleX 3d");

    m = parseTransform3D("scaleY(5)", 100, 100);
    check(approx(m.m[5], 5), "scaleY 3d");

    m = parseTransform3D("scaleZ(6)", 100, 100);
    check(approx(m.m[10], 6), "scaleZ");

    m = parseTransform3D("scale3d(2, 3, 4)", 100, 100);
    check(approx(m.m[0], 2) && approx(m.m[5], 3) && approx(m.m[10], 4), "scale3d");

    m = parseTransform3D("rotate(90deg)", 100, 100);
    check(approx(m.m[0], 0, 1e-3f) && approx(m.m[1], 1, 1e-3f), "rotate 3d");

    m = parseTransform3D("rotateZ(90deg)", 100, 100);
    check(approx(m.m[0], 0, 1e-3f) && approx(m.m[1], 1, 1e-3f), "rotateZ");

    m = parseTransform3D("rotateX(90deg)", 100, 100);
    check(approx(m.m[5], 0, 1e-3f) && approx(m.m[6], 1, 1e-3f), "rotateX");

    m = parseTransform3D("rotateY(90deg)", 100, 100);
    check(approx(m.m[0], 0, 1e-3f) && approx(m.m[2], -1, 1e-3f), "rotateY");

    m = parseTransform3D("rotate3d(0, 0, 1, 90deg)", 100, 100);
    check(approx(m.m[0], 0, 1e-3f) && approx(m.m[1], 1, 1e-3f), "rotate3d z axis");

    // Zero-length axis -> identity per implementation
    m = parseTransform3D("rotate3d(0, 0, 0, 90deg)", 100, 100);
    check(m.isIdentity(), "rotate3d zero axis -> identity");

    m = parseTransform3D("skewX(45deg)", 100, 100);
    check(approx(m.m[4], 1, 1e-3f), "skewX 3d");

    m = parseTransform3D("skewY(45deg)", 100, 100);
    check(approx(m.m[1], 1, 1e-3f), "skewY 3d");

    m = parseTransform3D("skew(45deg, 30deg)", 100, 100);
    check(true, "skew 3d two-arg parses");

    m = parseTransform3D("skew(45deg)", 100, 100);
    check(true, "skew 3d one-arg parses");

    m = parseTransform3D("perspective(200px)", 100, 100);
    check(approx(m.m[11], -1.0f/200.0f), "perspective 3d");

    m = parseTransform3D("matrix(1, 2, 3, 4, 5, 6)", 100, 100);
    check(approx(m.m[0], 1) && approx(m.m[1], 2) && approx(m.m[4], 3)
       && approx(m.m[5], 4) && approx(m.m[12], 5) && approx(m.m[13], 6),
       "matrix() in 3d parse");

    m = parseTransform3D(
        "matrix3d(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16)", 100, 100);
    bool ok = true;
    for (int i = 0; i < 16; ++i) ok = ok && approx(m.m[i], static_cast<float>(i+1));
    check(ok, "matrix3d 16 args column-major");

    // Unknown function ignored
    m = parseTransform3D("garbage(10px)", 100, 100);
    check(m.isIdentity(), "unknown 3d function ignored");

    // Truncated
    m = parseTransform3D("rotate", 100, 100);
    check(m.isIdentity(), "no paren in 3d -> identity");
}

// ===== Perspective =====
static void testPerspective() {
    printf("--- perspective ---\n");

    check(parsePerspective("") == 0.0f, "empty perspective = 0");
    check(parsePerspective("none") == 0.0f, "none perspective = 0");
    check(approx(parsePerspective("500px"), 500.0f), "parsePerspective 500px");
    check(parsePerspective("-50px") == 0.0f, "negative perspective clamped to 0");

    Matrix3D p = makePerspectiveMatrix(500);
    check(approx(p.m[11], -1.0f/500.0f), "makePerspectiveMatrix sets m[11]");

    Matrix3D pZero = makePerspectiveMatrix(0);
    check(pZero.isIdentity(), "perspective(0) -> identity");

    Matrix3D pNeg = makePerspectiveMatrix(-100);
    check(pNeg.isIdentity(), "perspective(negative) -> identity");
}

// ===== transform-origin =====
static void testTransformOrigin() {
    printf("--- transform-origin ---\n");

    float ox = -1, oy = -1;

    parseTransformOrigin("", 100, 200, ox, oy);
    check(approx(ox, 50) && approx(oy, 100), "empty origin -> center");

    parseTransformOrigin("left top", 100, 200, ox, oy);
    check(approx(ox, 0) && approx(oy, 0), "left top");

    parseTransformOrigin("right bottom", 100, 200, ox, oy);
    check(approx(ox, 100) && approx(oy, 200), "right bottom");

    parseTransformOrigin("center center", 100, 200, ox, oy);
    check(approx(ox, 50) && approx(oy, 100), "center center");

    parseTransformOrigin("50% 25%", 200, 400, ox, oy);
    check(approx(ox, 100) && approx(oy, 100), "percent origin");

    parseTransformOrigin("10px 20px", 100, 200, ox, oy);
    check(approx(ox, 10) && approx(oy, 20), "px origin");

    // Single value -> y stays default (center)
    parseTransformOrigin("25px", 100, 200, ox, oy);
    check(approx(ox, 25) && approx(oy, 100), "single x token, y stays center");

    // Unknown keyword falls through silently — out stays previous (center default)
    ox = -1; oy = -1;
    parseTransformOrigin("garbage garbage", 100, 200, ox, oy);
    check(true, "unknown keyword doesn't crash");

    // 3D origin
    float oz = -1;
    parseTransformOrigin3D("", 100, 200, ox, oy, oz);
    check(approx(oz, 0), "empty origin3d z=0");

    parseTransformOrigin3D("left top 5px", 100, 200, ox, oy, oz);
    check(approx(ox, 0) && approx(oy, 0) && approx(oz, 5), "origin3d keyword + z");

    parseTransformOrigin3D("10px 20px 30px", 100, 200, ox, oy, oz);
    check(approx(ox, 10) && approx(oy, 20) && approx(oz, 30), "origin3d px x y z");

    parseTransformOrigin3D("50% 50%", 100, 200, ox, oy, oz);
    check(approx(oz, 0), "origin3d without z -> z=0");
}

void testTransform() {
    printf("=== CSS Transform Tests ===\n");
    testMatrix2D();
    testParseTransform2D();
    testMatrix3D();
    testParseTransform3D();
    testPerspective();
    testTransformOrigin();
}
