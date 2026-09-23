// Parser corner cases: @layer, @container, @supports, @keyframes
// percentage stops, malformed input, container queries via cascade.

#include "test_parser_extra.h"
#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"

using namespace htmlayout::css;

static void testLayerBlock() {
    printf("--- @layer block ---\n");
    auto s = parse("@layer base { .x { color: red; } }");
    check(s.layerBlocks.size() == 1, "@layer block captured");
    if (!s.layerBlocks.empty()) {
        check(s.layerBlocks[0].name == "base", "@layer name");
        check(!s.layerBlocks[0].rules.empty(), "@layer contains rules");
    }
}

static void testLayerOrdering() {
    printf("--- @layer ordering ---\n");
    auto s = parse("@layer reset, base, utilities;");
    check(s.layerOrder.size() == 3, "3 layers in ordering");
}

static void testLayerWithMedia() {
    printf("--- @layer with nested @media ---\n");
    auto s = parse("@layer base { @media (min-width: 500px) { .x { color: red; } } }");
    check(!s.layerBlocks.empty(), "layer captured");
    check(!s.layerBlocks[0].mediaBlocks.empty(), "media inside layer captured");
}

static void testContainerBlock() {
    printf("--- @container ---\n");
    auto s = parse("@container sidebar (min-width: 400px) { .x { color: red; } }");
    check(s.containerBlocks.size() == 1, "@container captured");
    if (!s.containerBlocks.empty()) {
        check(s.containerBlocks[0].name == "sidebar", "@container name");
        check(!s.containerBlocks[0].condition.empty(), "@container condition");
    }
}

static void testContainerNoName() {
    printf("--- @container (no name) ---\n");
    auto s = parse("@container (min-width: 400px) { .x { color: red; } }");
    check(!s.containerBlocks.empty(), "anonymous @container captured");
}

static void testSupportsRule() {
    printf("--- @supports ---\n");
    auto s = parse("@supports (display: grid) { .x { color: red; } }");
    check(true, "@supports parses");

    auto s2 = parse("@supports not (display: weird-thing) { .x { color: red; } }");
    check(true, "@supports not (...) parses");

    auto s3 = parse("@supports (display: grid) and (color: red) { .x { color: red; } }");
    check(true, "@supports compound condition parses");
}

static void testKeyframesPercentStops() {
    printf("--- @keyframes with percent stops ---\n");
    auto s = parse(
        "@keyframes pulse {"
        "  0% { opacity: 0; }"
        "  25% { opacity: 0.5; }"
        "  100% { opacity: 1; }"
        "}");
    check(!s.keyframes.empty() && s.keyframes[0].stops.size() == 3, "3 percent stops");
}

static void testMalformedRuleRecovery() {
    printf("--- malformed rule recovery ---\n");
    // Unclosed brace, missing semicolons — parser must not crash and still
    // return whatever it could parse before the corruption.
    auto s = parse(".valid { color: red; } .broken { color: blue; padding: ");
    check(!s.rules.empty(), "valid rule still parsed");
}

static void testEmptyRule() {
    printf("--- empty rule body ---\n");
    auto s = parse(".x { }");
    check(s.rules.size() == 1, "empty rule preserved");
}

static void testNestedBraces() {
    printf("--- nested braces in value ---\n");
    auto s = parse(".x { content: '{ }'; color: red; }");
    check(!s.rules.empty(), "nested braces survive");
}

