#include "test_calc.h"
#include "test_helpers.h"
#include "layout/formatting_context.h"
#include "layout/box.h"
#include "css/properties.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

static bool approx(float a, float b, float tol = 0.5f) {
    return std::abs(a - b) < tol;
}

// ========== calc() Tests ==========

static void testCalcBasic() {
    printf("--- calc() basic expressions ---\n");
    check(approx(resolveLength("calc(100px + 50px)", 800, 16), 150.0f), "calc(100px + 50px) = 150");
    check(approx(resolveLength("calc(200px - 30px)", 800, 16), 170.0f), "calc(200px - 30px) = 170");
    check(approx(resolveLength("calc(10px * 3)", 800, 16), 30.0f), "calc(10px * 3) = 30");
    check(approx(resolveLength("calc(100px / 4)", 800, 16), 25.0f), "calc(100px / 4) = 25");
}

static void testCalcMixedUnits() {
    printf("--- calc() mixed units ---\n");
    check(approx(resolveLength("calc(100% - 40px)", 800, 16), 760.0f), "calc(100% - 40px) at 800 = 760");
    check(approx(resolveLength("calc(50% + 20px)", 600, 16), 320.0f), "calc(50% + 20px) at 600 = 320");
    check(approx(resolveLength("calc(2em + 10px)", 800, 16), 42.0f), "calc(2em + 10px) at 16px = 42");
}

static void testCalcNested() {
    printf("--- calc() nested/parenthesized ---\n");
    check(approx(resolveLength("calc((100px + 50px) * 2)", 800, 16), 300.0f), "calc((100+50)*2) = 300");
    check(approx(resolveLength("calc(100% - (20px + 20px))", 400, 16), 360.0f), "calc(100% - (20+20)) at 400 = 360");
}

// ========== calc() in layout context ==========

struct CalcLayoutNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    CalcLayoutNode* parentNode = nullptr;
    std::vector<CalcLayoutNode*> childNodes;
    ComputedStyle style_;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style_; }

    void addChild(CalcLayoutNode* child) {
        child->parentNode = this;
        childNodes.push_back(child);
    }

    void initBlock() {
        style_["display"] = "block";
        style_["position"] = "static";
        style_["width"] = "auto";
        style_["height"] = "auto";
        style_["min-width"] = "0";
        style_["min-height"] = "0";
        style_["max-width"] = "none";
        style_["max-height"] = "none";
        style_["margin-top"] = "0";
        style_["margin-right"] = "0";
        style_["margin-bottom"] = "0";
        style_["margin-left"] = "0";
        style_["padding-top"] = "0";
        style_["padding-right"] = "0";
        style_["padding-bottom"] = "0";
        style_["padding-left"] = "0";
        style_["border-top-width"] = "0";
        style_["border-right-width"] = "0";
        style_["border-bottom-width"] = "0";
        style_["border-left-width"] = "0";
        style_["border-top-style"] = "none";
        style_["border-right-style"] = "none";
        style_["border-bottom-style"] = "none";
        style_["border-left-style"] = "none";
        style_["box-sizing"] = "content-box";
        style_["font-size"] = "16px";
        style_["overflow"] = "visible";
    }
};

struct CalcTextMetrics : public TextMetrics {
    float measureWidth(const std::string& text, const std::string&, float fontSize, const std::string&) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(const std::string&, float fontSize, const std::string&) override {
        return fontSize * 1.2f;
    }
};

static void testCalcInLayout() {
    printf("--- calc() in layout ---\n");
    CalcLayoutNode root;
    root.initBlock();

    CalcLayoutNode child;
    child.initBlock();
    child.style_["width"] = "calc(100% - 40px)";
    child.style_["height"] = "calc(50px + 30px)";

    root.addChild(&child);

    CalcTextMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.width, 760.0f), "calc width: 100% of 800 - 40 = 760");
    check(approx(child.box.contentRect.height, 80.0f), "calc height: 50 + 30 = 80");
}

// ========== Viewport-aware layout ==========

static void testViewportLayout() {
    printf("--- Viewport-aware layout ---\n");
    CalcLayoutNode root;
    root.initBlock();

    CalcLayoutNode child;
    child.initBlock();
    child.style_["height"] = "50px";

    root.addChild(&child);

    CalcTextMetrics metrics;
    Viewport vp{1024, 768};
    layoutTree(&root, vp, metrics);

    check(approx(root.box.contentRect.width, 1024.0f), "viewport layout: root width = 1024");
}

// ========== Property registry tests ==========

