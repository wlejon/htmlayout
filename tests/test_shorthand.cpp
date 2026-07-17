#include "test_shorthand.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"
#include "css/properties.h"

using namespace htmlayout::css;

static void testMargin() {
    printf("--- Shorthand: margin ---\n");
    auto r1 = expandShorthand("margin", "10px");
    check(r1.size() == 4, "margin 1-value -> 4 longhands");
    check(r1[0].property == "margin-top" && r1[0].value == "10px", "margin 1v: top");
    check(r1[1].property == "margin-right" && r1[1].value == "10px", "margin 1v: right");
    check(r1[2].property == "margin-bottom" && r1[2].value == "10px", "margin 1v: bottom");
    check(r1[3].property == "margin-left" && r1[3].value == "10px", "margin 1v: left");

    auto r2 = expandShorthand("margin", "10px 20px");
    check(r2[0].value == "10px" && r2[1].value == "20px", "margin 2v: top/right");
    check(r2[2].value == "10px" && r2[3].value == "20px", "margin 2v: bottom/left");

    auto r3 = expandShorthand("margin", "10px 20px 30px");
    check(r3[0].value == "10px", "margin 3v: top=10px");
    check(r3[1].value == "20px" && r3[3].value == "20px", "margin 3v: right=left=20px");
    check(r3[2].value == "30px", "margin 3v: bottom=30px");

    auto r4 = expandShorthand("margin", "1px 2px 3px 4px");
    check(r4[0].value == "1px", "margin 4v: top=1px");
    check(r4[1].value == "2px", "margin 4v: right=2px");
    check(r4[2].value == "3px", "margin 4v: bottom=3px");
    check(r4[3].value == "4px", "margin 4v: left=4px");

    auto ra = expandShorthand("margin", "0 auto");
    check(ra[0].value == "0" && ra[1].value == "auto", "margin: 0 auto");
}

static void testPadding() {
    printf("--- Shorthand: padding ---\n");
    auto r = expandShorthand("padding", "5px 10px");
    check(r.size() == 4, "padding -> 4 longhands");
    check(r[0].property == "padding-top" && r[0].value == "5px", "padding-top");
    check(r[1].property == "padding-right" && r[1].value == "10px", "padding-right");
}

static void testBorder() {
    printf("--- Shorthand: border ---\n");
    auto r = expandShorthand("border", "1px solid black");
    check(r.size() == 13, "border -> 13 longhands (4 sides x 3 properties + border-image reset)");
    check(r[0].property == "border-top-width" && r[0].value == "1px", "border-top-width");
    check(r[1].property == "border-top-style" && r[1].value == "solid", "border-top-style");
    check(r[2].property == "border-top-color" && r[2].value == "black", "border-top-color");
    check(r[3].property == "border-right-width" && r[3].value == "1px", "border-right-width");
    check(r[12].property == "border-image-source" && r[12].value == "none",
          "border resets border-image-source");
}