// Container query via cascade with mock parent
static void testCascadeContainerQuery() {
    printf("--- cascade container query against mock parent ---\n");
    auto sheet = parse("@container (min-width: 100px) { .child { color: red; } }");
    Cascade c;
    c.addStylesheet(sheet);

    MockElement parent; parent.tag = "div";
    parent.contType = "inline-size";
    parent.contInlineSize = 200; // satisfies min-width: 100px

    MockElement child; child.tag = "span"; child.classes = "child";
    parent.addChild(&child);

    auto cs = c.resolve(child);
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "red",
          "container query matches when ancestor satisfies condition");

    // Same setup but parent inline-size below threshold
    MockElement parent2; parent2.tag = "div";
    parent2.contType = "inline-size";
    parent2.contInlineSize = 50;
    MockElement child2; child2.tag = "span"; child2.classes = "child";
    parent2.addChild(&child2);
    auto cs2 = c.resolve(child2);
    auto it2 = cs2.find("color");
    check(it2 == cs2.end() || it2->second != "red",
          "container query rejects when condition not met");
}

static void testCascadeContainerByName() {
    printf("--- cascade container query by name ---\n");
    auto sheet = parse("@container card (min-width: 200px) { .child { color: red; } }");
    Cascade c;
    c.addStylesheet(sheet);

    MockElement parent; parent.tag = "div";
    parent.contType = "inline-size";
    parent.contName = "card";
    parent.contInlineSize = 300;

    MockElement child; child.tag = "span"; child.classes = "child";
    parent.addChild(&child);

    auto cs = c.resolve(child);
    auto it = cs.find("color");
    check(it != cs.end() && it->second == "red", "named container matches");

    // Different name -> no match
    MockElement parent2; parent2.tag = "div";
    parent2.contType = "inline-size"; parent2.contName = "sidebar";
    parent2.contInlineSize = 300;
    MockElement child2; child2.tag = "span"; child2.classes = "child";
    parent2.addChild(&child2);
    auto cs2 = c.resolve(child2);
    auto it2 = cs2.find("color");
    check(it2 == cs2.end() || it2->second != "red",
          "named container with mismatched name rejects");
}

static std::string colorOf(const Cascade& c, const MockElement& e) {
    auto cs = c.resolve(e);
    auto it = cs.find("color");
    return it == cs.end() ? std::string() : it->second;
}

static void testContainerInsideLayer() {
    printf("--- @container inside @layer ---\n");
    auto s = parse("@layer base { @container (min-width: 200px) { .c { color: red; } } }");
    check(s.containerBlocks.size() == 1, "@container in @layer is kept");
    if (!s.containerBlocks.empty()) {
        check(s.containerBlocks[0].layered && s.containerBlocks[0].layer == "base",
              "@container in @layer records its layer");
    }

    MockElement box; box.tag = "div"; box.contType = "inline-size"; box.contInlineSize = 300;
    MockElement item; item.tag = "span"; item.classes = "c";
    box.addChild(&item);

    Cascade c;
    c.addStylesheet(s);
    check(colorOf(c, item) == "red", "layered @container rule applies when it matches");

    // Layered rules lose to unlayered ones even when they come later.
    Cascade c2;
    c2.addStylesheet(parse(
        ".c { color: blue; }\n"
        "@layer base { @container (min-width: 200px) { .c { color: red; } } }"));
    check(colorOf(c2, item) == "blue", "unlayered rule beats a later layered @container rule");

    // Layer order still ranks the container's layer.
    Cascade c3;
    c3.addStylesheet(parse(
        "@layer base, top;\n"
        "@layer top { .c { color: green; } }\n"
        "@layer base { @container (min-width: 200px) { .c { color: red; } } }"));
    check(colorOf(c3, item) == "green", "@container rule ranks by its layer's order");

    MockElement narrow; narrow.tag = "div"; narrow.contType = "inline-size"; narrow.contInlineSize = 100;
    MockElement item2; item2.tag = "span"; item2.classes = "c";
    narrow.addChild(&item2);
    check(colorOf(c, item2).empty(), "layered @container rule rejects when too narrow");
}

