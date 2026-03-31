#include "test_layout.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

// ---- Mock LayoutNode for testing ----

struct MockLayoutNode : public LayoutNode {
    std::string tag;
    bool isText = false;
    std::string text;
    MockLayoutNode* parentNode = nullptr;
    std::vector<MockLayoutNode*> childNodes;
    ComputedStyle style;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style; }

    void addChild(MockLayoutNode* child) {
        child->parentNode = this;
        childNodes.push_back(child);
    }

    void setDisplay(const std::string& val) { style["display"] = val; }
    void setWidth(const std::string& val) { style["width"] = val; }
    void setHeight(const std::string& val) { style["height"] = val; }
    void setMargin(const std::string& val) {
        style["margin-top"] = val; style["margin-right"] = val;
        style["margin-bottom"] = val; style["margin-left"] = val;
    }
    void setPadding(const std::string& val) {
        style["padding-top"] = val; style["padding-right"] = val;
        style["padding-bottom"] = val; style["padding-left"] = val;
    }
};

// ---- Mock TextMetrics (simple fixed-width) ----

struct MockTextMetrics : public TextMetrics {
    float measureWidth(const std::string& text,
                       const std::string&, float fontSize,
                       const std::string&) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(const std::string&, float fontSize,
                     const std::string&) override {
        return fontSize * 1.2f;
    }
};

static bool approx(float a, float b, float tol = 0.01f) {
    return std::abs(a - b) < tol;
}

// Helper to set up a block node with all default styles
static void initBlock(MockLayoutNode& node, const std::string& tagName = "div") {
    node.tag = tagName;
    node.setDisplay("block");
    node.style["position"] = "static";
    node.style["width"] = "auto";
    node.style["height"] = "auto";
    node.style["min-width"] = "0";
    node.style["min-height"] = "0";
    node.style["max-width"] = "none";
    node.style["max-height"] = "none";
    node.style["margin-top"] = "0";
    node.style["margin-right"] = "0";
    node.style["margin-bottom"] = "0";
    node.style["margin-left"] = "0";
    node.style["padding-top"] = "0";
    node.style["padding-right"] = "0";
    node.style["padding-bottom"] = "0";
    node.style["padding-left"] = "0";
    node.style["border-top-width"] = "0";
    node.style["border-right-width"] = "0";
    node.style["border-bottom-width"] = "0";
    node.style["border-left-width"] = "0";
    node.style["border-top-style"] = "none";
    node.style["border-right-style"] = "none";
    node.style["border-bottom-style"] = "none";
    node.style["border-left-style"] = "none";
    node.style["box-sizing"] = "content-box";
    node.style["font-size"] = "16px";
    node.style["overflow"] = "visible";
}

// ========== Length Resolution Tests ==========

static void testResolveLengthPx() {
    printf("--- Layout: resolveLength px ---\n");
    check(approx(resolveLength("10px", 100, 16), 10.0f), "10px = 10");
    check(approx(resolveLength("0px", 100, 16), 0.0f), "0px = 0");
    check(approx(resolveLength("100px", 200, 16), 100.0f), "100px = 100");
    check(approx(resolveLength("3.5px", 100, 16), 3.5f), "3.5px = 3.5");
}

static void testResolveLengthEm() {
    printf("--- Layout: resolveLength em ---\n");
    check(approx(resolveLength("1em", 100, 16), 16.0f), "1em at 16px = 16");
    check(approx(resolveLength("2em", 100, 20), 40.0f), "2em at 20px = 40");
    check(approx(resolveLength("0.5em", 100, 16), 8.0f), "0.5em at 16px = 8");
}

static void testResolveLengthPercent() {
    printf("--- Layout: resolveLength % ---\n");
    check(approx(resolveLength("50%", 200, 16), 100.0f), "50% of 200 = 100");
    check(approx(resolveLength("100%", 800, 16), 800.0f), "100% of 800 = 800");
    check(approx(resolveLength("25%", 400, 16), 100.0f), "25% of 400 = 100");
}

static void testResolveLengthAuto() {
    printf("--- Layout: resolveLength auto/none ---\n");
    check(approx(resolveLength("auto", 100, 16), 0.0f), "auto = 0");
    check(approx(resolveLength("none", 100, 16), 0.0f), "none = 0");
    check(approx(resolveLength("", 100, 16), 0.0f), "empty = 0");
}

static void testResolveLengthNamed() {
    printf("--- Layout: resolveLength named ---\n");
    check(approx(resolveLength("thin", 100, 16), 1.0f), "thin = 1");
    check(approx(resolveLength("medium", 100, 16), 3.0f), "medium = 3");
    check(approx(resolveLength("thick", 100, 16), 5.0f), "thick = 5");
}

static void testResolveLengthUnitless() {
    printf("--- Layout: resolveLength unitless ---\n");
    check(approx(resolveLength("42", 100, 16), 42.0f), "42 (unitless) = 42");
}

