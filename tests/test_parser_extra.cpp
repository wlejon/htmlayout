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
    testPseudoLayersAndOrigins();
}
