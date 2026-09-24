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
    testHiddenSubtreeClearsBoxes();
    testPercentWidthColumnFlexItem();
    testInlineBlockMinMaxWidth();
}