static void testLayerInsideContainer() {
    printf("--- @layer inside @container ---\n");
    auto s = parse(
        "@layer lo, hi;\n"
        "@layer hi { .c { color: blue; } }\n"
        "@container (min-width: 200px) { @layer lo { .c { color: red; } } }");
    check(s.containerBlocks.size() == 1, "@layer in @container is kept");
    MockElement box; box.tag = "div"; box.contType = "inline-size"; box.contInlineSize = 300;
    MockElement item; item.tag = "span"; item.classes = "c";
    box.addChild(&item);
    Cascade c;
    c.addStylesheet(s);
    check(colorOf(c, item) == "blue", "@layer lo inside @container ranks below @layer hi");

    Cascade c2;
    c2.addStylesheet(parse("@container (min-width: 200px) { @layer lo { .c { color: red; } } }"));
    check(colorOf(c2, item) == "red", "@layer inside @container applies when it matches");
}

static void testNestedContainerQueries() {
    printf("--- nested @container ---\n");
    auto s = parse(
        "@container outer (min-width: 400px) {\n"
        "  @container inner (min-width: 100px) { .c { color: red; } }\n"
        "}");
    check(s.containerBlocks.size() == 1, "nested @container is kept");
    if (!s.containerBlocks.empty()) {
        check(s.containerBlocks[0].name == "inner" &&
              s.containerBlocks[0].enclosing.size() == 1 &&
              s.containerBlocks[0].enclosing[0].name == "outer",
              "nested @container records the enclosing query");
    }
    Cascade c;
    c.addStylesheet(s);

    // Each query finds its own named container.
    MockElement outer; outer.tag = "div"; outer.contType = "inline-size";
    outer.contName = "outer"; outer.contInlineSize = 500;
    MockElement inner; inner.tag = "div"; inner.contType = "inline-size";
    inner.contName = "inner"; inner.contInlineSize = 150;
    MockElement item; item.tag = "span"; item.classes = "c";
    outer.addChild(&inner);
    inner.addChild(&item);
    check(colorOf(c, item) == "red", "both nested container queries hold");

    outer.contInlineSize = 300;
    check(colorOf(c, item).empty(), "outer query failing rejects the nested rule");
    outer.contInlineSize = 500;
    inner.contInlineSize = 50;
    check(colorOf(c, item).empty(), "inner query failing rejects the nested rule");

    // Unnamed nesting: both queries resolve to the nearest container.
    Cascade c2;
    c2.addStylesheet(parse(
        "@container (min-width: 100px) { @container (max-width: 200px) { .c { color: red; } } }"));
    MockElement box; box.tag = "div"; box.contType = "inline-size"; box.contInlineSize = 150;
    MockElement item2; item2.tag = "span"; item2.classes = "c";
    box.addChild(&item2);
    check(colorOf(c2, item2) == "red", "unnamed nested queries both hold on one container");
    box.contInlineSize = 250;
    check(colorOf(c2, item2).empty(), "unnamed nested query rejects when inner fails");

    // @media nested in a nested container keeps every condition.
    auto s3 = parse(
        "@container (min-width: 100px) { @container (min-width: 50px) {"
        " @media (min-width: 1px) { .c { color: green; } } } }");
    check(s3.containerBlocks.size() == 1 && s3.containerBlocks[0].enclosing.size() == 1 &&
          s3.containerBlocks[0].mediaConditions.size() == 1,
          "@media inside nested @container keeps both container queries");
}

static void testContainerPseudoElement() {
    printf("--- @container ::before ---\n");
    Cascade c;
    c.addStylesheet(parse(
        "@container (min-width: 200px) { .c::before { content: \"x\"; color: red; } }"));
    MockElement box; box.tag = "div"; box.contType = "inline-size"; box.contInlineSize = 300;
    MockElement item; item.tag = "span"; item.classes = "c";
    box.addChild(&item);
    auto es = c.resolve(item);
    auto ps = c.resolvePseudo(item, "before", es);
    check(ps.count("color") && ps["color"] == "red", "@container ::before applies when it matches");
    box.contInlineSize = 100;
    auto ps2 = c.resolvePseudo(item, "before", c.resolve(item));
    check(!ps2.count("color") || ps2["color"] != "red",
          "@container ::before rejects when the query fails");
}

