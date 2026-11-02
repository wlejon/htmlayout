#include "test_coverage.h"

using namespace htmlayout::css;
using namespace htmlayout::layout;

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
        ".item { color: red; }\n"
        "@container (min-width: 400px) { .item { color: blue; } }\n"
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

    // A matching container rule is ordered by its source position: a later
    // plain rule of equal specificity beats it.
    Cascade before;
    before.addStylesheet(parse(
        "@container (min-width: 400px) { .item { color: blue; } }\n"
        ".item { color: red; }\n"));
    check(before.resolve(item)["color"] == "red",
          "cascade: later plain rule beats an earlier matching @container rule");
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

    // The `position/size` syntax may be written without spaces around the
    // slash (`center/contain`). It must still expand to background-position
    // and background-size — otherwise size stays `auto` and a contained/cover
    // image renders at its natural (often oversized) dimensions.
    auto get = [](const std::vector<ExpandedDecl>& ds, const std::string& p) {
        for (auto& d : ds) if (d.property == p) return d.value;
        return std::string();
    };
    auto slash = expandShorthand("background", "#0d0f12 center/contain no-repeat");
    check(get(slash, "background-size") == "contain",
          "background: center/contain (no spaces) sets background-size: contain");
    check(get(slash, "background-position") == "center",
          "background: center/contain sets background-position: center");
    check(get(slash, "background-repeat") == "no-repeat",
          "background: ...no-repeat sets background-repeat");
    check(get(slash, "background-color") == "#0d0f12",
          "background: leading color sets background-color");

    auto cover = expandShorthand("background", "url(a.png) center/cover no-repeat");
    check(get(cover, "background-size") == "cover",
          "background: center/cover sets background-size: cover");
    check(get(cover, "background-image") == "url(a.png)",
          "background: url(...) sets background-image");

    // Spaces around the slash must keep working too.
    auto spaced = expandShorthand("background", "left top / 50% 60% no-repeat");
    check(get(spaced, "background-size") == "50% 60%",
          "background: left top / 50% 60% sets background-size");
    check(get(spaced, "background-position") == "left top",
          "background: left top / ... sets background-position");
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

    // pre — both display:block (from comma-list rule) and white-space:pre
    // (from dedicated rule) must apply together. Partial-rule application
    // bugs would let one through but not the other.
    MockElement pre; pre.tag = "pre";
    auto pres = cascade.resolve(pre);
    check(pres["display"] == "block", "UA: pre display = block");
    check(pres["white-space"] == "pre", "UA: pre white-space = pre");
    check(pres["font-family"] == "monospace", "UA: pre font-family = monospace");

    // Other tags listed alongside pre in the comma-list block rule — every
    // one of them must resolve to display:block.
    for (const char* tag : {"dl", "dt", "dd", "menu", "dir",
                            "figure", "blockquote", "address"}) {
        MockElement el; el.tag = tag;
        auto s = cascade.resolve(el);
        std::string label = std::string("UA: ") + tag + " display = block";
        check(s["display"] == "block", label.c_str());
    }

    // em, i
    MockElement em; em.tag = "em";
    auto ems = cascade.resolve(em);
    check(ems["font-style"] == "italic", "UA: em font-style = italic");

    // hr
    MockElement hr; hr.tag = "hr";
    auto hrs = cascade.resolve(hr);
    check(hrs["display"] == "block", "UA: hr display = block");
}

