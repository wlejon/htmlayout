#include "test_cascade.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"

using namespace htmlayout::css;

static void testBasicResolve() {
    printf("--- Cascade: basic resolve ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; font-size: 20px; }"));

    MockElement div; div.tag = "div";
    auto style = cascade.resolve(div);
    check(style["color"] == "red", "cascade: color resolved to red");
    check(style["font-size"] == "20px", "cascade: font-size resolved to 20px");
    check(style["display"] == "inline", "cascade: display gets initial value 'inline'");
}

static void testSpecificityOrder() {
    printf("--- Cascade: specificity ordering ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        "div { color: red; }\n"
        ".box { color: blue; }\n"
        "#main { color: green; }\n"
    ));
    MockElement e; e.tag = "div"; e.classes = "box"; e.elemId = "main";
    auto style = cascade.resolve(e);
    check(style["color"] == "green", "cascade: #id specificity wins over .class and tag");
}

static void testSourceOrder() {
    printf("--- Cascade: source order ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".a { color: red; }\n.a { color: blue; }\n"));
    MockElement e; e.tag = "div"; e.classes = "a";
    check(cascade.resolve(e)["color"] == "blue", "cascade: later source order wins at equal specificity");
}

static void testImportant() {
    printf("--- Cascade: !important ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        "#main { color: green; }\n"
        ".box { color: red !important; }\n"
    ));
    MockElement e; e.tag = "div"; e.classes = "box"; e.elemId = "main";
    check(cascade.resolve(e)["color"] == "red", "cascade: !important beats higher specificity");
}

static void testInlineStyle() {
    printf("--- Cascade: inline style ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("#main { color: green; font-size: 20px; }"));
    MockElement e; e.tag = "div"; e.elemId = "main";
    auto style = cascade.resolve(e, "color: blue; margin-top: 5px");
    check(style["color"] == "blue", "cascade: inline style overrides #id rule");
    check(style["font-size"] == "20px", "cascade: non-inline property still from stylesheet");
    check(style["margin-top"] == "5px", "cascade: inline-only property applied");
}

static void testInheritance() {
    printf("--- Cascade: inheritance ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".parent { color: red; font-size: 18px; margin-top: 10px; }"));

    MockElement parent; parent.tag = "div"; parent.classes = "parent";
    MockElement child; child.tag = "span";
    parent.addChild(&child);

    auto parentStyle = cascade.resolve(parent);
    auto childStyle = cascade.resolve(child, {}, &parentStyle);
    check(childStyle["color"] == "red", "cascade: child inherits color from parent");
    check(childStyle["font-size"] == "18px", "cascade: child inherits font-size from parent");
    check(childStyle["margin-top"] == "0", "cascade: child gets initial margin-top, not inherited");
}

static void testInitialValues() {
    printf("--- Cascade: initial values ---\n");
    Cascade cascade;
    MockElement e; e.tag = "div";
    auto style = cascade.resolve(e);
    check(style["display"] == "inline", "cascade: initial display = inline");
    check(style["color"] == "black", "cascade: initial color = black");
    check(style["position"] == "static", "cascade: initial position = static");
    check(style["opacity"] == "1", "cascade: initial opacity = 1");
    check(style["font-size"] == "16px", "cascade: initial font-size = 16px");
}

static void testMultipleStylesheets() {
    printf("--- Cascade: multiple stylesheets ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; font-size: 14px; }"));
    cascade.addStylesheet(parse("div { color: blue; }"));
    MockElement e; e.tag = "div";
    auto style = cascade.resolve(e);
    check(style["color"] == "blue", "cascade: later stylesheet wins for color");
    check(style["font-size"] == "14px", "cascade: earlier stylesheet property preserved");
}

static void testShadowDOMScoping() {
    printf("--- Cascade: shadow DOM scoping ---\n");
    int shadowRoot = 42;
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; }"));
    cascade.addStylesheet(parse("div { color: blue; }"), &shadowRoot);

    MockElement docElem; docElem.tag = "div";
    check(cascade.resolve(docElem)["color"] == "red", "cascade: doc-scoped element gets doc rules");

    MockElement shadowElem; shadowElem.tag = "div"; shadowElem.scopePtr = &shadowRoot;
    check(cascade.resolve(shadowElem)["color"] == "blue", "cascade: shadow-scoped element gets shadow rules");
}

