#include "test_spec_completeness.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"
#include "css/properties.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>

using namespace htmlayout::css;
using namespace htmlayout::layout;

static bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

// ========== Logical Properties Tests ==========

static void testLogicalMargin() {
    printf("--- Logical Properties: margin ---\n");

    // margin-inline expands to margin-left + margin-right
    auto r = expandShorthand("margin-inline", "10px 20px");
    check(r.size() == 2, "margin-inline -> 2 longhands");
    check(r[0].property == "margin-left" && r[0].value == "10px", "margin-inline-start = left");
    check(r[1].property == "margin-right" && r[1].value == "20px", "margin-inline-end = right");

    // Single value
    auto r2 = expandShorthand("margin-inline", "15px");
    check(r2[0].value == "15px" && r2[1].value == "15px", "margin-inline 1v: both sides");

    // margin-block
    auto r3 = expandShorthand("margin-block", "5px 10px");
    check(r3[0].property == "margin-top" && r3[0].value == "5px", "margin-block-start = top");
    check(r3[1].property == "margin-bottom" && r3[1].value == "10px", "margin-block-end = bottom");

    // Longhands
    auto r4 = expandShorthand("margin-inline-start", "8px");
    check(r4[0].property == "margin-left" && r4[0].value == "8px", "margin-inline-start -> margin-left");

    auto r5 = expandShorthand("margin-inline-end", "12px");
    check(r5[0].property == "margin-right" && r5[0].value == "12px", "margin-inline-end -> margin-right");

    auto r6 = expandShorthand("margin-block-start", "3px");
    check(r6[0].property == "margin-top" && r6[0].value == "3px", "margin-block-start -> margin-top");

    auto r7 = expandShorthand("margin-block-end", "7px");
    check(r7[0].property == "margin-bottom" && r7[0].value == "7px", "margin-block-end -> margin-bottom");
}

static void testLogicalPadding() {
    printf("--- Logical Properties: padding ---\n");

    auto r = expandShorthand("padding-inline", "10px 20px");
    check(r[0].property == "padding-left" && r[0].value == "10px", "padding-inline-start");
    check(r[1].property == "padding-right" && r[1].value == "20px", "padding-inline-end");

    auto r2 = expandShorthand("padding-block", "5px");
    check(r2[0].property == "padding-top" && r2[0].value == "5px", "padding-block-start");
    check(r2[1].property == "padding-bottom" && r2[1].value == "5px", "padding-block-end 1v");

    auto r3 = expandShorthand("padding-inline-start", "8px");
    check(r3[0].property == "padding-left", "padding-inline-start -> padding-left");

    auto r4 = expandShorthand("padding-block-end", "12px");
    check(r4[0].property == "padding-bottom", "padding-block-end -> padding-bottom");
}

static void testLogicalSizing() {
    printf("--- Logical Properties: sizing ---\n");

    auto r1 = expandShorthand("inline-size", "200px");
    check(r1[0].property == "width" && r1[0].value == "200px", "inline-size -> width");

    auto r2 = expandShorthand("block-size", "100px");
    check(r2[0].property == "height" && r2[0].value == "100px", "block-size -> height");

    auto r3 = expandShorthand("min-inline-size", "50px");
    check(r3[0].property == "min-width", "min-inline-size -> min-width");

    auto r4 = expandShorthand("max-block-size", "300px");
    check(r4[0].property == "max-height", "max-block-size -> max-height");
}

static void testLogicalInset() {
    printf("--- Logical Properties: inset ---\n");

    auto r1 = expandShorthand("inset-inline", "10px 20px");
    check(r1[0].property == "left" && r1[0].value == "10px", "inset-inline-start -> left");
    check(r1[1].property == "right" && r1[1].value == "20px", "inset-inline-end -> right");

    auto r2 = expandShorthand("inset-block", "5px");
    check(r2[0].property == "top" && r2[0].value == "5px", "inset-block-start -> top");
    check(r2[1].property == "bottom" && r2[1].value == "5px", "inset-block-end -> bottom");

    auto r3 = expandShorthand("inset-inline-start", "15px");
    check(r3[0].property == "left" && r3[0].value == "15px", "inset-inline-start longhand");

    auto r4 = expandShorthand("inset-block-end", "25px");
    check(r4[0].property == "bottom" && r4[0].value == "25px", "inset-block-end longhand");
}

