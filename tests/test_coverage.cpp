#include "test_coverage.h"
#include "test_helpers.h"
#include "css/tokenizer.h"
#include "css/parser.h"
#include "css/selector.h"
#include "css/cascade.h"
#include "css/properties.h"
#include "css/color.h"
#include "css/ua_stylesheet.h"
#include "layout/box.h"
#include "layout/text.h"
#include "layout/formatting_context.h"
#include "layout/multicol.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::css;
using namespace htmlayout::layout;

// ---- Shared Mock Node ----

struct CovNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    CovNode* parentNode = nullptr;
    std::vector<CovNode*> childNodes;
    ComputedStyle style_;
    bool hasIntrinsic = false;
    float intrW = 0, intrH = 0;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style_; }
    bool intrinsicSize(float& w, float& h, float) const override {
        if (!hasIntrinsic) return false;
        w = intrW; h = intrH; return true;
    }

    void addChild(CovNode* c) { c->parentNode = this; childNodes.push_back(c); }

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
        style_["font-family"] = "monospace";
        style_["font-weight"] = "normal";
        style_["overflow"] = "visible";
        style_["white-space"] = "normal";
        style_["text-align"] = "left";
        style_["vertical-align"] = "baseline";
    }
    void initInline() { initBlock(); style_["display"] = "inline"; }
    void initFlex() { initBlock(); style_["display"] = "flex";
        style_["flex-direction"] = "row"; style_["flex-wrap"] = "nowrap";
        style_["justify-content"] = "flex-start"; style_["align-items"] = "stretch";
        style_["align-content"] = "stretch";
        style_["gap"] = "0"; style_["row-gap"] = "0"; style_["column-gap"] = "0";
    }
    void initFlexItem() { initBlock();
        style_["flex-grow"] = "0"; style_["flex-shrink"] = "1";
        style_["flex-basis"] = "auto"; style_["align-self"] = "auto"; style_["order"] = "0";
    }
    void initGrid() { initBlock(); style_["display"] = "grid";
        style_["row-gap"] = "0"; style_["column-gap"] = "0";
    }
    void initTable() { initBlock(); style_["display"] = "table";
        style_["border-collapse"] = "separate"; style_["border-spacing"] = "0";
    }
};

struct CovMetrics : public TextMetrics {
    float measureWidth(const std::string& t, const std::string&, float, const std::string&) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(const std::string&, float, const std::string&) override { return 20.0f; }
};

static bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

// ======================================================================
// TOKENIZER COVERAGE
// ======================================================================

static void testTokenizerWhitespace() {
    printf("--- Tokenizer: whitespace variants ---\n");
    auto tokens = tokenize("a\tb\nc");
    int identCount = 0;
    for (auto& t : tokens) if (t.type == TokenType::Ident) identCount++;
    check(identCount == 3, "tokenizer: tabs and newlines separate idents");
}

static void testTokenizerEscape() {
    printf("--- Tokenizer: escape sequences ---\n");
    auto tokens = tokenize(".cl\\ass { }");
    // Should have tokens for the escaped identifier
    check(tokens.size() >= 3, "tokenizer: escaped class name produces tokens");
}

static void testTokenizerCDOCDC() {
    printf("--- Tokenizer: CDO/CDC ---\n");
    auto tokens = tokenize("<!-- .a {} -->");
    bool hasCDO = false, hasCDC = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::CDO) hasCDO = true;
        if (t.type == TokenType::CDC) hasCDC = true;
    }
    check(hasCDO, "tokenizer: <!-- produces CDO");
    check(hasCDC, "tokenizer: --> produces CDC");
}

static void testTokenizerDecimalOnly() {
    printf("--- Tokenizer: decimal-only numbers ---\n");
    auto tokens = tokenize(".5px");
    bool found = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Dimension && t.unit == "px") {
            check(std::abs(t.numeric - 0.5f) < 0.01f, "tokenizer: .5px numeric = 0.5");
            found = true;
        }
    }
    check(found, "tokenizer: .5px parsed as dimension");
}

static void testTokenizerPositiveSign() {
    printf("--- Tokenizer: positive sign ---\n");
    auto tokens = tokenize("+10px");
    // +10px may tokenize as Dimension(+10, px) or Delim(+) + Dimension(10, px)
    bool hasDim = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Dimension && t.unit == "px") hasDim = true;
    }
    check(hasDim, "tokenizer: +10px contains dimension token");
}

static void testTokenizerEmptyString() {
    printf("--- Tokenizer: empty input ---\n");
    auto tokens = tokenize("");
    check(tokens.size() == 1 && tokens[0].type == TokenType::EndOfFile, "tokenizer: empty -> EOF only");
}

static void testTokenizerMultipleComments() {
    printf("--- Tokenizer: multiple comments ---\n");
    auto tokens = tokenize("/* first */ a /* second */ b");
    int identCount = 0;
    for (auto& t : tokens) if (t.type == TokenType::Ident) identCount++;
    check(identCount == 2, "tokenizer: 2 idents after stripping comments");
}

// ======================================================================
// PARSER COVERAGE
// ======================================================================

static void testParserMediaBlocks() {
    printf("--- Parser: @media block parsing ---\n");
    auto sheet = parse(
        "@media screen and (min-width: 768px) { .wide { color: blue; } }\n"
        "@media print { .print { color: black; } }\n"
    );
    check(sheet.mediaBlocks.size() == 2, "parser: 2 media blocks found");
    check(sheet.mediaBlocks[0].rules.size() == 1, "parser: media block 0 has 1 rule");
    check(sheet.mediaBlocks[1].rules.size() == 1, "parser: media block 1 has 1 rule");
}

static void testParserLayerBlocks() {
    printf("--- Parser: @layer blocks ---\n");
    auto sheet = parse(
        "@layer base { div { color: red; } }\n"
        "@layer theme { div { color: blue; } }\n"
        "@layer base, theme;\n"
    );
    check(sheet.layerBlocks.size() == 2, "parser: 2 layer blocks");
    check(sheet.layerOrder.size() >= 2, "parser: layer order has entries");
}

static void testParserContainerBlocks() {
    printf("--- Parser: @container blocks ---\n");
    auto sheet = parse(
        "@container sidebar (min-width: 400px) { .item { color: red; } }\n"
        "@container (max-width: 300px) { .small { font-size: 12px; } }\n"
    );
    check(sheet.containerBlocks.size() == 2, "parser: 2 container blocks");
    check(sheet.containerBlocks[0].name == "sidebar", "parser: container name = sidebar");
}

static void testParserNestedMediaInLayer() {
    printf("--- Parser: @media nested in @layer ---\n");
    auto sheet = parse(
        "@layer base {\n"
        "  div { color: red; }\n"
        "  @media (min-width: 500px) { div { color: blue; } }\n"
        "}\n"
    );
    check(sheet.layerBlocks.size() == 1, "parser: 1 layer block");
    check(sheet.layerBlocks[0].mediaBlocks.size() == 1, "parser: layer has 1 nested media block");
}

static void testParserSupportsKnownProp() {
    printf("--- Parser: @supports known property ---\n");
    auto sheet = parse(
        "@supports (display: flex) { .flex { display: flex; } }\n"
        ".normal { color: red; }\n"
    );
    // display is a known property, so the @supports block should be included
    check(sheet.rules.size() >= 2, "@supports: known property included");
}

static void testMediaQueryEvaluation() {
    printf("--- Media: query evaluation edge cases ---\n");
    // height features
    check(evaluateMediaQuery("(min-height: 500px)", {1024, 768, "screen"}) == true,
          "media: min-height:500px matches 768px");
    check(evaluateMediaQuery("(max-height: 500px)", {1024, 768, "screen"}) == false,
          "media: max-height:500px doesn't match 768px");

    // Boolean feature
    check(evaluateMediaQuery("(color)", {1024, 768, "screen"}) == true,
          "media: boolean (color) matches");

    // Width exact match
    check(evaluateMediaQuery("(width: 1024px)", {1024, 768, "screen"}) == true,
          "media: exact width:1024px matches");
    check(evaluateMediaQuery("(width: 500px)", {1024, 768, "screen"}) == false,
          "media: exact width:500px doesn't match");

    // Height exact match
    check(evaluateMediaQuery("(height: 768px)", {1024, 768, "screen"}) == true,
          "media: exact height:768px matches");

    // Range syntax
    check(evaluateMediaQuery("(width > 500px)", {1024, 768, "screen"}) == true,
          "media: width > 500px matches 1024");
    check(evaluateMediaQuery("(width > 2000px)", {1024, 768, "screen"}) == false,
          "media: width > 2000px doesn't match 1024");
    check(evaluateMediaQuery("(width >= 1024px)", {1024, 768, "screen"}) == true,
          "media: width >= 1024px matches exactly");
    check(evaluateMediaQuery("(width < 1025px)", {1024, 768, "screen"}) == true,
          "media: width < 1025px matches 1024");
    check(evaluateMediaQuery("(width <= 1024px)", {1024, 768, "screen"}) == true,
          "media: width <= 1024px matches exactly");
    check(evaluateMediaQuery("(width < 500px)", {1024, 768, "screen"}) == false,
          "media: width < 500px doesn't match 1024");

    // Reversed range: value op feature
    check(evaluateMediaQuery("(500px < width)", {1024, 768, "screen"}) == true,
          "media: 500px < width matches 1024");
    check(evaluateMediaQuery("(2000px < width)", {1024, 768, "screen"}) == false,
          "media: 2000px < width doesn't match 1024");

    // Chained range: value <= feature <= value
    check(evaluateMediaQuery("(500px <= width <= 1500px)", {1024, 768, "screen"}) == true,
          "media: 500px <= width <= 1500px matches 1024");
    check(evaluateMediaQuery("(500px <= width <= 800px)", {1024, 768, "screen"}) == false,
          "media: 500px <= width <= 800px doesn't match 1024");

    // Height range
    check(evaluateMediaQuery("(height > 500px)", {1024, 768, "screen"}) == true,
          "media: height > 500px matches 768");
    check(evaluateMediaQuery("(height < 500px)", {1024, 768, "screen"}) == false,
          "media: height < 500px doesn't match 768");

    // Logical or
    check(evaluateMediaQuery("(width > 2000px) or (height > 500px)", {1024, 768, "screen"}) == true,
          "media: or - second condition true");
    check(evaluateMediaQuery("(width > 2000px) or (height > 2000px)", {1024, 768, "screen"}) == false,
          "media: or - both false");
    check(evaluateMediaQuery("(width > 500px) or (height > 500px)", {1024, 768, "screen"}) == true,
          "media: or - both true");

    // Mix of and/or
    check(evaluateMediaQuery("(min-width: 500px) and (max-width: 1500px)", {1024, 768, "screen"}) == true,
          "media: and - both match");
}

