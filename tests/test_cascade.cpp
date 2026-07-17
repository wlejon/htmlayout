#include "test_cascade.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"
#include "css/properties.h"
#include "css/ua_stylesheet.h"

using namespace htmlayout::css;

// Helper: look up a style value with fallback to initial value (matches layout behavior).
static const std::string& sv(const ComputedStyle& style, const std::string& prop) {
    auto it = style.find(prop);
    if (it != style.end()) return it->second;
    return initialValueRef(prop);
}

static void testBasicResolve() {
    printf("--- Cascade: basic resolve ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; font-size: 20px; }"));

    MockElement div; div.tag = "div";
    auto style = cascade.resolve(div);
    check(style["color"] == "red", "cascade: color resolved to red");
    check(style["font-size"] == "20px", "cascade: font-size resolved to 20px");
    check(sv(style, "display") == "inline", "cascade: display gets initial value 'inline'");
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
    check(sv(childStyle, "margin-top") == "0", "cascade: child gets initial margin-top, not inherited");
}

static void testInitialValues() {
    printf("--- Cascade: initial values ---\n");
    Cascade cascade;
    MockElement e; e.tag = "div";
    auto style = cascade.resolve(e);
    check(sv(style, "display") == "inline", "cascade: initial display = inline");
    check(sv(style, "color") == "black", "cascade: initial color = black");
    check(sv(style, "position") == "static", "cascade: initial position = static");
    check(sv(style, "opacity") == "1", "cascade: initial opacity = 1");
    check(sv(style, "font-size") == "16px", "cascade: initial font-size = 16px");
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
    check(sv(cascade.resolve(p), "font-weight") == "normal", "cascade: p doesn't match h1,h2,h3 rule");
}

