// Extra coverage tests: shorthand expansions, parser at-rules, logical
// properties, container/place-*, and a handful of cascade gaps that
// existing tests do not exercise.

#include "test_coverage_extra.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/properties.h"
#include "css/cascade.h"
#include "css/selector.h"

using namespace htmlayout::css;

static void testBackgroundShorthand() {
    printf("--- Shorthand: background ---\n");
    auto r = expandShorthand("background", "none");
    bool foundColor = false;
    foundColor = false;
    for (auto& d : r) if (d.property == "background-color" && d.value == "transparent") foundColor = true;
    check(foundColor, "background none -> color transparent");

    r = expandShorthand("background", "transparent");
    foundColor = false;
    for (auto& d : r) if (d.property == "background-color" && d.value == "transparent") foundColor = true;
    check(foundColor, "background transparent -> color transparent");

    r = expandShorthand("background", "red");
    bool foundRed = false;
    for (auto& d : r) if (d.property == "background-color" && d.value == "red") foundRed = true;
    check(foundRed, "background red -> color red");

    r = expandShorthand("background", "url(a.png) no-repeat center / cover red");
    bool hasImg = false, hasPos = false, hasSize = false, hasRep = false, hasColor = false;
    for (auto& d : r) {
        if (d.property == "background-image" && d.value.find("url(a.png)") != std::string::npos) hasImg = true;
        if (d.property == "background-repeat" && d.value == "no-repeat") hasRep = true;
        if (d.property == "background-position" && !d.value.empty()) hasPos = true;
        if (d.property == "background-size" && d.value == "cover") hasSize = true;
        if (d.property == "background-color" && d.value == "red") hasColor = true;
    }
    check(hasImg && hasRep && hasPos && hasSize && hasColor, "background full shorthand parses");

    // Multi-layer
    r = expandShorthand("background", "url(a.png), url(b.png) blue");
    bool layered = false;
    for (auto& d : r) if (d.property == "background-image" && d.value.find(",") != std::string::npos) layered = true;
    check(layered, "background multi-layer comma in image");
}

static void testBorderRadius() {
    printf("--- Shorthand: border-radius ---\n");
    auto r = expandShorthand("border-radius", "10px");
    check(r.size() == 4 && r[0].value == "10px", "border-radius 1v");

    r = expandShorthand("border-radius", "10px 20px");
    check(r[0].value == "10px" && r[1].value == "20px", "border-radius 2v");

    // With slash
    r = expandShorthand("border-radius", "10px / 5px");
    check(r.size() == 4, "border-radius with slash 4 corners");
    check(r[0].value.find('/') != std::string::npos, "slash preserved");
}

static void testOutline() {
    printf("--- Shorthand: outline ---\n");
    auto r = expandShorthand("outline", "2px solid red");
    bool w = false, s = false, c = false;
    for (auto& d : r) {
        if (d.property == "outline-width" && d.value == "2px") w = true;
        if (d.property == "outline-style" && d.value == "solid") s = true;
        if (d.property == "outline-color" && d.value == "red") c = true;
    }
    check(w && s && c, "outline expands");

    r = expandShorthand("outline", "dashed");
    bool sOnly = false;
    for (auto& d : r) if (d.property == "outline-style" && d.value == "dashed") sOnly = true;
    check(sOnly, "outline style-only");
}

static void testOverflowShorthand() {
    printf("--- Shorthand: overflow ---\n");
    auto r = expandShorthand("overflow", "hidden");
    bool x = false, y = false;
    for (auto& d : r) {
        if (d.property == "overflow-x" && d.value == "hidden") x = true;
        if (d.property == "overflow-y" && d.value == "hidden") y = true;
    }
    check(x && y, "overflow 1v applies to both");

    r = expandShorthand("overflow", "auto scroll");
    bool xa = false, yb = false;
    for (auto& d : r) {
        if (d.property == "overflow-x" && d.value == "auto") xa = true;
        if (d.property == "overflow-y" && d.value == "scroll") yb = true;
    }
    check(xa && yb, "overflow 2v: x y");
}

static void testColumnsShorthand() {
    printf("--- Shorthand: columns ---\n");
    auto r = expandShorthand("columns", "200px 3");
    bool w = false, c = false;
    for (auto& d : r) {
        if (d.property == "column-width" && d.value == "200px") w = true;
        if (d.property == "column-count" && d.value == "3") c = true;
    }
    check(w && c, "columns 200px 3");

    r = expandShorthand("columns", "auto");
    check(!r.empty(), "columns auto");
}