static void testCommaSelectors() {
    printf("--- Cascade: comma-separated selectors ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("h1, h2, h3 { font-weight: bold; color: navy; }"));

    MockElement h1; h1.tag = "h1";
    MockElement h2; h2.tag = "h2";
    MockElement h3; h3.tag = "h3";
    MockElement p; p.tag = "p";
    check(cascade.resolve(h1)["font-weight"] == "bold", "cascade: h1 gets bold from h1,h2,h3 rule");
    check(cascade.resolve(h2)["color"] == "navy", "cascade: h2 gets navy from h1,h2,h3 rule");
    check(cascade.resolve(h3)["font-weight"] == "bold", "cascade: h3 gets bold from h1,h2,h3 rule");
    check(cascade.resolve(p)["font-weight"] == "normal", "cascade: p doesn't match h1,h2,h3 rule");
}

static void testNoMatch() {
    printf("--- Cascade: no matching rules ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".special { color: red; }"));
    MockElement e; e.tag = "div";
    check(cascade.resolve(e)["color"] == "black", "cascade: unmatched element gets initial color");
}

static void testInheritanceChain() {
    printf("--- Cascade: multi-level inheritance ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".root { color: red; font-family: monospace; }\n"
        ".middle { font-size: 20px; }\n"
    ));
    MockElement root; root.tag = "div"; root.classes = "root";
    MockElement middle; middle.tag = "div"; middle.classes = "middle";
    MockElement leaf; leaf.tag = "span";
    root.addChild(&middle);
    middle.addChild(&leaf);

    auto rootStyle = cascade.resolve(root);
    auto midStyle = cascade.resolve(middle, {}, &rootStyle);
    auto leafStyle = cascade.resolve(leaf, {}, &midStyle);
    check(leafStyle["color"] == "red", "cascade: color inherits through 3 levels");
    check(leafStyle["font-family"] == "monospace", "cascade: font-family inherits through 3 levels");
    check(leafStyle["font-size"] == "20px", "cascade: font-size inherits from middle to leaf");
}

static void testImportantVsInline() {
    printf("--- Cascade: !important vs inline ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".forced { color: red !important; }"));
    MockElement e; e.tag = "div"; e.classes = "forced";
    check(cascade.resolve(e, "color: blue")["color"] == "red",
          "cascade: !important author beats normal inline");
}

static void testInheritInitialUnset() {
    printf("--- Cascade: inherit/initial/unset keywords ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".parent { color: red; margin-top: 20px; }\n"
        ".child { color: inherit; margin-top: inherit; display: initial; }\n"
    ));

    MockElement parent; parent.tag = "div"; parent.classes = "parent";
    MockElement child; child.tag = "div"; child.classes = "child";
    parent.addChild(&child);

    auto parentStyle = cascade.resolve(parent);
    auto childStyle = cascade.resolve(child, {}, &parentStyle);

    // color: inherit -> gets parent's red (color is normally inherited anyway)
    check(childStyle["color"] == "red", "inherit keyword: color = red from parent");
    // margin-top: inherit -> gets parent's 20px (margin-top is NOT normally inherited)
    check(childStyle["margin-top"] == "20px", "inherit keyword: margin-top forced from parent");
    // display: initial -> resets to "inline"
    check(childStyle["display"] == "inline", "initial keyword: display reset to inline");

    // Test unset: inherited prop -> inherit, non-inherited -> initial
    Cascade cascade2;
    cascade2.addStylesheet(parse(
        ".parent { color: blue; margin-left: 30px; }\n"
        ".child { color: unset; margin-left: unset; }\n"
    ));
    MockElement p2; p2.tag = "div"; p2.classes = "parent";
    MockElement c2; c2.tag = "div"; c2.classes = "child";
    p2.addChild(&c2);

    auto ps2 = cascade2.resolve(p2);
    auto cs2 = cascade2.resolve(c2, {}, &ps2);
    // color is inherited -> unset means inherit -> blue
    check(cs2["color"] == "blue", "unset keyword: inherited color = blue from parent");
    // margin-left is not inherited -> unset means initial -> 0
    check(cs2["margin-left"] == "0", "unset keyword: non-inherited margin-left = 0 (initial)");
}

static void testClear() {
    printf("--- Cascade: clear ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; }"));
    MockElement e; e.tag = "div";
    check(cascade.resolve(e)["color"] == "red", "cascade: before clear, color is red");
    cascade.clear();
    check(cascade.resolve(e)["color"] == "black", "cascade: after clear, color is initial");
}

void testCascade() {
    testBasicResolve();
    testSpecificityOrder();
    testSourceOrder();
    testImportant();
    testInlineStyle();
    testInheritance();
    testInitialValues();
    testMultipleStylesheets();
    testShadowDOMScoping();
    testCommaSelectors();
    testNoMatch();
    testInheritanceChain();
    testImportantVsInline();
    testInheritInitialUnset();
    testClear();
}