static void testNoMatch() {
    printf("--- Cascade: no matching rules ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".special { color: red; }"));
    MockElement e; e.tag = "div";
    check(sv(cascade.resolve(e), "color") == "black", "cascade: unmatched element gets initial color");
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

static void testMediaQueries() {
    printf("--- Cascade: @media query support ---\n");

    auto sheet = parse(
        "div { color: red; }\n"
        "@media (min-width: 768px) { div { color: blue; } }\n"
        "@media (max-width: 400px) { div { font-size: 12px; } }\n"
    );
    check(sheet.mediaBlocks.size() == 2, "@media: parser found 2 media blocks");
    check(sheet.rules.size() == 1, "@media: 1 unconditional rule");

    // Wide viewport: min-width: 768px matches, max-width: 400px doesn't
    MediaContext wide{1024, 768, "screen"};
    Cascade cascadeWide;
    cascadeWide.addStylesheet(sheet, nullptr, &wide);
    MockElement d1; d1.tag = "div";
    auto s1 = cascadeWide.resolve(d1);
    check(s1["color"] == "blue", "@media: min-width:768px matches at 1024px");
    check(sv(s1, "font-size") == "16px", "@media: max-width:400px doesn't match at 1024px");

    // Narrow viewport: max-width: 400px matches, min-width: 768px doesn't
    MediaContext narrow{320, 480, "screen"};
    Cascade cascadeNarrow;
    cascadeNarrow.addStylesheet(sheet, nullptr, &narrow);
    MockElement d2; d2.tag = "div";
    auto s2 = cascadeNarrow.resolve(d2);
    check(s2["color"] == "red", "@media: min-width:768px doesn't match at 320px");
    check(s2["font-size"] == "12px", "@media: max-width:400px matches at 320px");

    // Media type matching
    check(evaluateMediaQuery("screen", {1024, 768, "screen"}) == true, "@media: screen matches screen");
    check(evaluateMediaQuery("print", {1024, 768, "screen"}) == false, "@media: print doesn't match screen");
    check(evaluateMediaQuery("all", {1024, 768, "screen"}) == true, "@media: all matches anything");

    // Combined: screen and (min-width: 768px)
    check(evaluateMediaQuery("screen and (min-width: 768px)", {1024, 768, "screen"}) == true,
          "@media: screen and (min-width:768px) matches");
    check(evaluateMediaQuery("screen and (min-width: 768px)", {320, 480, "screen"}) == false,
          "@media: screen and (min-width:768px) doesn't match narrow");

    // Orientation
    check(evaluateMediaQuery("(orientation: landscape)", {1024, 768, "screen"}) == true,
          "@media: landscape matches wide viewport");
    check(evaluateMediaQuery("(orientation: portrait)", {320, 480, "screen"}) == true,
          "@media: portrait matches tall viewport");

    // not prefix
    check(evaluateMediaQuery("not print", {1024, 768, "screen"}) == true,
          "@media: not print matches screen");

    // prefers-color-scheme
    check(evaluateMediaQuery("(prefers-color-scheme: dark)", {1024, 768, "screen", "dark"}) == true,
          "@media: prefers-color-scheme:dark matches dark context");
    check(evaluateMediaQuery("(prefers-color-scheme: dark)", {1024, 768, "screen", "light"}) == false,
          "@media: prefers-color-scheme:dark doesn't match light context");
    check(evaluateMediaQuery("(prefers-color-scheme: light)", {1024, 768, "screen", "light"}) == true,
          "@media: prefers-color-scheme:light matches light context");
    check(evaluateMediaQuery("(prefers-color-scheme: light)", {1024, 768, "screen"}) == true,
          "@media: default context is light");
    check(evaluateMediaQuery("(prefers-color-scheme: DARK)", {1024, 768, "screen", "dark"}) == true,
          "@media: prefers-color-scheme value is case-insensitive");
    check(evaluateMediaQuery("not (prefers-color-scheme: dark)", {1024, 768, "screen", "light"}) == true,
          "@media: not (prefers-color-scheme:dark) matches light");
    check(evaluateMediaQuery("screen and (prefers-color-scheme: dark) and (min-width: 500px)",
                             {1024, 768, "screen", "dark"}) == true,
          "@media: prefers-color-scheme combines with type and width");

    // Cascade-level: a dark context includes @media (prefers-color-scheme: dark) blocks
    auto schemeSheet = parse(
        "div { color: black; }\n"
        "@media (prefers-color-scheme: dark) { div { color: white; } }\n"
    );
    MediaContext darkCtx{1024, 768, "screen", "dark"};
    Cascade cascadeDark;
    cascadeDark.addStylesheet(schemeSheet, nullptr, &darkCtx);
    MockElement d3; d3.tag = "div";
    auto s3 = cascadeDark.resolve(d3);
    check(s3["color"] == "white", "@media: dark-scheme block applies in dark context");

    MediaContext lightCtx{1024, 768, "screen", "light"};
    Cascade cascadeLight;
    cascadeLight.addStylesheet(schemeSheet, nullptr, &lightCtx);
    MockElement d4; d4.tag = "div";
    auto s4 = cascadeLight.resolve(d4);
    check(s4["color"] == "black", "@media: dark-scheme block skipped in light context");
}

static void testCustomProperties() {
    printf("--- Cascade: CSS custom properties (var()) ---\n");

    // Basic var() usage
    Cascade cascade;
    cascade.addStylesheet(parse(
        ":root { --main-color: red; --spacing: 10px; }\n"
        "div { color: var(--main-color); margin-top: var(--spacing); }\n"
    ));
    // Simulate :root as the parent
    MockElement root; root.tag = "html";
    MockElement div; div.tag = "div";
    root.addChild(&div);
    // :root won't match "html" via :root pseudo (it matches parent()==null)
    // So set custom props directly via a rule that matches
    Cascade cascade2;
    cascade2.addStylesheet(parse(
        "html { --main-color: red; --spacing: 10px; }\n"
        "div { color: var(--main-color); margin-top: var(--spacing); }\n"
    ));
    auto rootStyle = cascade2.resolve(root);
    auto divStyle = cascade2.resolve(div, {}, &rootStyle);
    check(divStyle["color"] == "red", "var(): basic variable resolution");
    check(divStyle["margin-top"] == "10px", "var(): spacing variable resolution");

    // var() with fallback
    Cascade cascade3;
    cascade3.addStylesheet(parse(
        "div { color: var(--undefined, blue); }\n"
    ));
    MockElement d2; d2.tag = "div";
    auto s2 = cascade3.resolve(d2);
    check(s2["color"] == "blue", "var(): fallback when variable undefined");

    // Custom properties inherit
    Cascade cascade4;
    cascade4.addStylesheet(parse(
        "div { --theme: green; }\n"
        "span { color: var(--theme); }\n"
    ));
    MockElement parent; parent.tag = "div";
    MockElement child; child.tag = "span";
    parent.addChild(&child);
    auto ps = cascade4.resolve(parent);
    auto cs = cascade4.resolve(child, {}, &ps);
    check(cs["color"] == "green", "var(): custom properties inherit to children");

    // Nested var() in fallback
    Cascade cascade5;
    cascade5.addStylesheet(parse(
        "div { --fallback-color: purple; color: var(--missing, var(--fallback-color)); }\n"
    ));
    MockElement d3; d3.tag = "div";
    auto s3 = cascade5.resolve(d3);
    check(s3["color"] == "purple", "var(): nested var() in fallback");

    // Invalid-at-computed-value-time: a unitless number substituted into a
    // length property is not a valid <length>, so the property falls back to
    // its initial value (width → auto), NOT treated as pixels.
    Cascade cascade6;
    cascade6.addStylesheet(parse(
        "div { --unitless: 20; width: var(--unitless); }\n"
    ));
    MockElement d4; d4.tag = "div";
    auto s4 = cascade6.resolve(d4);
    check(sv(s4, "width") == "auto", "var(): IACVT unitless length → width auto");

    // A valid substituted length is kept as-is.
    Cascade cascade7;
    cascade7.addStylesheet(parse(
        "div { --w: 120px; width: var(--w); }\n"
    ));
    MockElement d5; d5.tag = "div";
    auto s5 = cascade7.resolve(d5);
    check(s5["width"] == "120px", "var(): valid length kept");
}

static void testPseudoElements() {
    printf("--- Cascade: pseudo-elements (::before/::after) ---\n");

    Cascade cascade;
    cascade.addStylesheet(parse(
        "div::before { content: \">> \"; color: red; }\n"
        "div::after { content: \" <<\"; color: blue; }\n"
        ".special::before { content: \"* \"; font-weight: bold; }\n"
    ));

    MockElement div; div.tag = "div";
    auto divStyle = cascade.resolve(div);

    // Resolve ::before pseudo-element
    auto beforeStyle = cascade.resolvePseudo(div, "before", divStyle);
    check(!beforeStyle.empty(), "::before: style resolved (not empty)");
    check(beforeStyle["content"] == "\">> \"", "::before: content = '>> '");
    check(beforeStyle["color"] == "red", "::before: color = red");

    // Resolve ::after pseudo-element
    auto afterStyle = cascade.resolvePseudo(div, "after", divStyle);
    check(afterStyle["content"] == "\" <<\"", "::after: content = ' <<'");
    check(afterStyle["color"] == "blue", "::after: color = blue");

    // .special::before
    MockElement special; special.tag = "div"; special.classes = "special";
    auto specialStyle = cascade.resolve(special);
    auto specialBefore = cascade.resolvePseudo(special, "before", specialStyle);
    // Should get .special::before rule, not div::before (specificity)
    check(specialBefore["content"] == "\"* \"", "::before: .special rule wins");
    check(specialBefore["font-weight"] == "bold", "::before: font-weight = bold");

    // Element without ::before rules
    MockElement span; span.tag = "span";
    auto spanStyle = cascade.resolve(span);
    auto spanBefore = cascade.resolvePseudo(span, "before", spanStyle);
    check(spanBefore.empty(), "::before: empty for element without pseudo rules");

    // Inheritance: pseudo-element inherits from originating element
    Cascade cascade2;
    cascade2.addStylesheet(parse(
        "div { color: green; }\n"
        "div::before { content: \"x\"; }\n"
    ));
    MockElement d2; d2.tag = "div";
    auto d2Style = cascade2.resolve(d2);
    auto d2Before = cascade2.resolvePseudo(d2, "before", d2Style);
    check(d2Before["color"] == "green", "::before: inherits color from element");

    // ::placeholder / ::selection — styled-box pseudos (no generated content).
    Cascade cascade3;
    cascade3.addStylesheet(parse(
        "input { color: black; }\n"
        "input::placeholder { color: rgb(200, 50, 50); opacity: 0.5; }\n"
        "input::selection { background-color: gold; color: navy; }\n"
        ".fancy::placeholder { color: teal; }\n"
    ));

    check(cascade3.hasPseudoElementRules("placeholder"),
          "::placeholder: rules bucketed by name");
    check(cascade3.hasPseudoElementRules("selection"),
          "::selection: rules bucketed by name");

    MockElement input; input.tag = "input";
    auto inputStyle = cascade3.resolve(input);
    // The pseudo-element rule must NOT leak onto the element itself.
    check(inputStyle["color"] == "black",
          "::placeholder: rule does not restyle the input element");
    check(inputStyle["background-color"] != "gold",
          "::selection: rule does not restyle the input element");

    auto phStyle = cascade3.resolvePseudo(input, "placeholder", inputStyle);
    check(!phStyle.empty(), "::placeholder: style resolves");
    check(phStyle["color"] == "rgb(200, 50, 50)", "::placeholder: color applies");
    check(phStyle["opacity"] == "0.5", "::placeholder: opacity applies");

    auto selStyle = cascade3.resolvePseudo(input, "selection", inputStyle);
    check(selStyle["background-color"] == "gold", "::selection: background-color applies");
    check(selStyle["color"] == "navy", "::selection: color applies");

    // Compound subject: .fancy::placeholder wins over input::placeholder
    MockElement fancy; fancy.tag = "input"; fancy.classes = "fancy";
    auto fancyStyle = cascade3.resolve(fancy);
    auto fancyPh = cascade3.resolvePseudo(fancy, "placeholder", fancyStyle);
    check(fancyPh["color"] == "teal", "::placeholder: class-scoped rule wins by specificity");

    // Unstyled element resolves empty (consumer falls back to its default paint)
    MockElement plain; plain.tag = "textarea";
    auto plainStyle = cascade3.resolve(plain);
    auto plainSel = cascade3.resolvePseudo(plain, "selection", plainStyle);
    check(plainSel.empty(), "::selection: empty for element without matching rules");

    // Selector parsing: ::placeholder / ::selection compile as pseudo-elements
    auto sel1 = parseSelector("input::placeholder");
    bool foundPh = false;
    for (auto& s : sel1.chain.entries[0].compound.simples)
        if (s.type == SimpleSelectorType::PseudoElement && s.value == "placeholder")
            foundPh = true;
    check(foundPh, "::placeholder: parses as PseudoElement simple selector");
    check(parseSelector("p::selection").chain.entries.size() == 1,
          "::selection: parses as single compound");
}

static void testUserAgentStylesheet() {
    printf("--- Cascade: user-agent default stylesheet ---\n");

    auto& uaSheet = defaultUserAgentStylesheet();
    check(!uaSheet.rules.empty(), "UA stylesheet has rules");

    Cascade cascade;
    cascade.addStylesheet(uaSheet);

    // h1 should be block + bold + 32px
    MockElement h1; h1.tag = "h1";
    auto h1Style = cascade.resolve(h1);
    check(h1Style["display"] == "block", "UA: h1 display = block");
    check(h1Style["font-weight"] == "bold", "UA: h1 font-weight = bold");
    check(h1Style["font-size"] == "32px", "UA: h1 font-size = 32px");

    // p should be block with margins
    MockElement p; p.tag = "p";
    auto pStyle = cascade.resolve(p);
    check(pStyle["display"] == "block", "UA: p display = block");
    check(pStyle["margin-top"] == "16px", "UA: p margin-top = 16px");

    // span should remain inline (no UA rule)
    MockElement span; span.tag = "span";
    auto spanStyle = cascade.resolve(span);
    check(sv(spanStyle, "display") == "inline", "UA: span display = inline (default)");

    // strong should be bold
    MockElement strong; strong.tag = "strong";
    auto strongStyle = cascade.resolve(strong);
    check(strongStyle["font-weight"] == "bold", "UA: strong font-weight = bold");

    // a should be blue with underline
    MockElement a; a.tag = "a";
    auto aStyle = cascade.resolve(a);
    check(aStyle["color"] == "blue", "UA: a color = blue");
    check(aStyle["text-decoration"] == "underline", "UA: a text-decoration = underline");

    // head should be display:none
    MockElement head; head.tag = "head";
    auto headStyle = cascade.resolve(head);
    check(headStyle["display"] == "none", "UA: head display = none");

    // Author styles override UA
    cascade.addStylesheet(parse("h1 { font-size: 48px; }"));
    auto h1Custom = cascade.resolve(h1);
    check(h1Custom["font-size"] == "48px", "UA: author overrides UA font-size");
    check(h1Custom["font-weight"] == "bold", "UA: UA bold preserved when not overridden");
}

static void testClear() {
    printf("--- Cascade: clear ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; }"));
    MockElement e; e.tag = "div";
    check(cascade.resolve(e)["color"] == "red", "cascade: before clear, color is red");
    cascade.clear();
    check(sv(cascade.resolve(e), "color") == "black", "cascade: after clear, color is initial");
}

static void testRevertKeyword() {
    printf("--- Cascade: revert keyword ---\n");

    // UA stylesheet sets h1 { display: block; font-weight: bold; font-size: 2em; }
    // Author stylesheet overrides with h1 { display: revert; font-weight: revert; }
    // revert should roll back to the UA value

    Cascade cascade;
    cascade.addStylesheet(defaultUserAgentStylesheet(), nullptr, nullptr, Origin::UserAgent);
    cascade.addStylesheet(parse(
        "h1 { display: flex; font-weight: normal; }\n"
        "h1.reverted { display: revert; font-weight: revert; }\n"
    ));

    // Without revert: author style wins
    MockElement h1; h1.tag = "h1";
    auto style1 = cascade.resolve(h1);
    check(style1["display"] == "flex", "revert: author display=flex without revert");
    check(style1["font-weight"] == "normal", "revert: author font-weight=normal without revert");

    // With revert: falls back to UA values
    MockElement h1r; h1r.tag = "h1"; h1r.classes = "reverted";
    auto style2 = cascade.resolve(h1r);
    check(style2["display"] == "block", "revert: display reverts to UA block");
    check(style2["font-weight"] == "bold", "revert: font-weight reverts to UA bold");

    // revert on a property with no UA value → initial value
    Cascade cascade2;
    cascade2.addStylesheet(defaultUserAgentStylesheet(), nullptr, nullptr, Origin::UserAgent);
    cascade2.addStylesheet(parse("span { color: red; }\nspan.rev { color: revert; }"));

    MockElement span; span.tag = "span"; span.classes = "rev";
    auto style3 = cascade2.resolve(span);
    // UA doesn't set color on span, so revert → initial (black)
    check(style3["color"] == "black", "revert: color reverts to initial when no UA value");
}

static void testImportResolver() {
    printf("--- Cascade: @import basic resolution ---\n");
    Cascade cascade;
    cascade.setImportResolver([](const std::string& url) -> std::string {
        if (url == "base.css") return "div { color: blue; }";
        return "";
    });
    cascade.addStylesheet(parse("@import \"base.css\";\nspan { color: red; }"));

    MockElement div; div.tag = "div";
    check(cascade.resolve(div)["color"] == "blue", "import: div gets color from imported sheet");

    MockElement span; span.tag = "span";
    check(cascade.resolve(span)["color"] == "red", "import: span gets color from main sheet");
}

static void testImportCaching() {
    printf("--- Cascade: @import caching (each URL resolved once) ---\n");
    int resolveCount = 0;
    Cascade cascade;
    cascade.setImportResolver([&](const std::string& url) -> std::string {
        resolveCount++;
        if (url == "base.css") return "div { color: blue; }";
        return "";
    });
    // Import the same URL from two different stylesheets
    cascade.addStylesheet(parse("@import \"base.css\";"));
    cascade.addStylesheet(parse("@import \"base.css\";"));
    check(resolveCount == 1, "import: resolver called only once for same URL");
}

static void testImportNestedImports() {
    printf("--- Cascade: nested @import ---\n");
    Cascade cascade;
    cascade.setImportResolver([](const std::string& url) -> std::string {
        if (url == "a.css") return "@import \"b.css\";\ndiv { color: green; }";
        if (url == "b.css") return "div { font-size: 18px; }";
        return "";
    });
    cascade.addStylesheet(parse("@import \"a.css\";"));

    MockElement div; div.tag = "div";
    auto style = cascade.resolve(div);
    check(style["color"] == "green", "nested import: color from a.css");
    check(style["font-size"] == "18px", "nested import: font-size from b.css");
}

static void testImportSourceOrder() {
    printf("--- Cascade: @import source order (imported rules come first) ---\n");
    Cascade cascade;
    cascade.setImportResolver([](const std::string& url) -> std::string {
        if (url == "base.css") return "div { color: blue; }";
        return "";
    });
    // Main sheet overrides imported sheet at same specificity
    cascade.addStylesheet(parse("@import \"base.css\";\ndiv { color: red; }"));

    MockElement div; div.tag = "div";
    check(cascade.resolve(div)["color"] == "red", "import source order: main sheet wins over import");
}

static void testImportWithMediaCondition() {
    printf("--- Cascade: @import with media condition ---\n");
    Cascade cascade;
    cascade.setImportResolver([](const std::string& url) -> std::string {
        if (url == "wide.css") return "div { width: 500px; }";
        return "";
    });
    MediaContext narrow{400, 800, "screen"};
    cascade.addStylesheet(parse("@import \"wide.css\" (min-width: 600px);"),
                          nullptr, &narrow);

    MockElement div; div.tag = "div";
    auto style = cascade.resolve(div);
    // Media condition not met (400 < 600), so import should be skipped
    check(style.find("width") == style.end() || style["width"] != "500px",
          "import: media condition filters import when not matched");

    // Now with wide viewport
    Cascade cascade2;
    cascade2.setImportResolver([](const std::string& url) -> std::string {
        if (url == "wide.css") return "div { width: 500px; }";
        return "";
    });
    MediaContext wide{800, 600, "screen"};
    cascade2.addStylesheet(parse("@import \"wide.css\" (min-width: 600px);"),
                           nullptr, &wide);
    auto style2 = cascade2.resolve(div);
    check(style2["width"] == "500px", "import: media condition allows import when matched");
}

static void testImportWithLayer() {
    printf("--- Cascade: @import with layer ---\n");
    Cascade cascade;
    cascade.setImportResolver([](const std::string& url) -> std::string {
        if (url == "base.css") return "div { color: blue; }";
        return "";
    });
    // Import into a layer; unlayered rules should win over layered ones
    cascade.addStylesheet(parse(
        "@layer base;\n"
        "@import \"base.css\" layer(base);\n"
        "div { color: red; }\n"
    ));

    MockElement div; div.tag = "div";
    check(cascade.resolve(div)["color"] == "red",
          "import layer: unlayered rule wins over layered import");
}

static void testTableSpanAttributes() {
    printf("--- Cascade: table span attributes surfaced ---\n");
    Cascade cascade;

    MockElement td; td.tag = "td";
    td.attrs["colspan"] = "3";
    td.attrs["rowspan"] = "2";
    auto tdStyle = cascade.resolve(td);
    check(tdStyle["colspan"] == "3", "cascade: td colspan attribute surfaced");
    check(tdStyle["rowspan"] == "2", "cascade: td rowspan attribute surfaced");

    MockElement th; th.tag = "TH"; // tag match is case-insensitive
    th.attrs["colspan"] = "2";
    auto thStyle = cascade.resolve(th);
    check(thStyle["colspan"] == "2", "cascade: TH colspan surfaced (case-insensitive tag)");

    MockElement col; col.tag = "col";
    col.attrs["span"] = "4";
    auto colStyle = cascade.resolve(col);
    check(colStyle["span"] == "4", "cascade: col span attribute surfaced");

    MockElement div; div.tag = "div";
    div.attrs["colspan"] = "5";
    auto divStyle = cascade.resolve(div);
    check(divStyle.find("colspan") == divStyle.end(),
          "cascade: colspan not surfaced on non-cell elements");

    MockElement bad; bad.tag = "td";
    bad.attrs["colspan"] = "abc";
    auto badStyle = cascade.resolve(bad);
    check(badStyle.find("colspan") == badStyle.end(),
          "cascade: non-numeric colspan ignored");

    MockElement zero; zero.tag = "td";
    zero.attrs["colspan"] = "0";
    auto zeroStyle = cascade.resolve(zero);
    check(zeroStyle.find("colspan") == zeroStyle.end(),
          "cascade: colspan below 1 ignored");
}

static void testBlockification() {
    printf("--- Cascade: flex/grid item blockification ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".flex { display: flex; }\n"
        ".iflex { display: inline-flex; }\n"
        ".grid { display: grid; }\n"
        ".ib { display: inline-block; }\n"
        ".it { display: inline-table; }\n"
        ".childiflex { display: inline-flex; }\n"
        ".childigrid { display: inline-grid; }\n"
        ".none { display: none; }\n"
        ".contents { display: contents; }\n"
    ));

    MockElement flexParent; flexParent.tag = "div"; flexParent.classes = "flex";
    auto flexStyle = cascade.resolve(flexParent);

    // Default inline (no display declaration) blockifies to block
    MockElement span; span.tag = "span";
    auto s = cascade.resolve(span, {}, &flexStyle);
    check(sv(s, "display") == "block", "cascade: inline flex item blockified to block");

    MockElement ib; ib.tag = "span"; ib.classes = "ib";
    check(sv(cascade.resolve(ib, {}, &flexStyle), "display") == "block",
          "cascade: inline-block flex item blockified to block");

    MockElement it; it.tag = "span"; it.classes = "it";
    check(sv(cascade.resolve(it, {}, &flexStyle), "display") == "table",
          "cascade: inline-table flex item blockified to table");

    MockElement cif; cif.tag = "span"; cif.classes = "childiflex";
    check(sv(cascade.resolve(cif, {}, &flexStyle), "display") == "flex",
          "cascade: inline-flex flex item blockified to flex");
    MockElement cig; cig.tag = "span"; cig.classes = "childigrid";
    check(sv(cascade.resolve(cig, {}, &flexStyle), "display") == "grid",
          "cascade: inline-grid flex item blockified to grid");

    MockElement none; none.tag = "span"; none.classes = "none";
    check(sv(cascade.resolve(none, {}, &flexStyle), "display") == "none",
          "cascade: display:none flex child not blockified");
    MockElement cont; cont.tag = "span"; cont.classes = "contents";
    check(sv(cascade.resolve(cont, {}, &flexStyle), "display") == "contents",
          "cascade: display:contents flex child not blockified");

    MockElement blk; blk.tag = "div";
    check(sv(cascade.resolve(blk, {}, &flexStyle), "display") == "block",
          "cascade: block flex item stays block");

    // Grid and inline-flex containers blockify their items too
    MockElement gridParent; gridParent.tag = "div"; gridParent.classes = "grid";
    auto gridStyle = cascade.resolve(gridParent);
    MockElement gchild; gchild.tag = "span";
    check(sv(cascade.resolve(gchild, {}, &gridStyle), "display") == "block",
          "cascade: inline grid item blockified to block");

    MockElement iflexParent; iflexParent.tag = "div"; iflexParent.classes = "iflex";
    auto iflexStyle = cascade.resolve(iflexParent);
    MockElement ichild; ichild.tag = "span";
    check(sv(cascade.resolve(ichild, {}, &iflexStyle), "display") == "block",
          "cascade: item of inline-flex container blockified to block");

    // Children of a non-flex/grid parent keep inline display
    MockElement blockParent; blockParent.tag = "div";
    auto blockStyle = cascade.resolve(blockParent);
    MockElement bchild; bchild.tag = "span";
    check(sv(cascade.resolve(bchild, {}, &blockStyle), "display") == "inline",
          "cascade: child of block parent stays inline");
}

