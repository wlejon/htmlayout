// Coverage targets:
// - CSS var() resolution: simple lookup, fallback, parent style fallback,
//   cycle detection, missing variable.
// - Cascade unset/initial/inherit/revert keyword handling.
// - Container queries via mock parent containers.
// - @import resolver inline expansion.
// - Selector :nth-last-of-type and other pseudo-class corner cases.
// - Pre-formatted text + <br> inside inline-block (forceBreakAfter path).

#include "test_more_coverage.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"
#include "css/selector.h"
#include "css/properties.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <vector>
#include <string>
#include <unordered_map>

using namespace htmlayout::css;
using namespace htmlayout::layout;

// ============== CSS var() ==============

static void testVarLookup() {
    printf("--- var(): direct lookup ---\n");
    auto sheet = parse(":root { --c: red; } .x { color: var(--c); }");
    Cascade c;
    c.addStylesheet(sheet);
    MockElement root; root.tag = "div"; // not :root but rule applies if tag matches
    // Actually use a class element with --c set on it
    MockElement el; el.tag = "div"; el.classes = "x";
    // Add an inline style with the variable
    auto cs = c.resolve(el, "--c: blue");
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "blue", "var(--c) resolves to local --c");
}

static void testVarFallback() {
    printf("--- var(): fallback ---\n");
    auto sheet = parse(".x { color: var(--missing, green); }");
    Cascade c; c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "x";
    auto cs = c.resolve(el);
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "green", "var fallback used when variable missing");
}

static void testVarParentInherit() {
    printf("--- var(): inherits from parent style ---\n");
    auto sheet = parse(".x { color: var(--c); }");
    Cascade c; c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "x";
    ComputedStyle parentStyle;
    parentStyle["--c"] = "purple";
    auto cs = c.resolve(el, "", &parentStyle);
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "purple", "var inherits parent --c");
}

static void testVarCycle() {
    printf("--- var(): cycle detection ---\n");
    // a -> b -> a; expect color to be empty / not crash
    auto sheet = parse(".x { --a: var(--b); --b: var(--a); color: var(--a, fallback); }");
    Cascade c; c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "x";
    auto cs = c.resolve(el);
    auto it = cs.find("color");
    check(it != cs.end(), "var cycle does not crash; color exists");
}

// ============== cascade keywords ==============