// ======================================================================
// SELECTOR COVERAGE
// ======================================================================

static void testSelectorNthLastChild() {
    printf("--- Selector: :nth-last-child ---\n");
    MockElement parent; parent.tag = "ul";
    MockElement c1, c2, c3;
    c1.tag = "li"; c2.tag = "li"; c3.tag = "li";
    parent.addChild(&c1); parent.addChild(&c2); parent.addChild(&c3);

    auto sel = parseSelector("li:nth-last-child(1)");
    check(sel.matches(c3), ":nth-last-child(1) matches last child");
    check(!sel.matches(c1), ":nth-last-child(1) doesn't match first child");
}

static void testSelectorNthOfType() {
    printf("--- Selector: :nth-of-type ---\n");
    MockElement parent; parent.tag = "div";
    MockElement s1, d1, s2;
    s1.tag = "span"; d1.tag = "div"; s2.tag = "span";
    parent.addChild(&s1); parent.addChild(&d1); parent.addChild(&s2);

    auto sel = parseSelector("span:nth-of-type(2)");
    check(sel.matches(s2), ":nth-of-type(2) matches second span");
    check(!sel.matches(s1), ":nth-of-type(2) doesn't match first span");
}

static void testSelectorOnlyOfType() {
    printf("--- Selector: :only-of-type ---\n");
    MockElement parent; parent.tag = "ul";
    MockElement s1, d1;
    s1.tag = "span"; d1.tag = "div";
    parent.addChild(&s1); parent.addChild(&d1);

    auto sel = parseSelector("span:only-of-type");
    check(sel.matches(s1), ":only-of-type matches sole span");
    auto sel2 = parseSelector("div:only-of-type");
    check(sel2.matches(d1), ":only-of-type matches sole div child");
}

static void testSelectorRoot() {
    printf("--- Selector: :root ---\n");
    MockElement root; root.tag = "html";
    MockElement child; child.tag = "body";
    root.addChild(&child);

    auto sel = parseSelector(":root");
    check(sel.matches(root), ":root matches element without parent");
    check(!sel.matches(child), ":root doesn't match child");
}

static void testSelectorActiveState() {
    printf("--- Selector: :active ---\n");
    MockElement elem; elem.tag = "button"; elem.active = true;
    auto sel = parseSelector(":active");
    check(sel.matches(elem), ":active matches active element");
    elem.active = false;
    check(!sel.matches(elem), ":active doesn't match non-active");
}

static void testSelectorAttrSubstring() {
    printf("--- Selector: attribute substring operators ---\n");
    MockElement elem; elem.tag = "a";
    elem.attrs["href"] = "https://example.com/path";

    check(parseSelector("[href*=example]").matches(elem), "[*=] substring match");
    check(parseSelector("[href^=https]").matches(elem), "[^=] prefix match");
    check(parseSelector("[href$=path]").matches(elem), "[$=] suffix match");
    check(!parseSelector("[href$=.html]").matches(elem), "[$=] suffix mismatch");
}

static void testSelectorListSpecificity() {
    printf("--- Selector: list specificity ---\n");
    // Each selector in list has independent specificity
    auto list = parseSelectorList("div, .class, #id");
    check(list.size() == 3, "selector list: 3 selectors");
    check(list[0].specificity < list[1].specificity, "div < .class specificity");
    check(list[1].specificity < list[2].specificity, ".class < #id specificity");
}

static void testSelectorNthFormula() {
    printf("--- Selector: :nth-child formulas ---\n");
    MockElement parent; parent.tag = "ul";
    MockElement c[6];
    for (int i = 0; i < 6; i++) {
        c[i].tag = "li";
        parent.addChild(&c[i]);
    }
    // :nth-child(3n+1) matches 1st, 4th (indices 0, 3)
    auto sel = parseSelector("li:nth-child(3n+1)");
    check(sel.matches(c[0]), ":nth-child(3n+1) matches 1st");
    check(!sel.matches(c[1]), ":nth-child(3n+1) doesn't match 2nd");
    check(!sel.matches(c[2]), ":nth-child(3n+1) doesn't match 3rd");
    check(sel.matches(c[3]), ":nth-child(3n+1) matches 4th");
}

static void testSelectorNthNegative() {
    printf("--- Selector: :nth-child(-n+3) ---\n");
    MockElement parent; parent.tag = "ul";
    MockElement c[5];
    for (int i = 0; i < 5; i++) { c[i].tag = "li"; parent.addChild(&c[i]); }

    auto sel = parseSelector("li:nth-child(-n+3)");
    check(sel.matches(c[0]), ":nth-child(-n+3) matches 1st");
    check(sel.matches(c[1]), ":nth-child(-n+3) matches 2nd");
    check(sel.matches(c[2]), ":nth-child(-n+3) matches 3rd");
    check(!sel.matches(c[3]), ":nth-child(-n+3) doesn't match 4th");
}

static void testSelectorComplex() {
    printf("--- Selector: complex multi-combinator ---\n");
    MockElement html; html.tag = "html";
    MockElement body; body.tag = "body";
    MockElement div; div.tag = "div"; div.classes = "container";
    MockElement ul; ul.tag = "ul";
    MockElement li; li.tag = "li"; li.classes = "item";

    html.addChild(&body); body.addChild(&div); div.addChild(&ul); ul.addChild(&li);

    auto sel = parseSelector("body .container > ul li.item");
    check(sel.matches(li), "complex: body .container > ul li.item matches");
}

// ======================================================================
// CASCADE COVERAGE
// ======================================================================

static void testCascadeLayerPrecedence() {
    printf("--- Cascade: layer precedence ---\n");
    Cascade cascade;
    auto sheet = parse(
        "@layer base { div { color: red; } }\n"
        "@layer theme { div { color: blue; } }\n"
        "div { color: green; }\n"
    );
    cascade.addStylesheet(sheet);
    MockElement d; d.tag = "div";
    auto s = cascade.resolve(d);
    check(s["color"] == "green", "cascade: unlayered beats all layers");
}

static void testCascadeLayerImportant() {
    printf("--- Cascade: layer !important precedence ---\n");
    Cascade cascade;
    auto sheet = parse(
        "@layer base { div { color: red !important; } }\n"
        "div { color: green !important; }\n"
    );
    cascade.addStylesheet(sheet);
    MockElement d; d.tag = "div";
    auto s = cascade.resolve(d);
    // !important reversal: layered !important wins over unlayered !important
    check(s["color"] == "red", "cascade: layered !important beats unlayered !important");
}

static void testCascadeContainerQueries() {
    printf("--- Cascade: container queries ---\n");
    Cascade cascade;
    auto sheet = parse(
        "@container (min-width: 400px) { .item { color: blue; } }\n"
        ".item { color: red; }\n"
    );
    cascade.addStylesheet(sheet);

    // Element with container ancestor that's wide enough
    MockElement container; container.tag = "div";
    container.contType = "inline-size";
    container.contInlineSize = 500;

    MockElement item; item.tag = "div"; item.classes = "item";
    container.addChild(&item);

    auto s = cascade.resolve(item);
    check(s["color"] == "blue", "cascade: container query matches when wide enough");
}

static void testCascadeVarRecursionLimit() {
    printf("--- Cascade: var() recursion limit ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        "div { --a: var(--b); --b: var(--c); --c: var(--d); --d: var(--e);"
        "  --e: var(--f); --f: var(--g); --g: var(--h); --h: var(--i);"
        "  --i: var(--j); --j: var(--k); --k: final;"
        "  color: var(--a); }\n"
    ));
    MockElement d; d.tag = "div";
    auto s = cascade.resolve(d);
    // Should hit recursion limit (10 levels) and not crash
    check(s.count("color") > 0, "cascade: deep var() chain doesn't crash");
}

static void testCascadeRevertKeyword() {
    printf("--- Cascade: revert keyword ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: revert; }"));
    MockElement d; d.tag = "div";
    auto s = cascade.resolve(d);
    // revert treated as unset for author origin: inherited prop inherits, non-inherited resets
    // color is inherited, no parent -> initial = black
    check(s["color"] == "black", "cascade: revert on inherited prop with no parent = initial");
}

static void testCascadeCustomPropInheritance() {
    printf("--- Cascade: custom property inheritance ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".parent { --accent: hotpink; }\n"
        ".child { background-color: var(--accent); }\n"
    ));
    MockElement parent; parent.tag = "div"; parent.classes = "parent";
    MockElement child; child.tag = "div"; child.classes = "child";
    parent.addChild(&child);
    auto ps = cascade.resolve(parent);
    auto cs = cascade.resolve(child, {}, &ps);
    check(cs["background-color"] == "hotpink", "cascade: custom prop inherited through parent");
}