static void testNewProperties() {
    printf("--- New property registry ---\n");
    // box-shadow should be known
    check(initialValue("box-shadow") == "none", "box-shadow initial = none");
    check(!isInherited("box-shadow"), "box-shadow not inherited");

    // border-radius should be known
    check(initialValue("border-radius") == "0", "border-radius initial = 0");

    // transition should be known
    check(initialValue("transition") == "none", "transition initial = none");

    // animation should be known
    check(initialValue("animation") == "none", "animation initial = none");

    // transform should be known
    check(initialValue("transform") == "none", "transform initial = none");

    // text-overflow
    check(initialValue("text-overflow") == "clip", "text-overflow initial = clip");

    // overflow-wrap
    check(initialValue("overflow-wrap") == "normal", "overflow-wrap initial = normal");
    check(isInherited("overflow-wrap"), "overflow-wrap is inherited");

    // direction
    check(initialValue("direction") == "ltr", "direction initial = ltr");
    check(isInherited("direction"), "direction is inherited");

    // table properties
    check(initialValue("table-layout") == "auto", "table-layout initial = auto");
    check(initialValue("border-collapse") == "separate", "border-collapse initial = separate");
    check(isInherited("border-collapse"), "border-collapse is inherited");
}

// ========== Shorthand expansion tests ==========

static void testNewShorthands() {
    printf("--- New shorthand expansions ---\n");

    // border-radius
    auto br = expandShorthand("border-radius", "5px");
    check(br.size() == 4, "border-radius expands to 4 longhands");
    bool brOk = true;
    for (auto& e : br) {
        if (e.value != "5px") brOk = false;
    }
    check(brOk, "border-radius: 5px -> all corners 5px");

    // border-radius with 4 values
    auto br4 = expandShorthand("border-radius", "1px 2px 3px 4px");
    check(br4.size() == 4, "border-radius 4 values -> 4 longhands");
    check(br4[0].value == "1px" && br4[0].property == "border-top-left-radius", "border-radius[0] = 1px");
    check(br4[1].value == "2px" && br4[1].property == "border-top-right-radius", "border-radius[1] = 2px");
    check(br4[2].value == "3px" && br4[2].property == "border-bottom-right-radius", "border-radius[2] = 3px");
    check(br4[3].value == "4px" && br4[3].property == "border-bottom-left-radius", "border-radius[3] = 4px");

    // outline shorthand
    auto ol = expandShorthand("outline", "2px solid red");
    check(ol.size() == 3, "outline expands to 3 longhands");
    bool hasWidth = false, hasStyle = false, hasColor = false;
    for (auto& e : ol) {
        if (e.property == "outline-width" && e.value == "2px") hasWidth = true;
        if (e.property == "outline-style" && e.value == "solid") hasStyle = true;
        if (e.property == "outline-color" && e.value == "red") hasColor = true;
    }
    check(hasWidth && hasStyle && hasColor, "outline: 2px solid red -> correct longhands");

    // columns shorthand
    auto cols = expandShorthand("columns", "200px 3");
    check(cols.size() == 2, "columns expands to 2 longhands");
    bool hasColW = false, hasColC = false;
    for (auto& e : cols) {
        if (e.property == "column-width" && e.value == "200px") hasColW = true;
        if (e.property == "column-count" && e.value == "3") hasColC = true;
    }
    check(hasColW && hasColC, "columns: 200px 3 -> correct longhands");
}

// ========== Table layout tests ==========

static void testTableBasic() {
    printf("--- Table layout: basic ---\n");

    CalcLayoutNode table;
    table.initBlock();
    table.style_["display"] = "table";
    table.style_["border-spacing"] = "0";

    CalcLayoutNode row1, row2;
    row1.initBlock(); row1.style_["display"] = "table-row";
    row2.initBlock(); row2.style_["display"] = "table-row";

    CalcLayoutNode cell1, cell2, cell3, cell4;
    cell1.initBlock(); cell1.style_["display"] = "table-cell"; cell1.style_["height"] = "30px";
    cell2.initBlock(); cell2.style_["display"] = "table-cell"; cell2.style_["height"] = "30px";
    cell3.initBlock(); cell3.style_["display"] = "table-cell"; cell3.style_["height"] = "40px";
    cell4.initBlock(); cell4.style_["display"] = "table-cell"; cell4.style_["height"] = "40px";

    row1.addChild(&cell1);
    row1.addChild(&cell2);
    row2.addChild(&cell3);
    row2.addChild(&cell4);
    table.addChild(&row1);
    table.addChild(&row2);

    CalcTextMetrics metrics;
    layoutTree(&table, 400.0f, metrics);

    check(table.box.contentRect.width > 0, "table has positive width");
    check(table.box.contentRect.height > 0, "table has positive height");
    // Cells should be side by side
    check(cell1.box.contentRect.x < cell2.box.contentRect.x, "cell1 left of cell2");
    // Row 2 should be below row 1
    check(cell3.box.contentRect.y > cell1.box.contentRect.y, "row2 below row1");
}