static void testColumnRule() {
    printf("--- Shorthand: column-rule ---\n");
    auto r = expandShorthand("column-rule", "1px solid black");
    bool w = false, s = false, c = false;
    for (auto& d : r) {
        if (d.property == "column-rule-width" && d.value == "1px") w = true;
        if (d.property == "column-rule-style" && d.value == "solid") s = true;
        if (d.property == "column-rule-color" && d.value == "black") c = true;
    }
    check(w && s && c, "column-rule expands");
}

static void testContainerShorthand() {
    printf("--- Shorthand: container ---\n");
    auto r = expandShorthand("container", "inline-size / sidebar");
    bool t = false, n = false;
    for (auto& d : r) {
        if (d.property == "container-type" && d.value == "inline-size") t = true;
        if (d.property == "container-name" && d.value == "sidebar") n = true;
    }
    check(t && n, "container type / name");

    r = expandShorthand("container", "size");
    bool ts = false, nn = false;
    for (auto& d : r) {
        if (d.property == "container-type" && d.value == "size") ts = true;
        if (d.property == "container-name" && d.value == "none") nn = true;
    }
    check(ts && nn, "container type only -> name none");
}

static void testPlaceShorthands() {
    printf("--- Shorthand: place-* ---\n");
    auto r = expandShorthand("place-items", "center");
    bool ai = false, ji = false;
    for (auto& d : r) {
        if (d.property == "align-items" && d.value == "center") ai = true;
        if (d.property == "justify-items" && d.value == "center") ji = true;
    }
    check(ai && ji, "place-items center");

    r = expandShorthand("place-content", "start end");
    bool ac = false, jc = false;
    for (auto& d : r) {
        if (d.property == "align-content" && d.value == "start") ac = true;
        if (d.property == "justify-content" && d.value == "end") jc = true;
    }
    check(ac && jc, "place-content start end");

    r = expandShorthand("place-self", "stretch");
    bool as = false, js = false;
    for (auto& d : r) {
        if (d.property == "align-self" && d.value == "stretch") as = true;
        if (d.property == "justify-self" && d.value == "stretch") js = true;
    }
    check(as && js, "place-self stretch");
}

static void testLogicalProperties() {
    printf("--- Logical properties ---\n");
    auto r = expandShorthand("inline-size", "200px");
    check(r.size() == 1 && r[0].property == "width" && r[0].value == "200px", "inline-size -> width");

    r = expandShorthand("block-size", "100px");
    check(r[0].property == "height", "block-size -> height");

    r = expandShorthand("min-inline-size", "50px");
    check(r[0].property == "min-width", "min-inline-size -> min-width");

    r = expandShorthand("max-block-size", "300px");
    check(r[0].property == "max-height", "max-block-size -> max-height");

    // Inline-axis logical longhands stay logical after expansion; the cascade
    // maps them to physical sides using `direction` (see testInlineLogical).
    r = expandShorthand("margin-inline-start", "10px");
    check(r[0].property == "margin-inline-start", "margin-inline-start stays logical");

    r = expandShorthand("margin-block-end", "5px");
    check(r[0].property == "margin-bottom", "margin-block-end -> margin-bottom");

    r = expandShorthand("margin-inline", "10px 20px");
    bool ml = false, mr = false;
    for (auto& d : r) {
        if (d.property == "margin-inline-start" && d.value == "10px") ml = true;
        if (d.property == "margin-inline-end" && d.value == "20px") mr = true;
    }
    check(ml && mr, "margin-inline 2v");

    r = expandShorthand("margin-block", "5px");
    bool mt = false, mb = false;
    for (auto& d : r) {
        if (d.property == "margin-top" && d.value == "5px") mt = true;
        if (d.property == "margin-bottom" && d.value == "5px") mb = true;
    }
    check(mt && mb, "margin-block 1v -> both");

    r = expandShorthand("padding-inline", "1px 2px");
    check(r.size() == 2, "padding-inline 2v");

    r = expandShorthand("padding-block", "3px");
    check(r.size() == 2, "padding-block 1v");

    r = expandShorthand("padding-inline-start", "8px");
    check(r[0].property == "padding-inline-start", "padding-inline-start stays logical");

    r = expandShorthand("border-inline-start", "1px solid red");
    bool bw = false, bs = false, bc = false;
    for (auto& d : r) {
        if (d.property == "border-inline-start-width") bw = true;
        if (d.property == "border-inline-start-style") bs = true;
        if (d.property == "border-inline-start-color") bc = true;
    }
    check(bw && bs && bc, "border-inline-start expands");

    r = expandShorthand("border-block-end", "2px dashed blue");
    bool bbw = false;
    for (auto& d : r) if (d.property == "border-bottom-width") bbw = true;
    check(bbw, "border-block-end -> bottom");

    r = expandShorthand("border-inline", "1px solid green");
    int sides = 0;
    for (auto& d : r) if (d.property == "border-inline-start-width" || d.property == "border-inline-end-width") sides++;
    check(sides == 2, "border-inline applies to both sides");

    r = expandShorthand("border-block", "1px solid green");
    int blocks = 0;
    for (auto& d : r) if (d.property == "border-top-width" || d.property == "border-bottom-width") blocks++;
    check(blocks == 2, "border-block applies to both sides");

    r = expandShorthand("border-inline-width", "1px 2px");
    check(r.size() == 2, "border-inline-width 2v");
    r = expandShorthand("border-block-width", "3px");
    check(r.size() == 2, "border-block-width 1v");

    r = expandShorthand("border-inline-style", "solid dashed");
    check(r.size() == 2 && r[0].value == "solid" && r[1].value == "dashed", "border-inline-style 2v");
    r = expandShorthand("border-block-style", "dotted");
    check(r.size() == 2, "border-block-style 1v");
    r = expandShorthand("border-inline-color", "red blue");
    check(r.size() == 2, "border-inline-color 2v");
    r = expandShorthand("border-block-color", "green");
    check(r.size() == 2, "border-block-color 1v");

    r = expandShorthand("border-inline-start-width", "3px");
    check(r[0].property == "border-inline-start-width", "border-inline-start-width stays logical");
    r = expandShorthand("border-block-end-color", "red");
    check(r[0].property == "border-bottom-color", "border-block-end-color");
    r = expandShorthand("border-inline-end-style", "dashed");
    check(r[0].property == "border-inline-end-style", "border-inline-end-style stays logical");
    r = expandShorthand("border-block-start-color", "blue");
    check(r[0].property == "border-top-color", "border-block-start-color");

    r = expandShorthand("inset-inline-start", "10px");
    check(r[0].property == "inset-inline-start", "inset-inline-start stays logical");
    r = expandShorthand("inset-inline-end", "10px");
    check(r[0].property == "inset-inline-end", "inset-inline-end stays logical");
}

