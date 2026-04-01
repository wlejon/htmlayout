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
    std::vector<FlexMockNode*> childNodes;
    ComputedStyle style;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style; }
    void addChild(FlexMockNode* c) { c->parentNode = this; childNodes.push_back(c); }
};

struct FlexTextMetrics : public TextMetrics {
    float measureWidth(const std::string& t, const std::string&, float, const std::string&) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(const std::string&, float, const std::string&) override { return 20.0f; }
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

    // Item b has larger font, so its baseline is lower.
    // Item a should be pushed down so baselines align.
    // a baseline = margin(0) + border(0) + padding(10) + font(16) = 26
    // b baseline = margin(0) + border(0) + padding(5) + font(32) = 37
    // a should be offset by 37 - 26 = 11
    check(a.box.contentRect.y > b.box.contentRect.y,
          "flex baseline: smaller font item pushed down");
    // contentRect.y already includes margin+padding+border from container top
    // baseline = contentRect.y + fontSize (baseline is fontSize below content top)
    float aBaseline = a.box.contentRect.y + 16.0f;
    float bBaseline = b.box.contentRect.y + 32.0f;
    check(approx(aBaseline, bBaseline, 2), "flex baseline: baselines aligned");
}

// ========== Entry point ==========

void testFlexLayout() {
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
}
