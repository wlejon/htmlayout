// Sizing regressions: percentage widths on flex/grid items that are
// themselves containers, min/max-width on inline-blocks.

#include "test_sizing_fixes.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <string>
#include <vector>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct SNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    SNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style_; }
    void addChild(SNode* c) { c->parentNode = this; childNodes.push_back(c); }

    SNode& init(const char* display = "block") {
        style_["display"] = display;
        style_["position"] = "static";
        style_["width"] = "auto";
        style_["height"] = "auto";
        style_["min-width"] = "auto";
        style_["min-height"] = "auto";
        style_["max-width"] = "none";
        style_["max-height"] = "none";
        for (auto* p : {"margin", "padding"})
            for (auto* s : {"top", "right", "bottom", "left"})
                style_[std::string(p) + "-" + s] = "0";
        for (auto* s : {"top", "right", "bottom", "left"}) {
            style_[std::string("border-") + s + "-width"] = "0";
            style_[std::string("border-") + s + "-style"] = "none";
        }
        style_["box-sizing"] = "content-box";
        style_["font-size"] = "16px";
        style_["font-family"] = "monospace";
        style_["font-weight"] = "normal";
        style_["line-height"] = "normal";
        style_["overflow"] = "visible";
        style_["white-space"] = "normal";
        style_["text-align"] = "left";
        style_["vertical-align"] = "baseline";
        style_["direction"] = "ltr";
        style_["flex-direction"] = "row";
        style_["flex-wrap"] = "nowrap";
        style_["flex-grow"] = "0";
        style_["flex-shrink"] = "1";
        style_["flex-basis"] = "auto";
        style_["justify-content"] = "flex-start";
        style_["align-items"] = "stretch";
        style_["align-self"] = "auto";
        style_["align-content"] = "stretch";
        style_["row-gap"] = "0";
        style_["column-gap"] = "0";
        return *this;
    }
    SNode& textNode(const char* t) { isText = true; text = t; return *this; }
};

struct SMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

bool near(float a, float b, float tol = 0.5f) { return std::abs(a - b) < tol; }

} // namespace

// A flex item that is itself a column flex container with `width: 40%`: the
// row resolves its used width (400 of 1000) and hands it down; the column's
// stretched children are 400 wide, not 40% of 400.
static void testPercentWidthColumnFlexItem() {
    printf("--- sizing: %% width column-flex item ---\n");
    SNode row; row.init("flex"); row.style_["width"] = "1000px";
    SNode col; col.init("flex");
    col.style_["width"] = "40%"; col.style_["flex-direction"] = "column";
    SNode c; c.init();
    SNode t; t.textNode("x");
    c.addChild(&t); col.addChild(&c); row.addChild(&col);
    SMetrics m;
    layoutTree(&row, 1200, m);
    check(near(col.box.contentRect.width, 400), "column flex item is 40% of the row");
    check(near(c.box.contentRect.width, 400), "its stretched child fills it");

    // The same for a grid item.
    SNode row2; row2.init("flex"); row2.style_["width"] = "1000px";
    SNode grid; grid.init("grid"); grid.style_["width"] = "50%";
    grid.style_["grid-template-columns"] = "1fr 1fr";
    SNode g1; g1.init(); SNode g2; g2.init();
    grid.addChild(&g1); grid.addChild(&g2); row2.addChild(&grid);
    layoutTree(&row2, 1200, m);
    check(near(grid.box.contentRect.width, 500), "grid flex item is 50% of the row");
    check(near(g1.box.contentRect.width, 250), "its 1fr track is half of it");
}