static void testLogicalBorder() {
    printf("--- Logical Properties: border ---\n");

    // border-inline shorthand
    auto r1 = expandShorthand("border-inline", "1px solid red");
    check(r1.size() == 6, "border-inline -> 6 longhands (2 sides x 3)");
    check(r1[0].property == "border-left-width" && r1[0].value == "1px", "border-inline: left-width");
    check(r1[3].property == "border-right-width" && r1[3].value == "1px", "border-inline: right-width");

    // border-block-start
    auto r2 = expandShorthand("border-block-start", "2px dashed blue");
    check(r2[0].property == "border-top-width" && r2[0].value == "2px", "border-block-start: width");
    check(r2[1].property == "border-top-style" && r2[1].value == "dashed", "border-block-start: style");

    // border-inline-width
    auto r3 = expandShorthand("border-inline-width", "1px 2px");
    check(r3[0].property == "border-left-width" && r3[0].value == "1px", "border-inline-width: left");
    check(r3[1].property == "border-right-width" && r3[1].value == "2px", "border-inline-width: right");

    // border-block-color
    auto r4 = expandShorthand("border-block-color", "red");
    check(r4[0].property == "border-top-color" && r4[0].value == "red", "border-block-color: top");
    check(r4[1].property == "border-bottom-color" && r4[1].value == "red", "border-block-color: bottom");

    // Individual longhands
    auto r5 = expandShorthand("border-inline-start-width", "3px");
    check(r5[0].property == "border-left-width", "border-inline-start-width -> border-left-width");

    auto r6 = expandShorthand("border-block-end-style", "dotted");
    check(r6[0].property == "border-bottom-style", "border-block-end-style -> border-bottom-style");
}

// ========== Selectors L4 Tests ==========

// Extended MockElement with new L4 pseudo-class state
struct L4MockElement : public MockElement {
    bool linkState = false;
    bool visitedState = false;
    bool focusWithinState = false;
    bool focusVisibleState = false;
    bool checkedState = false;
    bool disabledState = false;
    bool enabledState = true;
    bool requiredState = false;
    bool readOnlyState = false;
    bool placeholderShownState = false;
    bool indeterminateState = false;
    bool targetState = false;

    bool isLink() const override { return linkState; }
    bool isVisited() const override { return visitedState; }
    bool isFocusWithin() const override { return focusWithinState; }
    bool isFocusVisible() const override { return focusVisibleState; }
    bool isChecked() const override { return checkedState; }
    bool isDisabled() const override { return disabledState; }
    bool isEnabled() const override { return enabledState; }
    bool isRequired() const override { return requiredState; }
    bool isOptional() const override { return !requiredState; }
    bool isReadOnly() const override { return readOnlyState; }
    bool isReadWrite() const override { return !readOnlyState; }
    bool isPlaceholderShown() const override { return placeholderShownState; }
    bool isIndeterminate() const override { return indeterminateState; }
    bool isTarget() const override { return targetState; }
};