static void testBorderImage() {
    printf("--- Shorthand: border-image ---\n");

    // Helper: find a longhand's value in the expansion.
    auto get = [](const std::vector<ExpandedDecl>& r, const char* prop) -> std::string {
        for (auto& d : r)
            if (d.property == prop) return d.value;
        return "<missing>";
    };

    // Full slash form: source slice fill / width / outset repeat.
    auto r1 = expandShorthand("border-image", "url(nine.png) 30 fill / 20px / 5 round space");
    check(r1.size() == 5, "border-image -> 5 longhands");
    check(get(r1, "border-image-source") == "url(nine.png)", "bi full: source");
    check(get(r1, "border-image-slice") == "30 fill", "bi full: slice keeps fill");
    check(get(r1, "border-image-width") == "20px", "bi full: width");
    check(get(r1, "border-image-outset") == "5", "bi full: outset");
    check(get(r1, "border-image-repeat") == "round space", "bi full: two repeats");

    // Source only: everything else resets to its initial value.
    auto r2 = expandShorthand("border-image", "url(x.png)");
    check(get(r2, "border-image-source") == "url(x.png)", "bi source-only: source");
    check(get(r2, "border-image-slice") == "100%", "bi source-only: slice initial");
    check(get(r2, "border-image-width") == "1", "bi source-only: width initial");
    check(get(r2, "border-image-outset") == "0", "bi source-only: outset initial");
    check(get(r2, "border-image-repeat") == "stretch", "bi source-only: repeat initial");

    // none resets the source (and the shorthand resets the rest).
    auto r3 = expandShorthand("border-image", "none");
    check(get(r3, "border-image-source") == "none", "bi none: source none");
    check(get(r3, "border-image-slice") == "100%", "bi none: slice initial");

    // Slice + repeat without any slash; repeat keyword is not eaten by slice.
    auto r4 = expandShorthand("border-image", "url(x.png) 25% repeat");
    check(get(r4, "border-image-slice") == "25%", "bi: percentage slice");
    check(get(r4, "border-image-repeat") == "repeat", "bi: single repeat");
    check(get(r4, "border-image-width") == "1", "bi: width stays initial");

    // Order freedom: repeat keywords before the source.
    auto r5 = expandShorthand("border-image", "stretch round url(x.png) 10 20 30 40");
    check(get(r5, "border-image-source") == "url(x.png)", "bi reordered: source");
    check(get(r5, "border-image-slice") == "10 20 30 40", "bi reordered: 4-value slice");
    check(get(r5, "border-image-repeat") == "stretch round", "bi reordered: repeats");

    // width `auto` and slashes without surrounding spaces.
    auto r6 = expandShorthand("border-image", "url(x.png) 10/auto/2px stretch");
    check(get(r6, "border-image-slice") == "10", "bi tight slash: slice");
    check(get(r6, "border-image-width") == "auto", "bi tight slash: width auto");
    check(get(r6, "border-image-outset") == "2px", "bi tight slash: outset");
    check(get(r6, "border-image-repeat") == "stretch", "bi tight slash: repeat");

    // url with a path slash must not be treated as a group separator.
    auto r7 = expandShorthand("border-image", "url(img/nine.png) 30 / 10px");
    check(get(r7, "border-image-source") == "url(img/nine.png)", "bi: slash inside url kept");
    check(get(r7, "border-image-width") == "10px", "bi: width after slash");

    // Gradient source is recognized as a source token.
    auto r8 = expandShorthand("border-image", "linear-gradient(red, blue) 30");
    check(get(r8, "border-image-source") == "linear-gradient(red, blue)", "bi: gradient source");
    check(get(r8, "border-image-slice") == "30", "bi gradient: slice");

    // Longhands pass through unexpanded (registry knows them).
    auto r9 = expandShorthand("border-image-slice", "30 fill");
    check(r9.size() == 1 && r9[0].property == "border-image-slice" && r9[0].value == "30 fill",
          "border-image-slice longhand passes through");
    check(initialValue("border-image-slice") == "100%", "slice initial = 100%");
    check(initialValue("border-image-width") == "1", "width initial = 1");
    check(initialValue("border-image-outset") == "0", "outset initial = 0");
    check(initialValue("border-image-repeat") == "stretch", "repeat initial = stretch");
    check(initialValue("border-image-source") == "none", "source initial = none");
    check(!isInherited("border-image-source"), "border-image-source not inherited");
    check(!isInherited("border-image-slice"), "border-image-slice not inherited");
}

static void testBorderSide() {
    printf("--- Shorthand: border-top ---\n");
    auto r = expandShorthand("border-top", "2px dashed red");
    check(r.size() == 3, "border-top -> 3 longhands");
    check(r[0].property == "border-top-width" && r[0].value == "2px", "border-top-width");
    check(r[1].property == "border-top-style" && r[1].value == "dashed", "border-top-style");
    check(r[2].property == "border-top-color" && r[2].value == "red", "border-top-color");
}