// min-width / max-width clamp an auto-width inline-block (CSS2 §10.4).
static void testInlineBlockMinMaxWidth() {
    printf("--- sizing: inline-block min/max-width ---\n");
    SNode root; root.init();
    SNode ib; ib.init("inline-block"); ib.style_["min-width"] = "96px";
    SNode t; t.textNode("abc");   // 30px of text
    ib.addChild(&t); root.addChild(&ib);
    SNode after; after.textNode("X");
    root.addChild(&after);
    SMetrics m;
    layoutTree(&root, 800, m);
    check(near(ib.box.contentRect.width, 96), "min-width widens a short inline-block");
    check(!after.box.textRuns.empty() && near(after.box.textRuns[0].x, 96),
          "following text starts after the min-width box");

    SNode root2; root2.init();
    SNode ib2; ib2.init("inline-block"); ib2.style_["max-width"] = "50px";
    SNode t2; t2.textNode("abcdefgh");   // 80px of text
    ib2.addChild(&t2); root2.addChild(&ib2);
    layoutTree(&root2, 800, m);
    check(near(ib2.box.contentRect.width, 50), "max-width caps an inline-block");

    SNode root3; root3.init();
    SNode ib3; ib3.init("inline-block"); ib3.style_["min-width"] = "100px";
    ib3.style_["box-sizing"] = "border-box";
    ib3.style_["padding-left"] = "10px"; ib3.style_["padding-right"] = "10px";
    SNode t3; t3.textNode("a");
    ib3.addChild(&t3); root3.addChild(&ib3);
    layoutTree(&root3, 800, m);
    check(near(ib3.box.contentRect.width, 80), "border-box min-width names the border box");
}

// Hiding an element empties the boxes of everything inside it, not only its
// own: a descendant must not keep reporting the rect of its last visible
// layout.
static void testHiddenSubtreeClearsBoxes() {
    printf("--- display:none clears the whole subtree ---\n");
    SNode root; root.init();
    SNode panel; panel.init();
    SNode body; body.init();
    SNode btn; btn.init("inline-block");
    SNode t; t.textNode("Click me");
    btn.addChild(&t); body.addChild(&btn); panel.addChild(&body); root.addChild(&panel);
    SMetrics m;
    layoutTree(&root, 800, m);
    check(btn.box.contentRect.width > 0 && !t.box.textRuns.empty(), "button laid out while visible");

    body.style_["display"] = "none";
    markDirty(&body);
    layoutTree(&root, 800, m);
    check(body.box.contentRect.width == 0, "hidden body has no box");
    check(btn.box.contentRect.width == 0 && btn.box.contentRect.height == 0,
          "button inside it has no box either");
    check(t.box.textRuns.empty(), "its text has no runs");

    body.style_["display"] = "block";
    markDirty(&body);
    layoutTree(&root, 800, m);
    check(near(btn.box.contentRect.width, 80) && !t.box.textRuns.empty(),
          "shown again, the button is laid out again");
}

// An auto-width inline-flex / inline-grid is an atomic inline and shrinks to
// fit its content instead of filling the line.
static void testInlineFlexShrinksToFit() {
    printf("--- sizing: inline-flex / inline-grid shrink to fit ---\n");
    SNode root; root.init();
    SNode chip; chip.init("inline-flex");
    SNode t; t.textNode("chip");
    chip.addChild(&t); root.addChild(&chip);
    SMetrics m;
    layoutTree(&root, 800, m);
    check(near(chip.box.contentRect.width, 40), "inline-flex is as wide as its text");

    SNode root2; root2.init();
    SNode g; g.init("inline-grid"); g.style_["grid-template-columns"] = "30px 50px";
    SNode a; a.init(); SNode b; b.init();
    g.addChild(&a); g.addChild(&b); root2.addChild(&g);
    layoutTree(&root2, 800, m);
    check(near(g.box.contentRect.width, 80), "inline-grid is as wide as its tracks");

    // Still capped by the line it sits on.
    SNode root3; root3.init();
    SNode wide; wide.init("inline-flex"); wide.style_["flex-wrap"] = "wrap";
    SNode t3; t3.textNode("aaaa bbbb cccc dddd");
    wide.addChild(&t3); root3.addChild(&wide);
    layoutTree(&root3, 100, m);
    check(wide.box.contentRect.width <= 100.5f, "inline-flex never exceeds the line");
}

