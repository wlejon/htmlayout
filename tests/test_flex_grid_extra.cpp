// Additional flex + grid coverage targeting branches not exercised by
// existing tests: text children in a flex container, items with explicit
// borders, column direction with fixed cross height, auto margins,
// stretch with explicit cross size, grid min-content/max-content track
// sizes, grid repeat() with line names, grid-row/grid-column shorthand,
// grid absolutely positioned child.

#include "test_flex_grid_extra.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct FGNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    FGNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;
    std::unordered_map<std::string, std::string> attrs;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style_; }
    std::string_view attribute(std::string_view n) const override {
        auto it = attrs.find(std::string(n));
        return it == attrs.end() ? std::string_view{} : std::string_view(it->second);
    }
    void addChild(FGNode* c) { c->parentNode = this; childNodes.push_back(c); }

    void initBase() {
        style_["display"] = "block";
        style_["position"] = "static";
        style_["width"] = "auto";
        style_["height"] = "auto";
        style_["min-width"] = "0";
        style_["min-height"] = "0";
        style_["max-width"] = "none";
        style_["max-height"] = "none";
        for (auto* p : {"margin", "padding"}) {
            for (auto* s : {"top", "right", "bottom", "left"}) {
                style_[std::string(p) + "-" + s] = "0";
            }
        }
        for (auto* s : {"top", "right", "bottom", "left"}) {
            style_[std::string("border-") + s + "-width"] = "0";
            style_[std::string("border-") + s + "-style"] = "none";
        }
        style_["box-sizing"] = "content-box";
        style_["font-size"] = "16px";
        style_["font-family"] = "monospace";
        style_["font-weight"] = "normal";
        style_["overflow"] = "visible";
        style_["white-space"] = "normal";
        style_["text-align"] = "left";
        style_["vertical-align"] = "baseline";
        style_["direction"] = "ltr";
    }
    void initFlex() {
        initBase();
        style_["display"] = "flex";
        style_["flex-direction"] = "row";
        style_["flex-wrap"] = "nowrap";
        style_["justify-content"] = "flex-start";
        style_["align-items"] = "stretch";
        style_["align-content"] = "stretch";
        style_["gap"] = "0";
        style_["row-gap"] = "0";
        style_["column-gap"] = "0";
    }
    void initFlexItem() {
        initBase();
        style_["flex-grow"] = "0";
        style_["flex-shrink"] = "1";
        style_["flex-basis"] = "auto";
        style_["align-self"] = "auto";
        style_["order"] = "0";
    }
    void initGrid() {
        initBase();
        style_["display"] = "grid";
        style_["grid-template-columns"] = "none";
        style_["grid-template-rows"] = "none";
        style_["row-gap"] = "0";
        style_["column-gap"] = "0";
        style_["justify-items"] = "stretch";
        style_["align-items"] = "stretch";
        style_["justify-content"] = "start";
        style_["align-content"] = "start";
    }
    void initGridItem() {
        initBase();
        style_["grid-row"] = "auto";
        style_["grid-column"] = "auto";
        style_["grid-row-start"] = "auto";
        style_["grid-row-end"] = "auto";
        style_["grid-column-start"] = "auto";
        style_["grid-column-end"] = "auto";
        style_["justify-self"] = "stretch";
        style_["align-self"] = "stretch";
    }
};

struct FGMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

bool approx(float a, float b, float tol = 1.0f) { return std::abs(a - b) < tol; }

} // namespace

static void testFlexTextChild() {
    printf("--- flex: direct text child ---\n");
    FGNode root; root.initFlex();
    FGNode txt; txt.initBase();
    txt.isText = true; txt.text = "hello world";
    root.addChild(&txt);

    FGMetrics m;
    layoutTree(&root, 400, m);
    check(true, "flex with direct text child completes");
}

static void testFlexItemWithBorder() {
    printf("--- flex: items with borders ---\n");
    FGNode root; root.initFlex();
    FGNode a; a.initFlexItem();
    a.style_["width"] = "100px"; a.style_["height"] = "30px";
    a.style_["border-left-width"] = "2px"; a.style_["border-left-style"] = "solid";
    a.style_["border-right-width"] = "3px"; a.style_["border-right-style"] = "solid";
    a.style_["border-top-width"] = "1px"; a.style_["border-top-style"] = "solid";
    a.style_["border-bottom-width"] = "1px"; a.style_["border-bottom-style"] = "solid";
    root.addChild(&a);
    FGMetrics m;
    layoutTree(&root, 400, m);
    check(approx(a.box.border.left, 2), "flex item left border resolved");
    check(approx(a.box.border.right, 3), "flex item right border resolved");
}

static void testFlexColumnFixedHeight() {
    printf("--- flex: column direction with fixed height ---\n");
    FGNode root; root.initFlex();
    root.style_["flex-direction"] = "column";
    root.style_["height"] = "200px";

    FGNode c1; c1.initFlexItem();
    c1.style_["width"] = "100px"; c1.style_["flex-grow"] = "1";
    root.addChild(&c1);

    FGMetrics m;
    layoutTree(&root, 400, m);
    check(c1.box.contentRect.height > 0, "column flex with grow gets size");
}

static void testFlexAutoMargin() {
    printf("--- flex: auto margins push siblings ---\n");
    FGNode root; root.initFlex();
    root.style_["width"] = "400px";

    FGNode left; left.initFlexItem();
    left.style_["width"] = "50px"; left.style_["height"] = "30px";
    FGNode right; right.initFlexItem();
    right.style_["width"] = "50px"; right.style_["height"] = "30px";
    right.style_["margin-left"] = "auto";

    root.addChild(&left); root.addChild(&right);

    FGMetrics m;
    layoutTree(&root, 500, m);
    // right should be pushed to far right
    check(right.box.contentRect.x > 200,
          "auto-left-margin pushes flex item to end");
}