// Resolve a single rule with many declarations (display + white-space +
// padding shorthand + margin longhands + border shorthand) and assert
// every longhand survives. Catches regressions where some declarations
// of a matched rule silently drop out of the cascade.
static void testCascadeFullPreRule() {
    printf("--- Cascade: full pre rule ---\n");
    const char* css = R"CSS(
        pre {
            display: block;
            white-space: pre;
            background-color: #f5f5f5;
            padding: 8px;
            border: 1px solid #ddd;
            overflow: auto;
            margin-top: 1em;
            margin-bottom: 1em;
        }
    )CSS";
    Cascade cascade;
    cascade.addStylesheet(parse(css));

    MockElement pre; pre.tag = "pre";
    auto s = cascade.resolve(pre);

    check(s["display"] == "block", "full-rule: display = block");
    check(s["white-space"] == "pre", "full-rule: white-space = pre");
    check(s["background-color"] == "#f5f5f5", "full-rule: background-color");
    check(s["padding-top"] == "8px", "full-rule: padding-top from shorthand");
    check(s["padding-right"] == "8px", "full-rule: padding-right from shorthand");
    check(s["padding-bottom"] == "8px", "full-rule: padding-bottom from shorthand");
    check(s["padding-left"] == "8px", "full-rule: padding-left from shorthand");
    check(s["border-top-width"] == "1px", "full-rule: border-top-width from shorthand");
    check(s["border-right-width"] == "1px", "full-rule: border-right-width from shorthand");
    check(s["border-bottom-width"] == "1px", "full-rule: border-bottom-width from shorthand");
    check(s["border-left-width"] == "1px", "full-rule: border-left-width from shorthand");
    check(s["overflow-x"] == "auto", "full-rule: overflow-x from shorthand");
    check(s["overflow-y"] == "auto", "full-rule: overflow-y from shorthand");
    check(s["margin-top"] == "1em", "full-rule: margin-top");
    check(s["margin-bottom"] == "1em", "full-rule: margin-bottom");
}

// Long comma-separated tag list — the shape used by browser UA sheets to
// declare many tags as block-level in a single rule. Each listed tag must
// match. Regressions in selector-list parsing or per-rule classification
// would let some tags fall through.
static void testCascadeCommaListAllMatch() {
    printf("--- Cascade: comma-list selector ---\n");
    const char* css = R"CSS(
        body, div, p, pre, section, article, nav, aside, header, footer, main,
        figure, figcaption, blockquote, fieldset, form, details, summary,
        address, hgroup, search, dl, dt, dd, dialog, canvas {
            display: block;
        }
    )CSS";
    Cascade cascade;
    cascade.addStylesheet(parse(css));

    const char* tags[] = {
        "body", "div", "p", "pre", "section", "article", "nav", "aside",
        "header", "footer", "main", "figure", "figcaption", "blockquote",
        "fieldset", "form", "details", "summary", "address", "hgroup",
        "search", "dl", "dt", "dd", "dialog", "canvas",
    };
    for (const char* tag : tags) {
        MockElement el; el.tag = tag;
        auto s = cascade.resolve(el);
        std::string label = std::string("comma-list: ") + tag + " display = block";
        check(s["display"] == "block", label.c_str());
    }
}

// Lock down the shorthand-expansion contract for padding/margin so a
// regression there is caught at the source rather than via downstream
// cascade behavior.
static void testExpandShorthandContract() {
    printf("--- Properties: expandShorthand contract ---\n");

    auto find = [](const std::vector<ExpandedDecl>& v, const std::string& p) -> std::string {
        for (auto& d : v) if (d.property == p) return d.value;
        return "";
    };

    auto pad = expandShorthand("padding", "8px");
    check(find(pad, "padding-top") == "8px", "padding 8px -> padding-top");
    check(find(pad, "padding-right") == "8px", "padding 8px -> padding-right");
    check(find(pad, "padding-bottom") == "8px", "padding 8px -> padding-bottom");
    check(find(pad, "padding-left") == "8px", "padding 8px -> padding-left");

    auto pad2 = expandShorthand("padding", "1px 2px 3px 4px");
    check(find(pad2, "padding-top") == "1px", "padding 4-val -> padding-top");
    check(find(pad2, "padding-right") == "2px", "padding 4-val -> padding-right");
    check(find(pad2, "padding-bottom") == "3px", "padding 4-val -> padding-bottom");
    check(find(pad2, "padding-left") == "4px", "padding 4-val -> padding-left");

    auto mar = expandShorthand("margin", "1em");
    check(find(mar, "margin-top") == "1em", "margin 1em -> margin-top");
    check(find(mar, "margin-right") == "1em", "margin 1em -> margin-right");
    check(find(mar, "margin-bottom") == "1em", "margin 1em -> margin-bottom");
    check(find(mar, "margin-left") == "1em", "margin 1em -> margin-left");
}

void testCoverageCss() {
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

    // UA stylesheet
    testUAStylesheetElements();
    testCascadeFullPreRule();
    testCascadeCommaListAllMatch();
    testExpandShorthandContract();
}

void testCoverage() {
    printf("=== Coverage Tests ===\n");
    testCoverageCss();
    testCoverageLayout();
    testCoverageGridFlex();
}