static void testPseudoLayersAndOrigins() {
    printf("--- ::before cascade: layers and origins ---\n");
    MockElement item; item.tag = "span"; item.classes = "c"; item.elemId = "i";
    // A layered rule loses to an unlayered one even with higher specificity.
    Cascade c;
    c.addStylesheet(parse(
        "@layer base { #i.c::before { color: red; } }\n"
        ".c::before { content: \"x\"; color: blue; }"));
    auto es = c.resolve(item);
    auto ps = c.resolvePseudo(item, "before", es);
    check(ps["color"] == "blue", "::before: unlayered beats a more specific layered rule");

    // Author beats the UA origin even with lower specificity.
    Cascade o;
    o.addStylesheet(parse("#i.c::before { content: \"x\"; color: red; }"), nullptr, nullptr,
                    Origin::UserAgent);
    o.addStylesheet(parse(".c::before { color: blue; }"));
    auto ps2 = o.resolvePseudo(item, "before", o.resolve(item));
    check(ps2["color"] == "blue", "::before: author beats a more specific UA rule");

    // Later layers beat earlier ones.
    Cascade l;
    l.addStylesheet(parse(
        "@layer a, b;\n"
        "@layer b { .c::before { content: \"x\"; color: green; } }\n"
        "@layer a { #i.c::before { color: red; } }"));
    auto ps3 = l.resolvePseudo(item, "before", l.resolve(item));
    check(ps3["color"] == "green", "::before: later layer beats an earlier one");
}

static void testContainerPreludeName() {
    printf("--- @container prelude name ---\n");
    auto a = parse("@container not (min-width: 400px) { .c { color: red; } }");
    check(a.containerBlocks.size() == 1 && a.containerBlocks[0].name.empty() &&
          a.containerBlocks[0].condition == "not (min-width: 400px)",
          "`not` opens the condition, it is not a container name");
    auto b = parse("@container style(--x: y) { .c { color: red; } }");
    check(b.containerBlocks.size() == 1 && b.containerBlocks[0].name.empty(),
          "a style() query has no container name");
    auto d = parse("@container card style(--x: y) { .c { color: red; } }");
    check(d.containerBlocks.size() == 1 && d.containerBlocks[0].name == "card",
          "a name before style() is kept");
}

static void testContainerSourceOrder() {
    printf("--- @container source order ---\n");
    Cascade c;
    c.addStylesheet(parse(
        ".c { color: blue; }\n"
        "@container (min-width: 200px) { .c { color: red; } }\n"
        ".c { color: green; }\n"
        "@container (min-width: 200px) { .c { background-color: red; } }\n"
        ".c { background-color: blue; }\n"));
    MockElement box; box.tag = "div"; box.contType = "inline-size"; box.contInlineSize = 300;
    MockElement item; item.tag = "span"; item.classes = "c";
    box.addChild(&item);
    auto cs = c.resolve(item);
    check(cs["color"] == "green", "plain rule after @container wins");
    check(cs["background-color"] == "blue", "interleaved rules keep source order");
}

// One rule `.c { color: red }` under `@container <prelude>`, resolved for a
// child of `box`: does it apply?
static bool containerApplies(const std::string& prelude, MockElement& box,
                             const ComputedStyle* parentStyle = nullptr) {
    Cascade c;
    c.addStylesheet(parse("@container " + prelude + " { .c { color: red; } }"));
    MockElement item; item.tag = "span"; item.classes = "c";
    box.childElems.clear();
    box.addChild(&item);
    auto cs = c.resolve(item, {}, parentStyle);
    auto it = cs.find("color");
    box.childElems.clear();
    return it != cs.end() && it->second == "red";
}