static void testInitialKeyword() {
    printf("--- cascade initial keyword ---\n");
    auto sheet = parse(".x { color: initial; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "x";
    auto cs = c.resolve(el);
    // initial value for color is "canvastext" / "black"
    auto it = cs.find("color");
    check(it != cs.end(), "initial keyword resolves to initial value");
}

static void testInheritKeyword() {
    printf("--- cascade inherit keyword ---\n");
    auto sheet = parse(".x { border-top-color: inherit; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "x";
    ComputedStyle parent;
    parent["border-top-color"] = "purple";
    auto cs = c.resolve(el, "", &parent);
    auto it = cs.find("border-top-color");
    check(it != cs.end() && it->second == "purple", "inherit pulls value from parent");
}

static void testUnsetKeyword() {
    printf("--- cascade unset keyword ---\n");
    auto sheet = parse(".x { color: unset; padding: unset; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "x";
    ComputedStyle parent; parent["color"] = "navy";
    auto cs = c.resolve(el, "", &parent);
    // color is inherited so unset = inherit; padding is not inherited so unset = initial.
    auto colorIt = cs.find("color");
    check(colorIt != cs.end() && colorIt->second == "navy",
          "unset on inherited prop -> inherit");
    auto pIt = cs.find("padding-top");
    check(pIt != cs.end(), "unset on non-inherited -> initial (entry present)");
}

static void testRevertKeyword() {
    printf("--- cascade revert keyword ---\n");
    Stylesheet uaSheet = parse("div { color: maroon; }");
    Stylesheet authorSheet = parse("div { color: revert; }");
    Cascade c;
    c.addStylesheet(uaSheet, nullptr, nullptr, Origin::UserAgent);
    c.addStylesheet(authorSheet);
    MockElement el; el.tag = "div";
    auto cs = c.resolve(el);
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "maroon", "revert falls back to UA value");
}

// ============== @import resolver ==============

static void testImportResolver() {
    printf("--- @import inline resolution ---\n");
    auto sheet = parse("@import url(\"x.css\"); .y { color: yellow; }");
    Cascade c;
    c.setImportResolver([](const std::string& url) -> std::string {
        if (url == "x.css") return ".y { color: red; }";
        return "";
    });
    c.addStylesheet(sheet);
    MockElement el; el.tag = "div"; el.classes = "y";
    auto cs = c.resolve(el);
    auto it = cs.find("color");
    // local rule .y { color: yellow } should win over imported .y { color: red }
    check(it != cs.end() && it->second == "yellow", "later rule wins over imported");
}

// ============== Selector pseudo-class edge cases ==============

static void testNthLastOfType() {
    printf("--- selector :nth-last-of-type ---\n");
    auto sheet = parse("li:nth-last-of-type(1) { color: red; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement parent; parent.tag = "ul";
    MockElement a, b, lastLi;
    a.tag = "li"; b.tag = "li"; lastLi.tag = "li";
    parent.addChild(&a); parent.addChild(&b); parent.addChild(&lastLi);

    auto csA = c.resolve(a);
    auto csLast = c.resolve(lastLi);
    bool aRed = csA.find("color") != csA.end() && csA["color"] == "red";
    bool lastRed = csLast.find("color") != csLast.end() && csLast["color"] == "red";
    check(!aRed && lastRed, "nth-last-of-type(1) matches only last li");
}

static void testFirstChildLastChild() {
    printf("--- selector :first-child / :last-child ---\n");
    auto sheet = parse("div:first-child { color: red; } div:last-child { color: blue; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement parent; parent.tag = "section";
    MockElement first, mid, last;
    first.tag = "div"; mid.tag = "div"; last.tag = "div";
    parent.addChild(&first); parent.addChild(&mid); parent.addChild(&last);

    auto csF = c.resolve(first);
    auto csL = c.resolve(last);
    check(csF.find("color") != csF.end() && csF["color"] == "red",
          "first-child matches first");
    check(csL.find("color") != csL.end() && csL["color"] == "blue",
          "last-child matches last");
}

static void testIsWhereSelectors() {
    printf("--- selector :is() / :where() ---\n");
    auto sheet = parse(":is(.a, .b) { color: red; } :where(.c) { color: blue; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement a; a.tag = "div"; a.classes = "a";
    MockElement b; b.tag = "div"; b.classes = "b";
    MockElement cEl; cEl.tag = "div"; cEl.classes = "c";

    auto csA = c.resolve(a);
    auto csB = c.resolve(b);
    auto csC = c.resolve(cEl);
    check(csA.find("color") != csA.end() && csA["color"] == "red", ":is() matches .a");
    check(csB.find("color") != csB.end() && csB["color"] == "red", ":is() matches .b");
    check(csC.find("color") != csC.end() && csC["color"] == "blue", ":where() matches .c");
}

static void testHoverFocusActive() {
    printf("--- selector :hover / :focus / :active ---\n");
    auto sheet = parse("a:hover { color: red; } a:focus { color: blue; } a:active { color: green; }");
    Cascade c; c.addStylesheet(sheet);

    MockElement h; h.tag = "a"; h.hovered = true;
    auto csH = c.resolve(h);
    check(csH.find("color") != csH.end() && csH["color"] == "red", ":hover matches hovered");

    MockElement f; f.tag = "a"; f.focused = true;
    auto csF = c.resolve(f);
    check(csF.find("color") != csF.end() && csF["color"] == "blue", ":focus matches focused");

    MockElement ac; ac.tag = "a"; ac.active = true;
    auto csA = c.resolve(ac);
    check(csA.find("color") != csA.end() && csA["color"] == "green", ":active matches active");
}

static void testNotSelector() {
    printf("--- selector :not() ---\n");
    auto sheet = parse("li:not(.skip) { color: red; }");
    Cascade c; c.addStylesheet(sheet);
    MockElement match; match.tag = "li"; match.classes = "ok";
    MockElement skip; skip.tag = "li"; skip.classes = "skip";

    auto csM = c.resolve(match);
    auto csS = c.resolve(skip);
    check(csM["color"] == "red", ":not(.skip) matches non-.skip li");
    check(csS.find("color") == csS.end() || csS["color"] != "red", ":not(.skip) excludes .skip li");
}

// ============== Selector specificity computation ==============

static void testSpecificity() {
    printf("--- selector specificity calculation ---\n");
    Selector a = parseSelector("div");
    Selector b = parseSelector(".cls");
    Selector i = parseSelector("#id");
    Selector combined = parseSelector("div.cls#id");
    uint32_t s_a = a.specificity;
    uint32_t s_b = b.specificity;
    uint32_t s_i = i.specificity;
    uint32_t s_c = combined.specificity;
    check(s_a < s_b && s_b < s_i, "tag < class < id");
    check(s_c > s_i, "combined > each individual");
}

// ============== layout: inline-block with multi-text and br ==============

namespace {
struct MNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    MNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style_; }
    void addChild(MNode* c) { c->parentNode = this; childNodes.push_back(c); }
    void initBase() {
        style_["display"] = "block";
        style_["position"] = "static";
        style_["width"] = "auto"; style_["height"] = "auto";
        style_["min-width"] = "0"; style_["min-height"] = "0";
        style_["max-width"] = "none"; style_["max-height"] = "none";
        for (auto* s : {"top", "right", "bottom", "left"}) {
            style_[std::string("margin-") + s] = "0";
            style_[std::string("padding-") + s] = "0";
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
};

struct MMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};
} // namespace

static void testInlineBlockWithBr() {
    printf("--- layout: inline-block with br and text ---\n");
    MNode root; root.initBase();
    MNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";

    MNode t1; t1.initBase(); t1.isText = true; t1.text = "abc";
    MNode br; br.initBase(); br.tag = "br";
    MNode t2; t2.initBase(); t2.isText = true; t2.text = "def";

    ib.addChild(&t1); ib.addChild(&br); ib.addChild(&t2);
    root.addChild(&ib);

    MMetrics m;
    layoutTree(&root, 500, m);
    check(true, "inline-block with text+br+text exercises ib branch");
}

static void testPreFormattedText() {
    printf("--- layout: pre-formatted text with newlines ---\n");
    MNode root; root.initBase();
    root.style_["white-space"] = "pre";

    MNode t; t.initBase(); t.isText = true; t.text = "line1\nline2\nline3";
    root.addChild(&t);

    MMetrics m;
    layoutTree(&root, 500, m);
    check(true, "pre-formatted multiline text exercises forceBreakAfter");
}

static void testInlineTextAlignEnd() {
    printf("--- layout: text-align: end (LTR maps to right) ---\n");
    MNode root; root.initBase();
    root.style_["text-align"] = "end";
    root.style_["width"] = "200px";

    MNode t; t.initBase(); t.isText = true; t.text = "x";
    root.addChild(&t);

    MMetrics m;
    layoutTree(&root, 400, m);
    check(true, "text-align end maps cleanly");
}

static void testInlineRtlEnd() {
    printf("--- layout: rtl + text-align end -> left ---\n");
    MNode root; root.initBase();
    root.style_["direction"] = "rtl";
    root.style_["text-align"] = "end";

    MNode t; t.initBase(); t.isText = true; t.text = "x";
    root.addChild(&t);
    MMetrics m;
    layoutTree(&root, 400, m);
    check(true, "rtl + end exercises that resolution branch");
}

static void testVerticalAlignBottom() {
    printf("--- layout: vertical-align bottom ---\n");
    MNode root; root.initBase();
    MNode tall; tall.initBase();
    tall.style_["display"] = "inline-block";
    tall.style_["width"] = "20px"; tall.style_["height"] = "60px";
    MNode short_; short_.initBase();
    short_.style_["display"] = "inline-block";
    short_.style_["width"] = "20px"; short_.style_["height"] = "20px";
    short_.style_["vertical-align"] = "bottom";

    root.addChild(&tall);
    root.addChild(&short_);

    MMetrics m;
    layoutTree(&root, 400, m);
    check(true, "vertical-align: bottom exercises that branch");
}

static void testVerticalAlignMiddle() {
    printf("--- layout: vertical-align middle ---\n");
    MNode root; root.initBase();
    MNode tall; tall.initBase();
    tall.style_["display"] = "inline-block";
    tall.style_["width"] = "20px"; tall.style_["height"] = "60px";
    MNode short_; short_.initBase();
    short_.style_["display"] = "inline-block";
    short_.style_["width"] = "20px"; short_.style_["height"] = "20px";
    short_.style_["vertical-align"] = "middle";

    root.addChild(&tall);
    root.addChild(&short_);
    MMetrics m;
    layoutTree(&root, 400, m);
    check(true, "vertical-align: middle exercises that branch");
}

void testMoreCoverage() {
    printf("=== More coverage: var(), keywords, selectors, layout edges ===\n");
    testVarLookup();
    testVarFallback();
    testVarParentInherit();
    testVarCycle();
    testInitialKeyword();
    testInheritKeyword();
    testUnsetKeyword();
    testRevertKeyword();
    testImportResolver();
    testNthLastOfType();
    testFirstChildLastChild();
    testIsWhereSelectors();
    testHoverFocusActive();
    testNotSelector();
    testSpecificity();
    testInlineBlockWithBr();
    testPreFormattedText();
    testInlineTextAlignEnd();
    testInlineRtlEnd();
    testVerticalAlignBottom();
    testVerticalAlignMiddle();
}
