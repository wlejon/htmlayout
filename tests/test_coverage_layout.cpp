#include "test_coverage.h"

using namespace htmlayout::css;
using namespace htmlayout::layout;

// ======================================================================
// LENGTH RESOLUTION COVERAGE
// ======================================================================

static void testResolveLengthViewport() {
    printf("--- Length: viewport units (separate w/h) ---\n");
    // Using the viewport-aware overload
    float val = resolveLength("50vw", 0, 16, 1024, 768);
    check(approx(val, 512.0f), "50vw at 1024px viewport = 512");

    val = resolveLength("100vh", 0, 16, 1024, 768);
    check(approx(val, 768.0f), "100vh at 768px viewport = 768");

    val = resolveLength("50vmin", 0, 16, 1024, 768);
    check(approx(val, 384.0f), "50vmin at min(1024,768) = 384");

    val = resolveLength("50vmax", 0, 16, 1024, 768);
    check(approx(val, 512.0f), "50vmax at max(1024,768) = 512");
}

static void testResolveLengthRem() {
    printf("--- Length: rem unit ---\n");
    // rem is based on root font size, approximated as 16px
    float val = resolveLength("2rem", 100, 20);
    check(approx(val, 32.0f), "2rem = 2 * 16px (root font size)");
}

static void testResolveLengthPt() {
    printf("--- Length: pt unit ---\n");
    // 1pt = 96/72 px = 1.333px
    float val = resolveLength("12pt", 100, 16);
    check(approx(val, 16.0f), "12pt = 16px");
}

static void testCalcComplexExpr() {
    printf("--- Calc: complex expressions ---\n");
    // Nested calc
    float val = resolveLength("calc(calc(50px + 50px) * 2)", 0, 16);
    check(approx(val, 200.0f), "calc(calc(50+50)*2) = 200");

    // Division
    val = resolveLength("calc(300px / 3)", 0, 16);
    check(approx(val, 100.0f), "calc(300/3) = 100");

    // Subtract with mixed units
    val = resolveLength("calc(100% - 20px)", 400, 16);
    check(approx(val, 380.0f), "calc(100% - 20px) at 400 = 380");
}

static void testEdgeResolution() {
    printf("--- Edges: resolveEdges ---\n");
    CovNode n;
    n.style_["margin-top"] = "10px";
    n.style_["margin-right"] = "20px";
    n.style_["margin-bottom"] = "15px";
    n.style_["margin-left"] = "5px";
    // Called outside a layout pass, so this also exercises the style cache's
    // uncached path: with no pass running the values must come from the live map.
    auto edges = resolveEdges(&n, kMarginProps, 400, 16);
    check(approx(edges.top, 10.0f), "margin-top = 10");
    check(approx(edges.right, 20.0f), "margin-right = 20");
    check(approx(edges.bottom, 15.0f), "margin-bottom = 15");
    check(approx(edges.left, 5.0f), "margin-left = 5");
}

// ======================================================================
// TEXT BREAKING COVERAGE
// ======================================================================

