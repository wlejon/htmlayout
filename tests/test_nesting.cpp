// css-nesting-1: nested style rules, `&`, relative selectors, nested
// conditional rules, and interleaved declarations.

#include "test_nesting.h"
#include "test_helpers.h"
#include "css/cascade.h"
#include "css/parser.h"
#include <string>

using namespace htmlayout::css;

namespace {

// Selector of the rule at `i`, or "<none>".
std::string sel(const std::vector<Rule>& rules, size_t i) {
    return i < rules.size() ? rules[i].selector : std::string("<none>");
}

std::string firstValue(const Rule& r, const char* prop) {
    for (auto& d : r.declarations)
        if (d.property == prop) return d.value;
    return {};
}

void checkNested(const char* css, const char* expected) {
    auto s = parse(css);
    // The nested rule is the last one emitted.
    std::string got = s.rules.empty() ? "<none>" : s.rules.back().selector;
    std::string name = std::string(css) + "  ->  " + expected;
    if (got != expected) name += "  [got " + got + "]";
    check(got == expected, name.c_str());
}

void testSelectorResolution() {
    printf("--- Nesting: selector resolution ---\n");
    checkNested(".a { .b { color: red } }", ".a .b");
    checkNested(".a { & .b { color: red } }", ".a .b");
    checkNested(".a { &.x { color: red } }", ".a.x");
    checkNested(".a { &:hover { color: red } }", ".a:hover");
    checkNested(".a { &::before { color: red } }", ".a::before");
    checkNested(".a { .p & { color: red } }", ".p .a");
    checkNested(".a { .p > & { color: red } }", ".p > .a");
    checkNested(".a { > .c { color: red } }", ".a > .c");
    checkNested(".a { + .c { color: red } }", ".a + .c");
    checkNested(".a { ~ .c { color: red } }", ".a ~ .c");
    checkNested(".a { & > .c { color: red } }", ".a > .c");
    checkNested(".a { & + & { color: red } }", ".a + .a");
    checkNested(".a { div& { color: red } }", "div.a");
    checkNested("span { div& { color: red } }", "div:is(span)");
    checkNested(".p .q { &:hover { color: red } }", ".p .q:hover");
    checkNested(".p .q { .x& { color: red } }", ".p .q.x");
    checkNested(".p > .q { & .r { color: red } }", ".p > .q .r");
    checkNested(".a { :not(&) { color: red } }", ":not(.a)");
    checkNested(".a { div:hover { color: red } }", ".a div:hover");
    checkNested(".a { [data-x=\"&\"] { color: red } }", ".a [data-x=\"&\"]");
    checkNested(".a { :is(.b, .c) & { color: red } }", ":is(.b, .c) .a");
    // Selector lists on both levels expand to the cross product.
    checkNested(".a, .b { .c { color: red } }", ".a .c, .b .c");
    checkNested(".a { .b, .c { color: red } }", ".a .b, .a .c");
    checkNested(".a, .b { &.x, > .y { color: red } }", ".a.x, .b.x, .a > .y, .b > .y");
    checkNested(".a, .b { :not(&) { color: red } }", ":not(.a, .b), :not(.a, .b)");
    // Deeper nesting resolves against the already-resolved parent.
    checkNested(".a { .b { .c { color: red } } }", ".a .b .c");
    checkNested(".a { &.b { & > .c { color: red } } }", ".a.b > .c");
    checkNested(".a, .b { .c { &:hover { color: red } } }", ".a .c:hover, .b .c:hover");
}

void testDeclarationInterleaving() {
    printf("--- Nesting: declarations and rule order ---\n");
    auto s = parse(".a { color: red; .b { color: blue } background: green; }");
    check(s.rules.size() == 3, "interleaved: three flat rules");
    check(sel(s.rules, 0) == ".a" && firstValue(s.rules[0], "color") == "red",
          "interleaved: leading declarations stay on .a");
    check(sel(s.rules, 1) == ".a .b" && firstValue(s.rules[1], "color") == "blue",
          "interleaved: nested rule next");
    check(sel(s.rules, 2) == ".a" && firstValue(s.rules[2], "background") == "green",
          "interleaved: trailing declarations after the nested rule");
    check(s.rules.size() == 3 && s.rules[0].sourcePos < s.rules[1].sourcePos &&
          s.rules[1].sourcePos < s.rules[2].sourcePos, "interleaved: source positions increase");

    s = parse(".a { .b { color: blue } }");
    check(s.rules.size() == 1 && sel(s.rules, 0) == ".a .b",
          "a rule holding only nested rules emits no empty parent rule");
    s = parse(".a { }");
    check(s.rules.size() == 1 && sel(s.rules, 0) == ".a", "an empty plain rule is still kept");
    s = parse(".a { --x: 1; color: red }");
    check(s.rules.size() == 1 && s.rules[0].declarations.size() == 2,
          "custom property declarations are not mistaken for rules");
    s = parse(".a { color: red; } .b { color: blue; }");
    check(s.rules.size() == 2 && sel(s.rules, 1) == ".b", "plain sheets parse as before");
}

void testNestedConditionals() {
    printf("--- Nesting: nested @media / @supports ---\n");
    auto s = parse(".a { color: red; @media (min-width: 500px) { color: blue; .b { color: green } } }");
    check(s.rules.size() == 1 && sel(s.rules, 0) == ".a", "@media nested: parent rule");
    check(s.mediaBlocks.size() == 1, "@media nested: one media block");
    if (s.mediaBlocks.size() == 1) {
        auto& mb = s.mediaBlocks[0];
        check(mb.condition == "(min-width: 500px)", "@media nested: condition");
        check(mb.rules.size() == 2 && sel(mb.rules, 0) == ".a" &&
              firstValue(mb.rules[0], "color") == "blue",
              "@media nested: bare declarations apply to the parent selector");
        check(sel(mb.rules, 1) == ".a .b", "@media nested: nested rule inside resolves");
    }

    s = parse("@media screen { .a { @media (min-width: 500px) { color: blue } } }");
    bool found = false;
    for (auto& mb : s.mediaBlocks)
        if (mb.condition == "(min-width: 500px)" && mb.andConditions.size() == 1 &&
            mb.andConditions[0] == "screen" && sel(mb.rules, 0) == ".a") found = true;
    check(found, "@media inside a rule inside @media carries the outer condition");

    s = parse(".a { @supports (display: grid) { color: blue } @supports (display: nope) { color: red } }");
    check(s.rules.size() == 1 && firstValue(s.rules[0], "color") == "blue",
          "@supports nested: true branch applies to the parent, false dropped");

    s = parse(".a { @container card (min-width: 300px) { color: blue } }");
    check(s.containerBlocks.size() == 1 && s.containerBlocks[0].name == "card" &&
          sel(s.containerBlocks[0].rules, 0) == ".a", "@container nested in a style rule");

    s = parse("@layer base { .a { color: red; .b { color: blue } @media (min-width: 1px) { color: green } } }");
    check(s.layerBlocks.size() == 1 && s.layerBlocks[0].rules.size() == 2 &&
          sel(s.layerBlocks[0].rules, 1) == ".a .b", "nesting inside @layer");
    check(s.layerBlocks.size() == 1 && s.layerBlocks[0].mediaBlocks.size() == 1,
          "nested @media inside @layer lands in the layer");
}

void testNestingCascade() {
    printf("--- Nesting: cascade ---\n");
    MockElement root; root.tag = "div"; root.classes = "p";
    MockElement mid; mid.tag = "div"; mid.classes = "y"; mid.parentElem = &root;
    MockElement a; a.tag = "div"; a.classes = "a c"; a.parentElem = &mid;
    root.childElems = {&mid};
    mid.childElems = {&a};

    {
        Cascade c;
        c.addStylesheet(parse(".x, .y { & > .c { color: red } }"));
        check(c.resolve(a)["color"] == "red", "parent list: .y > .c matches");
    }
    {
        Cascade c;
        c.addStylesheet(parse(".a { .p & { color: red } }"));
        check(c.resolve(a)["color"] == "red", "`.p &` matches an .a under .p");
        check(c.resolve(mid)["color"] != "red", "`.p &` does not match a non-.a");
    }
    {
        // Trailing declarations keep the parent's specificity: the nested
        // `.p .a` (0,2,0) wins over the later `.a` (0,1,0) block.
        Cascade c;
        c.addStylesheet(parse(".a { .p & { color: blue } color: green }"));
        check(c.resolve(a)["color"] == "blue", "nested more-specific rule beats later bare decls");
    }
    {
        // Same specificity: source order decides, and the trailing block is last.
        Cascade c;
        c.addStylesheet(parse(".a { color: red; &.c { color: blue } }"));
        check(c.resolve(a)["color"] == "blue", "&.c (0,2,0) beats .a");
        Cascade c2;
        c2.addStylesheet(parse(".a { color: red; & { color: blue } color: green }"));
        check(c2.resolve(a)["color"] == "green", "declarations after a nested rule come later");
    }
    {
        MediaContext narrow; narrow.viewportWidth = 400;
        MediaContext wide; wide.viewportWidth = 800;
        auto sheet = parse(".a { color: red; @media (min-width: 600px) { color: blue } }");
        Cascade c1; c1.addStylesheet(sheet, nullptr, &narrow);
        Cascade c2; c2.addStylesheet(sheet, nullptr, &wide);
        check(c1.resolve(a)["color"] == "red", "nested @media: not matching keeps parent");
        check(c2.resolve(a)["color"] == "blue", "nested @media: matching overrides parent");

        auto sheet2 = parse(".a { @media (min-width: 600px) { color: blue } color: red }");
        Cascade c3; c3.addStylesheet(sheet2, nullptr, &wide);
        check(c3.resolve(a)["color"] == "red", "nested @media before later declarations loses");

        auto sheet3 = parse("@media (min-width: 600px) { .a { color: blue } } .a { color: red }");
        Cascade c4; c4.addStylesheet(sheet3, nullptr, &wide);
        check(c4.resolve(a)["color"] == "red", "a later plain rule beats an earlier @media rule");

        auto sheet4 = parse("@media (min-width: 100px) { .a { @media (max-width: 500px) { color: blue } } }");
        Cascade c5; c5.addStylesheet(sheet4, nullptr, &narrow);
        Cascade c6; c6.addStylesheet(sheet4, nullptr, &wide);
        check(c5.resolve(a)["color"] == "blue", "nested @media inside @media: both match");
        check(c6.resolve(a)["color"] != "blue", "nested @media inside @media: inner fails");
    }
}

const LayerBlock* findLayer(const Stylesheet& s, const char* name) {
    for (auto& lb : s.layerBlocks)
        if (lb.name == name) return &lb;
    return nullptr;
}

void testNestedLayer() {
    printf("--- Nesting: @layer inside style rules and layers ---\n");
    auto s = parse(".a { color: green; @layer base { color: red; .b { color: blue } } }");
    const LayerBlock* lb = findLayer(s, "base");
    check(lb && lb->rules.size() == 2 && sel(lb->rules, 0) == ".a" &&
              firstValue(lb->rules[0], "color") == "red" && sel(lb->rules, 1) == ".a .b",
          "@layer in a style rule: bare declarations and nested rules land in the layer");
    check(s.rules.size() == 1 && firstValue(s.rules[0], "color") == "green",
          "@layer in a style rule: the parent's own declarations stay unlayered");

    s = parse("@layer outer { .a { @layer inner { color: red } } }");
    check(findLayer(s, "outer.inner") != nullptr, "a layer nested via a style rule is outer.inner");
    s = parse("@layer a { @layer b { .x { color: red } } }");
    lb = findLayer(s, "a.b");
    check(lb && sel(lb->rules, 0) == ".x", "@layer directly inside @layer is a.b");
    s = parse(".a { @layer x, y; }");
    check(s.layerOrder.size() == 2 && s.layerOrder[0] == "x" && s.layerOrder[1] == "y",
          "@layer statement inside a style rule declares order");
    s = parse("@layer p { @layer q, r; }");
    check(s.layerOrder.size() == 2 && s.layerOrder[0] == "p.q", "nested @layer statement qualifies names");
    s = parse(".a { @supports (display: nope) { @layer base { color: red } } }");
    check(s.layerBlocks.empty(), "@layer inside a false @supports is dropped");

    s = parse("@media (min-width: 600px) { .a { @layer base { color: blue } } }");
    lb = findLayer(s, "base");
    check(lb && lb->rules.empty() && lb->mediaBlocks.size() == 1 &&
              lb->mediaBlocks[0].condition == "(min-width: 600px)" &&
              sel(lb->mediaBlocks[0].rules, 0) == ".a",
          "@layer inside @media keeps the media condition");

    MockElement a; a.tag = "div"; a.classes = "a";
    {
        Cascade c;
        c.addStylesheet(parse(".a { @layer base { color: red } }"));
        check(c.resolve(a)["color"] == "red", "cascade: nested @layer declarations apply");
    }
    {
        // Unlayered beats layered, even when the layered rule comes later.
        Cascade c;
        c.addStylesheet(parse(".a { color: green; @layer base { color: red } }"));
        check(c.resolve(a)["color"] == "green", "cascade: nested layer loses to the unlayered parent");
    }
    {
        // Later-declared layers win: `top` is declared after `base`.
        Cascade c;
        c.addStylesheet(parse("@layer base, top; .a { @layer top { color: blue } @layer base { color: red } }"));
        check(c.resolve(a)["color"] == "blue", "cascade: nested layers follow the declared order");
    }
    {
        MediaContext narrow; narrow.viewportWidth = 400;
        MediaContext wide; wide.viewportWidth = 800;
        auto sheet = parse("@media (min-width: 600px) { .a { @layer base { color: blue } } }");
        Cascade c1; c1.addStylesheet(sheet, nullptr, &narrow);
        Cascade c2; c2.addStylesheet(sheet, nullptr, &wide);
        check(c1.resolve(a)["color"] != "blue", "cascade: layer in @media, media fails");
        check(c2.resolve(a)["color"] == "blue", "cascade: layer in @media, media matches");
    }
    {
        // A layer's own rules beat its sublayers', even when the parent's
        // order was declared up front (sublayer created after the parent).
        Cascade c;
        c.addStylesheet(parse("@layer b, a; @layer a { .a { color: green } @layer x { .a { color: red } } }"));
        check(c.resolve(a)["color"] == "green", "cascade: a layer's own rules beat its sublayer");
    }
    {
        // Sublayers sit where their parent sits: a.y is declared after b,
        // but a is before b, so b wins.
        Cascade c;
        c.addStylesheet(parse("@layer a { @layer x { .a { color: red } } } @layer b { .a { color: blue } } "
                              "@layer a { @layer y { .a { color: red } } }"));
        check(c.resolve(a)["color"] == "blue", "cascade: a later sublayer of an earlier layer loses");
    }
    {
        // `@layer a.b` declares `a` first, so a's own rules outrank a.b.
        Cascade c;
        c.addStylesheet(parse("@layer a.b { .a { color: red } } @layer a { .a { color: green } }"));
        check(c.resolve(a)["color"] == "green", "cascade: a dotted layer name declares its parent first");
    }
    {
        // !important reverses the layer order, hierarchically too.
        Cascade c;
        c.addStylesheet(parse("@layer a { .a { color: green !important } @layer x { .a { color: red !important } } }"));
        check(c.resolve(a)["color"] == "red", "cascade: !important in a sublayer beats its parent's");
    }
}

void testHostileNesting() {
    printf("--- Nesting: hostile depth and expansion ---\n");
    // Deep block nesting: dropped past the bound, not a stack overflow.
    std::string deep;
    for (int i = 0; i < 100000; i++) deep += ".a{";
    deep += "color:red";
    for (int i = 0; i < 100000; i++) deep += "}";
    deep += " .ok { color: blue }";
    auto s = parse(deep);
    check(!s.rules.empty() && s.rules.back().selector == ".ok", "100000-deep nesting parses; later rules survive");

    // A geometric cross product is dropped rather than built.
    std::string wide = ".a, .b, .c, .d";
    for (int i = 0; i < 12; i++) wide += " { & .a, & .b, & .c, & .d";
    wide += " { color: red }";
    for (int i = 0; i < 12; i++) wide += " }";
    wide += " .ok { color: blue }";
    s = parse(wide);
    check(!s.rules.empty() && s.rules.back().selector == ".ok", "4^13 selector expansion is bounded");

    std::string dup = ".a";
    for (int i = 0; i < 60; i++) dup += " { & &";
    dup += " { color: red }";
    for (int i = 0; i < 60; i++) dup += " }";
    s = parse(dup);
    check(s.rules.size() < 64, "`& &` doubling is bounded");

    // Functional pseudo-class arguments nest under a bound too.
    for (const char* fn : {":not(", ":is(", ":has(", ":where("}) {
        std::string nested;
        for (int i = 0; i < 100000; i++) nested += fn;
        nested += ".a";
        for (int i = 0; i < 100000; i++) nested += ")";
        auto list = parseSelectorList(nested);
        check(list.size() == 1, (std::string("100000-deep ") + fn + ") parses").c_str());
        MockElement e; e.tag = "div"; e.classes = "a";
        (void)list[0].matches(e);
    }
    check(parseSelectorList(":not(:not(.a))").size() == 1, "shallow :not(:not()) still parses");
    {
        MockElement e; e.tag = "div"; e.classes = "a";
        MockElement f; f.tag = "div"; f.classes = "b";
        auto l = parseSelectorList(":is(:is(:is(.a)))");
        check(l.size() == 1 && l[0].matches(e) && !l[0].matches(f), "shallow :is nesting still matches");
    }
}

void testMediaInContainer() {
    printf("--- Nesting: @media inside @container ---\n");
    auto s = parse("@container card (min-width: 300px) { .a { color: red } "
                   "@media (min-width: 600px) { .a { color: blue } } }");
    check(s.containerBlocks.size() == 2, "@media in @container: two container blocks");
    bool found = false;
    for (auto& cb : s.containerBlocks)
        if (cb.name == "card" && cb.condition == "(min-width: 300px)" &&
            cb.mediaConditions.size() == 1 && cb.mediaConditions[0] == "(min-width: 600px)" &&
            sel(cb.rules, 0) == ".a") found = true;
    check(found, "@media in @container: the inner block keeps the query and adds the condition");

    s = parse(".a { @container (min-width: 1px) { @media print { color: red } } }");
    check(s.containerBlocks.size() == 1 && s.containerBlocks[0].mediaConditions.size() == 1 &&
              s.containerBlocks[0].mediaConditions[0] == "print" &&
              sel(s.containerBlocks[0].rules, 0) == ".a",
          "@media in @container in a style rule");
    s = parse("@media screen { @container (min-width: 1px) { .a { color: red } } }");
    check(s.containerBlocks.size() == 1 && s.containerBlocks[0].mediaConditions.size() == 1 &&
              s.containerBlocks[0].mediaConditions[0] == "screen",
          "@container inside @media carries the media condition");

    MockElement box; box.tag = "div"; box.contType = "inline-size"; box.contInlineSize = 500;
    MockElement a; a.tag = "div"; a.classes = "a";
    box.addChild(&a);
    MediaContext narrow; narrow.viewportWidth = 400;
    MediaContext wide; wide.viewportWidth = 800;
    auto sheet = parse("@container (min-width: 300px) { .a { color: red } "
                       "@media (min-width: 600px) { .a { color: blue } } .a { background: green } }");
    Cascade c1; c1.addStylesheet(sheet, nullptr, &narrow);
    Cascade c2; c2.addStylesheet(sheet, nullptr, &wide);
    auto s1 = c1.resolve(a), s2 = c2.resolve(a);
    check(s1["color"] == "red", "cascade: @media in @container, media fails");
    check(s2["color"] == "blue", "cascade: @media in @container, media matches");
    check(s1["background-color"] == "green" && s2["background-color"] == "green",
          "cascade: container rules after a nested @media still apply");
    auto sheet2 = parse("@container (min-width: 300px) { @media (min-width: 600px) { .a { color: blue } } "
                        ".a { color: red } }");
    Cascade c3; c3.addStylesheet(sheet2, nullptr, &wide);
    check(c3.resolve(a)["color"] == "red", "cascade: container rules keep source order around @media");
}

void testTopLevelAmpersand() {
    printf("--- Nesting: top-level & ---\n");
    auto s = parse("& { color: red }");
    check(s.rules.size() == 1 && sel(s.rules, 0) == ":scope", "top-level & is :scope");
    s = parse("& > .b { color: red }");
    check(sel(s.rules, 0) == ":scope > .b", "top-level & > .b");
    s = parse("&.x { color: red }");
    check(sel(s.rules, 0) == ":scope.x", "top-level &.x");
    s = parse(".a, & .b { color: red }");
    check(sel(s.rules, 0) == ".a, :scope .b", "top-level & in one alternative leaves the others");
    s = parse("& { .c { color: red } }");
    check(sel(s.rules, 0) == ":scope .c", "rules nested in a top-level & resolve against :scope");
    s = parse("[data-x=\"&\"] { color: red }");
    check(sel(s.rules, 0) == "[data-x=\"&\"]", "an & inside a string is not the nesting selector");

    MockElement root; root.tag = "html";
    MockElement body; body.tag = "body"; body.classes = "b";
    root.addChild(&body);
    Cascade c;
    c.addStylesheet(parse("& { color: red } & > .b { background: blue }"));
    check(c.resolve(root)["color"] == "red", "cascade: top-level & matches the root");
    check(c.resolve(body)["color"] != "red", "cascade: top-level & does not match a child");
    check(c.resolve(body)["background-color"] == "blue", "cascade: & > .b matches the root's child");
}

} // namespace

void testNesting() {
    testSelectorResolution();
    testDeclarationInterleaving();
    testNestedConditionals();
    testNestingCascade();
    testNestedLayer();
    testMediaInContainer();
    testTopLevelAmpersand();
    testHostileNesting();
}