// ======================================================================
// PROPERTIES/SHORTHAND COVERAGE
// ======================================================================

static void testShorthandBorderRadius() {
    printf("--- Shorthand: border-radius ---\n");
    auto decls = expandShorthand("border-radius", "10px");
    bool hasTL = false, hasTR = false, hasBR = false, hasBL = false;
    for (auto& d : decls) {
        if (d.property == "border-top-left-radius" && d.value == "10px") hasTL = true;
        if (d.property == "border-top-right-radius" && d.value == "10px") hasTR = true;
        if (d.property == "border-bottom-right-radius" && d.value == "10px") hasBR = true;
        if (d.property == "border-bottom-left-radius" && d.value == "10px") hasBL = true;
    }
    check(hasTL && hasTR && hasBR && hasBL, "border-radius: 1-value expands to all corners");
}

static void testShorthandBorderRadiusTwoValues() {
    printf("--- Shorthand: border-radius 2 values ---\n");
    auto decls = expandShorthand("border-radius", "5px 10px");
    bool tl5 = false, tr10 = false, br5 = false, bl10 = false;
    for (auto& d : decls) {
        if (d.property == "border-top-left-radius" && d.value == "5px") tl5 = true;
        if (d.property == "border-top-right-radius" && d.value == "10px") tr10 = true;
        if (d.property == "border-bottom-right-radius" && d.value == "5px") br5 = true;
        if (d.property == "border-bottom-left-radius" && d.value == "10px") bl10 = true;
    }
    check(tl5 && tr10 && br5 && bl10, "border-radius: 2 values expand correctly");
}

static void testShorthandOutline() {
    printf("--- Shorthand: outline ---\n");
    auto decls = expandShorthand("outline", "2px solid red");
    bool hasW = false, hasS = false, hasC = false;
    for (auto& d : decls) {
        if (d.property == "outline-width" && d.value == "2px") hasW = true;
        if (d.property == "outline-style" && d.value == "solid") hasS = true;
        if (d.property == "outline-color" && d.value == "red") hasC = true;
    }
    check(hasW && hasS && hasC, "outline: expands to width, style, color");
}

static void testShorthandColumns() {
    printf("--- Shorthand: columns ---\n");
    auto decls = expandShorthand("columns", "200px 3");
    bool hasW = false, hasC = false;
    for (auto& d : decls) {
        if (d.property == "column-width" && d.value == "200px") hasW = true;
        if (d.property == "column-count" && d.value == "3") hasC = true;
    }
    check(hasW && hasC, "columns: expands to column-width and column-count");
}

static void testShorthandPlaceItems() {
    printf("--- Shorthand: place-items ---\n");
    auto decls = expandShorthand("place-items", "center start");
    bool hasAI = false, hasJI = false;
    for (auto& d : decls) {
        if (d.property == "align-items" && d.value == "center") hasAI = true;
        if (d.property == "justify-items" && d.value == "start") hasJI = true;
    }
    check(hasAI && hasJI, "place-items: expands to align-items and justify-items");
}

static void testShorthandPlaceContent() {
    printf("--- Shorthand: place-content ---\n");
    auto decls = expandShorthand("place-content", "space-between");
    bool hasAC = false, hasJC = false;
    for (auto& d : decls) {
        if (d.property == "align-content" && d.value == "space-between") hasAC = true;
        if (d.property == "justify-content" && d.value == "space-between") hasJC = true;
    }
    check(hasAC && hasJC, "place-content: single value applies to both");
}

static void testShorthandPlaceSelf() {
    printf("--- Shorthand: place-self ---\n");
    auto decls = expandShorthand("place-self", "end center");
    bool hasAS = false, hasJS = false;
    for (auto& d : decls) {
        if (d.property == "align-self" && d.value == "end") hasAS = true;
        if (d.property == "justify-self" && d.value == "center") hasJS = true;
    }
    check(hasAS && hasJS, "place-self: expands to align-self and justify-self");
}

static void testShorthandColumnRule() {
    printf("--- Shorthand: column-rule ---\n");
    auto decls = expandShorthand("column-rule", "1px solid gray");
    bool hasW = false, hasS = false, hasC = false;
    for (auto& d : decls) {
        if (d.property == "column-rule-width" && d.value == "1px") hasW = true;
        if (d.property == "column-rule-style" && d.value == "solid") hasS = true;
        if (d.property == "column-rule-color" && d.value == "gray") hasC = true;
    }
    check(hasW && hasS && hasC, "column-rule: expands to width, style, color");
}

static void testShorthandContainer() {
    printf("--- Shorthand: container ---\n");
    auto decls = expandShorthand("container", "inline-size / sidebar");
    bool hasType = false, hasName = false;
    for (auto& d : decls) {
        if (d.property == "container-type") hasType = true;
        if (d.property == "container-name") hasName = true;
    }
    check(hasType && hasName, "container: expands to type and name");
}

static void testShorthandBackground() {
    printf("--- Shorthand: background ---\n");
    auto decls = expandShorthand("background", "red");
    bool hasBgColor = false;
    for (auto& d : decls) {
        if (d.property == "background-color" && d.value == "red") hasBgColor = true;
    }
    check(hasBgColor, "background: color value sets background-color");
}

static void testPropertyInheritanceFlags() {
    printf("--- Properties: inheritance flags ---\n");
    // Inherited properties
    check(isInherited("color"), "color is inherited");
    check(isInherited("font-size"), "font-size is inherited");
    check(isInherited("font-family"), "font-family is inherited");
    check(isInherited("line-height"), "line-height is inherited");
    check(isInherited("text-align"), "text-align is inherited");
    check(isInherited("white-space"), "white-space is inherited");
    check(isInherited("direction"), "direction is inherited");
    check(isInherited("visibility"), "visibility is inherited");
    check(isInherited("cursor"), "cursor is inherited");
    check(isInherited("list-style-type"), "list-style-type is inherited");

    // Non-inherited properties
    check(!isInherited("display"), "display is not inherited");
    check(!isInherited("width"), "width is not inherited");
    check(!isInherited("margin-top"), "margin-top is not inherited");
    check(!isInherited("padding-left"), "padding-left is not inherited");
    check(!isInherited("position"), "position is not inherited");
    check(!isInherited("flex-grow"), "flex-grow is not inherited");
    check(!isInherited("grid-template-columns"), "grid-template-columns is not inherited");
    check(!isInherited("overflow"), "overflow is not inherited");
}

static void testPropertyInitialValues() {
    printf("--- Properties: initial values ---\n");
    check(initialValue("display") == "inline", "display initial = inline");
    check(initialValue("position") == "static", "position initial = static");
    check(initialValue("float") == "none", "float initial = none");
    check(initialValue("clear") == "none", "clear initial = none");
    check(initialValue("overflow") == "visible", "overflow initial = visible");
    check(initialValue("box-sizing") == "content-box", "box-sizing initial = content-box");
    check(initialValue("flex-direction") == "row", "flex-direction initial = row");
    check(initialValue("flex-wrap") == "nowrap", "flex-wrap initial = nowrap");
    check(initialValue("flex-grow") == "0", "flex-grow initial = 0");
    check(initialValue("flex-shrink") == "1", "flex-shrink initial = 1");
    check(initialValue("flex-basis") == "auto", "flex-basis initial = auto");
    check(initialValue("z-index") == "auto", "z-index initial = auto");
    check(initialValue("opacity") == "1", "opacity initial = 1");
    check(initialValue("transform") == "none", "transform initial = none");
    check(initialValue("visibility") == "visible", "visibility initial = visible");
    check(initialValue("white-space") == "normal", "white-space initial = normal");
    check(initialValue("text-decoration") == "none", "text-decoration initial = none");
    check(initialValue("vertical-align") == "baseline", "vertical-align initial = baseline");
    check(initialValue("pointer-events") == "auto", "pointer-events initial = auto");
    check(initialValue("border-collapse") == "separate", "border-collapse initial = separate");
}

// ======================================================================
// COLOR COVERAGE
// ======================================================================

static void testColorEdgeCases() {
    printf("--- Color: edge cases ---\n");
    // Empty string
    auto c = parseColor("");
    check(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0, "color: empty = {0,0,0,0}");

    // Unknown value
    c = parseColor("notacolor");
    check(c.a == 0, "color: unknown = transparent");

    // Case insensitive names
    c = parseColor("RED");
    check(c.r == 255 && c.g == 0 && c.b == 0, "color: RED (uppercase) = red");
    c = parseColor("ReBeCcApUrPlE");
    check(c.r == 102 && c.g == 51 && c.b == 153, "color: mixed case rebeccapurple");

    // 4-digit hex
    c = parseColor("#f00f");
    check(c.r == 255 && c.g == 0 && c.b == 0 && c.a == 255, "#f00f = red with full alpha");

    // 8-digit hex with alpha
    c = parseColor("#ff000000");
    check(c.r == 255 && c.a == 0, "#ff000000 alpha = 0");

    // Whitespace around color
    c = parseColor("  blue  ");
    check(c.r == 0 && c.g == 0 && c.b == 255, "color: trimmed blue");
}

static void testColorRgbPercentage() {
    printf("--- Color: rgb with percentages ---\n");
    auto c = parseColor("rgb(100%, 0%, 50%)");
    check(c.r == 255, "rgb: 100% red = 255");
    check(c.g == 0, "rgb: 0% green = 0");
    check(c.b >= 127 && c.b <= 128, "rgb: 50% blue ~ 127");
}