static void testTableEqualColumns() {
    printf("--- Table layout: equal columns ---\n");

    CalcLayoutNode table;
    table.initBlock();
    table.style_["display"] = "table";
    table.style_["width"] = "400px";
    table.style_["border-spacing"] = "0";

    CalcLayoutNode row;
    row.initBlock(); row.style_["display"] = "table-row";

    CalcLayoutNode cell1, cell2;
    cell1.initBlock(); cell1.style_["display"] = "table-cell"; cell1.style_["height"] = "30px";
    cell2.initBlock(); cell2.style_["display"] = "table-cell"; cell2.style_["height"] = "30px";

    row.addChild(&cell1);
    row.addChild(&cell2);
    table.addChild(&row);

    CalcTextMetrics metrics;
    layoutTree(&table, 800.0f, metrics);

    // Both cells should roughly share the width
    check(cell1.box.contentRect.width > 0, "cell1 has positive width");
    check(cell2.box.contentRect.width > 0, "cell2 has positive width");
}

// ========== visibility:hidden test ==========

static void testVisibilityHidden() {
    printf("--- visibility:hidden vs display:none ---\n");

    CalcLayoutNode root;
    root.initBlock();

    CalcLayoutNode visible, hidden, none;
    visible.initBlock(); visible.style_["height"] = "30px";
    hidden.initBlock(); hidden.style_["height"] = "40px"; hidden.style_["visibility"] = "hidden";
    none.initBlock(); none.style_["height"] = "50px"; none.style_["display"] = "none";

    root.addChild(&visible);
    root.addChild(&hidden);
    root.addChild(&none);

    CalcTextMetrics metrics;
    layoutTree(&root, 600.0f, metrics);

    // visibility:hidden still occupies space
    check(approx(hidden.box.contentRect.height, 40.0f), "visibility:hidden still has height");
    check(hidden.box.contentRect.width > 0, "visibility:hidden still has width");

    // display:none takes no space
    check(approx(none.box.contentRect.height, 0.0f), "display:none has zero height");

    // Root height includes hidden but not none
    check(approx(root.box.contentRect.height, 70.0f), "root height = 30 + 40 = 70 (hidden included, none excluded)");
}

// ========== z-index hit testing ==========

static void testZIndexHitTest() {
    printf("--- z-index hit testing ---\n");

    CalcLayoutNode root;
    root.initBlock();
    root.style_["width"] = "400px";
    root.style_["height"] = "400px";
    root.style_["pointer-events"] = "auto";

    // Two overlapping children, lower sibling has higher z-index
    CalcLayoutNode child1, child2;
    child1.initBlock();
    child1.style_["position"] = "absolute";
    child1.style_["top"] = "0"; child1.style_["left"] = "0";
    child1.style_["width"] = "200px"; child1.style_["height"] = "200px";
    child1.style_["z-index"] = "2";
    child1.style_["pointer-events"] = "auto";
    child1.tag = "first";

    child2.initBlock();
    child2.style_["position"] = "absolute";
    child2.style_["top"] = "0"; child2.style_["left"] = "0";
    child2.style_["width"] = "200px"; child2.style_["height"] = "200px";
    child2.style_["z-index"] = "1";
    child2.style_["pointer-events"] = "auto";
    child2.tag = "second";

    root.addChild(&child1);
    root.addChild(&child2);

    CalcTextMetrics metrics;
    layoutTree(&root, 400.0f, metrics);

    auto* hit = hitTest(&root, 100, 100);
    // child1 has higher z-index, should be hit even though child2 is later in source
    check(hit != nullptr, "z-index: hit something");
    if (hit) {
        check(hit->tagName() == "first", "z-index: higher z-index element hit first");
    }
}

// ========== Entry point ==========

void testCalc() {
    printf("=== Calc, Properties, Table, Visibility, Z-Index Tests ===\n");
    testCalcBasic();
    testCalcMixedUnits();
    testCalcNested();
    testCalcInLayout();
    testViewportLayout();
    testNewProperties();
    testNewShorthands();
    testTableBasic();
    testTableEqualColumns();
    testVisibilityHidden();
    testZIndexHitTest();
}