static void testListStyleShorthand() {
    printf("--- Shorthand: list-style ---\n");
    auto r = expandShorthand("list-style", "square inside");
    bool t = false, p = false;
    for (auto& d : r) {
        if (d.property == "list-style-type" && d.value == "square") t = true;
        if (d.property == "list-style-position" && d.value == "inside") p = true;
    }
    check(t && p, "list-style type+pos");
}

static void testTransitionAnimation() {
    printf("--- Shorthand: transition/animation ---\n");
    auto r = expandShorthand("transition", "all 0.3s ease");
    check(r.size() == 1 && r[0].property == "transition", "transition stored as-is");
    r = expandShorthand("animation", "fadeIn 1s linear");
    check(r.size() == 1 && r[0].property == "animation", "animation stored as-is");
}

static void testFontFaceRule() {
    printf("--- @font-face parsing ---\n");
    std::string css =
        "@font-face {"
        "  font-family: \"MyFont\";"
        "  src: url(\"my.woff2\");"
        "  font-weight: 700;"
        "  font-style: italic;"
        "}";
    auto sheet = parse(css);
    check(sheet.fontFaces.size() == 1, "one @font-face captured");
    if (!sheet.fontFaces.empty()) {
        auto& ff = sheet.fontFaces[0];
        check(ff.family == "MyFont", "@font-face family unquoted");
        check(ff.src == "my.woff2", "@font-face src URL unquoted");
        check(ff.weight == 700, "@font-face weight 700");
        check(ff.italic, "@font-face italic");
    }

    auto s2 = parse("@font-face { font-family: 'Bar'; src: url(b.ttf); font-weight: normal; font-style: oblique; }");
    check(!s2.fontFaces.empty() && s2.fontFaces[0].weight == 400 && s2.fontFaces[0].italic,
          "@font-face weight normal=400, oblique=italic");

    auto s3 = parse("@font-face { font-family: A; src: url(a.ttf); font-weight: bold; }");
    check(!s3.fontFaces.empty() && s3.fontFaces[0].weight == 700, "@font-face bold=700");
}

static void testKeyframesRule() {
    printf("--- @keyframes parsing ---\n");
    auto s = parse(
        "@keyframes spin {"
        "  from { transform: rotate(0deg); }"
        "  50% { transform: rotate(180deg); }"
        "  to { transform: rotate(360deg); }"
        "}");
    check(s.keyframes.size() == 1, "one @keyframes captured");
    if (!s.keyframes.empty()) {
        auto& kf = s.keyframes[0];
        check(kf.name == "spin", "@keyframes name");
        check(kf.stops.size() >= 3, "3 stops parsed");
    }

    // @-webkit-keyframes
    auto s2 = parse("@-webkit-keyframes ride { 0% { left: 0; } 100% { left: 100px; } }");
    check(!s2.keyframes.empty() && s2.keyframes[0].name == "ride", "@-webkit-keyframes name");
}

