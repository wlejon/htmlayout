#include "test_webcomponents.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"
#include "css/selector.h"
#include <cstdio>

using namespace htmlayout::css;

// ---- :host and :host(selector) ----

static void testHostMatching() {
    printf("--- :host matching ---\n");

    // Create a shadow host element with a shadow root
    int shadowRoot = 42; // dummy shadow root pointer value
    MockElement host;
    host.tag = "my-widget";
    host.classes = "fancy";
    host.shadowRootPtr = &shadowRoot;

    // :host should match an element that is a shadow host
    auto sel = parseSelector(":host");
    check(sel.matches(host), ":host matches shadow host element");

    // Non-host element should not match
    MockElement regular;
    regular.tag = "div";
    check(!sel.matches(regular), ":host does not match regular element");

    // :host(.fancy) should match host with class fancy
    auto sel2 = parseSelector(":host(.fancy)");
    check(sel2.matches(host), ":host(.fancy) matches host with class fancy");

    // :host(.other) should not match
    auto sel3 = parseSelector(":host(.other)");
    check(!sel3.matches(host), ":host(.other) does not match host without that class");
}

// ---- :host-context(selector) ----

static void testHostContext() {
    printf("--- :host-context matching ---\n");

    int shadowRoot = 43;

    // Build tree: body.dark-theme > div > my-component (shadow host)
    MockElement body;
    body.tag = "body";
    body.classes = "dark-theme";

    MockElement container;
    container.tag = "div";
    body.addChild(&container);

    MockElement host;
    host.tag = "my-component";
    host.shadowRootPtr = &shadowRoot;
    container.addChild(&host);

    // :host-context(.dark-theme) should match because body ancestor has that class
    auto sel = parseSelector(":host-context(.dark-theme)");
    check(sel.matches(host), ":host-context(.dark-theme) matches when ancestor has class");

    // :host-context(.light-theme) should not match
    auto sel2 = parseSelector(":host-context(.light-theme)");
    check(!sel2.matches(host), ":host-context(.light-theme) does not match");

    // :host-context(div) should match because direct parent is a div
    auto sel3 = parseSelector(":host-context(div)");
    check(sel3.matches(host), ":host-context(div) matches when parent is div");

    // Non-host should not match :host-context
    MockElement regular;
    regular.tag = "span";
    auto sel4 = parseSelector(":host-context(.dark-theme)");
    check(!sel4.matches(regular), ":host-context does not match non-host element");
}

// ---- :host cascade scoping ----

static void testHostCascade() {
    printf("--- :host cascade scoping ---\n");

    int shadowRoot = 44;

    MockElement host;
    host.tag = "my-button";
    host.classes = "primary";
    host.shadowRootPtr = &shadowRoot;

    // Shadow-scoped stylesheet with :host rules
    std::string shadowCSS = R"(
        :host { display: block; color: red; }
        :host(.primary) { color: blue; }
    )";
    auto sheet = parse(shadowCSS);

    Cascade cascade;
    cascade.addStylesheet(sheet, &shadowRoot);

    auto style = cascade.resolve(host);
    check(style["display"] == "block", ":host sets display:block on shadow host");
    check(style["color"] == "blue", ":host(.primary) overrides color to blue");
}

// ---- ::slotted(selector) ----

static void testSlotted() {
    printf("--- ::slotted() matching ---\n");

    int shadowRoot = 45;

    // Create a slot element inside the shadow tree
    MockElement slot;
    slot.tag = "slot";
    slot.scopePtr = &shadowRoot;

    // Create a light DOM element that's distributed into the slot
    MockElement lightChild;
    lightChild.tag = "span";
    lightChild.classes = "label";
    lightChild.assignedSlotElem = &slot;

    // Shadow-scoped stylesheet with ::slotted rules
    std::string shadowCSS = R"(
        ::slotted(span) { color: green; }
        ::slotted(.label) { font-weight: bold; }
    )";
    auto sheet = parse(shadowCSS);

    Cascade cascade;
    cascade.addStylesheet(sheet, &shadowRoot);

    auto style = cascade.resolve(lightChild);
    check(style["color"] == "green", "::slotted(span) applies to slotted span");
    check(style["font-weight"] == "bold", "::slotted(.label) applies to slotted .label");

    // Non-slotted element should not get ::slotted styles
    MockElement unslotted;
    unslotted.tag = "span";
    unslotted.classes = "label";

    auto style2 = cascade.resolve(unslotted);
    check(style2["color"] != "green", "::slotted does not apply to unslotted element");
}

// ---- ::part(name) ----

