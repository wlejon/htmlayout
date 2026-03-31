#include "test_grid.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include "css/color.h"
#include "css/properties.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

struct GridNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    GridNode* parentNode = nullptr;
    std::vector<GridNode*> childNodes;
    ComputedStyle style_;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style_; }

    void addChild(GridNode* child) {
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

struct GridMetrics : public TextMetrics {
    float measureWidth(const std::string& text, const std::string&, float fontSize, const std::string&) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(const std::string&, float fontSize, const std::string&) override {
        return fontSize * 1.2f;
    }
};

static bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

// ========== Grid Layout Tests ==========

static void testGridFixedColumns() {
    printf("--- Grid: fixed column widths ---\n");
    GridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "100px 200px 100px";
    grid.style_["width"] = "400px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    GridNode c1, c2, c3;
    c1.initBlock(); c1.style_["height"] = "50px";
    c2.initBlock(); c2.style_["height"] = "50px";
    c3.initBlock(); c3.style_["height"] = "50px";

    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);

    GridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    check(approx(c1.box.contentRect.width, 100.0f), "grid col 1 width = 100");
    check(approx(c2.box.contentRect.width, 200.0f), "grid col 2 width = 200");
    check(approx(c3.box.contentRect.width, 100.0f), "grid col 3 width = 100");
    // All on same row
    check(approx(c1.box.contentRect.y, c2.box.contentRect.y, 1.0f), "grid items on same row");
    // c2 starts after c1
    check(c2.box.contentRect.x > c1.box.contentRect.x, "c2 right of c1");
}

static void testGridFrUnits() {
    printf("--- Grid: fr units ---\n");
    GridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "1fr 2fr";
    grid.style_["width"] = "300px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    GridNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "40px";
    c2.initBlock(); c2.style_["height"] = "40px";

    grid.addChild(&c1);
    grid.addChild(&c2);

    GridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    // 1fr + 2fr = 3fr total, 300px / 3 = 100px per fr
    check(approx(c1.box.contentRect.width, 100.0f), "1fr = 100px");
    check(approx(c2.box.contentRect.width, 200.0f), "2fr = 200px");
}

static void testGridMultiRow() {
    printf("--- Grid: multiple rows ---\n");
    GridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "1fr 1fr";
    grid.style_["width"] = "200px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    GridNode c1, c2, c3, c4;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    c3.initBlock(); c3.style_["height"] = "40px";
    c4.initBlock(); c4.style_["height"] = "40px";

    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);
    grid.addChild(&c4);

    GridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    // c1, c2 on row 0; c3, c4 on row 1
    check(approx(c1.box.contentRect.y, c2.box.contentRect.y, 1.0f), "row 0: c1 and c2 same y");
    check(approx(c3.box.contentRect.y, c4.box.contentRect.y, 1.0f), "row 1: c3 and c4 same y");
    check(c3.box.contentRect.y > c1.box.contentRect.y, "row 1 below row 0");
}

static void testGridWithGap() {
    printf("--- Grid: with gap ---\n");
    GridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "1fr 1fr";
    grid.style_["width"] = "210px";
    grid.style_["row-gap"] = "10px";
    grid.style_["column-gap"] = "10px";

    GridNode c1, c2, c3, c4;
    c1.initBlock(); c1.style_["height"] = "50px";
    c2.initBlock(); c2.style_["height"] = "50px";
    c3.initBlock(); c3.style_["height"] = "50px";
    c4.initBlock(); c4.style_["height"] = "50px";

    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);
    grid.addChild(&c4);

    GridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    // Each column should be (210 - 10) / 2 = 100px
    check(approx(c1.box.contentRect.width, 100.0f), "grid with gap: col width = 100");
    // Row gap should separate rows
    float rowGap = c3.box.contentRect.y - (c1.box.contentRect.y + c1.box.contentRect.height);
    check(rowGap > 5.0f, "grid: row gap present");
}