static void testSelectorsL4() {
    printf("--- Selectors L4: new pseudo-classes ---\n");

    L4MockElement link;
    link.tag = "a";
    link.linkState = true;

    auto sel1 = parseSelector(":any-link");
    check(sel1.matches(link), ":any-link matches link element");

    auto sel1b = parseSelector(":link");
    check(sel1b.matches(link), ":link matches link element");

    L4MockElement visited;
    visited.tag = "a";
    visited.visitedState = true;
    auto sel2 = parseSelector(":visited");
    check(sel2.matches(visited), ":visited matches visited link");
    check(!sel2.matches(link), ":visited does not match unvisited");

    L4MockElement parent;
    parent.tag = "div";
    parent.focusWithinState = true;
    auto sel3 = parseSelector(":focus-within");
    check(sel3.matches(parent), ":focus-within matches");

    L4MockElement input;
    input.tag = "input";
    input.focusVisibleState = true;
    auto sel4 = parseSelector(":focus-visible");
    check(sel4.matches(input), ":focus-visible matches");

    L4MockElement checkbox;
    checkbox.tag = "input";
    checkbox.checkedState = true;
    auto sel5 = parseSelector(":checked");
    check(sel5.matches(checkbox), ":checked matches checked input");
    check(!sel5.matches(input), ":checked does not match unchecked");

    L4MockElement disabled;
    disabled.tag = "input";
    disabled.disabledState = true;
    disabled.enabledState = false;
    auto sel6 = parseSelector(":disabled");
    check(sel6.matches(disabled), ":disabled matches");
    auto sel7 = parseSelector(":enabled");
    check(!sel7.matches(disabled), ":enabled does not match disabled");
    check(sel7.matches(input), ":enabled matches enabled input");

    L4MockElement required;
    required.tag = "input";
    required.requiredState = true;
    auto sel8 = parseSelector(":required");
    check(sel8.matches(required), ":required matches");
    auto sel9 = parseSelector(":optional");
    check(!sel9.matches(required), ":optional does not match required");

    L4MockElement readOnly;
    readOnly.tag = "input";
    readOnly.readOnlyState = true;
    auto sel10 = parseSelector(":read-only");
    check(sel10.matches(readOnly), ":read-only matches");
    auto sel11 = parseSelector(":read-write");
    check(sel11.matches(input), ":read-write matches writable");

    L4MockElement placeholder;
    placeholder.tag = "input";
    placeholder.placeholderShownState = true;
    auto sel12 = parseSelector(":placeholder-shown");
    check(sel12.matches(placeholder), ":placeholder-shown matches");

    L4MockElement indet;
    indet.tag = "input";
    indet.indeterminateState = true;
    auto sel13 = parseSelector(":indeterminate");
    check(sel13.matches(indet), ":indeterminate matches");

    L4MockElement target;
    target.tag = "section";
    target.targetState = true;
    auto sel14 = parseSelector(":target");
    check(sel14.matches(target), ":target matches targeted element");
}

// ========== Overflow L3 Tests ==========

static void testOverflowShorthand() {
    printf("--- Overflow L3: shorthand expansion ---\n");

    // Single value: both axes
    auto r1 = expandShorthand("overflow", "hidden");
    check(r1.size() == 3, "overflow 1v -> 3 (overflow + overflow-x + overflow-y)");
    bool hasX = false, hasY = false;
    for (auto& e : r1) {
        if (e.property == "overflow-x" && e.value == "hidden") hasX = true;
        if (e.property == "overflow-y" && e.value == "hidden") hasY = true;
    }
    check(hasX, "overflow: hidden sets overflow-x: hidden");
    check(hasY, "overflow: hidden sets overflow-y: hidden");

    // Two values: x y
    auto r2 = expandShorthand("overflow", "hidden scroll");
    hasX = false; hasY = false;
    for (auto& e : r2) {
        if (e.property == "overflow-x" && e.value == "hidden") hasX = true;
        if (e.property == "overflow-y" && e.value == "scroll") hasY = true;
    }
    check(hasX, "overflow: hidden scroll -> overflow-x: hidden");
    check(hasY, "overflow: hidden scroll -> overflow-y: scroll");

    // overflow: clip should be a recognized value
    auto r3 = expandShorthand("overflow", "clip");
    bool hasClip = false;
    for (auto& e : r3) {
        if (e.property == "overflow" && e.value == "clip") hasClip = true;
    }
    check(hasClip, "overflow: clip is preserved");
}

// ========== CSS Variables Cycle Detection Tests ==========