static void testPart() {
    printf("--- ::part() matching ---\n");

    int shadowRoot = 46;

    // Create a host element
    MockElement host;
    host.tag = "fancy-card";
    host.shadowRootPtr = &shadowRoot;

    // Create an element inside the shadow tree with a part name
    MockElement innerTitle;
    innerTitle.tag = "h2";
    innerTitle.partNames = "title";
    innerTitle.scopePtr = &shadowRoot;

    MockElement innerBody;
    innerBody.tag = "div";
    innerBody.partNames = "body content";  // multiple part names
    innerBody.scopePtr = &shadowRoot;

    // Document-scoped stylesheet with ::part rules
    std::string docCSS = R"(
        ::part(title) { color: navy; }
        ::part(body) { padding-top: 10px; }
        ::part(content) { margin-top: 5px; }
    )";
    auto sheet = parse(docCSS);

    Cascade cascade;
    cascade.addStylesheet(sheet); // document scope (nullptr)

    auto style1 = cascade.resolve(innerTitle);
    check(style1["color"] == "navy", "::part(title) applies to element with part=title");

    auto style2 = cascade.resolve(innerBody);
    check(style2["padding-top"] == "10px", "::part(body) applies to element with part=body");
    check(style2["margin-top"] == "5px", "::part(content) applies to element with part=content");

    // Element without part name should not get ::part styles
    MockElement noPart;
    noPart.tag = "div";
    noPart.scopePtr = &shadowRoot;

    auto style3 = cascade.resolve(noPart);
    check(style3["color"] != "navy", "::part does not apply to element without part name");
}

// ---- :defined ----

static void testDefined() {
    printf("--- :defined pseudo-class ---\n");

    MockElement definedElem;
    definedElem.tag = "my-element";
    definedElem.defined = true;

    MockElement undefinedElem;
    undefinedElem.tag = "my-element";
    undefinedElem.defined = false;

    auto sel = parseSelector(":defined");
    check(sel.matches(definedElem), ":defined matches defined element");
    check(!sel.matches(undefinedElem), ":defined does not match undefined element");

    // :not(:defined) should match undefined
    auto sel2 = parseSelector(":not(:defined)");
    check(!sel2.matches(definedElem), ":not(:defined) does not match defined element");
    check(sel2.matches(undefinedElem), ":not(:defined) matches undefined element");
}

// ---- @layer cascade layers ----

static void testLayerCascade() {
    printf("--- @layer cascade layers ---\n");

    // Test basic layer ordering: unlayered wins over layered for normal declarations
    std::string css = R"(
        @layer reset, base, components;

        @layer reset {
            div { color: black; }
        }
        @layer base {
            div { color: gray; }
        }
        @layer components {
            div { color: blue; }
        }
        div { color: red; }
    )";
    auto sheet = parse(css);

    MockElement div;
    div.tag = "div";

    Cascade cascade;
    cascade.addStylesheet(sheet);
    auto style = cascade.resolve(div);

    // Unlayered rules should win over layered rules
    check(style["color"] == "red", "unlayered rule wins over all layers");

    // Test layer ordering: later layers win over earlier layers
    std::string css2 = R"(
        @layer reset {
            span { color: black; }
        }
        @layer theme {
            span { color: blue; }
        }
    )";
    auto sheet2 = parse(css2);

    MockElement span;
    span.tag = "span";

    Cascade cascade2;
    cascade2.addStylesheet(sheet2);
    auto style2 = cascade2.resolve(span);
    check(style2["color"] == "blue", "later layer wins over earlier layer");

    // Test !important reversal: layered !important wins over unlayered !important
    std::string css3 = R"(
        @layer base {
            p { color: green !important; }
        }
        p { color: red !important; }
    )";
    auto sheet3 = parse(css3);

    MockElement p;
    p.tag = "p";

    Cascade cascade3;
    cascade3.addStylesheet(sheet3);
    auto style3 = cascade3.resolve(p);
    check(style3["color"] == "green", "layered !important wins over unlayered !important");

    // Test !important with multiple layers: earlier layer wins
    std::string css4 = R"(
        @layer first {
            em { color: red !important; }
        }
        @layer second {
            em { color: blue !important; }
        }
    )";
    auto sheet4 = parse(css4);

    MockElement em;
    em.tag = "em";

    Cascade cascade4;
    cascade4.addStylesheet(sheet4);
    auto style4 = cascade4.resolve(em);
    check(style4["color"] == "red", "earlier layer !important wins over later layer !important");
}

// ---- @layer declaration ordering ----