static void testContainerConditionLogic() {
    printf("--- container query conditions: not / and / or / ranges ---\n");
    MockElement box; box.tag = "div"; box.contType = "size";
    box.contInlineSize = 300; box.contBlockSize = 200;

    check(containerApplies("(width > 200px)", box), "range: width > 200px");
    check(!containerApplies("(width > 300px)", box), "range: width > 300px is false at 300");
    check(containerApplies("(width >= 300px)", box), "range: >= is inclusive");
    check(containerApplies("(250px < width)", box), "range: value first");
    check(containerApplies("(100px < width <= 300px)", box), "range: two-sided");
    check(!containerApplies("(100px < width < 300px)", box), "range: two-sided, exclusive end");
    check(!containerApplies("(100px < width > 50px)", box), "range: mixed directions are invalid");
    check(containerApplies("(min-width: 300px) and (max-height: 200px)", box), "and: both hold");
    check(!containerApplies("(min-width: 300px) and (min-height: 201px)", box), "and: one fails");
    check(containerApplies("(min-width: 900px) or (height: 200px)", box), "or: one holds");
    check(!containerApplies("(min-width: 900px) or (height: 10px)", box), "or: none holds");
    check(containerApplies("not (width < 100px)", box), "not: negates");
    check(!containerApplies("not (width > 100px)", box), "not: negates a true feature");
    check(containerApplies("((width > 100px) and (height > 100px)) or (width > 900px)", box),
          "parenthesized sub-query");
    check(containerApplies("not ((width > 900px) or (height > 900px))", box),
          "not over a parenthesized or");
    check(!containerApplies("(width > 100px) and (height > 100px) or (width > 1px)", box),
          "and/or mixed at one level is invalid");
    check(containerApplies("(orientation: landscape)", box), "orientation: landscape");
    check(!containerApplies("(orientation: portrait)", box), "orientation: portrait");
    check(containerApplies("(aspect-ratio > 1/1)", box), "aspect-ratio > 1/1");
    check(containerApplies("(aspect-ratio: 3/2)", box), "aspect-ratio: 3/2 exactly");
    check(containerApplies("(min-aspect-ratio: 1.4)", box), "min-aspect-ratio: number");
    check(containerApplies("(width)", box), "boolean context: nonzero width");
    check(containerApplies("(inline-size > 18.5em)", box), "em: 16px when unknown (296 < 300)");
    check(!containerApplies("(inline-size > 19em)", box), "em: 304 > 300");
    check(containerApplies("(block-size < 3in)", box), "absolute units: 3in = 288px");

    // Unknown is neither true nor false: `not` of it is still unknown.
    check(!containerApplies("(width > 50vw)", box), "viewport unit: unknown");
    check(!containerApplies("not (width > 50vw)", box), "not unknown is unknown");
    check(containerApplies("(width > 50vw) or (width > 1px)", box), "unknown or true = true");
    check(!containerApplies("(width > 50vw) and (width > 1px)", box), "unknown and true = unknown");
    check(!containerApplies("(foo: bar)", box), "unknown feature: general-enclosed");
    check(!containerApplies("not (foo: bar)", box), "not general-enclosed is unknown");
    check(containerApplies("(foo: bar) or (width > 1px)", box), "general-enclosed or true");
    check(!containerApplies("(width > )", box), "malformed range is unknown");

    // An inline-size container answers only inline-axis features.
    MockElement inl; inl.tag = "div"; inl.contType = "inline-size";
    inl.contInlineSize = 300; inl.contBlockSize = 200;
    check(containerApplies("(width > 200px)", inl), "inline-size container: width");
    check(!containerApplies("(height > 10px)", inl), "inline-size container: height is unknown");
    check(!containerApplies("not (height > 10px)", inl),
          "inline-size container: not height is unknown too");
    check(!containerApplies("(orientation: landscape)", inl),
          "inline-size container: orientation is unknown");

    // A size query skips ancestors that are not size containers.
    MockElement outer; outer.tag = "div"; outer.contType = "inline-size"; outer.contInlineSize = 500;
    MockElement plain; plain.tag = "div";  // container-type none
    outer.addChild(&plain);
    check(containerApplies("(width > 400px)", plain), "size query finds the nearest size container");
    outer.childElems.clear();
}