static void testGridRepeat() {
    printf("--- Grid: repeat() ---\n");
    GridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "repeat(3, 1fr)";
    grid.style_["width"] = "300px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    GridNode c1, c2, c3;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    c3.initBlock(); c3.style_["height"] = "30px";

    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);

    GridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    check(approx(c1.box.contentRect.width, 100.0f), "repeat(3, 1fr): each = 100px");
    check(approx(c2.box.contentRect.width, 100.0f), "repeat(3, 1fr): each = 100px");
    check(approx(c3.box.contentRect.width, 100.0f), "repeat(3, 1fr): each = 100px");
}

// ========== Color Parsing Tests ==========

static void testColorNamed() {
    printf("--- Color: named colors ---\n");
    auto c = parseColor("red");
    check(c.r == 255 && c.g == 0 && c.b == 0 && c.a == 255, "red = 255,0,0,255");

    c = parseColor("blue");
    check(c.r == 0 && c.g == 0 && c.b == 255, "blue = 0,0,255");

    c = parseColor("transparent");
    check(c.a == 0, "transparent alpha = 0");

    c = parseColor("rebeccapurple");
    check(c.r == 102 && c.g == 51 && c.b == 153, "rebeccapurple = 102,51,153");
}

static void testColorHex() {
    printf("--- Color: hex ---\n");
    auto c = parseColor("#ff0000");
    check(c.r == 255 && c.g == 0 && c.b == 0, "#ff0000 = red");

    c = parseColor("#0f0");
    check(c.r == 0 && c.g == 255 && c.b == 0, "#0f0 = green");

    c = parseColor("#00ff0080");
    check(c.r == 0 && c.g == 255 && c.b == 0 && c.a == 128, "#00ff0080 alpha = 128");

    c = parseColor("#F00");
    check(c.r == 255 && c.g == 0 && c.b == 0, "#F00 case insensitive");
}

static void testColorRgb() {
    printf("--- Color: rgb()/rgba() ---\n");
    auto c = parseColor("rgb(128, 64, 32)");
    check(c.r == 128 && c.g == 64 && c.b == 32 && c.a == 255, "rgb(128,64,32)");

    c = parseColor("rgba(255, 0, 0, 0.5)");
    check(c.r == 255 && c.g == 0 && c.b == 0, "rgba red channel");
    check(c.a >= 126 && c.a <= 128, "rgba alpha 0.5 -> ~128");
}

static void testColorHsl() {
    printf("--- Color: hsl() ---\n");
    auto c = parseColor("hsl(0, 100%, 50%)");
    check(c.r >= 254 && c.g <= 1 && c.b <= 1, "hsl(0,100%,50%) = red");

    c = parseColor("hsl(120, 100%, 50%)");
    check(c.g >= 254 && c.r <= 1 && c.b <= 1, "hsl(120,100%,50%) = green");

    c = parseColor("hsl(240, 100%, 50%)");
    check(c.b >= 254 && c.r <= 1 && c.g <= 1, "hsl(240,100%,50%) = blue");
}

// ========== Attribute Selector Case Insensitive Tests ==========

static void testAttrCaseInsensitive() {
    printf("--- Selector: attribute case-insensitive flag ---\n");

    MockElement elem;
    elem.tag = "div";
    elem.attrs["type"] = "TEXT";

    // Case-sensitive match (default) should NOT match
    auto sel1 = parseSelector("[type=text]");
    check(!sel1.matches(elem), "[type=text] does NOT match 'TEXT' (case-sensitive)");

    // Case-insensitive match should match
    auto sel2 = parseSelector("[type=text i]");
    check(sel2.matches(elem), "[type=text i] matches 'TEXT' (case-insensitive)");

    // Exact case should still match
    auto sel3 = parseSelector("[type=TEXT]");
    check(sel3.matches(elem), "[type=TEXT] matches 'TEXT' exactly");
}

// ========== Line Height Resolution Tests ==========