static void testBorderWidth() {
    printf("--- Shorthand: border-width ---\n");
    auto r = expandShorthand("border-width", "1px 2px 3px 4px");
    check(r.size() == 4, "border-width -> 4 longhands");
    check(r[0].value == "1px" && r[1].value == "2px", "border-width top/right");
    check(r[2].value == "3px" && r[3].value == "4px", "border-width bottom/left");
}

static void testFlex() {
    printf("--- Shorthand: flex ---\n");
    auto r1 = expandShorthand("flex", "none");
    check(r1.size() == 3, "flex none -> 3 longhands");
    check(r1[0].value == "0" && r1[1].value == "0" && r1[2].value == "auto", "flex: none = 0 0 auto");

    auto r2 = expandShorthand("flex", "auto");
    check(r2[0].value == "1" && r2[1].value == "1" && r2[2].value == "auto", "flex: auto = 1 1 auto");

    auto r3 = expandShorthand("flex", "1");
    check(r3[0].value == "1" && r3[2].value == "0", "flex: 1 = grow:1 basis:0");

    auto r4 = expandShorthand("flex", "2 1 100px");
    check(r4[0].value == "2", "flex 3v: grow=2");
    check(r4[1].value == "1", "flex 3v: shrink=1");
    check(r4[2].value == "100px", "flex 3v: basis=100px");
}

static void testFlexFlow() {
    printf("--- Shorthand: flex-flow ---\n");
    auto r1 = expandShorthand("flex-flow", "column wrap");
    check(r1[0].property == "flex-direction" && r1[0].value == "column", "flex-flow: direction=column");
    check(r1[1].property == "flex-wrap" && r1[1].value == "wrap", "flex-flow: wrap=wrap");

    auto r2 = expandShorthand("flex-flow", "row-reverse");
    check(r2[0].value == "row-reverse", "flex-flow single: direction=row-reverse");
    check(r2[1].value == "nowrap", "flex-flow single: wrap defaults to nowrap");
}

static void testGap() {
    printf("--- Shorthand: gap ---\n");
    auto r1 = expandShorthand("gap", "10px");
    check(r1.size() == 2, "gap single -> 2 longhands");
    check(r1[0].property == "row-gap" && r1[0].value == "10px", "gap single: row-gap");
    check(r1[1].property == "column-gap" && r1[1].value == "10px", "gap single: column-gap");

    auto r2 = expandShorthand("gap", "10px 20px");
    check(r2[0].value == "10px" && r2[1].value == "20px", "gap: row=10px col=20px");
}

static void testFont() {
    printf("--- Shorthand: font ---\n");
    auto r = expandShorthand("font", "italic bold 16px/1.5 Arial, sans-serif");
    check(r.size() == 5, "font -> 5 longhands");
    check(r[0].property == "font-style" && r[0].value == "italic", "font: style=italic");
    check(r[1].property == "font-weight" && r[1].value == "bold", "font: weight=bold");
    check(r[2].property == "font-size" && r[2].value == "16px", "font: size=16px");
    check(r[3].property == "line-height" && r[3].value == "1.5", "font: line-height=1.5");
    check(r[4].property == "font-family", "font: family property");

    auto r2 = expandShorthand("font", "14px monospace");
    check(r2[2].value == "14px", "font simple: size=14px");
    check(r2[4].value == "monospace", "font simple: family=monospace");
}

static void testListStyle() {
    printf("--- Shorthand: list-style ---\n");
    auto r = expandShorthand("list-style", "square inside");
    check(r[0].property == "list-style-type" && r[0].value == "square", "list-style: type=square");
    check(r[1].property == "list-style-position" && r[1].value == "inside", "list-style: position=inside");
}

static void testNotRecognized() {
    printf("--- Shorthand: non-shorthand passthrough ---\n");
    auto r = expandShorthand("color", "red");
    check(r.size() == 1, "non-shorthand returns 1 entry");
    check(r[0].property == "color" && r[0].value == "red", "non-shorthand passes through");
}