// ========== Block Layout Tests ==========

static void testBlockSingleChild() {
    printf("--- Layout: block single child ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(root.box.contentRect.width, 800.0f), "root width fills viewport");
    check(approx(root.box.contentRect.height, 50.0f), "root height shrinks to child");
    check(approx(child.box.contentRect.width, 800.0f), "child fills parent width");
    check(approx(child.box.contentRect.height, 50.0f), "child has explicit height");
}

static void testBlockMultipleChildren() {
    printf("--- Layout: block multiple children ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode c1, c2, c3;
    initBlock(c1); c1.setHeight("30px");
    initBlock(c2); c2.setHeight("40px");
    initBlock(c3); c3.setHeight("50px");

    root.addChild(&c1);
    root.addChild(&c2);
    root.addChild(&c3);

    MockTextMetrics metrics;
    layoutTree(&root, 600.0f, metrics);

    check(approx(root.box.contentRect.height, 120.0f), "root height = sum of children (30+40+50)");
    check(approx(c1.box.contentRect.y, 0.0f), "child1 y = 0");
    check(approx(c2.box.contentRect.y, 30.0f), "child2 y = 30");
    check(approx(c3.box.contentRect.y, 70.0f), "child3 y = 70");
}

static void testBlockExplicitWidth() {
    printf("--- Layout: block explicit width ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setWidth("400px");
    child.setHeight("50px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.width, 400.0f), "child has explicit 400px width");
}

static void testBlockExplicitHeight() {
    printf("--- Layout: block explicit height ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setHeight("200px");

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(root.box.contentRect.height, 200.0f), "root has explicit 200px height (not shrink-wrapped)");
}

static void testBlockMargin() {
    printf("--- Layout: block with margin ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");
    child.style["margin-top"] = "10px";
    child.style["margin-bottom"] = "20px";
    child.style["margin-left"] = "15px";

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.margin.top, 10.0f), "child margin-top = 10");
    check(approx(child.box.margin.bottom, 20.0f), "child margin-bottom = 20");
    check(approx(child.box.margin.left, 15.0f), "child margin-left = 15");
    // Root height should include child margins
    check(approx(root.box.contentRect.height, 80.0f), "root height = 10 + 50 + 20 = 80");
}

static void testBlockMarginCollapsing() {
    printf("--- Layout: margin collapsing ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode c1, c2;
    initBlock(c1); c1.setHeight("30px");
    c1.style["margin-bottom"] = "20px";

    initBlock(c2); c2.setHeight("40px");
    c2.style["margin-top"] = "30px";

    root.addChild(&c1);
    root.addChild(&c2);

    MockTextMetrics metrics;
    layoutTree(&root, 600.0f, metrics);

    // Margins collapse: max(20, 30) = 30px gap between boxes
    // c1: y=0, h=30; gap=30; c2: y=60, h=40
    check(approx(c2.box.contentRect.y, 60.0f), "collapsed margin: c2 y = 30 + max(20,30) = 60");
    // Total height: 30 + 30(collapsed) + 40 + 0(c1 has no top margin, c2 has no bottom margin)
    // Actually: c1 top margin (0) + c1 (30) + collapsed(30) + c2 (40) + c2 bottom margin (0) = 100
    check(approx(root.box.contentRect.height, 100.0f), "root height with collapsed margins = 100");
}

static void testBlockPadding() {
    printf("--- Layout: block with padding ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setPadding("10px");

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(root.box.padding.top, 10.0f), "root padding-top = 10");
    check(approx(root.box.padding.left, 10.0f), "root padding-left = 10");
    // Content width = 800 - 10 - 10 = 780 (padding eats into available)
    check(approx(root.box.contentRect.width, 780.0f), "root content width = 800 - 20 padding");
    // Child fills content area
    check(approx(child.box.contentRect.width, 780.0f), "child fills parent content width");
}

static void testBlockBorder() {
    printf("--- Layout: block with border ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.style["border-top-style"] = "solid";
    root.style["border-top-width"] = "2px";
    root.style["border-bottom-style"] = "solid";
    root.style["border-bottom-width"] = "2px";
    root.style["border-left-style"] = "solid";
    root.style["border-left-width"] = "3px";
    root.style["border-right-style"] = "solid";
    root.style["border-right-width"] = "3px";

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(root.box.border.top, 2.0f), "root border-top = 2");
    check(approx(root.box.border.left, 3.0f), "root border-left = 3");
    check(approx(root.box.contentRect.width, 794.0f), "content width = 800 - 3 - 3 = 794");
}

static void testBlockPercentWidth() {
    printf("--- Layout: block percent width ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setWidth("50%");
    child.setHeight("30px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.width, 400.0f), "50% of 800 = 400");
}

static void testBlockDisplayNone() {
    printf("--- Layout: display none ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode c1, c2;
    initBlock(c1); c1.setHeight("30px");
    initBlock(c2); c2.setHeight("40px");
    c2.setDisplay("none");

    root.addChild(&c1);
    root.addChild(&c2);

    MockTextMetrics metrics;
    layoutTree(&root, 600.0f, metrics);

    check(approx(root.box.contentRect.height, 30.0f), "root height only includes visible child");
    check(approx(c2.box.contentRect.width, 0.0f), "hidden child has zero width");
    check(approx(c2.box.contentRect.height, 0.0f), "hidden child has zero height");
}

static void testBlockBoxSizing() {
    printf("--- Layout: border-box sizing ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setWidth("200px");
    child.setHeight("100px");
    child.setPadding("10px");
    child.style["border-top-style"] = "solid";
    child.style["border-top-width"] = "5px";
    child.style["border-bottom-style"] = "solid";
    child.style["border-bottom-width"] = "5px";
    child.style["border-left-style"] = "solid";
    child.style["border-left-width"] = "5px";
    child.style["border-right-style"] = "solid";
    child.style["border-right-width"] = "5px";
    child.style["box-sizing"] = "border-box";

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // border-box: 200px total = content + padding + border
    // content = 200 - 10 - 10 - 5 - 5 = 170
    check(approx(child.box.contentRect.width, 170.0f), "border-box: content width = 200 - 30 = 170");
    // height: 100 - 10 - 10 - 5 - 5 = 70
    check(approx(child.box.contentRect.height, 70.0f), "border-box: content height = 100 - 30 = 70");
}

static void testBlockMinMaxWidth() {
    printf("--- Layout: min/max width ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setWidth("50px");
    child.setHeight("30px");
    child.style["min-width"] = "100px";

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.width, 100.0f), "min-width constrains: 50px -> 100px");
}

static void testBlockNested() {
    printf("--- Layout: nested blocks ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode outer;
    initBlock(outer);
    outer.setPadding("20px");

    MockLayoutNode inner;
    initBlock(inner);
    inner.setHeight("40px");

    outer.addChild(&inner);
    root.addChild(&outer);

    MockTextMetrics metrics;
    layoutTree(&root, 600.0f, metrics);

    // outer: content width = 600 - 40 padding = 560
    check(approx(outer.box.contentRect.width, 560.0f), "outer content = 600 - 40 padding");
    // inner: fills outer content area
    check(approx(inner.box.contentRect.width, 560.0f), "inner fills outer content area");
    // outer height: padding (20) + inner (40) + padding (20) = auto shrinks to 40
    // (the content height is just the inner's area, padding is separate)
    check(approx(outer.box.contentRect.height, 40.0f), "outer content height = inner 40px");
}

static void testBlockEmResolution() {
    printf("--- Layout: em-based dimensions ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.style["font-size"] = "20px";
    child.setWidth("10em");
    child.setHeight("5em");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.width, 200.0f), "10em at 20px font-size = 200px");
    check(approx(child.box.contentRect.height, 100.0f), "5em at 20px font-size = 100px");
}

// ========== Positioned Layout Tests ==========

static void testPositionRelative() {
    printf("--- Layout: position relative ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");
    child.style["position"] = "relative";
    child.style["top"] = "10px";
    child.style["left"] = "20px";

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // Relative: offset from normal position. Normal x=0, y=0.
    check(approx(child.box.contentRect.x, 20.0f), "relative: x offset by left:20px");
    check(approx(child.box.contentRect.y, 10.0f), "relative: y offset by top:10px");
    // Root height is not affected by relative offset
    check(approx(root.box.contentRect.height, 50.0f), "relative: doesn't affect parent height");
}

static void testPositionRelativeBottom() {
    printf("--- Layout: position relative bottom/right ---\n");
    MockLayoutNode root;
    initBlock(root);

    MockLayoutNode child;
    initBlock(child);
    child.setHeight("50px");
    child.style["position"] = "relative";
    child.style["bottom"] = "15px";
    child.style["right"] = "25px";

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // bottom:15 means move UP by 15; right:25 means move LEFT by 25
    check(approx(child.box.contentRect.y, -15.0f), "relative bottom: y offset up by 15px");
    check(approx(child.box.contentRect.x, -25.0f), "relative right: x offset left by 25px");
}

static void testPositionAbsolute() {
    printf("--- Layout: position absolute ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setHeight("400px");

    MockLayoutNode inflow;
    initBlock(inflow);
    inflow.setHeight("50px");

    MockLayoutNode absChild;
    initBlock(absChild);
    absChild.setWidth("100px");
    absChild.setHeight("60px");
    absChild.style["position"] = "absolute";
    absChild.style["top"] = "20px";
    absChild.style["left"] = "30px";

    root.addChild(&inflow);
    root.addChild(&absChild);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // Absolute child positioned at top:20, left:30 relative to containing block
    check(approx(absChild.box.contentRect.x, 30.0f), "absolute: x = left:30px");
    check(approx(absChild.box.contentRect.y, 20.0f), "absolute: y = top:20px");
    check(approx(absChild.box.contentRect.width, 100.0f), "absolute: explicit width preserved");
    check(approx(absChild.box.contentRect.height, 60.0f), "absolute: explicit height preserved");
    // Absolute doesn't affect in-flow height — root keeps explicit 400px
    check(approx(root.box.contentRect.height, 400.0f), "absolute: doesn't affect parent height");
    // In-flow child is at y=0
    check(approx(inflow.box.contentRect.y, 0.0f), "absolute: in-flow child unaffected");
}

static void testPositionAbsoluteBottomRight() {
    printf("--- Layout: position absolute bottom/right ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setWidth("600px");
    root.setHeight("400px");

    MockLayoutNode absChild;
    initBlock(absChild);
    absChild.setWidth("100px");
    absChild.setHeight("50px");
    absChild.style["position"] = "absolute";
    absChild.style["bottom"] = "10px";
    absChild.style["right"] = "20px";

    root.addChild(&absChild);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // bottom:10 -> y = cbHeight - bottom - height = 400 - 10 - 50 = 340
    check(approx(absChild.box.contentRect.y, 340.0f), "absolute bottom: y = 400-10-50 = 340");
    // right:20 -> x = cbWidth - right - width = 600 - 20 - 100 = 480
    check(approx(absChild.box.contentRect.x, 480.0f), "absolute right: x = 600-20-100 = 480");
}

static void testOverflowClipping() {
    printf("--- Layout: overflow clipping ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setWidth("200px");
    root.setHeight("100px");
    root.style["overflow"] = "hidden";

    // Child extends beyond parent
    MockLayoutNode child;
    initBlock(child);
    child.setWidth("300px");  // wider than parent
    child.setHeight("150px"); // taller than parent

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);
    applyOverflowClipping(&root);

    // Child should be clipped to parent's content area
    check(approx(child.box.contentRect.width, 200.0f), "overflow clip: width clipped to parent 200px");
    check(approx(child.box.contentRect.height, 100.0f), "overflow clip: height clipped to parent 100px");
}

static void testOverflowVisible() {
    printf("--- Layout: overflow visible (no clipping) ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setWidth("200px");
    root.setHeight("100px");
    root.style["overflow"] = "visible";

    MockLayoutNode child;
    initBlock(child);
    child.setWidth("300px");
    child.setHeight("150px");

    root.addChild(&child);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);
    applyOverflowClipping(&root);

    // overflow:visible -> no clipping
    check(approx(child.box.contentRect.width, 300.0f), "overflow visible: width not clipped");
    check(approx(child.box.contentRect.height, 150.0f), "overflow visible: height not clipped");
}

static void testPositionAbsoluteStretch() {
    printf("--- Layout: position absolute stretch ---\n");
    MockLayoutNode root;
    initBlock(root);
    root.setWidth("600px");
    root.setHeight("400px");

    MockLayoutNode absChild;
    initBlock(absChild);
    absChild.style["position"] = "absolute";
    absChild.style["top"] = "10px";
    absChild.style["bottom"] = "20px";
    absChild.style["left"] = "30px";
    absChild.style["right"] = "40px";
    // width/height = auto, so stretch between offsets

    root.addChild(&absChild);

    MockTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // width = 600 - 30 - 40 = 530
    check(approx(absChild.box.contentRect.width, 530.0f), "absolute stretch: width = 600-30-40 = 530");
    // height = 400 - 10 - 20 = 370
    check(approx(absChild.box.contentRect.height, 370.0f), "absolute stretch: height = 400-10-20 = 370");
}

// ========== Entry point ==========

void testLayout() {
    // Length resolution
    testResolveLengthPx();
    testResolveLengthEm();
    testResolveLengthPercent();
    testResolveLengthAuto();
    testResolveLengthNamed();
    testResolveLengthUnitless();

    // Block layout
    testBlockSingleChild();
    testBlockMultipleChildren();
    testBlockExplicitWidth();
    testBlockExplicitHeight();
    testBlockMargin();
    testBlockMarginCollapsing();
    testBlockPadding();
    testBlockBorder();
    testBlockPercentWidth();
    testBlockDisplayNone();
    testBlockBoxSizing();
    testBlockMinMaxWidth();
    testBlockNested();
    testBlockEmResolution();

    // Overflow clipping
    testOverflowClipping();
    testOverflowVisible();

    // Positioned layout
    testPositionRelative();
    testPositionRelativeBottom();
    testPositionAbsolute();
    testPositionAbsoluteBottomRight();
    testPositionAbsoluteStretch();
}
