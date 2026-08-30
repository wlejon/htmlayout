#include "test_flex.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

struct FlexMockNode : public LayoutNode {
    std::string tag;
    bool isText = false;
    std::string text;
    FlexMockNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override {
        return childNodes;
    }
    const ComputedStyle& computedStyle() const override { return style; }
    void addChild(FlexMockNode* c) { c->parentNode = this; childNodes.push_back(c); }
};

struct FlexTextMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

static bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

static void initFlexContainer(FlexMockNode& n) {
    n.tag = "div";
    n.style["display"] = "flex";
    n.style["flex-direction"] = "row";
    n.style["flex-wrap"] = "nowrap";
    n.style["justify-content"] = "flex-start";
    n.style["align-items"] = "stretch";
    n.style["align-content"] = "stretch";
    n.style["position"] = "static";
    n.style["width"] = "auto";
    n.style["height"] = "auto";
    n.style["min-width"] = "0"; n.style["min-height"] = "0";
    n.style["max-width"] = "none"; n.style["max-height"] = "none";
    n.style["margin-top"] = "0"; n.style["margin-right"] = "0";
    n.style["margin-bottom"] = "0"; n.style["margin-left"] = "0";
    n.style["padding-top"] = "0"; n.style["padding-right"] = "0";
    n.style["padding-bottom"] = "0"; n.style["padding-left"] = "0";
    n.style["border-top-width"] = "0"; n.style["border-right-width"] = "0";
    n.style["border-bottom-width"] = "0"; n.style["border-left-width"] = "0";
    n.style["border-top-style"] = "none"; n.style["border-right-style"] = "none";
    n.style["border-bottom-style"] = "none"; n.style["border-left-style"] = "none";
    n.style["box-sizing"] = "content-box";
    n.style["font-size"] = "16px";
    n.style["overflow"] = "visible";
    n.style["gap"] = "0"; n.style["row-gap"] = "0"; n.style["column-gap"] = "0";
}

static void initFlexItem(FlexMockNode& n) {
    initFlexContainer(n);
    n.style["display"] = "block";
    n.style["flex-grow"] = "0";
    n.style["flex-shrink"] = "1";
    n.style["flex-basis"] = "auto";
    n.style["align-self"] = "auto";
    n.style["order"] = "0";
}

// ========== Tests ==========

static void testFlexBasicRow() {
    printf("--- Flex: basic row ---\n");
    FlexMockNode root; initFlexContainer(root);
    FlexMockNode c1, c2, c3;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "50px";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "50px";
    initFlexItem(c3); c3.style["width"] = "100px"; c3.style["height"] = "50px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(c1.box.contentRect.x, 0), "flex row: c1 x=0");
    check(approx(c2.box.contentRect.x, 100), "flex row: c2 x=100");
    check(approx(c3.box.contentRect.x, 200), "flex row: c3 x=200");
    check(approx(c1.box.contentRect.y, c2.box.contentRect.y), "flex row: same y");
}