static void testMonospaceDefaultFontSize() {
    printf("--- Cascade: monospace default font-size quirk ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        "body { font-family: monospace; }\n"
        ".mono14 { font-size: 14px; }\n"
        ".sans { font-family: sans-serif; }\n"
        ".stack { font-family: Menlo, monospace; }\n"));

    MockElement html; html.tag = "html";
    MockElement body; body.tag = "body";
    MockElement pre14; pre14.tag = "pre"; pre14.classes = "mono14";
    MockElement mono; mono.tag = "span";
    MockElement sans; sans.tag = "span"; sans.classes = "sans";
    MockElement stack; stack.tag = "span"; stack.classes = "stack";
    html.addChild(&body);
    body.addChild(&pre14);
    body.addChild(&mono);
    body.addChild(&sans);
    body.addChild(&stack);

    auto htmlStyle = cascade.resolve(html);
    check(sv(htmlStyle, "font-size") == "16px", "cascade: default medium = 16px");
    auto bodyStyle = cascade.resolve(body, {}, &htmlStyle);
    check(sv(bodyStyle, "font-size") == "13px", "cascade: monospace body medium = 13px");
    check(sv(cascade.resolve(pre14, {}, &bodyStyle), "font-size") == "14px",
          "cascade: explicit px overrides the monospace quirk");
    check(sv(cascade.resolve(mono, {}, &bodyStyle), "font-size") == "13px",
          "cascade: monospace child keeps 13px");
    check(sv(cascade.resolve(sans, {}, &bodyStyle), "font-size") == "16px",
          "cascade: sans child of monospace re-expands to 16px (keyword inherits)");
    check(sv(cascade.resolve(stack, {}, &bodyStyle), "font-size") == "16px",
          "cascade: 'Menlo, monospace' stack does not trigger the quirk");
}