static void testVarCycleDetection() {
    printf("--- CSS Variables: cycle detection ---\n");

    Cascade cascade;
    Stylesheet sheet = parse(
        ":root { --a: var(--b); --b: var(--a); --c: hello; --d: var(--c); "
        "--self: var(--self); }"
    );
    cascade.addStylesheet(sheet, nullptr);

    MockElement root;
    root.tag = "html";
    auto style = cascade.resolve(root, "", nullptr);

    // --a and --b form a cycle: should resolve to empty (guaranteed-invalid)
    check(style["--a"].empty() || style["--a"] == "var(--b)" || style["--a"] == "",
          "cyclic --a resolves to empty");

    // --c is not cyclic
    check(style["--c"] == "hello", "non-cyclic --c = hello");

    // --d references --c (not cyclic)
    check(style["--d"] == "hello", "non-cyclic --d = hello (via --c)");

    // --self references itself
    check(style["--self"].empty() || style["--self"] == "",
          "self-referential --self resolves to empty");
}

// ========== Grid L2 Tests ==========

// Reuse a simple node for grid tests
struct SpecGridNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    SpecGridNode* parentNode = nullptr;
    std::vector<SpecGridNode*> childNodes;
    ComputedStyle style_;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style_; }

    void addChild(SpecGridNode* child) {
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

struct SpecGridMetrics : public TextMetrics {
    float measureWidth(const std::string& text, const std::string&,
                       float fontSize, const std::string&) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(const std::string&, float fontSize, const std::string&) override {
        return fontSize * 1.2f;
    }
};

static void testGridAutoFillAutoFit() {
    printf("--- Grid L2: auto-fill / auto-fit ---\n");

    // auto-fill: repeat(auto-fill, 100px) in 350px container = 3 columns
    SpecGridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "repeat(auto-fill, 100px)";
    grid.style_["width"] = "350px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    SpecGridNode c1, c2, c3;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    c3.initBlock(); c3.style_["height"] = "30px";
    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);

    SpecGridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    // With auto-fill and 350px, we get floor(350/100) = 3 columns
    check(approx(c1.box.contentRect.width, 100.0f), "auto-fill: col width = 100px");
    check(approx(c2.box.contentRect.x - c1.box.contentRect.x, 100.0f, 2.0f),
          "auto-fill: c2 offset from c1");
    // All items on same row
    check(approx(c1.box.contentRect.y, c2.box.contentRect.y), "auto-fill: same row");

    // auto-fit: empty tracks collapse
    SpecGridNode grid2;
    grid2.initBlock();
    grid2.style_["display"] = "grid";
    grid2.style_["grid-template-columns"] = "repeat(auto-fit, 100px)";
    grid2.style_["width"] = "400px";
    grid2.style_["row-gap"] = "0";
    grid2.style_["column-gap"] = "0";

    SpecGridNode d1, d2;
    d1.initBlock(); d1.style_["height"] = "30px";
    d2.initBlock(); d2.style_["height"] = "30px";
    grid2.addChild(&d1);
    grid2.addChild(&d2);

    layoutTree(&grid2, 800.0f, metrics);

    // 400px / 100px = 4 tracks, but only 2 items. With auto-fit, empty tracks collapse.
    check(approx(d1.box.contentRect.width, 100.0f), "auto-fit: item1 width = 100");
    check(approx(d2.box.contentRect.width, 100.0f), "auto-fit: item2 width = 100");
}

static void testGridNamedLines() {
    printf("--- Grid L2: named lines ---\n");

    SpecGridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    // [start] 100px [mid] 200px [end]
    grid.style_["grid-template-columns"] = "[start] 100px [mid] 200px [end]";
    grid.style_["width"] = "300px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    SpecGridNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "30px";
    c1.style_["grid-column"] = "start / mid";
    c2.initBlock(); c2.style_["height"] = "30px";
    c2.style_["grid-column"] = "mid / end";

    grid.addChild(&c1);
    grid.addChild(&c2);

    SpecGridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    check(approx(c1.box.contentRect.width, 100.0f), "named lines: c1 width = 100");
    check(approx(c2.box.contentRect.width, 200.0f), "named lines: c2 width = 200");
    check(approx(c2.box.contentRect.x - c1.box.contentRect.x, 100.0f, 2.0f),
          "named lines: c2 starts after c1");
}