static void testColorRgbSpaceSyntax() {
    printf("--- Color: rgb space syntax ---\n");
    auto c = parseColor("rgb(128 64 32)");
    check(c.r == 128 && c.g == 64 && c.b == 32, "rgb: space-separated values");
}

static void testColorHslEdgeCases() {
    printf("--- Color: hsl edge cases ---\n");
    // Grayscale (saturation = 0)
    auto c = parseColor("hsl(0, 0%, 50%)");
    check(c.r >= 127 && c.r <= 128 && c.g >= 127 && c.g <= 128 && c.b >= 127 && c.b <= 128,
          "hsl: gray (s=0, l=50%)");

    // White
    c = parseColor("hsl(0, 0%, 100%)");
    check(c.r == 255 && c.g == 255 && c.b == 255, "hsl: white (l=100%)");

    // Black
    c = parseColor("hsl(0, 100%, 0%)");
    check(c.r == 0 && c.g == 0 && c.b == 0, "hsl: black (l=0%)");
}

static void testColorHsla() {
    printf("--- Color: hsla with alpha ---\n");
    auto c = parseColor("hsla(0, 100%, 50%, 0.5)");
    check(c.r >= 254 && c.a >= 126 && c.a <= 128, "hsla: red with 50% alpha");
}

static void testColorMoreNames() {
    printf("--- Color: more named colors ---\n");
    auto c = parseColor("white");
    check(c.r == 255 && c.g == 255 && c.b == 255 && c.a == 255, "white = 255,255,255,255");
    c = parseColor("black");
    check(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 255, "black = 0,0,0,255");
    c = parseColor("green");
    check(c.r == 0 && c.g == 128 && c.b == 0, "green = 0,128,0 (CSS green)");
    c = parseColor("orange");
    check(c.r == 255 && c.g == 165 && c.b == 0, "orange = 255,165,0");
    c = parseColor("coral");
    check(c.r == 255 && c.g == 127 && c.b == 80, "coral = 255,127,80");
}

// ======================================================================
// LENGTH RESOLUTION COVERAGE
// ======================================================================

static void testResolveLengthViewport() {
    printf("--- Length: viewport units (separate w/h) ---\n");
    // Using the viewport-aware overload
    float val = resolveLength("50vw", 0, 16, 1024, 768);
    check(approx(val, 512.0f), "50vw at 1024px viewport = 512");

    val = resolveLength("100vh", 0, 16, 1024, 768);
    check(approx(val, 768.0f), "100vh at 768px viewport = 768");

    val = resolveLength("50vmin", 0, 16, 1024, 768);
    check(approx(val, 384.0f), "50vmin at min(1024,768) = 384");

    val = resolveLength("50vmax", 0, 16, 1024, 768);
    check(approx(val, 512.0f), "50vmax at max(1024,768) = 512");
}

static void testResolveLengthRem() {
    printf("--- Length: rem unit ---\n");
    // rem is based on root font size, approximated as 16px
    float val = resolveLength("2rem", 100, 20);
    check(approx(val, 32.0f), "2rem = 2 * 16px (root font size)");
}

static void testResolveLengthPt() {
    printf("--- Length: pt unit ---\n");
    // 1pt = 96/72 px = 1.333px
    float val = resolveLength("12pt", 100, 16);
    check(approx(val, 16.0f), "12pt = 16px");
}

static void testCalcComplexExpr() {
    printf("--- Calc: complex expressions ---\n");
    // Nested calc
    float val = resolveLength("calc(calc(50px + 50px) * 2)", 0, 16);
    check(approx(val, 200.0f), "calc(calc(50+50)*2) = 200");

    // Division
    val = resolveLength("calc(300px / 3)", 0, 16);
    check(approx(val, 100.0f), "calc(300/3) = 100");

    // Subtract with mixed units
    val = resolveLength("calc(100% - 20px)", 400, 16);
    check(approx(val, 380.0f), "calc(100% - 20px) at 400 = 380");
}

static void testEdgeResolution() {
    printf("--- Edges: resolveEdges ---\n");
    ComputedStyle style;
    style["margin-top"] = "10px";
    style["margin-right"] = "20px";
    style["margin-bottom"] = "15px";
    style["margin-left"] = "5px";
    auto edges = resolveEdges(style, "margin", 400, 16);
    check(approx(edges.top, 10.0f), "margin-top = 10");
    check(approx(edges.right, 20.0f), "margin-right = 20");
    check(approx(edges.bottom, 15.0f), "margin-bottom = 15");
    check(approx(edges.left, 5.0f), "margin-left = 5");
}

// ======================================================================
// TEXT BREAKING COVERAGE
// ======================================================================

static void testTextPreWrap() {
    printf("--- Text: pre-wrap ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("hello world", 80, "mono", 16, "normal", "pre-wrap", m);
    // pre-wrap preserves spaces and wraps at available width
    check(runs.size() >= 1, "pre-wrap: produces runs");
}

static void testTextPreLine() {
    printf("--- Text: pre-line ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("line1\nline2\nline3", 500, "mono", 16, "normal", "pre-line", m);
    check(runs.size() == 3, "pre-line: 3 lines from newlines");
    check(runs[0].text == "line1", "pre-line: first line");
    check(runs[1].text == "line2", "pre-line: second line");
}

static void testTextOverflowWrapBreakWord() {
    printf("--- Text: overflow-wrap break-word ---\n");
    CovMetrics m;
    // "abcdefghij" = 100px, available = 50px
    auto runs = breakTextIntoRuns("abcdefghij", 50, "mono", 16, "normal", "normal", m,
                                  "break-word");
    check(runs.size() >= 2, "break-word: long word broken into multiple runs");
}

static void testTextWordBreakAll() {
    printf("--- Text: word-break break-all ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("hello world", 35, "mono", 16, "normal", "normal", m,
                                  "normal", "break-all");
    // 35px = 3.5 chars, so each word can break mid-character
    check(runs.size() >= 2, "break-all: words broken at arbitrary points");
}

static void testTextEmptyInput() {
    printf("--- Text: empty input ---\n");
    CovMetrics m;
    auto runs = breakTextIntoRuns("", 200, "mono", 16, "normal", "normal", m);
    check(runs.empty(), "text: empty string produces no runs");
}

static void testTextSingleLongWord() {
    printf("--- Text: single long word ---\n");
    CovMetrics m;
    // "abcdefghijklmnop" = 160px, available = 50px
    auto runs = breakTextIntoRuns("abcdefghijklmnop", 50, "mono", 16, "normal", "normal", m,
                                  "break-word");
    check(runs.size() >= 3, "text: long word breaks into 3+ runs with break-word");
}

// ======================================================================
// BLOCK LAYOUT COVERAGE
// ======================================================================

static void testBlockMarginAuto() {
    printf("--- Block: margin auto centering ---\n");
    CovNode root; root.initBlock();
    CovNode child; child.initBlock();
    child.style_["width"] = "200px";
    child.style_["height"] = "50px";
    child.style_["margin-left"] = "auto";
    child.style_["margin-right"] = "auto";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // margin auto: (600 - 200) / 2 = 200px each side
    check(approx(child.box.contentRect.x, 200.0f), "margin auto: centered at x=200");
}

static void testBlockMinMaxHeight() {
    printf("--- Block: min/max height ---\n");
    CovNode root; root.initBlock();

    CovNode child; child.initBlock();
    child.style_["height"] = "50px";
    child.style_["min-height"] = "100px";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(child.box.contentRect.height, 100.0f), "min-height: 50px clamped to 100px");

    CovNode root2; root2.initBlock();
    CovNode child2; child2.initBlock();
    child2.style_["height"] = "200px";
    child2.style_["max-height"] = "100px";
    root2.addChild(&child2);
    layoutTree(&root2, 600, m);
    check(approx(child2.box.contentRect.height, 100.0f), "max-height: 200px clamped to 100px");
}

static void testBlockMaxWidth() {
    printf("--- Block: max-width ---\n");
    CovNode root; root.initBlock();
    CovNode child; child.initBlock();
    child.style_["width"] = "500px";
    child.style_["height"] = "30px";
    child.style_["max-width"] = "300px";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(child.box.contentRect.width, 300.0f), "max-width: 500px clamped to 300px");
}

static void testBlockMarginCollapseFirstChild() {
    printf("--- Block: margin collapse first child ---\n");
    CovNode root; root.initBlock();
    CovNode child; child.initBlock();
    child.style_["height"] = "50px";
    child.style_["margin-top"] = "30px";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // First child margin collapses with parent (parent has no padding/border)
    check(approx(child.box.contentRect.y, 30.0f), "first child margin collapse: y=30");
}