static void testLineHeightResolution() {
    printf("--- Line height: normal/unitless resolution ---\n");

    // normal -> 1.2 * fontSize
    check(approx(resolveLineHeight("normal", 16.0f), 19.2f), "line-height: normal at 16px = 19.2");
    check(approx(resolveLineHeight("normal", 20.0f), 24.0f), "line-height: normal at 20px = 24.0");

    // Unitless multiplier
    check(approx(resolveLineHeight("1.5", 16.0f), 24.0f), "line-height: 1.5 at 16px = 24.0");
    check(approx(resolveLineHeight("2", 16.0f), 32.0f), "line-height: 2 at 16px = 32.0");

    // Length values
    check(approx(resolveLineHeight("20px", 16.0f), 20.0f), "line-height: 20px = 20.0");
    check(approx(resolveLineHeight("1.5em", 16.0f), 24.0f), "line-height: 1.5em at 16px = 24.0");

    // Empty -> same as normal
    check(approx(resolveLineHeight("", 16.0f), 19.2f), "line-height: empty = normal");
}

// ========== Margin Collapsing Through Empty Boxes ==========

static void testMarginCollapseEmptyBox() {
    printf("--- Margin: collapsing through empty boxes ---\n");

    GridNode root;
    root.initBlock();

    GridNode c1, empty, c2;
    c1.initBlock(); c1.style_["height"] = "30px";
    c1.style_["margin-bottom"] = "20px";

    // Empty box with margins that should collapse through
    empty.initBlock(); empty.style_["height"] = "0";
    empty.style_["margin-top"] = "15px";
    empty.style_["margin-bottom"] = "25px";

    c2.initBlock(); c2.style_["height"] = "40px";
    c2.style_["margin-top"] = "10px";

    root.addChild(&c1);
    root.addChild(&empty);
    root.addChild(&c2);

    GridMetrics metrics;
    layoutTree(&root, 600.0f, metrics);

    // c1 bottom margin (20) should collapse with empty's self-collapsed margin (max(15,25)=25)
    // Then that (25) collapses with c2 top margin (10) -> max(25, 10) = 25
    // So c2.y should be 30 + 25 = 55
    check(approx(c2.box.contentRect.y, 55.0f, 2.0f), "margin collapse through empty: c2 at y=55");
}

// ========== Position Sticky Tests ==========

static void testPositionSticky() {
    printf("--- Position: sticky ---\n");

    GridNode root;
    root.initBlock();

    GridNode child;
    child.initBlock();
    child.style_["height"] = "50px";
    child.style_["position"] = "sticky";
    child.style_["top"] = "10px";

    root.addChild(&child);

    GridMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    // Sticky behaves like relative during initial layout
    check(approx(child.box.contentRect.y, 10.0f), "sticky: offset by top:10px");
    check(approx(root.box.contentRect.height, 50.0f), "sticky: doesn't affect parent height");
}

// ========== @supports parsing test ==========

static void testSupportsRule() {
    printf("--- Parser: @supports ---\n");

    std::string css = R"(
        @supports (display: grid) {
            .grid { display: grid; }
        }
        @supports (display: nonexistent) {
            .fail { display: none; }
        }
        .normal { color: red; }
    )";

    auto sheet = parse(css);
    // The @supports rules should be included since we broadly support CSS properties
    // Plus the normal rule
    check(sheet.rules.size() >= 2, "@supports: rules included from supported condition");
}

// ========== Grid Properties in Registry ==========

static void testGridProperties() {
    printf("--- Grid properties in registry ---\n");
    check(initialValue("grid-template-columns") == "none", "grid-template-columns initial = none");
    check(initialValue("grid-template-rows") == "none", "grid-template-rows initial = none");
    check(initialValue("grid-area") == "auto", "grid-area initial = auto");
    check(initialValue("grid-auto-flow") == "row", "grid-auto-flow initial = row");
    check(initialValue("justify-items") == "stretch", "justify-items initial = stretch");
}

// ========== Entry point ==========

void testGridLayout() {
    printf("=== Grid, Color, Attribute, LineHeight, MarginCollapse, Sticky, @supports Tests ===\n");
    testGridFixedColumns();
    testGridFrUnits();
    testGridMultiRow();
    testGridWithGap();
    testGridRepeat();
    testColorNamed();
    testColorHex();
    testColorRgb();
    testColorHsl();
    testAttrCaseInsensitive();
    testLineHeightResolution();
    testMarginCollapseEmptyBox();
    testPositionSticky();
    testSupportsRule();
    testGridProperties();
}