static void testFlexGrow() {
    printf("--- Flex: flex-grow ---\n");
    FlexMockNode root; initFlexContainer(root);
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["flex-basis"] = "100px"; c1.style["flex-grow"] = "1"; c1.style["height"] = "40px";
    initFlexItem(c2); c2.style["flex-basis"] = "100px"; c2.style["flex-grow"] = "3"; c2.style["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // Free space = 600 - 200 = 400. c1 gets 100, c2 gets 300.
    // c1 total = 200, c2 total = 400
    float c1w = c1.box.contentRect.width + c1.box.padding.left + c1.box.padding.right + c1.box.border.left + c1.box.border.right;
    float c2w = c2.box.contentRect.width + c2.box.padding.left + c2.box.padding.right + c2.box.border.left + c2.box.border.right;
    check(approx(c1w, 200), "flex-grow: c1 = 200px (100 + 100)");
    check(approx(c2w, 400), "flex-grow: c2 = 400px (100 + 300)");
}

static void testFlexShrink() {
    printf("--- Flex: flex-shrink ---\n");
    FlexMockNode root; initFlexContainer(root);
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["flex-basis"] = "400px"; c1.style["flex-shrink"] = "1"; c1.style["height"] = "40px";
    initFlexItem(c2); c2.style["flex-basis"] = "400px"; c2.style["flex-shrink"] = "3"; c2.style["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // Overflow = 800 - 600 = 200. Weighted: c1=400*1=400, c2=400*3=1200, total=1600
    // c1 shrinks by 200*400/1600 = 50 → 350
    // c2 shrinks by 200*1200/1600 = 150 → 250
    float c1w = c1.box.contentRect.width + c1.box.padding.left + c1.box.padding.right + c1.box.border.left + c1.box.border.right;
    float c2w = c2.box.contentRect.width + c2.box.padding.left + c2.box.padding.right + c2.box.border.left + c2.box.border.right;
    check(approx(c1w, 350), "flex-shrink: c1 = 350px");
    check(approx(c2w, 250), "flex-shrink: c2 = 250px");
}

static void testFlexColumn() {
    printf("--- Flex: column direction ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["flex-direction"] = "column";
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "50px";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "50px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(c1.box.contentRect.y, 0), "flex column: c1 y=0");
    check(c2.box.contentRect.y > c1.box.contentRect.y, "flex column: c2 below c1");
}

static void testFlexJustifyCenter() {
    printf("--- Flex: justify-content center ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["justify-content"] = "center";
    FlexMockNode c1;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "40px";
    root.addChild(&c1);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // Free = 500, center offset = 250
    check(approx(c1.box.contentRect.x, 250), "justify center: c1 at 250");
}

static void testFlexJustifySpaceBetween() {
    printf("--- Flex: justify-content space-between ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["justify-content"] = "space-between";
    FlexMockNode c1, c2, c3;
    initFlexItem(c1); c1.style["width"] = "50px"; c1.style["height"] = "30px";
    initFlexItem(c2); c2.style["width"] = "50px"; c2.style["height"] = "30px";
    initFlexItem(c3); c3.style["width"] = "50px"; c3.style["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    FlexTextMetrics m;
    layoutTree(&root, 300, m);

    // Free = 150, gaps = 150/2 = 75
    check(approx(c1.box.contentRect.x, 0), "space-between: c1 at 0");
    check(approx(c3.box.contentRect.x, 250), "space-between: c3 at 250");
}

static void testFlexJustifyFlexEnd() {
    printf("--- Flex: justify-content flex-end ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["justify-content"] = "flex-end";
    FlexMockNode c1;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "40px";
    root.addChild(&c1);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(c1.box.contentRect.x, 500), "justify flex-end: c1 at 500");
}

static void testFlexAlignCenter() {
    printf("--- Flex: align-items center ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["align-items"] = "center";
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "30px";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "60px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // Line cross = 60. c1 (h=30) centered → offset 15
    check(approx(c1.box.contentRect.y, 15), "align center: c1 centered at y=15");
    check(approx(c2.box.contentRect.y, 0), "align center: c2 at y=0");
}

static void testFlexAlignFlexEnd() {
    printf("--- Flex: align-items flex-end ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["align-items"] = "flex-end";
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "30px";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "60px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // Line cross = 60. c1 at bottom → 60 - 30 = 30
    check(approx(c1.box.contentRect.y, 30), "align flex-end: c1 at y=30");
    check(approx(c2.box.contentRect.y, 0), "align flex-end: c2 at y=0");
}

static void testFlexWrap() {
    printf("--- Flex: wrap ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["flex-wrap"] = "wrap";
    FlexMockNode c1, c2, c3;
    initFlexItem(c1); c1.style["width"] = "150px"; c1.style["height"] = "40px";
    initFlexItem(c2); c2.style["width"] = "150px"; c2.style["height"] = "40px";
    initFlexItem(c3); c3.style["width"] = "150px"; c3.style["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    FlexTextMetrics m;
    layoutTree(&root, 350, m);

    // 350px fits 2 items (300px), c3 wraps
    check(approx(c1.box.contentRect.y, c2.box.contentRect.y), "wrap: c1 and c2 same line");
    check(c3.box.contentRect.y > c1.box.contentRect.y, "wrap: c3 wraps to next line");
    check(approx(root.box.contentRect.height, 80), "wrap: container height = 2 lines × 40px");
}

static void testFlexWrapInsideColumn() {
    printf("--- Flex: wrap inside a column flex ---\n");
    // A wrapping row measured by a column-flex parent had its own previous
    // height handed back to it as a definite cross size, and every
    // default-stretch item was pre-set to that height before its layout: each
    // line came out as tall as the whole row had been, and the row grew
    // fivefold per pass (82 → 402 → 2002 → … 944,786px in a real sheet).
    // Items must be auto-height for the pre-stretch to bite, so each holds
    // one line of text (20px in this mock).
    FlexMockNode col; initFlexContainer(col);
    col.style["flex-direction"] = "column";
    col.style["width"] = "100px";
    FlexMockNode row; initFlexItem(row);
    row.style["display"] = "flex";
    row.style["flex-direction"] = "row";
    row.style["flex-wrap"] = "wrap";
    FlexMockNode c[5], t[5];
    for (int i = 0; i < 5; i++) {
        initFlexItem(c[i]);
        c[i].style["width"] = "30px";
        t[i].isText = true; t[i].text = "A";
        c[i].addChild(&t[i]);
        row.addChild(&c[i]);
    }
    col.addChild(&row);

    FlexTextMetrics m;
    layoutTree(&col, 100, m);
    // Three 30px items to a 100px line: two lines of one 20px text line each.
    check(approx(c[0].box.contentRect.y, c[2].box.contentRect.y), "wrap in column: first three share a line");
    check(approx(c[3].box.contentRect.y - c[0].box.contentRect.y, 20), "wrap in column: the second line is one line down");
    check(approx(row.box.contentRect.height, 40), "wrap in column: the row is exactly its two lines");
    check(approx(col.box.contentRect.height, 40), "wrap in column: and the column is the row");
    // The feedback needed a second pass to show; the incremental path re-lays
    // the same tree every frame.
    layoutTree(&col, 100, m);
    check(approx(row.box.contentRect.height, 40), "wrap in column: and it stays so on the next pass");
}

static void testFlexBaselineAlignment() {
    printf("--- Flex: baseline alignment ---\n");
    // Two one-line text items, 10px and 20px type, aligned on their
    // baselines. The mock's line box is 20px whatever the size, with a 16px
    // ascent, so both baselines sit 16px below their item's top and the
    // items must share a top edge. The old arithmetic took the font-size as
    // the ascent and dropped the smaller item by the 10px difference.
    FlexMockNode root; initFlexContainer(root);
    root.style["align-items"] = "baseline";
    FlexMockNode a, b, ta, tb;
    initFlexItem(a); initFlexItem(b);
    a.style["font-size"] = "10px";
    b.style["font-size"] = "20px";
    ta.isText = true; ta.text = "A";
    tb.isText = true; tb.text = "B";
    a.addChild(&ta); b.addChild(&tb);
    root.addChild(&a); root.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&root, 400, m);
    check(approx(a.box.contentRect.y, b.box.contentRect.y),
          "baseline: same line box, same ascent → same top edge whatever the font-size");

    // A taller line box moves the baseline down by its half-leading, and
    // the other item follows it. (A fresh tree: an unmarked style change is
    // invisible to the incremental path, which keeps the clean item's box.)
    FlexMockNode root2; initFlexContainer(root2);
    root2.style["align-items"] = "baseline";
    FlexMockNode a2, b2, ta2, tb2;
    initFlexItem(a2); initFlexItem(b2);
    a2.style["font-size"] = "10px";
    b2.style["font-size"] = "20px";
    b2.style["line-height"] = "30px";
    ta2.isText = true; ta2.text = "A";
    tb2.isText = true; tb2.text = "B";
    a2.addChild(&ta2); b2.addChild(&tb2);
    root2.addChild(&a2); root2.addChild(&b2);
    layoutTree(&root2, 400, m);
    check(approx(a2.box.contentRect.y - b2.box.contentRect.y, 5),
          "baseline: a 30px line box puts 5px of half-leading above the baseline");
}

static void testFlexGap() {
    printf("--- Flex: gap ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["column-gap"] = "20px";
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "40px";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // c1 at 0, c2 at 100 + 20gap = 120
    check(approx(c2.box.contentRect.x, 120), "gap: c2 at 120 (100 + 20 gap)");
}

static void testFlexOrder() {
    printf("--- Flex: order ---\n");
    FlexMockNode root; initFlexContainer(root);
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "40px"; c1.style["order"] = "2";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "40px"; c2.style["order"] = "1";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // c2 (order=1) should come before c1 (order=2)
    check(c2.box.contentRect.x < c1.box.contentRect.x, "order: c2 (order=1) before c1 (order=2)");
}

static void testFlexDisplayNone() {
    printf("--- Flex: display none ---\n");
    FlexMockNode root; initFlexContainer(root);
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "40px";
    initFlexItem(c2); c2.style["display"] = "none"; c2.style["width"] = "200px"; c2.style["height"] = "200px";
    root.addChild(&c1); root.addChild(&c2);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(c2.box.contentRect.width, 0), "display:none flex item has zero width");
    check(approx(root.box.contentRect.height, 40), "container height unaffected by hidden item");
}

static void testFlexAlignStretch() {
    printf("--- Flex: align-items stretch ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["align-items"] = "stretch";
    FlexMockNode c1, c2;
    initFlexItem(c1); c1.style["width"] = "100px"; c1.style["height"] = "30px";
    initFlexItem(c2); c2.style["width"] = "100px"; c2.style["height"] = "60px";
    // c1 has no explicit height for stretch to apply, but we set h=30 explicitly
    // Let's test with auto height
    FlexMockNode c3;
    initFlexItem(c3); c3.style["width"] = "100px"; c3.style["height"] = "auto";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // Line cross = 60 (from c2). c3 with auto height should stretch to 60
    check(approx(c3.box.contentRect.height, 60), "stretch: auto-height item stretches to line height");
}

static void testFlexAbsolutePositioning() {
    printf("--- Flex: absolute positioning ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["position"] = "relative";
    root.style["width"] = "600px";
    root.style["height"] = "400px";

    FlexMockNode inflow;
    initFlexItem(inflow); inflow.style["width"] = "100px"; inflow.style["height"] = "50px";

    FlexMockNode absChild;
    initFlexItem(absChild);
    absChild.style["position"] = "absolute";
    absChild.style["width"] = "80px";
    absChild.style["height"] = "40px";
    absChild.style["top"] = "10px";
    absChild.style["left"] = "20px";

    root.addChild(&inflow);
    root.addChild(&absChild);

    FlexTextMetrics m;
    layoutTree(&root, 800, m);

    check(approx(absChild.box.contentRect.x, 20), "flex abs: x = left:20");
    check(approx(absChild.box.contentRect.y, 10), "flex abs: y = top:10");
    check(approx(absChild.box.contentRect.width, 80), "flex abs: width preserved");
    check(approx(absChild.box.contentRect.height, 40), "flex abs: height preserved");
    // In-flow item unaffected
    check(approx(inflow.box.contentRect.x, 0), "flex abs: in-flow item at x=0");
}

static void testFlexAbsoluteBottomRight() {
    printf("--- Flex: absolute bottom/right ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["position"] = "relative";
    root.style["width"] = "500px";
    root.style["height"] = "300px";

    FlexMockNode absChild;
    initFlexItem(absChild);
    absChild.style["position"] = "absolute";
    absChild.style["width"] = "100px";
    absChild.style["height"] = "50px";
    absChild.style["bottom"] = "10px";
    absChild.style["right"] = "20px";

    root.addChild(&absChild);

    FlexTextMetrics m;
    layoutTree(&root, 800, m);

    // x = 500 - 20 - 100 = 380
    check(approx(absChild.box.contentRect.x, 380), "flex abs bottom/right: x = 380");
    // y = 300 - 10 - 50 = 240
    check(approx(absChild.box.contentRect.y, 240), "flex abs bottom/right: y = 240");
}

static void testFlexAbsoluteStretch() {
    printf("--- Flex: absolute stretch ---\n");
    FlexMockNode root; initFlexContainer(root);
    root.style["position"] = "relative";
    root.style["width"] = "600px";
    root.style["height"] = "400px";

    FlexMockNode absChild;
    initFlexItem(absChild);
    absChild.style["position"] = "absolute";
    absChild.style["top"] = "10px";
    absChild.style["bottom"] = "20px";
    absChild.style["left"] = "30px";
    absChild.style["right"] = "40px";

    root.addChild(&absChild);

    FlexTextMetrics m;
    layoutTree(&root, 800, m);

    // width = 600 - 30 - 40 = 530
    check(approx(absChild.box.contentRect.width, 530), "flex abs stretch: width = 530");
    // height = 400 - 10 - 20 = 370
    check(approx(absChild.box.contentRect.height, 370), "flex abs stretch: height = 370");
}

// ========== align-content tests ==========

static void testFlexAlignContentCenter() {
    // Two wrapping lines in a 300px tall container, lines should be centered
    FlexMockNode container; initFlexContainer(container);
    container.style["flex-wrap"] = "wrap";
    container.style["width"] = "200px";
    container.style["height"] = "300px";
    container.style["align-content"] = "center";

    // Two items each 120px wide -> wraps into 2 lines
    FlexMockNode a; initFlexItem(a); a.style["width"] = "120px"; a.style["height"] = "40px";
    FlexMockNode b; initFlexItem(b); b.style["width"] = "120px"; b.style["height"] = "40px";
    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // Total cross = 40 + 40 = 80, free = 300 - 80 = 220, offset = 110
    check(approx(a.box.contentRect.y, 110, 2), "align-content center: first line offset");
    check(approx(b.box.contentRect.y, 150, 2), "align-content center: second line offset");
}

static void testFlexAlignContentSpaceBetween() {
    FlexMockNode container; initFlexContainer(container);
    container.style["flex-wrap"] = "wrap";
    container.style["width"] = "200px";
    container.style["height"] = "300px";
    container.style["align-content"] = "space-between";

    FlexMockNode a; initFlexItem(a); a.style["width"] = "120px"; a.style["height"] = "40px";
    FlexMockNode b; initFlexItem(b); b.style["width"] = "120px"; b.style["height"] = "40px";
    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // First line at top, second at bottom (300 - 40 = 260)
    check(approx(a.box.contentRect.y, 0, 2), "align-content space-between: first at top");
    check(approx(b.box.contentRect.y, 260, 2), "align-content space-between: second at bottom");
}

static void testFlexAlignContentFlexEnd() {
    FlexMockNode container; initFlexContainer(container);
    container.style["flex-wrap"] = "wrap";
    container.style["width"] = "200px";
    container.style["height"] = "300px";
    container.style["align-content"] = "flex-end";

    FlexMockNode a; initFlexItem(a); a.style["width"] = "120px"; a.style["height"] = "40px";
    FlexMockNode b; initFlexItem(b); b.style["width"] = "120px"; b.style["height"] = "40px";
    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // Free = 220, first line at 220, second at 260
    check(approx(a.box.contentRect.y, 220, 2), "align-content flex-end: first line");
    check(approx(b.box.contentRect.y, 260, 2), "align-content flex-end: second line");
}

static void testFlexAlignContentSpaceAround() {
    FlexMockNode container; initFlexContainer(container);
    container.style["flex-wrap"] = "wrap";
    container.style["width"] = "200px";
    container.style["height"] = "300px";
    container.style["align-content"] = "space-around";

    FlexMockNode a; initFlexItem(a); a.style["width"] = "120px"; a.style["height"] = "40px";
    FlexMockNode b; initFlexItem(b); b.style["width"] = "120px"; b.style["height"] = "40px";
    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // Free = 220, per-line gap = 110, half-gap = 55
    // First line at 55, second at 55 + 40 + 110 = 205
    check(approx(a.box.contentRect.y, 55, 2), "align-content space-around: first line");
    check(approx(b.box.contentRect.y, 205, 2), "align-content space-around: second line");
}

static void testFlexAlignContentSpaceEvenly() {
    FlexMockNode container; initFlexContainer(container);
    container.style["flex-wrap"] = "wrap";
    container.style["width"] = "200px";
    container.style["height"] = "300px";
    container.style["align-content"] = "space-evenly";

    FlexMockNode a; initFlexItem(a); a.style["width"] = "120px"; a.style["height"] = "40px";
    FlexMockNode b; initFlexItem(b); b.style["width"] = "120px"; b.style["height"] = "40px";
    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // Free = 220, gaps = 220/3 ≈ 73.3
    // First at 73.3, second at 73.3 + 40 + 73.3 = 186.6
    check(approx(a.box.contentRect.y, 73, 2), "align-content space-evenly: first line");
    check(approx(b.box.contentRect.y, 187, 2), "align-content space-evenly: second line");
}

static void testFlexAlignContentStretch() {
    FlexMockNode container; initFlexContainer(container);
    container.style["flex-wrap"] = "wrap";
    container.style["width"] = "200px";
    container.style["height"] = "300px";
    container.style["align-content"] = "stretch";

    FlexMockNode a; initFlexItem(a); a.style["width"] = "120px"; a.style["height"] = "auto";
    FlexMockNode b; initFlexItem(b); b.style["width"] = "120px"; b.style["height"] = "auto";
    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // Each line gets 150px cross size (300/2), items should stretch to fill
    check(approx(a.box.contentRect.y, 0, 2), "align-content stretch: first at top");
    check(approx(b.box.contentRect.y, 150, 2), "align-content stretch: second at midpoint");
    // Items with auto height should have been stretched
    check(approx(a.box.contentRect.height, 150, 2), "align-content stretch: first item height");
    check(approx(b.box.contentRect.height, 150, 2), "align-content stretch: second item height");
}

// ========== baseline alignment tests ==========

static void testFlexAlignBaseline() {
    // Two items with different font sizes should align on baseline
    FlexMockNode container; initFlexContainer(container);
    container.style["align-items"] = "baseline";
    container.style["width"] = "400px";

    FlexMockNode a; initFlexItem(a);
    a.style["width"] = "100px"; a.style["height"] = "40px";
    a.style["font-size"] = "16px";
    a.style["padding-top"] = "10px";

    FlexMockNode b; initFlexItem(b);
    b.style["width"] = "100px"; b.style["height"] = "40px";
    b.style["font-size"] = "32px";
    b.style["padding-top"] = "5px";

    container.addChild(&a); container.addChild(&b);

    FlexTextMetrics m;
    layoutTree(&container, 400, m);

    // Neither item has inline content, so each baseline is synthesized from
    // its font: half-leading, then the ascent. This mock's line box is 20px
    // and its ascent 16px at every size, so the font-size does not move the
    // baseline at all — only the padding does:
    //   a baseline = padding(10) + 16 = 26
    //   b baseline = padding(5)  + 16 = 21
    // so b is pushed down by 5 to meet a. (The arithmetic this replaced took
    // the font-size itself as the ascent, which put b's baseline at 37 and
    // pushed a down instead — every smaller label sank under its neighbour.)
    check(approx(b.box.contentRect.y - b.box.padding.top,
                 a.box.contentRect.y - a.box.padding.top + 5.0f),
          "flex baseline: the item with less padding above its baseline is pushed down");
    // contentRect.y already includes margin+padding+border from the container
    // top; the baseline is the ascent (16) below the content top for both.
    check(approx(a.box.contentRect.y + 16.0f, b.box.contentRect.y + 16.0f),
          "flex baseline: baselines aligned");
}

static void testFlexShrunkItemChildWidth() {
    printf("--- Flex: children of a shrunk item see the flexed width ---\n");
    // Container 760px; 4 items each width:180 + padding-left:40 ->
    // hypothetical 220 each, 880 total, shrink 30 each -> border box 190,
    // content 150. A block child must be laid out against the flexed
    // content width (150), not the specified style width (180).
    FlexMockNode root; initFlexContainer(root);
    root.style["width"] = "760px";

    FlexMockNode items[4];
    FlexMockNode kids[4];
    for (int i = 0; i < 4; i++) {
        initFlexItem(items[i]);
        items[i].style["width"] = "180px";
        items[i].style["padding-left"] = "40px";
        initFlexItem(kids[i]);          // plain block child
        kids[i].style["display"] = "block";
        kids[i].style["height"] = "10px";
        items[i].addChild(&kids[i]);
        root.addChild(&items[i]);
    }

    FlexTextMetrics m;
    layoutTree(&root, 800, m);

    check(approx(items[0].box.contentRect.width, 150),
          "shrunk item: content width = 190 - 40 padding");
    check(approx(kids[0].box.contentRect.width, 150),
          "shrunk item child: laid out at flexed width, not style width");
    check(approx(items[1].box.contentRect.x - items[0].box.contentRect.x, 190),
          "shrunk items: packed at 190px pitch");
}

static void testFlexColumnMaxHeightClamp() {
    printf("--- Flex: column main size clamped by max-height ---\n");
    // A column flex container with height:900px but max-height:400px must
    // distribute free space over the CLAMPED 400px, not the 900px height:
    // a fixed header + footer plus a flex:1 body should fit inside 400, with
    // the body shrunk to 400-40-50=310. Without the clamp the body grows to
    // 810 (900-40-50) and the footer is pushed outside the box — the modal
    // dialog bug (scrollable list overgrows, fixed footer clipped away).
    FlexMockNode root; initFlexContainer(root);
    root.style["flex-direction"] = "column";
    root.style["width"] = "300px";
    root.style["height"] = "900px";
    root.style["max-height"] = "400px";

    FlexMockNode head, body, foot;
    initFlexItem(head); head.style["height"] = "40px"; head.style["flex-shrink"] = "0";
    initFlexItem(body); body.style["flex-grow"] = "1"; body.style["flex-shrink"] = "1";
    body.style["overflow"] = "auto";
    initFlexItem(foot); foot.style["height"] = "50px"; foot.style["flex-shrink"] = "0";
    root.addChild(&head); root.addChild(&body); root.addChild(&foot);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(body.box.contentRect.height, 310),
          "column max-height: flex body shrunk to fit clamped height (310)");
    float footBottom = foot.box.contentRect.y + foot.box.contentRect.height;
    check(footBottom <= 400 + 1,
          "column max-height: fixed footer stays inside the 400px box");
}

static void testFlexColumnAutoHeightGrow() {
    printf("--- Flex: auto-height column is sized by its contents ---\n");
    // A column flex container with no height has an INDEFINITE main size, so
    // there is no free space and `flex-grow` has nothing to grow into: the
    // container is the height of what is in it. The main size fell back to the
    // container's *width* for want of anything better, which a growing item
    // then filled — a 300px-wide column came out 300px tall, and one with a
    // max-height came out exactly its max-height whatever was in it, the
    // end-of-function clamp being all that stopped it.
    //
    // The item is `flex: 1; min-height: 0` — the universal scroll pane, whose
    // flex base is 0 and whose automatic minimum is switched off, so it is also
    // the case that says the contribution must be the item's *content* height:
    // count the base and the container collapses to 20 instead.
    FlexMockNode root; initFlexContainer(root);
    root.style["flex-direction"] = "column";
    root.style["width"] = "300px";

    FlexMockNode head, body, text;
    initFlexItem(head); head.style["height"] = "20px"; head.style["flex-shrink"] = "0";
    initFlexItem(body);
    body.style["flex-grow"] = "1"; body.style["flex-basis"] = "0";
    body.style["min-height"] = "0";
    text.tag = "#text"; text.isText = true; text.text = "log"; initFlexItem(text);
    body.addChild(&text);
    root.addChild(&head); root.addChild(&body);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    // One 20px line of text in the body, under a 20px head.
    check(approx(body.box.contentRect.height, 20),
          "auto-height column: growing item is its content height, not the width");
    check(approx(root.box.contentRect.height, 40),
          "auto-height column: container is 40 (head + body), not its 300px width");

    // And with a max-height on it: the cap is a cap, not the height.
    root.style["max-height"] = "200px";
    markSubtreeDirty(&root);
    layoutTree(&root, 600, m);
    check(approx(root.box.contentRect.height, 40),
          "auto-height column: max-height caps, it does not become the height");
}

static void testFlexBorderBoxMinWidth() {
    printf("--- Flex: min-width under box-sizing:border-box ---\n");
    // The padded-button case. `min-width:30px` with border-box sizing and 8px
    // of padding a side means the BORDER box is at least 30, so the content box
    // is at least 14. Clamping the content box to 30 instead lays the single
    // item out across a 30px main axis inside a 14px content box, and
    // justify-content:center then centres it in that phantom width: the item
    // lands ~8px right of where it belongs, flush against the right border.
    //
    // Here the content box is 40 (a 60px box less 2×10 padding) and the item is
    // 20 wide, so centring must put it 10 from the content edge whatever the
    // min-width says. Clamping to the raw 60 gives 20 — half the item hanging
    // out past the padding.
    FlexMockNode root; initFlexContainer(root);
    root.style["box-sizing"] = "border-box";
    root.style["width"] = "60px";
    root.style["min-width"] = "60px";
    root.style["padding-left"] = "10px";
    root.style["padding-right"] = "10px";
    root.style["justify-content"] = "center";

    FlexMockNode item;
    initFlexItem(item); item.style["width"] = "20px"; item.style["height"] = "10px";
    item.style["flex-shrink"] = "0";
    root.addChild(&item);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(root.box.contentRect.width, 40),
          "border-box min-width: content box is the 60px box less its padding");
    check(approx(item.box.contentRect.x, 10),
          "border-box min-width: item centred in the content box, not in a phantom 60");
    check(item.box.contentRect.x + item.box.contentRect.width
              <= root.box.contentRect.width + 1,
          "border-box min-width: and does not run out through the right padding");
}

static void testFlexBorderBoxColumnHeight() {
    printf("--- Flex: column height under box-sizing:border-box ---\n");
    // Same confusion on the block axis: `height:100px` with border-box sizing
    // and 10px of padding top and bottom leaves 80px of content. A column
    // container that distributes free space over 100 instead grows its flexible
    // item 20px too tall, and the bottom of the last item falls outside the box.
    FlexMockNode root; initFlexContainer(root);
    root.style["flex-direction"] = "column";
    root.style["box-sizing"] = "border-box";
    root.style["width"] = "100px";
    root.style["height"] = "100px";
    root.style["padding-top"] = "10px";
    root.style["padding-bottom"] = "10px";

    FlexMockNode body;
    initFlexItem(body); body.style["flex-grow"] = "1"; body.style["flex-basis"] = "0";
    root.addChild(&body);

    FlexTextMetrics m;
    layoutTree(&root, 600, m);

    check(approx(body.box.contentRect.height, 80),
          "border-box column: flexible item fills the content box, not the border box");
}

// ========== Entry point ==========

void testFlexLayout() {
    testFlexBorderBoxMinWidth();
    testFlexBorderBoxColumnHeight();
    testFlexColumnMaxHeightClamp();
    testFlexColumnAutoHeightGrow();
    testFlexBasicRow();
    testFlexGrow();
    testFlexShrink();
    testFlexColumn();
    testFlexJustifyCenter();
    testFlexJustifySpaceBetween();
    testFlexJustifyFlexEnd();
    testFlexAlignCenter();
    testFlexAlignFlexEnd();
    testFlexWrap();
    testFlexWrapInsideColumn();
    testFlexBaselineAlignment();
    testFlexGap();
    testFlexOrder();
    testFlexDisplayNone();
    testFlexAlignStretch();

    // Absolute positioning in flex
    testFlexAbsolutePositioning();
    testFlexAbsoluteBottomRight();
    testFlexAbsoluteStretch();

    // baseline alignment
    testFlexAlignBaseline();

    // align-content
    testFlexAlignContentCenter();
    testFlexAlignContentSpaceBetween();
    testFlexAlignContentFlexEnd();
    testFlexAlignContentSpaceAround();
    testFlexAlignContentSpaceEvenly();
    testFlexAlignContentStretch();
    testFlexShrunkItemChildWidth();
}