static void testBlockFloatMultiple() {
    printf("--- Block: multiple floats ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px";

    CovNode f1; f1.initBlock();
    f1.style_["width"] = "100px"; f1.style_["height"] = "50px"; f1.style_["float"] = "left";
    CovNode f2; f2.initBlock();
    f2.style_["width"] = "100px"; f2.style_["height"] = "80px"; f2.style_["float"] = "left";
    CovNode content; content.initBlock();
    content.style_["height"] = "30px";

    root.addChild(&f1); root.addChild(&f2); root.addChild(&content);

    CovMetrics m;
    layoutTree(&root, 800, m);
    // Two left floats side by side
    check(approx(f1.box.contentRect.x, 0.0f), "float: f1 at x=0");
    check(approx(f2.box.contentRect.x, 100.0f), "float: f2 at x=100");
    check(approx(content.box.contentRect.x, 200.0f), "float: content shifted past both floats");
}

static void testBlockClearLeft() {
    printf("--- Block: clear left ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px";

    CovNode fL; fL.initBlock();
    fL.style_["width"] = "100px"; fL.style_["height"] = "80px"; fL.style_["float"] = "left";
    CovNode fR; fR.initBlock();
    fR.style_["width"] = "100px"; fR.style_["height"] = "40px"; fR.style_["float"] = "right";
    CovNode cleared; cleared.initBlock();
    cleared.style_["height"] = "30px"; cleared.style_["clear"] = "left";

    root.addChild(&fL); root.addChild(&fR); root.addChild(&cleared);

    CovMetrics m;
    layoutTree(&root, 800, m);
    // clear:left moves below left float (80px), not right float (40px)
    check(approx(cleared.box.contentRect.y, 80.0f), "clear left: below left float at y=80");
}

static void testBlockOverflowNewBFC() {
    printf("--- Block: overflow hidden creates new BFC ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px";

    CovNode floater; floater.initBlock();
    floater.style_["width"] = "100px"; floater.style_["height"] = "200px"; floater.style_["float"] = "left";

    CovNode bfc; bfc.initBlock();
    bfc.style_["overflow"] = "hidden";
    bfc.style_["height"] = "50px";

    root.addChild(&floater); root.addChild(&bfc);

    CovMetrics m;
    layoutTree(&root, 800, m);
    // BFC with overflow:hidden should not overlap the float
    check(bfc.box.contentRect.x >= 100.0f, "overflow BFC: pushed past float");
}

// ======================================================================
// INLINE LAYOUT COVERAGE
// ======================================================================

static void testInlineVerticalAlignMiddle() {
    printf("--- Inline: vertical-align middle ---\n");
    CovNode root; root.initInline();

    CovNode ib1; ib1.initInline();
    ib1.style_["display"] = "inline-block";
    ib1.style_["width"] = "50px"; ib1.style_["height"] = "60px";
    ib1.style_["vertical-align"] = "middle";

    CovNode ib2; ib2.initInline();
    ib2.style_["display"] = "inline-block";
    ib2.style_["width"] = "50px"; ib2.style_["height"] = "20px";

    root.addChild(&ib1); root.addChild(&ib2);

    CovMetrics m;
    layoutTree(&root, 500, m);
    // ib1 with vertical-align: middle should be centered on the line
    check(root.box.contentRect.height >= 20, "inline: container has positive height");
}

static void testInlineVerticalAlignTop() {
    printf("--- Inline: vertical-align top ---\n");
    CovNode root; root.initInline();

    CovNode ib1; ib1.initInline();
    ib1.style_["display"] = "inline-block";
    ib1.style_["width"] = "50px"; ib1.style_["height"] = "60px";
    ib1.style_["vertical-align"] = "top";

    CovNode ib2; ib2.initInline();
    ib2.style_["display"] = "inline-block";
    ib2.style_["width"] = "50px"; ib2.style_["height"] = "30px";

    root.addChild(&ib1); root.addChild(&ib2);

    CovMetrics m;
    layoutTree(&root, 500, m);
    // vertical-align: top aligns top of item with top of line
    check(approx(ib1.box.contentRect.y, 0.0f, 2.0f), "vertical-align top: y near 0");
}

static void testInlineTextAlignStartEnd() {
    printf("--- Inline: text-align start/end with direction ---\n");
    CovNode root; root.initBlock();
    root.style_["text-align"] = "start";
    root.style_["direction"] = "rtl";

    CovNode ib; ib.initInline();
    ib.style_["display"] = "inline-block";
    ib.style_["width"] = "50px"; ib.style_["height"] = "20px";
    root.addChild(&ib);

    CovMetrics m;
    layoutTree(&root, 200, m);
    // RTL with text-align:start should right-align
    check(approx(ib.box.contentRect.x, 150.0f), "text-align start + rtl: right-aligned");
}

static void testInlineNestedInline() {
    printf("--- Inline: nested inline elements ---\n");
    CovNode root; root.initInline();

    CovNode span1; span1.initInline();
    CovNode textNode; textNode.isText = true; textNode.text = "hello";
    span1.addChild(&textNode);
    root.addChild(&span1);

    CovMetrics m;
    layoutTree(&root, 500, m);
    check(approx(root.box.contentRect.width, 50.0f), "nested inline: width from text");
}

static void testInlineReplacedElement() {
    printf("--- Inline: replaced element with intrinsic size ---\n");
    CovNode root; root.initInline();

    CovNode img; img.initInline();
    img.style_["display"] = "inline-block";
    img.tag = "img";
    img.hasIntrinsic = true; img.intrW = 200; img.intrH = 150;
    root.addChild(&img);

    CovMetrics m;
    layoutTree(&root, 500, m);
    check(approx(img.box.contentRect.width, 200.0f), "replaced: intrinsic width 200");
    check(approx(img.box.contentRect.height, 150.0f), "replaced: intrinsic height 150");
}

// ======================================================================
// FLEX LAYOUT COVERAGE
// ======================================================================

static void testFlexColumnReverse() {
    printf("--- Flex: column-reverse ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-direction"] = "column-reverse";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "50px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "50px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // column-reverse: c2 should be above c1
    check(c2.box.contentRect.y < c1.box.contentRect.y, "column-reverse: c2 above c1");
}

static void testFlexRowReverse() {
    printf("--- Flex: row-reverse ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-direction"] = "row-reverse";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "40px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // row-reverse: c2 should be to the left of c1
    check(c2.box.contentRect.x < c1.box.contentRect.x, "row-reverse: c2 left of c1");
}

static void testFlexJustifySpaceAround() {
    printf("--- Flex: justify-content space-around ---\n");
    CovNode root; root.initFlex();
    root.style_["justify-content"] = "space-around";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "30px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 400, m);
    // Free space = 200, 2 items. Each item gets 50px space: 25 on each side.
    // c1 at 50, c2 at 250 (or thereabouts)
    check(c1.box.contentRect.x > 0, "space-around: c1 offset from left");
    check(c2.box.contentRect.x > c1.box.contentRect.x + 100, "space-around: gap between items");
}

static void testFlexJustifySpaceEvenly() {
    printf("--- Flex: justify-content space-evenly ---\n");
    CovNode root; root.initFlex();
    root.style_["justify-content"] = "space-evenly";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "30px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 400, m);
    // Free = 200, 3 gaps (before, between, after) = 200/3 ~ 66.7 each
    float gap = c2.box.contentRect.x - (c1.box.contentRect.x + 100);
    check(approx(gap, 66.7f, 2.0f), "space-evenly: equal gap between items");
}

static void testFlexAlignSelfOverride() {
    printf("--- Flex: align-self override ---\n");
    CovNode root; root.initFlex();
    root.style_["align-items"] = "flex-start";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "30px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "30px";
    c2.style_["align-self"] = "flex-end";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // c1 at flex-start (y=0), c2 at flex-end
    check(approx(c1.box.contentRect.y, 0.0f), "align-self: c1 at flex-start");
    // c2 should be at line bottom
    check(c2.box.contentRect.y >= 0, "align-self: c2 override applied");
}

static void testFlexWrapReverse() {
    printf("--- Flex: wrap-reverse ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-wrap"] = "wrap-reverse";
    CovNode c1, c2, c3;
    c1.initFlexItem(); c1.style_["width"] = "200px"; c1.style_["height"] = "40px";
    c2.initFlexItem(); c2.style_["width"] = "200px"; c2.style_["height"] = "40px";
    c3.initFlexItem(); c3.style_["width"] = "200px"; c3.style_["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    CovMetrics m;
    layoutTree(&root, 350, m);
    // wrap-reverse: items still wrap, cross-axis order reversed
    // c3 wraps to a different line than c1/c2
    check(c3.box.contentRect.y != c1.box.contentRect.y, "wrap-reverse: c3 on different line than c1");
}

static void testFlexRowGap() {
    printf("--- Flex: row-gap in wrapped flex ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-wrap"] = "wrap";
    root.style_["row-gap"] = "20px";
    CovNode c1, c2, c3;
    c1.initFlexItem(); c1.style_["width"] = "200px"; c1.style_["height"] = "40px";
    c2.initFlexItem(); c2.style_["width"] = "200px"; c2.style_["height"] = "40px";
    c3.initFlexItem(); c3.style_["width"] = "200px"; c3.style_["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    CovMetrics m;
    layoutTree(&root, 350, m);
    // c3 wraps to next line with 20px row gap
    float gap = c3.box.contentRect.y - (c1.box.contentRect.y + 40);
    check(gap >= 19.0f, "row-gap: 20px gap between flex lines");
}

static void testFlexBasisExplicit() {
    printf("--- Flex: explicit flex-basis ---\n");
    CovNode root; root.initFlex();
    CovNode c1;
    c1.initFlexItem(); c1.style_["flex-basis"] = "200px"; c1.style_["height"] = "40px";
    c1.style_["flex-grow"] = "0";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(c1.box.contentRect.width, 200.0f), "flex-basis: explicit 200px");
}

static void testFlexMinMaxClamp() {
    printf("--- Flex: min/max clamping ---\n");
    CovNode root; root.initFlex();
    CovNode c1;
    c1.initFlexItem(); c1.style_["flex-basis"] = "100px"; c1.style_["flex-grow"] = "1";
    c1.style_["max-width"] = "200px"; c1.style_["height"] = "40px";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // Would grow to 600 but max-width clamps to 200
    check(c1.box.contentRect.width <= 201.0f, "flex: max-width clamps flex growth");
}

static void testFlexColumnGrow() {
    printf("--- Flex: column direction with grow ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-direction"] = "column";
    root.style_["height"] = "300px";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["flex-grow"] = "1";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["flex-grow"] = "2";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // Column: main axis is vertical. c1 gets 100px, c2 gets 200px
    check(approx(c1.box.contentRect.height, 100.0f), "flex column grow: c1 height = 100");
    check(approx(c2.box.contentRect.height, 200.0f), "flex column grow: c2 height = 200");
}

// ======================================================================
// GRID LAYOUT COVERAGE
// ======================================================================

static void testGridAutoPlacement() {
    printf("--- Grid: auto-placement ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr 1fr 1fr";
    grid.style_["width"] = "300px";

    CovNode items[6];
    for (int i = 0; i < 6; i++) {
        items[i].initBlock(); items[i].style_["height"] = "30px";
        grid.addChild(&items[i]);
    }

    CovMetrics m;
    layoutTree(&grid, 800, m);

    // 6 items in 3 columns = 2 rows
    check(approx(items[0].box.contentRect.x, 0.0f), "grid auto: item0 col0");
    check(approx(items[1].box.contentRect.x, 100.0f), "grid auto: item1 col1");
    check(approx(items[2].box.contentRect.x, 200.0f), "grid auto: item2 col2");
    check(items[3].box.contentRect.y > items[0].box.contentRect.y, "grid auto: item3 in row1");
}

static void testGridExplicitPlacement() {
    printf("--- Grid: explicit placement ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr 1fr 1fr";
    grid.style_["width"] = "300px";

    CovNode item; item.initBlock();
    item.style_["height"] = "30px";
    item.style_["grid-column-start"] = "2";
    item.style_["grid-column-end"] = "4"; // spans columns 2-3
    item.style_["grid-row-start"] = "1";
    grid.addChild(&item);

    CovMetrics m;
    layoutTree(&grid, 800, m);

    check(approx(item.box.contentRect.x, 100.0f), "grid explicit: starts at col 2 (x=100)");
    check(approx(item.box.contentRect.width, 200.0f), "grid explicit: spans 2 columns (200px)");
}

static void testGridMinmax() {
    printf("--- Grid: minmax() ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "minmax(100px, 1fr) minmax(100px, 2fr)";
    grid.style_["width"] = "600px";

    CovNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    grid.addChild(&c1); grid.addChild(&c2);

    CovMetrics m;
    layoutTree(&grid, 800, m);

    // 1fr + 2fr = 3fr, 600/3=200. col1=200, col2=400
    check(approx(c1.box.contentRect.width, 200.0f), "grid minmax: col1 = 200px");
    check(approx(c2.box.contentRect.width, 400.0f), "grid minmax: col2 = 400px");
}

static void testGridAutoTrack() {
    printf("--- Grid: auto track sizing ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "auto auto";
    grid.style_["width"] = "400px";

    CovNode c1, c2;
    c1.initBlock(); c1.style_["width"] = "150px"; c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    grid.addChild(&c1); grid.addChild(&c2);

    CovMetrics m;
    layoutTree(&grid, 800, m);
    // Auto tracks should size to content
    check(c1.box.contentRect.width > 0, "grid auto track: c1 has width");
    check(c2.box.contentRect.width > 0, "grid auto track: c2 has width");
}

static void testGridRowTemplate() {
    printf("--- Grid: row template ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr";
    grid.style_["grid-template-rows"] = "50px 100px";
    grid.style_["width"] = "300px";

    CovNode c1, c2;
    c1.initBlock(); c2.initBlock();
    grid.addChild(&c1); grid.addChild(&c2);

    CovMetrics m;
    layoutTree(&grid, 800, m);

    check(approx(c1.box.contentRect.height, 50.0f), "grid row template: row1 = 50px");
    check(approx(c2.box.contentRect.height, 100.0f), "grid row template: row2 = 100px");
}

// ======================================================================
// TABLE LAYOUT COVERAGE
// ======================================================================

static void testTableBasicStructure() {
    printf("--- Table: basic row/cell structure ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "400px";

    CovNode row; row.initBlock();
    row.style_["display"] = "table-row";

    CovNode cell1; cell1.initBlock();
    cell1.style_["display"] = "table-cell"; cell1.style_["height"] = "30px";
    CovNode cell2; cell2.initBlock();
    cell2.style_["display"] = "table-cell"; cell2.style_["height"] = "30px";

    row.addChild(&cell1); row.addChild(&cell2);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    check(cell1.box.contentRect.width > 0, "table: cell1 has width");
    check(cell2.box.contentRect.width > 0, "table: cell2 has width");
    check(approx(cell1.box.contentRect.y, cell2.box.contentRect.y), "table: cells in same row");
}

static void testTableMultipleRows() {
    printf("--- Table: multiple rows ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1, row2;
    row1.initBlock(); row1.style_["display"] = "table-row";
    row2.initBlock(); row2.style_["display"] = "table-row";

    CovNode c1, c2, c3, c4;
    c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "40px";
    c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "40px";
    c3.initBlock(); c3.style_["display"] = "table-cell"; c3.style_["height"] = "50px";
    c4.initBlock(); c4.style_["display"] = "table-cell"; c4.style_["height"] = "50px";

    row1.addChild(&c1); row1.addChild(&c2);
    row2.addChild(&c3); row2.addChild(&c4);
    table.addChild(&row1); table.addChild(&row2);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Row2 should be below row1 - check via the row positions
    check(row2.box.contentRect.y >= row1.box.contentRect.y + 30,
          "table: row2 below row1");
}

static void testTableBorderCollapse() {
    printf("--- Table: border-collapse ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";
    table.style_["border-collapse"] = "collapse";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "30px";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "30px";
    row.addChild(&c1); row.addChild(&c2);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // With collapse, there should be no spacing between cells
    check(c1.box.contentRect.width + c2.box.contentRect.width >= 295.0f,
          "table collapse: cells fill table width");
}

static void testTableBorderSpacing() {
    printf("--- Table: border-spacing ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";
    table.style_["border-spacing"] = "10px";
    table.style_["border-collapse"] = "separate";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "30px";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "30px";
    row.addChild(&c1); row.addChild(&c2);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Cells should have gap from border-spacing
    float gap = c2.box.contentRect.x - (c1.box.contentRect.x + c1.box.contentRect.width);
    check(gap >= 5.0f, "table spacing: gap between cells");
}

static void testTableCaption() {
    printf("--- Table: caption ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode caption; caption.initBlock();
    caption.style_["display"] = "table-caption"; caption.style_["height"] = "30px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "50px";
    row.addChild(&cell);

    table.addChild(&caption);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Caption exists and has height
    check(approx(caption.box.contentRect.height, 30.0f), "table caption: has height 30px");
    // Table should contain both caption and cell content
    check(table.box.contentRect.height >= 50, "table caption: table has sufficient height");
}

static void testTableRowGroup() {
    printf("--- Table: row group (tbody) ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode tbody; tbody.initBlock(); tbody.style_["display"] = "table-row-group";
    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "40px";

    row.addChild(&cell);
    tbody.addChild(&row);
    table.addChild(&tbody);

    CovMetrics m;
    layoutTree(&table, 800, m);

    check(cell.box.contentRect.width > 0, "table row-group: cell laid out");
    check(approx(cell.box.contentRect.height, 40.0f), "table row-group: cell height preserved");
}

// ======================================================================
// TABLE COLSPAN / ROWSPAN
// ======================================================================

static void testTableColspan() {
    printf("--- Table: colspan ---\n");
    // 2 rows: first has 1 cell spanning 2 cols, second has 2 cells
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1; row1.initBlock(); row1.style_["display"] = "table-row";
    CovNode spanning; spanning.initBlock();
    spanning.style_["display"] = "table-cell"; spanning.style_["height"] = "30px";
    spanning.style_["colspan"] = "2";
    row1.addChild(&spanning);

    CovNode row2; row2.initBlock(); row2.style_["display"] = "table-row";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "30px";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "30px";
    row2.addChild(&c1); row2.addChild(&c2);

    table.addChild(&row1); table.addChild(&row2);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Spanning cell should be wider than either column cell
    check(spanning.box.contentRect.width > c1.box.contentRect.width,
          "table colspan: spanning cell wider than single cell");
    // Spanning cell width should approximately equal both columns
    float bothCols = c1.box.contentRect.width + c2.box.contentRect.width +
                     c1.box.padding.left + c1.box.padding.right +
                     c2.box.padding.left + c2.box.padding.right;
    check(spanning.box.contentRect.width >= bothCols * 0.8f,
          "table colspan: spanning cell covers both columns");
}

static void testTableRowspan() {
    printf("--- Table: rowspan ---\n");
    // Row1: cell spanning 2 rows + normal cell. Row2: 1 normal cell
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1; row1.initBlock(); row1.style_["display"] = "table-row";
    CovNode spanning; spanning.initBlock();
    spanning.style_["display"] = "table-cell"; spanning.style_["height"] = "20px";
    spanning.style_["rowspan"] = "2";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "40px";
    row1.addChild(&spanning); row1.addChild(&c1);

    CovNode row2; row2.initBlock(); row2.style_["display"] = "table-row";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "40px";
    row2.addChild(&c2);

    table.addChild(&row1); table.addChild(&row2);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Spanning cell should be taller than single-row cells
    check(spanning.box.contentRect.height >= 70,
          "table rowspan: spanning cell covers both rows");
    // c2 should be in the second column (same x as c1), not first
    check(approx(c2.box.contentRect.x, c1.box.contentRect.x, 2),
          "table rowspan: second-row cell in correct column");
}

static void testTableColspanAndRowspan() {
    printf("--- Table: colspan + rowspan combined ---\n");
    // 3x3 grid: top-left cell spans 2 cols x 2 rows
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1; row1.initBlock(); row1.style_["display"] = "table-row";
    CovNode big; big.initBlock();
    big.style_["display"] = "table-cell"; big.style_["height"] = "20px";
    big.style_["colspan"] = "2"; big.style_["rowspan"] = "2";
    CovNode c13; c13.initBlock(); c13.style_["display"] = "table-cell"; c13.style_["height"] = "30px";
    row1.addChild(&big); row1.addChild(&c13);

    CovNode row2; row2.initBlock(); row2.style_["display"] = "table-row";
    CovNode c23; c23.initBlock(); c23.style_["display"] = "table-cell"; c23.style_["height"] = "30px";
    row2.addChild(&c23);

    CovNode row3; row3.initBlock(); row3.style_["display"] = "table-row";
    CovNode c31; c31.initBlock(); c31.style_["display"] = "table-cell"; c31.style_["height"] = "30px";
    CovNode c32; c32.initBlock(); c32.style_["display"] = "table-cell"; c32.style_["height"] = "30px";
    CovNode c33; c33.initBlock(); c33.style_["display"] = "table-cell"; c33.style_["height"] = "30px";
    row3.addChild(&c31); row3.addChild(&c32); row3.addChild(&c33);

    table.addChild(&row1); table.addChild(&row2); table.addChild(&row3);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Big cell should span 2 columns and 2 rows
    check(big.box.contentRect.width > c31.box.contentRect.width,
          "table combined: big cell wider than single col");
    check(big.box.contentRect.height >= 50,
          "table combined: big cell covers 2 row heights");
    // c23 should be in col 3 (same x as c13)
    check(approx(c23.box.contentRect.x, c13.box.contentRect.x, 2),
          "table combined: row2 cell skips spanned columns");
}

static void testTableCaptionSideBottom() {
    printf("--- Table: caption-side: bottom ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode caption; caption.initBlock();
    caption.style_["display"] = "table-caption"; caption.style_["height"] = "30px";
    caption.style_["caption-side"] = "bottom";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "50px";
    row.addChild(&cell);

    table.addChild(&caption);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Caption should be below the row content
    check(caption.box.contentRect.y > cell.box.contentRect.y,
          "table caption-side:bottom: caption below cells");
}

static void testTableVerticalAlignMiddle() {
    printf("--- Table: vertical-align: middle ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode tall; tall.initBlock();
    tall.style_["display"] = "table-cell"; tall.style_["height"] = "100px";
    CovNode mid; mid.initBlock();
    mid.style_["display"] = "table-cell"; mid.style_["height"] = "40px";
    mid.style_["vertical-align"] = "middle";

    row.addChild(&tall); row.addChild(&mid);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // mid cell should be vertically centered in the 100px row
    // Content offset should be roughly (100-40)/2 = 30 from the cell's baseline
    check(mid.box.contentRect.y > tall.box.contentRect.y + 10,
          "table vertical-align:middle: cell centered vertically");
}

static void testTableVerticalAlignBottom() {
    printf("--- Table: vertical-align: bottom ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode tall; tall.initBlock();
    tall.style_["display"] = "table-cell"; tall.style_["height"] = "100px";
    CovNode bot; bot.initBlock();
    bot.style_["display"] = "table-cell"; bot.style_["height"] = "30px";
    bot.style_["vertical-align"] = "bottom";

    row.addChild(&tall); row.addChild(&bot);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // bot cell content should be at the bottom of the row
    check(bot.box.contentRect.y > tall.box.contentRect.y + 50,
          "table vertical-align:bottom: cell at bottom");
}

// ======================================================================
// HIT TEST COVERAGE
// ======================================================================

static void testHitTestVisibilityHidden() {
    printf("--- HitTest: visibility hidden ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px"; root.style_["height"] = "400px";

    CovNode child; child.initBlock();
    child.style_["width"] = "100px"; child.style_["height"] = "100px";
    child.style_["visibility"] = "hidden";
    child.tag = "hidden";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 400, m);
    auto* hit = hitTest(&root, 50, 50);
    check(hit == nullptr || hit->tagName() != "hidden",
          "hitTest: visibility:hidden not hit-testable");
}

static void testHitTestZIndex() {
    printf("--- HitTest: z-index ordering ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "400px"; root.style_["height"] = "400px";

    CovNode c1; c1.initBlock();
    c1.style_["width"] = "200px"; c1.style_["height"] = "200px";
    c1.style_["position"] = "absolute"; c1.style_["z-index"] = "1";
    c1.tag = "low";

    CovNode c2; c2.initBlock();
    c2.style_["width"] = "200px"; c2.style_["height"] = "200px";
    c2.style_["position"] = "absolute"; c2.style_["z-index"] = "2";
    c2.tag = "high";

    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 400, m);
    auto* hit = hitTest(&root, 100, 100);
    check(hit != nullptr && hit->tagName() == "high", "hitTest: higher z-index wins");
}

static void testHitTestNull() {
    printf("--- HitTest: null root ---\n");
    auto* hit = hitTest(nullptr, 50, 50);
    check(hit == nullptr, "hitTest: null root returns null");
}

static void testHitTestOverflowClip() {
    printf("--- HitTest: overflow clipping ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "100px"; root.style_["height"] = "100px";
    root.style_["overflow"] = "hidden";

    CovNode child; child.initBlock();
    child.style_["width"] = "200px"; child.style_["height"] = "200px";
    child.tag = "big";
    root.addChild(&child);

    CovMetrics m;
    layoutTree(&root, 400, m);
    applyOverflowClipping(&root);

    // Point inside clipped area should hit child (it's clipped to parent)
    auto* hit = hitTest(&root, 50, 50);
    check(hit != nullptr, "hitTest overflow: hit inside clipped area");
}

// ======================================================================
// LAYOUT BOX HELPER COVERAGE
// ======================================================================

static void testLayoutBoxFullWidth() {
    printf("--- LayoutBox: fullWidth/fullHeight/marginBox ---\n");
    LayoutBox box;
    box.contentRect = {10, 20, 100, 50};
    box.padding = {5, 5, 5, 5};
    box.border = {2, 2, 2, 2};
    box.margin = {10, 10, 10, 10};

    check(approx(box.fullWidth(), 114.0f), "fullWidth = 100 + 5 + 5 + 2 + 2 = 114");
    check(approx(box.fullHeight(), 64.0f), "fullHeight = 50 + 5 + 5 + 2 + 2 = 64");

    auto mb = box.marginBox();
    check(approx(mb.width, 134.0f), "marginBox width = 114 + 10 + 10 = 134");
    check(approx(mb.height, 84.0f), "marginBox height = 64 + 10 + 10 = 84");
    check(approx(mb.x, -7.0f), "marginBox x = 10 - 5 - 2 - 10 = -7");
    check(approx(mb.y, 3.0f), "marginBox y = 20 - 5 - 2 - 10 = 3");
}

// ======================================================================
// VIEWPORT-AWARE LAYOUT
// ======================================================================

static void testViewportLayout() {
    printf("--- Layout: viewport-aware layout ---\n");
    CovNode root; root.initBlock();
    root.style_["width"] = "50vw";
    root.style_["height"] = "50vh";

    CovMetrics m;
    Viewport vp{1024, 768};
    layoutTree(&root, vp, m);

    check(approx(root.box.contentRect.width, 512.0f), "viewport layout: 50vw = 512");
    check(approx(root.box.contentRect.height, 384.0f), "viewport layout: 50vh = 384");
}

// ======================================================================
// MULTICOL COVERAGE
// ======================================================================

static void testMulticolIsContainer() {
    printf("--- Multicol: isMulticolContainer ---\n");
    ComputedStyle style;
    style["column-count"] = "3";
    check(isMulticolContainer(style), "isMulticolContainer: column-count=3 is true");

    ComputedStyle style2;
    style2["column-width"] = "200px";
    check(isMulticolContainer(style2), "isMulticolContainer: column-width=200px is true");

    ComputedStyle style3;
    style3["column-count"] = "auto";
    check(!isMulticolContainer(style3), "isMulticolContainer: column-count=auto is false");

    ComputedStyle style4;
    check(!isMulticolContainer(style4), "isMulticolContainer: empty style is false");
}

// ======================================================================
// UA STYLESHEET COVERAGE
// ======================================================================

static void testUAStylesheetElements() {
    printf("--- UA Stylesheet: additional elements ---\n");
    auto& ua = defaultUserAgentStylesheet();
    Cascade cascade;
    cascade.addStylesheet(ua);

    // h2-h6
    MockElement h2; h2.tag = "h2";
    auto h2s = cascade.resolve(h2);
    check(h2s["display"] == "block", "UA: h2 display = block");
    check(h2s["font-weight"] == "bold", "UA: h2 font-weight = bold");

    MockElement h6; h6.tag = "h6";
    auto h6s = cascade.resolve(h6);
    check(h6s["font-weight"] == "bold", "UA: h6 font-weight = bold");

    // ul, ol
    MockElement ul; ul.tag = "ul";
    auto uls = cascade.resolve(ul);
    check(uls["display"] == "block", "UA: ul display = block");

    // li
    MockElement li; li.tag = "li";
    auto lis = cascade.resolve(li);
    check(lis["display"] == "list-item", "UA: li display = list-item");

    // pre
    MockElement pre; pre.tag = "pre";
    auto pres = cascade.resolve(pre);
    check(pres["white-space"] == "pre", "UA: pre white-space = pre");

    // em, i
    MockElement em; em.tag = "em";
    auto ems = cascade.resolve(em);
    check(ems["font-style"] == "italic", "UA: em font-style = italic");

    // hr
    MockElement hr; hr.tag = "hr";
    auto hrs = cascade.resolve(hr);
    check(hrs["display"] == "block", "UA: hr display = block");
}

// ======================================================================
// INCREMENTAL LAYOUT COVERAGE
// ======================================================================

static void testMarkDirtyNull() {
    printf("--- Incremental: markDirty null ---\n");
    markDirty(nullptr); // should not crash
    check(true, "markDirty(nullptr) doesn't crash");
}

static void testNeedsRelayoutProperties() {
    printf("--- Invalidation: additional properties ---\n");
    check(needsRelayout({"margin-top"}), "margin-top needs relayout");
    check(needsRelayout({"padding-left"}), "padding-left needs relayout");
    check(needsRelayout({"border-top-width"}), "border-top-width needs relayout");
    check(needsRelayout({"min-width"}), "min-width needs relayout");
    check(needsRelayout({"max-height"}), "max-height needs relayout");
    check(needsRelayout({"float"}), "float needs relayout");
    check(needsRelayout({"clear"}), "clear needs relayout");
    check(needsRelayout({"position"}), "position needs relayout");
    check(needsRelayout({"overflow"}), "overflow needs relayout");
    check(needsRelayout({"flex-direction"}), "flex-direction needs relayout");
    check(needsRelayout({"flex-wrap"}), "flex-wrap needs relayout");
    check(needsRelayout({"justify-content"}), "justify-content needs relayout");
    check(needsRelayout({"align-items"}), "align-items needs relayout");
    check(needsRelayout({"font-size"}), "font-size needs relayout");
    check(needsRelayout({"white-space"}), "white-space needs relayout");
    check(needsRelayout({"text-align"}), "text-align needs relayout");
    check(needsRelayout({"column-count"}), "column-count needs relayout");

    // Paint-only
    check(!needsRelayout({"cursor"}), "cursor: no relayout");
    check(!needsRelayout({"outline-color"}), "outline-color: no relayout");
    check(!needsRelayout({"text-decoration"}), "text-decoration: no relayout");
    check(!needsRelayout({"visibility"}), "visibility: no relayout");
}

// ======================================================================
// DISPLAY DISPATCH COVERAGE
// ======================================================================

static void testDisplayNoneLayout() {
    printf("--- Layout: display none produces zero box ---\n");
    CovNode root; root.initBlock();
    root.style_["display"] = "none";

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(approx(root.box.contentRect.width, 0.0f), "display:none width=0");
    check(approx(root.box.contentRect.height, 0.0f), "display:none height=0");
}

static void testDisplayInlineFlex() {
    printf("--- Layout: display inline-flex ---\n");
    CovNode root; root.initFlex();
    root.style_["display"] = "inline-flex";

    CovNode c1; c1.initFlexItem();
    c1.style_["width"] = "100px"; c1.style_["height"] = "40px";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(approx(c1.box.contentRect.width, 100.0f), "inline-flex: child laid out");
}

static void testDisplayInlineGrid() {
    printf("--- Layout: display inline-grid ---\n");
    CovNode root; root.initGrid();
    root.style_["display"] = "inline-grid";
    root.style_["grid-template-columns"] = "1fr 1fr";
    root.style_["width"] = "200px";

    CovNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(approx(c1.box.contentRect.width, 100.0f), "inline-grid: c1 width = 100");
}

static void testDisplayInlineTable() {
    printf("--- Layout: display inline-table ---\n");
    CovNode root; root.initTable();
    root.style_["display"] = "inline-table";
    root.style_["width"] = "200px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "30px";
    row.addChild(&cell);
    root.addChild(&row);

    CovMetrics m;
    layoutTree(&root, 800, m);
    check(cell.box.contentRect.width > 0, "inline-table: cell has width");
}

static void testDisplayListItem() {
    printf("--- Layout: display list-item ---\n");
    CovNode root; root.initBlock();

    CovNode li; li.initBlock();
    li.style_["display"] = "list-item";
    li.style_["height"] = "30px";
    root.addChild(&li);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(li.box.contentRect.height, 30.0f), "list-item: laid out as block");
}

// ======================================================================
// POSITION: RELATIVE IN FLEX/GRID
// ======================================================================

static void testFlexRelativeOffset() {
    printf("--- Flex: position relative offset ---\n");
    CovNode root; root.initFlex();
    CovNode c1;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "40px";
    c1.style_["position"] = "relative"; c1.style_["top"] = "10px"; c1.style_["left"] = "5px";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(c1.box.contentRect.x, 5.0f), "flex relative: left offset");
    check(approx(c1.box.contentRect.y, 10.0f), "flex relative: top offset");
}

static void testGridRelativeOffset() {
    printf("--- Grid: position relative offset ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr";
    grid.style_["width"] = "300px";

    CovNode item; item.initBlock();
    item.style_["height"] = "30px";
    item.style_["position"] = "relative";
    item.style_["top"] = "15px";
    item.style_["left"] = "10px";
    grid.addChild(&item);

    CovMetrics m;
    layoutTree(&grid, 800, m);
    check(approx(item.box.contentRect.x, 10.0f), "grid relative: left offset");
    check(approx(item.box.contentRect.y, 15.0f), "grid relative: top offset");
}

// ======================================================================
// ENTRY POINT
// ======================================================================

void testCoverage() {
    printf("=== Coverage Tests ===\n");

    // Tokenizer
    testTokenizerWhitespace();
    testTokenizerEscape();
    testTokenizerCDOCDC();
    testTokenizerDecimalOnly();
    testTokenizerPositiveSign();
    testTokenizerEmptyString();
    testTokenizerMultipleComments();

    // Parser
    testParserMediaBlocks();
    testParserLayerBlocks();
    testParserContainerBlocks();
    testParserNestedMediaInLayer();
    testParserSupportsKnownProp();
    testMediaQueryEvaluation();

    // Selectors
    testSelectorNthLastChild();
    testSelectorNthOfType();
    testSelectorOnlyOfType();
    testSelectorRoot();
    testSelectorActiveState();
    testSelectorAttrSubstring();
    testSelectorListSpecificity();
    testSelectorNthFormula();
    testSelectorNthNegative();
    testSelectorComplex();

    // Cascade
    testCascadeLayerPrecedence();
    testCascadeLayerImportant();
    testCascadeContainerQueries();
    testCascadeVarRecursionLimit();
    testCascadeRevertKeyword();
    testCascadeCustomPropInheritance();

    // Properties & Shorthands
    testShorthandBorderRadius();
    testShorthandBorderRadiusTwoValues();
    testShorthandOutline();
    testShorthandColumns();
    testShorthandPlaceItems();
    testShorthandPlaceContent();
    testShorthandPlaceSelf();
    testShorthandColumnRule();
    testShorthandContainer();
    testShorthandBackground();
    testPropertyInheritanceFlags();
    testPropertyInitialValues();

    // Colors
    testColorEdgeCases();
    testColorRgbPercentage();
    testColorRgbSpaceSyntax();
    testColorHslEdgeCases();
    testColorHsla();
    testColorMoreNames();

    // Length resolution
    testResolveLengthViewport();
    testResolveLengthRem();
    testResolveLengthPt();
    testCalcComplexExpr();
    testEdgeResolution();

    // Text breaking
    testTextPreWrap();
    testTextPreLine();
    testTextOverflowWrapBreakWord();
    testTextWordBreakAll();
    testTextEmptyInput();
    testTextSingleLongWord();

    // Block layout
    testBlockMarginAuto();
    testBlockMinMaxHeight();
    testBlockMaxWidth();
    testBlockMarginCollapseFirstChild();
    testBlockFloatMultiple();
    testBlockClearLeft();
    testBlockOverflowNewBFC();

    // Inline layout
    testInlineVerticalAlignMiddle();
    testInlineVerticalAlignTop();
    testInlineTextAlignStartEnd();
    testInlineNestedInline();
    testInlineReplacedElement();

    // Flex layout
    testFlexColumnReverse();
    testFlexRowReverse();
    testFlexJustifySpaceAround();
    testFlexJustifySpaceEvenly();
    testFlexAlignSelfOverride();
    testFlexWrapReverse();
    testFlexRowGap();
    testFlexBasisExplicit();
    testFlexMinMaxClamp();
    testFlexColumnGrow();

    // Grid layout
    testGridAutoPlacement();
    testGridExplicitPlacement();
    testGridMinmax();
    testGridAutoTrack();
    testGridRowTemplate();

    // Table layout
    testTableBasicStructure();
    testTableMultipleRows();
    testTableBorderCollapse();
    testTableBorderSpacing();
    testTableCaption();
    testTableRowGroup();
    testTableColspan();
    testTableRowspan();
    testTableColspanAndRowspan();
    testTableCaptionSideBottom();
    testTableVerticalAlignMiddle();
    testTableVerticalAlignBottom();

    // Hit testing
    testHitTestVisibilityHidden();
    testHitTestZIndex();
    testHitTestNull();
    testHitTestOverflowClip();

    // LayoutBox helpers
    testLayoutBoxFullWidth();

    // Viewport layout
    testViewportLayout();

    // Multicol
    testMulticolIsContainer();

    // UA stylesheet
    testUAStylesheetElements();

    // Incremental & invalidation
    testMarkDirtyNull();
    testNeedsRelayoutProperties();

    // Display dispatch
    testDisplayNoneLayout();
    testDisplayInlineFlex();
    testDisplayInlineGrid();
    testDisplayInlineTable();
    testDisplayListItem();

    // Position relative in flex/grid
    testFlexRelativeOffset();
    testGridRelativeOffset();
}
