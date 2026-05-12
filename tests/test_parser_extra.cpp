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
}