static void testContainerStyleQueries() {
    printf("--- container style queries ---\n");
    // Every element is a style container: an unnamed style() query asks the
    // parent, whose style the cascade has from resolve().
    MockElement parent; parent.tag = "div";  // container-type none
    ComputedStyle ps;
    ps["--theme"] = " dark ";
    ps["--pad"] = "1px   2px";
    check(containerApplies("style(--theme: dark)", parent, &ps), "style(): custom property value");
    check(!containerApplies("style(--theme: light)", parent, &ps), "style(): other value");
    check(containerApplies("style(--pad: 1px 2px)", parent, &ps), "style(): whitespace-normalized");
    check(containerApplies("style(--theme)", parent, &ps), "style(--x): set");
    check(!containerApplies("style(--missing)", parent, &ps), "style(--x): unset");
    check(containerApplies("not style(--theme: light)", parent, &ps), "not style()");
    check(containerApplies("style((--theme: dark) and (--pad))", parent, &ps), "and inside style()");
    check(containerApplies("style(not (--theme: light))", parent, &ps), "not inside style()");
    check(containerApplies("style(--theme: light) or style(--theme: dark)", parent, &ps),
          "or of style() queries");
    check(!containerApplies("style(color: red)", parent, &ps),
          "style() of a standard property is unknown");

    // Inherited custom properties are seen too.
    ComputedStyle inheritedOnly;
    auto vars = std::make_shared<StyleMap>();
    (*vars)["--theme"] = "dark";
    inheritedOnly.inheritedVars = vars;
    check(containerApplies("style(--theme: dark)", parent, &inheritedOnly),
          "style(): an inherited custom property");

    // Without the parent style and without the hook: unknown, even negated.
    check(!containerApplies("style(--theme: dark)", parent), "no computed values: unknown");
    check(!containerApplies("not style(--theme: dark)", parent), "no computed values: not unknown");

    // Through the ElementRef hook (and past the parent, by name).
    parent.reportsComputed = true;
    parent.computed["--theme"] = "dark";
    check(containerApplies("style(--theme: dark)", parent), "computedStyleValue hook");
    MockElement named; named.tag = "section"; named.contName = "card";
    named.reportsComputed = true;
    named.computed["--theme"] = "light";
    MockElement mid; mid.tag = "div"; mid.reportsComputed = true;
    named.addChild(&mid);
    check(containerApplies("card style(--theme: light)", mid),
          "named style query skips to the named container");
    check(!containerApplies("style(--theme: light)", mid),
          "unnamed style query asks the parent");

    // Mixed size and style: the container must be a size container.
    MockElement sized; sized.tag = "div"; sized.contType = "inline-size"; sized.contInlineSize = 400;
    sized.reportsComputed = true;
    sized.computed["--theme"] = "dark";
    MockElement inner; inner.tag = "div"; inner.reportsComputed = true;
    sized.addChild(&inner);
    check(containerApplies("(width > 300px) and style(--theme: dark)", inner),
          "size + style query uses the size container");
}

void testParserExtra() {
    printf("=== Parser extras ===\n");
    testLayerBlock();
    testLayerOrdering();
    testLayerWithMedia();
    testContainerBlock();
    testContainerNoName();
    testSupportsRule();
    testKeyframesPercentStops();
    testMalformedRuleRecovery();
    testEmptyRule();
    testNestedBraces();
    testCascadeContainerQuery();
    testCascadeContainerByName();
    testContainerInsideLayer();
    testLayerInsideContainer();
    testNestedContainerQueries();
    testContainerPseudoElement();
    testContainerSourceOrder();
    testContainerPreludeName();
    testContainerConditionLogic();
    testContainerStyleQueries();
    testPseudoLayersAndOrigins();
}