static void testInlineLogicalDirection() {
    printf("--- Cascade: inline logical properties resolve per direction ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".flow { margin-inline-start: 40px; margin-inline-end: 8px;"
        "        padding-inline-start: 3px; inset-inline-start: 20px;"
        "        border-inline-start-width: 5px; }\n"));

    // ltr: inline-start = left, inline-end = right.
    MockElement ltr; ltr.tag = "div"; ltr.classes = "flow";
    auto sLtr = cascade.resolve(ltr, "direction: ltr");
    check(sv(sLtr, "margin-left") == "40px", "ltr: margin-inline-start -> margin-left");
    check(sv(sLtr, "margin-right") == "8px", "ltr: margin-inline-end -> margin-right");
    check(sv(sLtr, "padding-left") == "3px", "ltr: padding-inline-start -> padding-left");
    check(sv(sLtr, "left") == "20px", "ltr: inset-inline-start -> left");
    check(sv(sLtr, "border-left-width") == "5px", "ltr: border-inline-start-width -> border-left-width");
    check(sv(sLtr, "margin-inline-start") == "0", "ltr: logical key removed after mapping");

    // rtl: inline-start = right, inline-end = left.
    MockElement rtl; rtl.tag = "div"; rtl.classes = "flow";
    auto sRtl = cascade.resolve(rtl, "direction: rtl");
    check(sv(sRtl, "margin-right") == "40px", "rtl: margin-inline-start -> margin-right");
    check(sv(sRtl, "margin-left") == "8px", "rtl: margin-inline-end -> margin-left");
    check(sv(sRtl, "padding-right") == "3px", "rtl: padding-inline-start -> padding-right");
    check(sv(sRtl, "right") == "20px", "rtl: inset-inline-start -> right");
    check(sv(sRtl, "border-right-width") == "5px", "rtl: border-inline-start-width -> border-right-width");

    // Direction inherited from the parent (the `dir` attribute path) also drives
    // the mapping.
    ComputedStyle parentRtl;
    parentRtl["direction"] = "rtl";
    MockElement child; child.tag = "div"; child.classes = "flow";
    auto sInh = cascade.resolve(child, {}, &parentRtl);
    check(sv(sInh, "margin-right") == "40px", "inherited rtl: margin-inline-start -> margin-right");
}