static void testImportRule() {
    printf("--- @import parsing ---\n");
    auto s = parse("@import url(\"a.css\");");
    check(s.imports.size() == 1 && s.imports[0].url == "a.css", "basic @import url()");

    auto s2 = parse("@import \"b.css\" print;");
    check(!s2.imports.empty() && s2.imports[0].mediaCondition.find("print") != std::string::npos,
          "@import with media");

    auto s3 = parse("@import url(c.css) layer(utilities);");
    check(!s3.imports.empty() && s3.imports[0].layer == "utilities",
          "@import with layer(name)");

    auto s4 = parse("@import url(d.css) layer;");
    check(!s4.imports.empty(), "@import with anonymous layer parses");
}

static void testMediaQueryEvaluation() {
    printf("--- @media query evaluation ---\n");
    MediaContext ctx; ctx.viewportWidth = 800; ctx.viewportHeight = 600; ctx.mediaType = "screen";
    check(evaluateMediaQuery("(min-width: 500px)", ctx), "min-width: 500px @ 800px = true");
    check(!evaluateMediaQuery("(min-width: 1000px)", ctx), "min-width: 1000px @ 800px = false");
    check(evaluateMediaQuery("(max-width: 1000px)", ctx), "max-width: 1000px @ 800px = true");
    check(evaluateMediaQuery("screen", ctx), "screen media type matches");
    check(!evaluateMediaQuery("print", ctx), "print media type does not match screen");
    check(evaluateMediaQuery("all", ctx), "all matches");
}

static void testInlineStyleParse() {
    printf("--- inline style parse ---\n");
    auto decls = parseInlineStyle("color: red; padding: 10px; font-size: 14px");
    check(decls.size() == 3, "3 declarations parsed");

    auto d2 = parseInlineStyle("color: red !important");
    check(!d2.empty() && d2[0].important, "!important picked up");

    auto d3 = parseInlineStyle("");
    check(d3.empty(), "empty inline -> 0");

    auto d4 = parseInlineStyle("invalid-stuff");
    (void)d4;
    check(true, "malformed inline doesn't crash");
}

static void testNestedAtRulesParse() {
    printf("--- nested @media inside rules ---\n");
    auto s = parse("@media (min-width: 600px) { .x { color: red; } } .y { color: blue; }");
    check(s.mediaBlocks.size() == 1, "one media block");
    check(s.rules.size() == 1, "one top-level rule");
}

// ===== Cascade specificity edge cases =====
static void testCascadeImportant() {
    printf("--- cascade !important ---\n");
    auto sheet = parse(".a { color: red; } .a { color: blue !important; } .a { color: green; }");
    MockElement el;
    el.tag = "div";
    el.classes = "a";
    Cascade cascade;
    cascade.addStylesheet(sheet);
    auto computed = cascade.resolve(el);
    auto it = computed.find("color");
    check(it != computed.end() && it->second == "blue",
          "!important wins over later non-important");
}

static void testCascadeSpecificity() {
    printf("--- cascade specificity ---\n");
    auto sheet = parse("#x { color: red; } .y { color: blue; } div { color: green; }");
    MockElement el;
    el.tag = "div"; el.elemId = "x"; el.classes = "y";
    Cascade c;
    c.addStylesheet(sheet);
    auto cs = c.resolve(el);
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "red", "id beats class beats tag");
}

static void testGridShorthandExpansion() {
    printf("--- Shorthand: grid-row / grid-column / grid-area / grid-template ---\n");
    auto r = expandShorthand("grid-row", "1 / 3");
    check(r.size() == 1 && r[0].property == "grid-row", "grid-row stored as-is");

    r = expandShorthand("grid-column", "2 / 5");
    check(r.size() == 1 && r[0].property == "grid-column", "grid-column stored as-is");

    r = expandShorthand("grid-area", "header");
    check(r.size() == 1 && r[0].property == "grid-area", "grid-area stored as-is");

    r = expandShorthand("grid-template", "100px 1fr / auto 200px");
    check(r.size() == 1 && r[0].property == "grid-template", "grid-template stored as-is");
}

void testCoverageExtra() {
    printf("=== Extra Coverage Tests ===\n");
    testBackgroundShorthand();
    testBorderRadius();
    testOutline();
    testOverflowShorthand();
    testColumnsShorthand();
    testColumnRule();
    testContainerShorthand();
    testPlaceShorthands();
    testLogicalProperties();
    testListStyleShorthand();
    testTransitionAnimation();
    testFontFaceRule();
    testKeyframesRule();
    testImportRule();
    testMediaQueryEvaluation();
    testInlineStyleParse();
    testNestedAtRulesParse();
    testCascadeImportant();
    testCascadeSpecificity();
    testGridShorthandExpansion();
}