// A shrink-to-fit wrapping flex row sized to the exact sum of its items keeps
// them on one line even when the float sums differ in the last bit.
static void testFlexWrapExactFit() {
    printf("--- sizing: flex-wrap exact fit ---\n");
    SNode root; root.init();
    SNode row; row.init("flex");
    row.style_["flex-wrap"] = "wrap"; row.style_["column-gap"] = "20px";
    row.style_["position"] = "absolute";
    SNode a; a.init(); a.style_["width"] = "43.5177px";
    SNode b; b.init(); b.style_["width"] = "48.9414px";
    row.addChild(&a); row.addChild(&b); root.addChild(&row);
    SMetrics m;
    layoutTree(&root, 800, m);
    check(near(a.box.contentRect.y, b.box.contentRect.y), "both items on one line");

    // Pinned directly: a container a hair narrower than the float sum.
    SNode row2; row2.init("flex");
    row2.style_["flex-wrap"] = "wrap"; row2.style_["width"] = "112.459px";
    row2.style_["column-gap"] = "20px";
    SNode c; c.init(); c.style_["width"] = "43.5177px"; c.style_["flex-shrink"] = "0";
    SNode d; d.init(); d.style_["width"] = "48.9414px"; d.style_["flex-shrink"] = "0";
    row2.addChild(&c); row2.addChild(&d);
    layoutTree(&row2, 800, m);
    check(near(c.box.contentRect.y, d.box.contentRect.y), "last-bit overshoot does not wrap");
}

// table-layout: fixed sizes columns from <col> and the first row only.
static void testTableLayoutFixed() {
    printf("--- table-layout: fixed ---\n");
    SNode wrap; wrap.init(); wrap.style_["width"] = "400px";
    SNode table; table.init("table");
    table.style_["width"] = "100%"; table.style_["table-layout"] = "fixed";
    table.style_["border-collapse"] = "collapse";
    table.style_["border-spacing"] = "0";
    SNode col1; col1.init("table-column"); col1.tag = "col"; col1.style_["width"] = "100px";
    SNode col2; col2.init("table-column"); col2.tag = "col";
    SNode row; row.init("table-row"); row.tag = "tr";
    SNode c1; c1.init("table-cell"); c1.tag = "td";
    SNode c2; c2.init("table-cell"); c2.tag = "td"; c2.style_["white-space"] = "nowrap";
    c2.style_["overflow"] = "hidden"; c2.style_["overflow-x"] = "hidden";
    c2.style_["text-overflow"] = "ellipsis";
    SNode t1; t1.textNode("a");
    SNode t2; t2.textNode(std::string(200, 'x').c_str());
    c1.addChild(&t1); c2.addChild(&t2);
    row.addChild(&c1); row.addChild(&c2);
    table.addChild(&col1); table.addChild(&col2); table.addChild(&row);
    wrap.addChild(&table);
    SMetrics m;
    layoutTree(&wrap, 800, m);
    check(near(table.box.fullWidth(), 400), "fixed table keeps its 100% width");
    check(near(c1.box.fullWidth(), 100), "first column takes the <col> width");
    check(near(c2.box.fullWidth(), 300), "second column takes the rest, not its content");
    check(c2.box.textTruncated, "the nowrap cell's text is ellipsized");

    // Without <col>, the first row's cell widths decide; later rows cannot.
    SNode table2; table2.init("table");
    table2.style_["width"] = "300px"; table2.style_["table-layout"] = "fixed";
    table2.style_["border-spacing"] = "0";
    SNode r1; r1.init("table-row"); SNode r2; r2.init("table-row");
    SNode a1; a1.init("table-cell"); a1.style_["width"] = "50px";
    SNode a2; a2.init("table-cell");
    SNode b1; b1.init("table-cell"); b1.style_["width"] = "250px";
    SNode b2; b2.init("table-cell");
    r1.addChild(&a1); r1.addChild(&a2); r2.addChild(&b1); r2.addChild(&b2);
    table2.addChild(&r1); table2.addChild(&r2);
    layoutTree(&table2, 800, m);
    check(near(a1.box.fullWidth(), 50) && near(b1.box.fullWidth(), 50),
          "first row's width sets the column");
    check(near(a2.box.fullWidth(), 250), "the other column gets the rest");
}