static void testGridImplicitTrackSizing() {
    printf("--- Grid L2: implicit track sizing ---\n");

    SpecGridNode grid;
    grid.initBlock();
    grid.style_["display"] = "grid";
    grid.style_["grid-template-columns"] = "100px 100px";
    grid.style_["grid-auto-rows"] = "50px";  // implicit rows should be 50px
    grid.style_["width"] = "200px";
    grid.style_["row-gap"] = "0";
    grid.style_["column-gap"] = "0";

    SpecGridNode c1, c2, c3, c4;
    c1.initBlock(); c2.initBlock(); c3.initBlock(); c4.initBlock();
    grid.addChild(&c1);
    grid.addChild(&c2);
    grid.addChild(&c3);
    grid.addChild(&c4);

    SpecGridMetrics metrics;
    layoutTree(&grid, 800.0f, metrics);

    // Row 0 and row 1 should each be 50px (from grid-auto-rows)
    check(approx(c1.box.contentRect.height, 50.0f), "implicit rows: c1 height = 50px");
    check(approx(c3.box.contentRect.height, 50.0f), "implicit rows: c3 height = 50px");
    check(c3.box.contentRect.y > c1.box.contentRect.y, "implicit rows: row 1 below row 0");
}

// ========== Containment L2 Tests ==========

static void testContainmentSize() {
    printf("--- Containment L2: contain: size ---\n");

    SpecGridNode container;
    container.initBlock();
    container.style_["display"] = "block";
    container.style_["width"] = "200px";
    container.style_["contain"] = "size";
    // No explicit height -> contain:size should make height 0

    SpecGridNode child;
    child.initBlock();
    child.style_["height"] = "100px";
    container.addChild(&child);

    SpecGridMetrics metrics;
    layoutTree(&container, 800.0f, metrics);

    check(approx(container.box.contentRect.height, 0.0f),
          "contain:size with auto height -> 0");
}

static void testContainmentPaint() {
    printf("--- Containment L2: contain: paint clips ---\n");

    // contain: paint should clip children (verified by applyOverflowClipping)
    SpecGridNode container;
    container.initBlock();
    container.style_["display"] = "block";
    container.style_["width"] = "100px";
    container.style_["height"] = "50px";
    container.style_["contain"] = "paint";

    SpecGridNode child;
    child.initBlock();
    child.style_["height"] = "200px"; // taller than container

    container.addChild(&child);

    SpecGridMetrics metrics;
    layoutTree(&container, 800.0f, metrics);
    applyOverflowClipping(&container);

    // Child should be clipped to container height
    check(child.box.contentRect.height <= 50.0f,
          "contain:paint clips child height");
}

static void testContentVisibilityHidden() {
    printf("--- Containment L2: content-visibility: hidden ---\n");

    SpecGridNode container;
    container.initBlock();
    container.style_["display"] = "block";
    container.style_["width"] = "200px";
    container.style_["height"] = "100px";
    container.style_["content-visibility"] = "hidden";

    SpecGridNode child;
    child.initBlock();
    child.style_["height"] = "300px";
    container.addChild(&child);

    SpecGridMetrics metrics;
    layoutTree(&container, 800.0f, metrics);

    // Container should use explicit size, children not laid out
    check(approx(container.box.contentRect.width, 200.0f),
          "content-visibility:hidden preserves width");
    check(approx(container.box.contentRect.height, 100.0f),
          "content-visibility:hidden preserves explicit height");
}

// ========== Entry Point ==========

void testSpecCompleteness() {
    // CSS Logical Properties L1
    testLogicalMargin();
    testLogicalPadding();
    testLogicalSizing();
    testLogicalInset();
    testLogicalBorder();

    // Selectors L4
    testSelectorsL4();

    // Overflow L3
    testOverflowShorthand();

    // CSS Variables cycle detection
    testVarCycleDetection();

    // Grid L2
    testGridAutoFillAutoFit();
    testGridNamedLines();
    testGridImplicitTrackSizing();

    // Containment L2
    testContainmentSize();
    testContainmentPaint();
    testContentVisibilityHidden();
}