static void testTextPreWrap() {
    printf("--- Text: pre-wrap ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("hello world", 80, "mono", 16, "normal", "pre-wrap", m);
    // pre-wrap preserves spaces and wraps at available width
    check(runs.size() >= 1, "pre-wrap: produces runs");
}

static void testTextPreLine() {
    printf("--- Text: pre-line ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("line1\nline2\nline3", 500, "mono", 16, "normal", "pre-line", m);
    check(runs.size() == 3, "pre-line: 3 lines from newlines");
    check(runs[0].text == "line1", "pre-line: first line");
    check(runs[1].text == "line2", "pre-line: second line");
    // Each preserved newline must mark a hard break on the run that
    // ended at it; the trailing run (no following newline) must not.
    check(runs[0].forceBreakAfter, "pre-line: run 0 ends in hard break");
    check(runs[1].forceBreakAfter, "pre-line: run 1 ends in hard break");
    check(!runs[2].forceBreakAfter, "pre-line: trailing run has no hard break");
}

static void testTextPreNewlineHardBreak() {
    printf("--- Text: pre newline -> forceBreakAfter ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("a\nb\nc", 500, "mono", 16, "normal", "pre", m);
    check(runs.size() == 3, "pre: 3 runs from newlines");
    check(runs[0].text == "a" && runs[0].forceBreakAfter, "pre: run 0 has hard break");
    check(runs[1].text == "b" && runs[1].forceBreakAfter, "pre: run 1 has hard break");
    check(runs[2].text == "c" && !runs[2].forceBreakAfter, "pre: trailing run has no hard break");
}

// IFC end-to-end: a <pre>-style block containing four newline-separated
// lines must lay out four lines tall, not collapse to one. Regressions
// in the IFC line builder (where it ignores per-run hard breaks) would
// pack everything onto one line.
static void testIFCPreNewlineLines() {
    printf("--- IFC: pre block with newlines wraps to N lines ---\n");
    CovNode root; root.initBlock();
    root.style_["white-space"] = "pre";
    root.style_["line-height"] = "20px";

    CovNode txt; txt.isText = true;
    txt.text = "line1\nline2\nline3\nline4";
    root.addChild(&txt);

    CovMetrics m;
    layoutTree(&root, 500, m);
    // CovMetrics::lineHeight() returns 20.0f; four lines -> 80px.
    check(approx(root.box.contentRect.height, 80.0f),
          "pre IFC: four newline-separated lines -> 4 line-heights tall");
}

static void testTextOverflowWrapBreakWord() {
    printf("--- Text: overflow-wrap break-word ---\n");
    CovMetrics m;
    // "abcdefghij" = 100px, available = 50px
    auto runs = breakTextIntoRuns("abcdefghij", 50, "mono", 16, "normal", "normal", m,
                                  "break-word");
    check(runs.size() >= 2, "break-word: long word broken into multiple runs");
}

static void testTextWordBreakAll() {
    printf("--- Text: word-break break-all ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("hello world", 35, "mono", 16, "normal", "normal", m,
                                  "normal", "break-all");
    // 35px = 3.5 chars, so each word can break mid-character
    check(runs.size() >= 2, "break-all: words broken at arbitrary points");
}

static void testTextEmptyInput() {
    printf("--- Text: empty input ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("", 200, "mono", 16, "normal", "normal", m);
    check(runs.empty(), "text: empty string produces no runs");
}

static void testTextSingleLongWord() {
    printf("--- Text: single long word ---\n");
    CovMetrics m;
    // "abcdefghijklmnop" = 160px, available = 50px
    auto runs = breakTextIntoRuns("abcdefghijklmnop", 50, "mono", 16, "normal", "normal", m,
                                  "break-word");
    check(runs.size() >= 3, "text: long word breaks into 3+ runs with break-word");
}

// Structural UTF-8 validity — no truncated multi-byte sequences, no stray
// continuation bytes. Character-level wrapping must never slice a sequence:
// downstream Skia text APIs fatally abort on invalid UTF-8.
static bool isStructurallyValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        size_t n;
        if      (b < 0x80)          n = 1;
        else if ((b & 0xE0) == 0xC0) n = 2;
        else if ((b & 0xF0) == 0xE0) n = 3;
        else if ((b & 0xF8) == 0xF0) n = 4;
        else return false; // stray continuation or invalid lead byte
        if (i + n > s.size()) return false; // truncated sequence
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

static void testTextBreakWordMultibyte() {
    printf("--- Text: break-word never splits UTF-8 sequences ---\n");
    CovMetrics m;
    // One long word mixing 1-byte and 4-byte (emoji) codepoints. At 10px per
    // byte and 50px available, byte-level breaking would slice the emoji.
    std::string word = "abc\xF0\x9F\x98\x80\xF0\x9F\x98\x80xyz\xF0\x9F\x98\x80""abc";
    auto runs = breakTextIntoRuns(word, 50, "mono", 16, "normal", "normal", m,
                                  "break-word");
    check(runs.size() >= 2, "break-word multibyte: long word broken into runs");
    std::string joined;
    for (auto& r : runs) {
        check(isStructurallyValidUtf8(r.text),
              "break-word multibyte: run is valid UTF-8");
        joined += r.text;
    }
    check(joined == word, "break-word multibyte: no bytes lost or reordered");
}

static void testTextPreWrapMultibyte() {
    printf("--- Text: pre-wrap never splits UTF-8 sequences ---\n");
    CovMetrics m;
    std::string line = "aa\xF0\x9F\x98\x80\xF0\x9F\x98\x80""bb\xF0\x9F\x98\x80""cc";
    auto runs = breakTextIntoRuns(line, 50, "mono", 16, "normal", "pre-wrap", m);
    check(runs.size() >= 2, "pre-wrap multibyte: overflowing line wrapped");
    std::string joined;
    for (auto& r : runs) {
        check(isStructurallyValidUtf8(r.text),
              "pre-wrap multibyte: run is valid UTF-8");
        joined += r.text;
    }
    check(joined == line, "pre-wrap multibyte: no bytes lost or reordered");
}

// ======================================================================
// BLOCK LAYOUT COVERAGE
// ======================================================================

static void testBlockMarginAuto() {
    printf("--- Block: margin auto centering ---\n");
    CovNode root; root.initBlock();
    CovNode child; child.initBlock();
    child.style_["width"] = "200px";
    child.style_["height"] = "50px";
    child.style_["margin-left"] = "auto";
    child.style_["margin-right"] = "auto";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // margin auto: (600 - 200) / 2 = 200px each side
    check(approx(child.box.contentRect.x, 200.0f), "margin auto: centered at x=200");
}

static void testBlockMinMaxHeight() {
    printf("--- Block: min/max height ---\n");
    CovNode root; root.initBlock();

    CovNode child; child.initBlock();
    child.style_["height"] = "50px";
    child.style_["min-height"] = "100px";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(child.box.contentRect.height, 100.0f), "min-height: 50px clamped to 100px");

    CovNode root2; root2.initBlock();
    CovNode child2; child2.initBlock();
    child2.style_["height"] = "200px";
    child2.style_["max-height"] = "100px";
    root2.addChild(&child2);
    layoutTree(&root2, 600, m);
    check(approx(child2.box.contentRect.height, 100.0f), "max-height: 200px clamped to 100px");
}

static void testBlockMaxWidth() {
    printf("--- Block: max-width ---\n");
    CovNode root; root.initBlock();
    CovNode child; child.initBlock();
    child.style_["width"] = "500px";
    child.style_["height"] = "30px";
    child.style_["max-width"] = "300px";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(child.box.contentRect.width, 300.0f), "max-width: 500px clamped to 300px");
}

static void testBlockMarginCollapseFirstChild() {
    printf("--- Block: margin collapse first child ---\n");
    CovNode root; root.initBlock();
    CovNode child; child.initBlock();
    child.style_["height"] = "50px";
    child.style_["margin-top"] = "30px";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // First child margin collapses with parent (parent has no padding/border)
    check(approx(child.box.contentRect.y, 30.0f), "first child margin collapse: y=30");
}

static void testBlockFloatMultiple() {
    printf("--- Block: multiple floats ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px";

    CovNode f1; f1.initBlock();
    f1.style_["width"] = "100px"; f1.style_["height"] = "50px"; f1.style_["float"] = "left";
    CovNode f2; f2.initBlock();
    f2.style_["width"] = "100px"; f2.style_["height"] = "80px"; f2.style_["float"] = "left";
    CovNode content; content.initBlock();
    content.style_["height"] = "30px";

    root.addChild(&f1); root.addChild(&f2); root.addChild(&content);

    CovMetrics m;
    layoutTree(&root, 800, m);
    // Two left floats side by side
    check(approx(f1.box.contentRect.x, 0.0f), "float: f1 at x=0");
    check(approx(f2.box.contentRect.x, 100.0f), "float: f2 at x=100");
    // In-flow block box overlaps the floats — only line boxes shorten
    check(approx(content.box.contentRect.x, 0.0f), "float: block box keeps x=0 beside floats");
}

static void testBlockClearLeft() {
    printf("--- Block: clear left ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px";

    CovNode fL; fL.initBlock();
    fL.style_["width"] = "100px"; fL.style_["height"] = "80px"; fL.style_["float"] = "left";
    CovNode fR; fR.initBlock();
    fR.style_["width"] = "100px"; fR.style_["height"] = "40px"; fR.style_["float"] = "right";
    CovNode cleared; cleared.initBlock();
    cleared.style_["height"] = "30px"; cleared.style_["clear"] = "left";

    root.addChild(&fL); root.addChild(&fR); root.addChild(&cleared);

    CovMetrics m;
    layoutTree(&root, 800, m);
    // clear:left moves below left float (80px), not right float (40px)
    check(approx(cleared.box.contentRect.y, 80.0f), "clear left: below left float at y=80");
}

static void testBlockOverflowNewBFC() {
    printf("--- Block: overflow hidden creates new BFC ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px";

    CovNode floater; floater.initBlock();
    floater.style_["width"] = "100px"; floater.style_["height"] = "200px"; floater.style_["float"] = "left";

    CovNode bfc; bfc.initBlock();
    bfc.style_["overflow"] = "hidden";
    bfc.style_["height"] = "50px";

    root.addChild(&floater); root.addChild(&bfc);

    CovMetrics m;
    layoutTree(&root, 800, m);
    // BFC with overflow:hidden should not overlap the float
    check(bfc.box.contentRect.x >= 100.0f, "overflow BFC: pushed past float");
}

// ======================================================================
// INLINE LAYOUT COVERAGE
// ======================================================================

static void testInlineVerticalAlignMiddle() {
    printf("--- Inline: vertical-align middle ---\n");
    CovNode root; root.initInline();

    CovNode ib1; ib1.initInline();
    ib1.style_["display"] = "inline-block";
    ib1.style_["width"] = "50px"; ib1.style_["height"] = "60px";
    ib1.style_["vertical-align"] = "middle";

    CovNode ib2; ib2.initInline();
    ib2.style_["display"] = "inline-block";
    ib2.style_["width"] = "50px"; ib2.style_["height"] = "20px";

    root.addChild(&ib1); root.addChild(&ib2);

    CovMetrics m;
    layoutTree(&root, 500, m);
    // ib1 with vertical-align: middle should be centered on the line
    check(root.box.contentRect.height >= 20, "inline: container has positive height");
}

static void testInlineVerticalAlignTop() {
    printf("--- Inline: vertical-align top ---\n");
    CovNode root; root.initInline();

    CovNode ib1; ib1.initInline();
    ib1.style_["display"] = "inline-block";
    ib1.style_["width"] = "50px"; ib1.style_["height"] = "60px";
    ib1.style_["vertical-align"] = "top";

    CovNode ib2; ib2.initInline();
    ib2.style_["display"] = "inline-block";
    ib2.style_["width"] = "50px"; ib2.style_["height"] = "30px";

    root.addChild(&ib1); root.addChild(&ib2);

    CovMetrics m;
    layoutTree(&root, 500, m);
    // vertical-align: top aligns the item's top with the line-box top. The
    // inline root's own box is its font strip (tall children carry negative
    // y within it), so assert the relative geometry: the baseline-aligned
    // 30px ib2 has its bottom on the 60px line's baseline, placing its top
    // 30px below ib1's line-top-aligned top.
    check(approx(ib1.box.contentRect.y, ib2.box.contentRect.y - 30.0f, 2.0f),
          "vertical-align top: 30px above the baseline-aligned sibling");
}

static void testInlineTextAlignStartEnd() {
    printf("--- Inline: text-align start/end with direction ---\n");
    CovNode root; root.initBlock();
    root.style_["text-align"] = "start";
    root.style_["direction"] = "rtl";

    CovNode ib; ib.initInline();
    ib.style_["display"] = "inline-block";
    ib.style_["width"] = "50px"; ib.style_["height"] = "20px";
    root.addChild(&ib);

    CovMetrics m;
    layoutTree(&root, 200, m);
    // RTL with text-align:start should right-align
    check(approx(ib.box.contentRect.x, 150.0f), "text-align start + rtl: right-aligned");
}

static void testInlineNestedInline() {
    printf("--- Inline: nested inline elements ---\n");
    CovNode root; root.initInline();

    CovNode span1; span1.initInline();
    CovNode textNode; textNode.isText = true; textNode.text = "hello";
    span1.addChild(&textNode);
    root.addChild(&span1);

    CovMetrics m;
    layoutTree(&root, 500, m);
    check(approx(root.box.contentRect.width, 50.0f), "nested inline: width from text");
}

static void testInlineReplacedElement() {
    printf("--- Inline: replaced element with intrinsic size ---\n");
    CovNode root; root.initInline();

    CovNode img; img.initInline();
    img.style_["display"] = "inline-block";
    img.tag = "img";
    img.hasIntrinsic = true; img.intrW = 200; img.intrH = 150;
    root.addChild(&img);

    CovMetrics m;
    layoutTree(&root, 500, m);
    check(approx(img.box.contentRect.width, 200.0f), "replaced: intrinsic width 200");
    check(approx(img.box.contentRect.height, 150.0f), "replaced: intrinsic height 150");
}

// ======================================================================
// HIT TEST COVERAGE
// ======================================================================

static void testHitTestVisibilityHidden() {
    printf("--- HitTest: visibility hidden ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px"; root.style_["height"] = "400px";

    CovNode child; child.initBlock();
    child.style_["width"] = "100px"; child.style_["height"] = "100px";
    child.style_["visibility"] = "hidden";
    child.tag = "hidden";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 400, m);
    auto* hit = hitTest(&root, 50, 50);
    check(hit == nullptr || hit->tagName() != "hidden",
          "hitTest: visibility:hidden not hit-testable");
}

static void testHitTestZIndex() {
    printf("--- HitTest: z-index ordering ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px"; root.style_["height"] = "400px";

    CovNode c1; c1.initBlock();
    c1.style_["width"] = "200px"; c1.style_["height"] = "200px";
    c1.style_["position"] = "absolute"; c1.style_["z-index"] = "1";
    c1.tag = "low";

    CovNode c2; c2.initBlock();
    c2.style_["width"] = "200px"; c2.style_["height"] = "200px";
    c2.style_["position"] = "absolute"; c2.style_["z-index"] = "2";
    c2.tag = "high";

    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 400, m);
    auto* hit = hitTest(&root, 100, 100);
    check(hit != nullptr && hit->tagName() == "high", "hitTest: higher z-index wins");
}

static void testHitTestNull() {
    printf("--- HitTest: null root ---\n");
    auto* hit = hitTest(nullptr, 50, 50);
    check(hit == nullptr, "hitTest: null root returns null");
}

static void testHitTestOverflowClip() {
    printf("--- HitTest: overflow clipping ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "100px"; root.style_["height"] = "100px";
    root.style_["overflow"] = "hidden";

    CovNode child; child.initBlock();
    child.style_["width"] = "200px"; child.style_["height"] = "200px";
    child.tag = "big";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 400, m);
    applyOverflowClipping(&root);

    // Point inside clipped area should hit child (it's clipped to parent)
    auto* hit = hitTest(&root, 50, 50);
    check(hit != nullptr, "hitTest overflow: hit inside clipped area");
}

// ======================================================================
// LAYOUT BOX HELPER COVERAGE
// ======================================================================

static void testLayoutBoxFullWidth() {
    printf("--- LayoutBox: fullWidth/fullHeight/marginBox ---\n");
    LayoutBox box;
    box.contentRect = {10, 20, 100, 50};
    box.padding = {5, 5, 5, 5};
    box.border = {2, 2, 2, 2};
    box.margin = {10, 10, 10, 10};

    check(approx(box.fullWidth(), 114.0f), "fullWidth = 100 + 5 + 5 + 2 + 2 = 114");
    check(approx(box.fullHeight(), 64.0f), "fullHeight = 50 + 5 + 5 + 2 + 2 = 64");

    auto mb = box.marginBox();
    check(approx(mb.width, 134.0f), "marginBox width = 114 + 10 + 10 = 134");
    check(approx(mb.height, 84.0f), "marginBox height = 64 + 10 + 10 = 84");
    check(approx(mb.x, -7.0f), "marginBox x = 10 - 5 - 2 - 10 = -7");
    check(approx(mb.y, 3.0f), "marginBox y = 20 - 5 - 2 - 10 = 3");
}

// ======================================================================
// VIEWPORT-AWARE LAYOUT
// ======================================================================

static void testViewportLayout() {
    printf("--- Layout: viewport-aware layout ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "50vw";
    root.style_["height"] = "50vh";

    CovMetrics m;
    Viewport vp{1024, 768};
    layoutTree(&root, vp, m);

    check(approx(root.box.contentRect.width, 512.0f), "viewport layout: 50vw = 512");
    check(approx(root.box.contentRect.height, 384.0f), "viewport layout: 50vh = 384");
}

// ======================================================================
// MULTICOL COVERAGE
// ======================================================================

static void testMulticolIsContainer() {
    printf("--- Multicol: isMulticolContainer ---\n");
    // These all run outside a layout pass, which is half the point of the test:
    // with no pass in flight the style cache must not be consulted at all and
    // every read has to come from the node's live ComputedStyle.
    CovNode n1;
    n1.style_["column-count"] = "3";
    check(isMulticolContainer(&n1), "isMulticolContainer: column-count=3 is true");

    CovNode n2;
    n2.style_["column-width"] = "200px";
    check(isMulticolContainer(&n2), "isMulticolContainer: column-width=200px is true");

    CovNode n3;
    n3.style_["column-count"] = "auto";
    check(!isMulticolContainer(&n3), "isMulticolContainer: column-count=auto is false");

    CovNode n4;
    check(!isMulticolContainer(&n4), "isMulticolContainer: empty style is false");
}

// ======================================================================
// INCREMENTAL LAYOUT COVERAGE
// ======================================================================

static void testMarkDirtyNull() {
    printf("--- Incremental: markDirty null ---\n");
    markDirty(nullptr); // should not crash
    check(true, "markDirty(nullptr) doesn't crash");
}

static void testNeedsRelayoutProperties() {
    printf("--- Invalidation: additional properties ---\n");
    check(needsRelayout({"margin-top"}), "margin-top needs relayout");
    check(needsRelayout({"padding-left"}), "padding-left needs relayout");
    check(needsRelayout({"border-top-width"}), "border-top-width needs relayout");
    check(needsRelayout({"min-width"}), "min-width needs relayout");
    check(needsRelayout({"max-height"}), "max-height needs relayout");
    check(needsRelayout({"float"}), "float needs relayout");
    check(needsRelayout({"clear"}), "clear needs relayout");
    check(needsRelayout({"position"}), "position needs relayout");
    check(needsRelayout({"overflow"}), "overflow needs relayout");
    check(needsRelayout({"flex-direction"}), "flex-direction needs relayout");
    check(needsRelayout({"flex-wrap"}), "flex-wrap needs relayout");
    check(needsRelayout({"justify-content"}), "justify-content needs relayout");
    check(needsRelayout({"align-items"}), "align-items needs relayout");
    check(needsRelayout({"font-size"}), "font-size needs relayout");
    check(needsRelayout({"white-space"}), "white-space needs relayout");
    check(needsRelayout({"text-align"}), "text-align needs relayout");
    check(needsRelayout({"column-count"}), "column-count needs relayout");

    // Paint-only
    check(!needsRelayout({"cursor"}), "cursor: no relayout");
    check(!needsRelayout({"outline-color"}), "outline-color: no relayout");
    check(!needsRelayout({"text-decoration"}), "text-decoration: no relayout");
    check(!needsRelayout({"visibility"}), "visibility: no relayout");
}

// ======================================================================
// DISPLAY DISPATCH COVERAGE
// ======================================================================

static void testDisplayNoneLayout() {
    printf("--- Layout: display none produces zero box ---\n");
    CovNode root; root.initBlock();
    root.style_["display"] = "none";

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(approx(root.box.contentRect.width, 0.0f), "display:none width=0");
    check(approx(root.box.contentRect.height, 0.0f), "display:none height=0");
}

static void testDisplayInlineFlex() {
    printf("--- Layout: display inline-flex ---\n");
    CovNode root; root.initFlex();
    root.style_["display"] = "inline-flex";

    CovNode c1; c1.initFlexItem();
    c1.style_["width"] = "100px"; c1.style_["height"] = "40px";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(approx(c1.box.contentRect.width, 100.0f), "inline-flex: child laid out");
}

static void testDisplayInlineGrid() {
    printf("--- Layout: display inline-grid ---\n");
    CovNode root; root.initGrid();
    root.style_["display"] = "inline-grid";
    root.style_["grid-template-columns"] = "1fr 1fr";
    root.style_["width"] = "200px";

    CovNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(approx(c1.box.contentRect.width, 100.0f), "inline-grid: c1 width = 100");
}

static void testDisplayInlineTable() {
    printf("--- Layout: display inline-table ---\n");
    CovNode root; root.initTable();
    root.style_["display"] = "inline-table";
    root.style_["width"] = "200px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "30px";
    row.addChild(&cell);
    root.addChild(&row);

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(cell.box.contentRect.width > 0, "inline-table: cell has width");
}

static void testDisplayListItem() {
    printf("--- Layout: display list-item ---\n");
    CovNode root; root.initBlock();

    CovNode li; li.initBlock();
    li.style_["display"] = "list-item";
    li.style_["height"] = "30px";
    root.addChild(&li);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(li.box.contentRect.height, 30.0f), "list-item: laid out as block");
}

void testCoverageLayout() {
    // Length resolution
    testResolveLengthViewport();
    testResolveLengthRem();
    testResolveLengthPt();
    testCalcComplexExpr();
    testEdgeResolution();

    // Text breaking
    testTextPreWrap();
    testTextPreLine();
    testTextPreNewlineHardBreak();
    testIFCPreNewlineLines();
    testTextOverflowWrapBreakWord();
    testTextWordBreakAll();
    testTextEmptyInput();
    testTextSingleLongWord();
    testTextBreakWordMultibyte();
    testTextPreWrapMultibyte();

    // Block layout
    testBlockMarginAuto();
    testBlockMinMaxHeight();
    testBlockMaxWidth();
    testBlockMarginCollapseFirstChild();
    testBlockFloatMultiple();
    testBlockClearLeft();
    testBlockOverflowNewBFC();

    // Inline layout
    testInlineVerticalAlignMiddle();
    testInlineVerticalAlignTop();
    testInlineTextAlignStartEnd();
    testInlineNestedInline();
    testInlineReplacedElement();

    // Hit testing
    testHitTestVisibilityHidden();
    testHitTestZIndex();
    testHitTestNull();
    testHitTestOverflowClip();

    // LayoutBox helpers
    testLayoutBoxFullWidth();

    // Viewport layout
    testViewportLayout();

    // Multicol
    testMulticolIsContainer();

    // Incremental & invalidation
    testMarkDirtyNull();
    testNeedsRelayoutProperties();

    // Display dispatch
    testDisplayNoneLayout();
    testDisplayInlineFlex();
    testDisplayInlineGrid();
    testDisplayInlineTable();
    testDisplayListItem();
}