static void testLayerOrdering() {
    printf("--- @layer declaration ordering ---\n");

    // Pre-declared order should be respected even if blocks appear later
    std::string css = R"(
        @layer components, base;

        @layer base {
            div { color: gray; }
        }
        @layer components {
            div { color: blue; }
        }
    )";
    auto sheet = parse(css);

    MockElement div;
    div.tag = "div";

    Cascade cascade;
    cascade.addStylesheet(sheet);
    auto style = cascade.resolve(div);
    // Pre-declared order: components(0), base(1). base is later -> base wins.
    check(style["color"] == "gray", "pre-declared @layer order is respected (base after components)");
}

// ---- @container queries ----

static void testContainerQueries() {
    printf("--- @container queries ---\n");

    // Build tree: container(500px wide) > child
    MockElement container;
    container.tag = "div";
    container.contType = "inline-size";
    container.contInlineSize = 500;

    MockElement child;
    child.tag = "p";
    container.addChild(&child);

    // CSS with @container query
    std::string css = R"(
        @container (min-width: 400px) {
            p { color: blue; }
        }
        @container (min-width: 600px) {
            p { font-weight: bold; }
        }
    )";
    auto sheet = parse(css);

    Cascade cascade;
    cascade.addStylesheet(sheet);
    auto style = cascade.resolve(child);

    check(style["color"] == "blue", "@container (min-width: 400px) applies when container is 500px");
    check(style["font-weight"] != "bold", "@container (min-width: 600px) does not apply when container is 500px");
}

// ---- Named container queries ----

static void testNamedContainerQueries() {
    printf("--- named @container queries ---\n");

    // Build tree: sidebar(300px) > card(200px) > text
    MockElement sidebar;
    sidebar.tag = "aside";
    sidebar.contType = "inline-size";
    sidebar.contName = "sidebar";
    sidebar.contInlineSize = 300;

    MockElement card;
    card.tag = "div";
    card.contType = "inline-size";
    card.contName = "card";
    card.contInlineSize = 200;
    sidebar.addChild(&card);

    MockElement text;
    text.tag = "p";
    card.addChild(&text);

    std::string css = R"(
        @container sidebar (min-width: 250px) {
            p { color: green; }
        }
        @container card (min-width: 250px) {
            p { font-style: italic; }
        }
    )";
    auto sheet = parse(css);

    Cascade cascade;
    cascade.addStylesheet(sheet);
    auto style = cascade.resolve(text);

    check(style["color"] == "green", "named container query matches sidebar (300px >= 250px)");
    check(style["font-style"] != "italic", "named container query does not match card (200px < 250px)");
}

// ---- Container type: size (block-size queries) ----

static void testContainerSizeType() {
    printf("--- container-type: size ---\n");

    MockElement container;
    container.tag = "div";
    container.contType = "size";
    container.contInlineSize = 400;
    container.contBlockSize = 300;

    MockElement child;
    child.tag = "span";
    container.addChild(&child);

    std::string css = R"(
        @container (min-height: 200px) {
            span { color: purple; }
        }
        @container (min-height: 400px) {
            span { font-weight: bold; }
        }
    )";
    auto sheet = parse(css);

    Cascade cascade;
    cascade.addStylesheet(sheet);
    auto style = cascade.resolve(child);

    check(style["color"] == "purple", "container height query matches (300px >= 200px)");
    check(style["font-weight"] != "bold", "container height query does not match (300px < 400px)");
}

// ---- Container shorthand property ----

static void testContainerShorthand() {
    printf("--- container shorthand ---\n");

    auto expanded = expandShorthand("container", "inline-size / sidebar");
    check(expanded.size() == 2, "container shorthand expands to 2 properties");
    bool hasType = false, hasName = false;
    for (auto& e : expanded) {
        if (e.property == "container-type" && e.value == "inline-size") hasType = true;
        if (e.property == "container-name" && e.value == "sidebar") hasName = true;
    }
    check(hasType, "container shorthand sets container-type");
    check(hasName, "container shorthand sets container-name");
}

// ---- Parsing round-trip tests ----