static void testHoverPseudoTracking() {
    printf("--- Cascade: usesHoverPseudo tracking ---\n");
    {
        Cascade c;
        c.addStylesheet(parse("div { color: red; } .cell { background: #16161c; }"));
        check(!c.usesHoverPseudo(), "no :hover rules -> usesHoverPseudo() false");
    }
    {
        Cascade c;
        c.addStylesheet(parse("a:hover { color: blue; }"));
        check(c.usesHoverPseudo(), ":hover rule -> usesHoverPseudo() true");
    }
    {
        Cascade c;  // :hover on a non-subject compound (ancestor)
        c.addStylesheet(parse(".menu:hover .item { color: blue; }"));
        check(c.usesHoverPseudo(), ":hover on ancestor compound -> true");
    }
    {
        Cascade c;
        c.addStylesheet(parse("li:not(:hover) { opacity: 0.5; }"));
        check(c.usesHoverPseudo(), ":hover nested in :not() -> true");
    }
    {
        Cascade c;
        c.addStylesheet(parse("div:has(:hover) { outline: 1px solid; }"));
        check(c.usesHoverPseudo(), ":hover nested in :has() -> true");
    }
    {
        Cascade c;
        c.addStylesheet(parse("a:hover { color: blue; }"));
        c.clear();
        check(!c.usesHoverPseudo(), "clear() resets usesHoverPseudo()");
    }
}

void testCascade() {
    testInlineLogicalDirection();
    testHoverPseudoTracking();
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
    testMediaQueries();
    testCustomProperties();
    testPseudoElements();
    testUserAgentStylesheet();
    testClear();
    testRevertKeyword();
    testImportResolver();
    testImportCaching();
    testImportNestedImports();
    testImportSourceOrder();
    testImportWithMediaCondition();
    testImportWithLayer();
    testTableSpanAttributes();
    testBlockification();
    testMonospaceDefaultFontSize();
}