// text-overflow: ellipsis truncates an overflowing line at the content edge.
static void testTextOverflowEllipsis() {
    printf("--- text-overflow: ellipsis ---\n");
    SNode box; box.init(); box.style_["width"] = "120px";
    box.style_["white-space"] = "nowrap";
    box.style_["overflow"] = "hidden"; box.style_["overflow-x"] = "hidden";
    box.style_["text-overflow"] = "ellipsis";
    SNode t; t.textNode("ARDY Motion text to G1 skeleton motion");
    box.addChild(&t);
    SMetrics m;   // 10px per byte; the ellipsis is 3 bytes = 30px
    layoutTree(&box, 800, m);
    check(box.box.textTruncated, "the block is marked truncated");
    float right = 0;
    bool hasEllipsis = false;
    for (auto& r : t.box.textRuns) {
        right = std::max(right, r.x + r.width);
        if (r.text.find("\xE2\x80\xA6") != std::string::npos) hasEllipsis = true;
    }
    check(hasEllipsis, "an ellipsis is drawn");
    check(right <= 120.5f, "the truncated line ends inside the box");

    // Fits: nothing happens.
    SNode box2; box2.init(); box2.style_["width"] = "400px";
    box2.style_["white-space"] = "nowrap";
    box2.style_["overflow"] = "hidden"; box2.style_["overflow-x"] = "hidden";
    box2.style_["text-overflow"] = "ellipsis";
    SNode t2; t2.textNode("short");
    box2.addChild(&t2);
    layoutTree(&box2, 800, m);
    check(!box2.box.textTruncated && t2.box.textRuns.size() == 1 &&
          t2.box.textRuns[0].text == "short", "text that fits is untouched");

    // overflow: visible never ellipsizes.
    SNode box3; box3.init(); box3.style_["width"] = "50px";
    box3.style_["white-space"] = "nowrap"; box3.style_["text-overflow"] = "ellipsis";
    SNode t3; t3.textNode("much too long");
    box3.addChild(&t3);
    layoutTree(&box3, 800, m);
    check(!box3.box.textTruncated, "overflow:visible does not ellipsize");

    // Widened again: the truncation from last pass is undone.
    box.style_["width"] = "600px";
    markDirty(&box);
    layoutTree(&box, 800, m);
    std::string all;
    for (auto& r : t.box.textRuns) all += r.text;
    check(!box.box.textTruncated && all.find("\xE2\x80\xA6") == std::string::npos &&
          all.find("motion") != std::string::npos, "relaid wider, the full text returns");
}

// Intrinsic sizes count letter-spacing the way layout does: one advance per
// code point (not per byte), the trailing one included.
static void testLetterSpacingIntrinsics() {
    printf("--- sizing: letter-spacing in intrinsic widths ---\n");
    SMetrics m;   // 10px per byte
    SNode dots; dots.init(); dots.style_["letter-spacing"] = "2px";
    SNode t; t.textNode("\xC2\xB7\xC2\xB7\xC2\xB7");   // three U+00B7, six bytes
    dots.addChild(&t);
    check(near(computeMinContentWidth(&dots, m), 66), "min-content: 3 code points of spacing");
    check(near(computeMaxContentWidth(&dots, m), 66), "max-content agrees");

    SNode nw; nw.init(); nw.style_["letter-spacing"] = "2px";
    nw.style_["white-space"] = "nowrap";
    SNode t2; t2.textNode("ab");
    nw.addChild(&t2);
    check(near(computeMaxContentWidth(&nw, m), 24), "nowrap max-content has the trailing slot");
    check(near(computeMinContentWidth(&nw, m), 24), "nowrap min-content too");

    // The blastgrid chip: a content-sized row in a centred column keeps its
    // fixed-width dot whole when a sibling carries its own letter-spacing.
    SNode col; col.init("flex");
    col.style_["flex-direction"] = "column"; col.style_["align-items"] = "center";
    SNode row; row.init("flex"); row.style_["column-gap"] = "7px";
    SNode dot; dot.init("block"); dot.style_["width"] = "11px"; dot.style_["height"] = "11px";
    SNode name; name.init(); SNode tn; tn.textNode("NAME"); name.addChild(&tn);
    SNode wins; wins.init(); wins.style_["letter-spacing"] = "2px";
    SNode tw; tw.textNode("\xC2\xB7\xC2\xB7\xC2\xB7"); wins.addChild(&tw);
    row.addChild(&dot); row.addChild(&name); row.addChild(&wins);
    col.addChild(&row);
    layoutTree(&col, 800, m);
    check(near(dot.box.contentRect.width, 11), "the dot keeps its 11px");
    check(near(wins.box.contentRect.width, 66), "the spaced item is its content width");
}

void testSizingFixes() {
    printf("=== Sizing fixes ===\n");
    testLetterSpacingIntrinsics();
    testInlineFlexShrinksToFit();
    testFlexWrapExactFit();
    testTableLayoutFixed();
    testTextOverflowEllipsis();
    testHiddenSubtreeClearsBoxes();
    testPercentWidthColumnFlexItem();
    testInlineBlockMinMaxWidth();
}