static void testParserRoundTrips() {
    printf("--- parser round-trip tests ---\n");

    // @layer block parsing
    std::string css = R"(
        @layer reset {
            * { margin: 0; }
        }
    )";
    auto sheet = parse(css);
    check(sheet.layerBlocks.size() == 1, "@layer block is parsed");
    check(sheet.layerBlocks[0].name == "reset", "@layer block has correct name");
    check(sheet.layerBlocks[0].rules.size() == 1, "@layer block contains 1 rule");

    // @layer ordering declaration
    std::string css2 = "@layer reset, theme, components;";
    auto sheet2 = parse(css2);
    check(sheet2.layerOrder.size() == 3, "@layer ordering declares 3 layers");
    check(sheet2.layerOrder[0] == "reset", "first declared layer is reset");
    check(sheet2.layerOrder[1] == "theme", "second declared layer is theme");
    check(sheet2.layerOrder[2] == "components", "third declared layer is components");

    // @container block parsing
    std::string css3 = R"(
        @container (min-width: 300px) {
            .card { display: flex; }
        }
    )";
    auto sheet3 = parse(css3);
    check(sheet3.containerBlocks.size() == 1, "@container block is parsed");
    check(sheet3.containerBlocks[0].condition.find("min-width") != std::string::npos,
          "@container block has correct condition");
    check(sheet3.containerBlocks[0].rules.size() == 1, "@container block contains 1 rule");

    // Named @container
    std::string css4 = R"(
        @container sidebar (min-width: 200px) {
            .item { color: red; }
        }
    )";
    auto sheet4 = parse(css4);
    check(sheet4.containerBlocks.size() == 1, "named @container block is parsed");
    check(sheet4.containerBlocks[0].name == "sidebar", "named @container has correct name");
}

// ---- Selector parsing for new pseudo-classes/elements ----

static void testSelectorParsing() {
    printf("--- web component selector parsing ---\n");

    // ::slotted(selector)
    auto sel1 = parseSelector("::slotted(.item)");
    check(!sel1.chain.entries.empty(), "::slotted(.item) parses successfully");
    check(sel1.chain.entries[0].compound.simples[0].type == SimpleSelectorType::PseudoElement,
          "::slotted is a pseudo-element");
    check(sel1.chain.entries[0].compound.simples[0].value == "slotted",
          "::slotted has correct name");
    check(!sel1.chain.entries[0].compound.simples[0].slottedArg.empty(),
          "::slotted has parsed argument");

    // ::part(name)
    auto sel2 = parseSelector("::part(title)");
    check(!sel2.chain.entries.empty(), "::part(title) parses successfully");
    check(sel2.chain.entries[0].compound.simples[0].type == SimpleSelectorType::PseudoElement,
          "::part is a pseudo-element");
    check(sel2.chain.entries[0].compound.simples[0].partArg == "title",
          "::part has correct part name");

    // :host-context(selector)
    auto sel3 = parseSelector(":host-context(.dark)");
    check(!sel3.chain.entries.empty(), ":host-context(.dark) parses successfully");
    check(sel3.chain.entries[0].compound.simples[0].value == "host-context",
          ":host-context has correct name");
    check(!sel3.chain.entries[0].compound.simples[0].hostArg.empty(),
          ":host-context has parsed argument");

    // :defined
    auto sel4 = parseSelector(":defined");
    check(!sel4.chain.entries.empty(), ":defined parses successfully");
    check(sel4.chain.entries[0].compound.simples[0].value == "defined",
          ":defined has correct name");
}

// ---- Specificity tests ----

static void testWebComponentSpecificity() {
    printf("--- web component specificity ---\n");

    // :host has class-level specificity (pseudo-class)
    uint32_t hostSpec = calculateSpecificity(":host");
    uint32_t classSpec = calculateSpecificity(".foo");
    check(hostSpec == classSpec, ":host has same specificity as a class selector");

    // :host(.foo) = pseudo-class + class
    uint32_t hostArgSpec = calculateSpecificity(":host(.foo)");
    check(hostArgSpec > hostSpec, ":host(.foo) has higher specificity than :host");

    // ::slotted(span) = pseudo-element + type
    uint32_t slottedSpec = calculateSpecificity("::slotted(span)");
    uint32_t elemSpec = calculateSpecificity("span");
    check(slottedSpec > elemSpec, "::slotted(span) has higher specificity than span");

    // ::part(foo) = pseudo-element (type-level specificity)
    uint32_t partSpec = calculateSpecificity("::part(foo)");
    check(partSpec > 0, "::part(foo) has non-zero specificity");
}

// ---- Main entry point ----

void testWebComponents() {
    printf("=== Web Components ===\n");

    testHostMatching();
    testHostContext();
    testHostCascade();
    testSlotted();
    testPart();
    testDefined();
    testLayerCascade();
    testLayerOrdering();
    testContainerQueries();
    testNamedContainerQueries();
    testContainerSizeType();
    testContainerShorthand();
    testParserRoundTrips();
    testSelectorParsing();
    testWebComponentSpecificity();
}