static void testInCascade() {
    printf("--- Shorthand: integration with cascade ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".box { margin: 10px 20px; padding: 5px; border: 1px solid red; }"));
    MockElement e; e.tag = "div"; e.classes = "box";
    auto style = cascade.resolve(e);
    check(style["margin-top"] == "10px", "cascade shorthand: margin-top=10px");
    check(style["margin-right"] == "20px", "cascade shorthand: margin-right=20px");
    check(style["margin-bottom"] == "10px", "cascade shorthand: margin-bottom=10px");
    check(style["margin-left"] == "20px", "cascade shorthand: margin-left=20px");
    check(style["padding-top"] == "5px", "cascade shorthand: padding-top=5px");
    check(style["border-top-width"] == "1px", "cascade shorthand: border-top-width=1px");
    check(style["border-top-style"] == "solid", "cascade shorthand: border-top-style=solid");
    check(style["border-top-color"] == "red", "cascade shorthand: border-top-color=red");
}

static void testOverrideLonghand() {
    printf("--- Shorthand: shorthand overrides longhand ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".box { margin-top: 100px; }\n"
        ".box { margin: 20px; }\n"
    ));
    MockElement e; e.tag = "div"; e.classes = "box";
    check(cascade.resolve(e)["margin-top"] == "20px",
          "cascade: shorthand margin overrides earlier margin-top");
}

static void testLonghandOverridesShorthand() {
    printf("--- Shorthand: longhand overrides shorthand ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".box { margin: 10px; margin-top: 50px; }"));
    MockElement e; e.tag = "div"; e.classes = "box";
    auto style = cascade.resolve(e);
    check(style["margin-top"] == "50px", "cascade: longhand margin-top overrides shorthand");
    check(style["margin-right"] == "10px", "cascade: other sides from shorthand preserved");
}

static void testInset() {
    printf("--- Shorthand: inset ---\n");
    // 1 value: all sides
    auto r1 = expandShorthand("inset", "10px");
    check(r1.size() == 4, "inset 1-value -> 4 longhands");
    check(r1[0].property == "top" && r1[0].value == "10px", "inset 1v: top");
    check(r1[1].property == "right" && r1[1].value == "10px", "inset 1v: right");
    check(r1[2].property == "bottom" && r1[2].value == "10px", "inset 1v: bottom");
    check(r1[3].property == "left" && r1[3].value == "10px", "inset 1v: left");

    // 2 values: vertical horizontal
    auto r2 = expandShorthand("inset", "10px 20px");
    check(r2[0].property == "top" && r2[0].value == "10px", "inset 2v: top=10px");
    check(r2[1].property == "right" && r2[1].value == "20px", "inset 2v: right=20px");
    check(r2[2].property == "bottom" && r2[2].value == "10px", "inset 2v: bottom=10px");
    check(r2[3].property == "left" && r2[3].value == "20px", "inset 2v: left=20px");

    // 4 values: top right bottom left
    auto r4 = expandShorthand("inset", "1px 2px 3px 4px");
    check(r4[0].property == "top" && r4[0].value == "1px", "inset 4v: top=1px");
    check(r4[1].property == "right" && r4[1].value == "2px", "inset 4v: right=2px");
    check(r4[2].property == "bottom" && r4[2].value == "3px", "inset 4v: bottom=3px");
    check(r4[3].property == "left" && r4[3].value == "4px", "inset 4v: left=4px");

    // auto value
    auto ra = expandShorthand("inset", "0 auto");
    check(ra[0].value == "0" && ra[1].value == "auto", "inset: 0 auto");
}

void testShorthand() {
    testMargin();
    testPadding();
    testBorder();
    testBorderSide();
    testBorderWidth();
    testBorderImage();
    testFlex();
    testFlexFlow();
    testGap();
    testFont();
    testListStyle();
    testNotRecognized();
    testInCascade();
    testOverrideLonghand();
    testLonghandOverridesShorthand();
    testInset();
}