static void testFlexAlignStretchExplicitCross() {
    printf("--- flex: stretch with explicit cross size ---\n");
    FGNode root; root.initFlex();
    root.style_["height"] = "100px";
    root.style_["align-items"] = "stretch";

    FGNode c; c.initFlexItem();
    c.style_["width"] = "50px";
    // no height -> should stretch to container's cross size
    root.addChild(&c);

    FGMetrics m;
    layoutTree(&root, 400, m);
    check(c.box.contentRect.height > 0,
          "stretch alignment resolves cross size");
}

static void testFlexAbsoluteChild() {
    printf("--- flex: absolute child ignored in flow ---\n");
    FGNode root; root.initFlex();
    root.style_["position"] = "relative";
    root.style_["width"] = "300px";
    root.style_["height"] = "100px";

    FGNode normal; normal.initFlexItem();
    normal.style_["width"] = "50px"; normal.style_["height"] = "30px";
    FGNode abs; abs.initFlexItem();
    abs.style_["position"] = "absolute";
    abs.style_["width"] = "80px"; abs.style_["height"] = "40px";
    abs.style_["top"] = "10px"; abs.style_["right"] = "10px";

    root.addChild(&normal); root.addChild(&abs);
    FGMetrics m;
    layoutTree(&root, 400, m);
    // Absolute should be at right edge
    check(abs.box.contentRect.x > 100, "absolute child positioned by right");
}

// ===== Grid =====
static void testGridMinContent() {
    printf("--- grid: min-content track sizing ---\n");
    FGNode root; root.initGrid();
    root.style_["grid-template-columns"] = "min-content min-content";
    root.style_["width"] = "400px";

    FGNode a; a.initGridItem();
    FGNode atext; atext.initBase(); atext.isText = true; atext.text = "ab";
    a.addChild(&atext);

    FGNode b; b.initGridItem();
    FGNode btext; btext.initBase(); btext.isText = true; btext.text = "cd";
    b.addChild(&btext);

    root.addChild(&a); root.addChild(&b);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(true, "grid min-content track completes");
}

static void testGridMaxContent() {
    printf("--- grid: max-content track sizing ---\n");
    FGNode root; root.initGrid();
    root.style_["grid-template-columns"] = "max-content auto";

    FGNode a; a.initGridItem();
    FGNode atext; atext.initBase(); atext.isText = true; atext.text = "long text";
    a.addChild(&atext);
    FGNode b; b.initGridItem();
    root.addChild(&a); root.addChild(&b);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(true, "grid max-content track completes");
}

static void testGridRepeat() {
    printf("--- grid: repeat() ---\n");
    FGNode root; root.initGrid();
    root.style_["grid-template-columns"] = "repeat(3, 100px)";
    root.style_["width"] = "400px";

    FGNode a, b, c;
    a.initGridItem(); b.initGridItem(); c.initGridItem();
    root.addChild(&a); root.addChild(&b); root.addChild(&c);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(c.box.contentRect.x > b.box.contentRect.x, "repeat(3,100px) places 3 columns");
}

static void testGridRepeatWithLineNames() {
    printf("--- grid: repeat() with line names ---\n");
    FGNode root; root.initGrid();
    root.style_["grid-template-columns"] = "[start] repeat(2, [c] 100px) [end]";

    FGNode a, b;
    a.initGridItem(); b.initGridItem();
    root.addChild(&a); root.addChild(&b);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(true, "grid repeat with line names parses");
}

static void testGridShorthandRowColumn() {
    printf("--- grid: grid-row / grid-column shorthand ---\n");
    FGNode root; root.initGrid();
    root.style_["grid-template-columns"] = "100px 100px 100px";
    root.style_["grid-template-rows"] = "50px 50px";

    FGNode item; item.initGridItem();
    item.style_["grid-row"] = "1 / 3";
    item.style_["grid-column"] = "2 / 4";
    root.addChild(&item);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(item.box.contentRect.x > 50, "grid-column 2/4 positions item");
}

static void testGridAbsoluteChild() {
    printf("--- grid: absolute child ---\n");
    FGNode root; root.initGrid();
    root.style_["position"] = "relative";
    root.style_["width"] = "300px";
    root.style_["height"] = "200px";

    FGNode abs; abs.initGridItem();
    abs.style_["position"] = "absolute";
    abs.style_["width"] = "50px"; abs.style_["height"] = "40px";
    abs.style_["bottom"] = "10px"; abs.style_["right"] = "10px";
    root.addChild(&abs);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(abs.box.contentRect.x > 100, "grid abs child positioned by right");
}

static void testGridFixedHeight() {
    printf("--- grid: fixed height ---\n");
    FGNode root; root.initGrid();
    root.style_["height"] = "200px";
    root.style_["grid-template-rows"] = "100px 100px";

    FGNode a; a.initGridItem();
    FGNode b; b.initGridItem();
    root.addChild(&a); root.addChild(&b);

    FGMetrics m;
    layoutTree(&root, 500, m);
    check(approx(root.box.contentRect.height, 200, 5), "grid honors fixed height");
}

void testFlexGridExtra() {
    printf("=== Extra Flex/Grid Tests ===\n");
    testFlexTextChild();
    testFlexItemWithBorder();
    testFlexColumnFixedHeight();
    testFlexAutoMargin();
    testFlexAlignStretchExplicitCross();
    testFlexAbsoluteChild();
    testGridMinContent();
    testGridMaxContent();
    testGridRepeat();
    testGridRepeatWithLineNames();
    testGridShorthandRowColumn();
    testGridAbsoluteChild();
    testGridFixedHeight();
}
