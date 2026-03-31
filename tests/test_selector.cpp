#include "test_selector.h"
#include "test_helpers.h"
#include "css/selector.h"

using namespace htmlayout::css;

static void testSimpleTag() {
    printf("--- Selector: simple tag ---\n");
    MockElement div; div.tag = "div";
    MockElement span; span.tag = "span";
    auto sel = parseSelector("div");
    check(sel.matches(div), "div matches div");
    check(!sel.matches(span), "div doesn't match span");
}

static void testSimpleClass() {
    printf("--- Selector: simple class ---\n");
    MockElement e; e.tag = "div"; e.classes = "box highlight";
    MockElement e2; e2.tag = "div"; e2.classes = "other";
    auto sel = parseSelector(".box");
    check(sel.matches(e), ".box matches element with class 'box highlight'");
    check(!sel.matches(e2), ".box doesn't match element with class 'other'");
    auto sel2 = parseSelector(".highlight");
    check(sel2.matches(e), ".highlight matches element with class 'box highlight'");
}

static void testSimpleId() {
    printf("--- Selector: simple id ---\n");
    MockElement e; e.tag = "div"; e.elemId = "main";
    MockElement e2; e2.tag = "div"; e2.elemId = "sidebar";
    auto sel = parseSelector("#main");
    check(sel.matches(e), "#main matches element with id 'main'");
    check(!sel.matches(e2), "#main doesn't match element with id 'sidebar'");
}

static void testUniversal() {
    printf("--- Selector: universal ---\n");
    MockElement div; div.tag = "div";
    MockElement span; span.tag = "span";
    auto sel = parseSelector("*");
    check(sel.matches(div), "* matches div");
    check(sel.matches(span), "* matches span");
}

static void testCompound() {
    printf("--- Selector: compound ---\n");
    MockElement e; e.tag = "div"; e.classes = "box"; e.elemId = "main";
    MockElement e2; e2.tag = "span"; e2.classes = "box"; e2.elemId = "main";
    MockElement e3; e3.tag = "div"; e3.classes = "other"; e3.elemId = "main";
    auto sel = parseSelector("div.box#main");
    check(sel.matches(e), "div.box#main matches matching element");
    check(!sel.matches(e2), "div.box#main doesn't match span");
    check(!sel.matches(e3), "div.box#main doesn't match wrong class");
}

static void testMultipleClasses() {
    printf("--- Selector: multiple classes ---\n");
    MockElement e; e.tag = "div"; e.classes = "a b c";
    MockElement e2; e2.tag = "div"; e2.classes = "a c";
    auto sel = parseSelector(".a.b");
    check(sel.matches(e), ".a.b matches element with 'a b c'");
    check(!sel.matches(e2), ".a.b doesn't match element with 'a c'");
}

static void testAttribute() {
    printf("--- Selector: attribute ---\n");
    MockElement e; e.tag = "a";
    e.attrs["href"] = "https://example.com";
    e.attrs["data-type"] = "link primary";
    e.attrs["lang"] = "en-US";

    auto sel1 = parseSelector("[href]");
    check(sel1.matches(e), "[href] matches element with href");
    MockElement e2; e2.tag = "span";
    check(!sel1.matches(e2), "[href] doesn't match element without href");

    check(parseSelector("[href=\"https://example.com\"]").matches(e), "[href=url] exact match");
    check(parseSelector("[data-type~=\"primary\"]").matches(e), "[data-type~=primary] includes match");
    check(parseSelector("[lang|=\"en\"]").matches(e), "[lang|=en] matches en-US");
    check(parseSelector("[href^=\"https\"]").matches(e), "[href^=https] prefix match");
    check(parseSelector("[href$=\".com\"]").matches(e), "[href$=.com] suffix match");
    check(parseSelector("[href*=\"example\"]").matches(e), "[href*=example] substring match");
}

static void testDescendant() {
    printf("--- Selector: descendant combinator ---\n");
    MockElement div; div.tag = "div"; div.classes = "nav";
    MockElement ul; ul.tag = "ul";
    MockElement li; li.tag = "li"; li.classes = "item";
    div.addChild(&ul);
    ul.addChild(&li);

    check(parseSelector("div li").matches(li), "div li matches descendant");
    check(!parseSelector("div li").matches(ul), "div li doesn't match ul");
    check(parseSelector(".nav .item").matches(li), ".nav .item matches nested descendant");
}

static void testChild() {
    printf("--- Selector: child combinator ---\n");
    MockElement div; div.tag = "div";
    MockElement ul; ul.tag = "ul";
    MockElement li; li.tag = "li";
    div.addChild(&ul);
    ul.addChild(&li);

    check(parseSelector("div > ul").matches(ul), "div > ul matches direct child");
    check(!parseSelector("div > li").matches(li), "div > li doesn't match grandchild");
    check(parseSelector("ul > li").matches(li), "ul > li matches direct child");
}

static void testAdjacentSibling() {
    printf("--- Selector: adjacent sibling ---\n");
    MockElement parent; parent.tag = "div";
    MockElement h1; h1.tag = "h1";
    MockElement p1; p1.tag = "p"; p1.classes = "first";
    MockElement p2; p2.tag = "p"; p2.classes = "second";
    parent.addChild(&h1);
    parent.addChild(&p1);
    parent.addChild(&p2);

    check(parseSelector("h1 + p").matches(p1), "h1 + p matches adjacent p");
    check(!parseSelector("h1 + p").matches(p2), "h1 + p doesn't match non-adjacent p");
}

static void testGeneralSibling() {
    printf("--- Selector: general sibling ---\n");
    MockElement parent; parent.tag = "div";
    MockElement h1; h1.tag = "h1";
    MockElement p1; p1.tag = "p";
    MockElement p2; p2.tag = "p";
    parent.addChild(&h1);
    parent.addChild(&p1);
    parent.addChild(&p2);

    check(parseSelector("h1 ~ p").matches(p1), "h1 ~ p matches first p sibling");
    check(parseSelector("h1 ~ p").matches(p2), "h1 ~ p matches second p sibling");
    check(!parseSelector("h1 ~ p").matches(h1), "h1 ~ p doesn't match h1 itself");
}

static void testPseudoClasses() {
    printf("--- Selector: pseudo-classes ---\n");
    MockElement parent; parent.tag = "ul";
    MockElement li1; li1.tag = "li";
    MockElement li2; li2.tag = "li";
    MockElement li3; li3.tag = "li";
    parent.addChild(&li1);
    parent.addChild(&li2);
    parent.addChild(&li3);

    check(parseSelector(":first-child").matches(li1), ":first-child matches first li");
    check(!parseSelector(":first-child").matches(li2), ":first-child doesn't match second li");
    check(parseSelector(":last-child").matches(li3), ":last-child matches last li");
    check(!parseSelector(":last-child").matches(li1), ":last-child doesn't match first li");

    check(!parseSelector(":nth-child(2)").matches(li1), ":nth-child(2) doesn't match 1st");
    check(parseSelector(":nth-child(2)").matches(li2), ":nth-child(2) matches 2nd");
    check(!parseSelector(":nth-child(2)").matches(li3), ":nth-child(2) doesn't match 3rd");

    check(parseSelector(":nth-child(odd)").matches(li1), ":nth-child(odd) matches 1st");
    check(!parseSelector(":nth-child(odd)").matches(li2), ":nth-child(odd) doesn't match 2nd");
    check(parseSelector(":nth-child(odd)").matches(li3), ":nth-child(odd) matches 3rd");

    check(!parseSelector(":nth-child(even)").matches(li1), ":nth-child(even) doesn't match 1st");
    check(parseSelector(":nth-child(even)").matches(li2), ":nth-child(even) matches 2nd");
}

static void testNot() {
    printf("--- Selector: :not() ---\n");
    MockElement div; div.tag = "div"; div.classes = "box";
    MockElement span; span.tag = "span"; span.classes = "box";

    check(parseSelector("div:not(.hidden)").matches(div), "div:not(.hidden) matches div.box");
    MockElement divHidden; divHidden.tag = "div"; divHidden.classes = "hidden";
    check(!parseSelector("div:not(.hidden)").matches(divHidden), "div:not(.hidden) doesn't match div.hidden");

    check(parseSelector(":not(span)").matches(div), ":not(span) matches div");
    check(!parseSelector(":not(span)").matches(span), ":not(span) doesn't match span");
}

static void testDynamic() {
    printf("--- Selector: dynamic pseudo-classes ---\n");
    MockElement a; a.tag = "a"; a.hovered = true;
    MockElement a2; a2.tag = "a";
    check(parseSelector("a:hover").matches(a), "a:hover matches hovered a");
    check(!parseSelector("a:hover").matches(a2), "a:hover doesn't match non-hovered a");

    MockElement input; input.tag = "input"; input.focused = true;
    check(parseSelector("input:focus").matches(input), "input:focus matches focused input");
}

static void testEmpty() {
    printf("--- Selector: :empty ---\n");
    MockElement empty; empty.tag = "div";
    MockElement notEmpty; notEmpty.tag = "div";
    MockElement child; child.tag = "span";
    notEmpty.addChild(&child);
    check(parseSelector(":empty").matches(empty), ":empty matches childless element");
    check(!parseSelector(":empty").matches(notEmpty), ":empty doesn't match element with children");
}

static void testCommaList() {
    printf("--- Selector: comma-separated list ---\n");
    auto selectors = parseSelectorList("h1, h2, .title");
    check(selectors.size() == 3, "3 selectors from comma list");
    MockElement h1; h1.tag = "h1";
    MockElement h2; h2.tag = "h2";
    MockElement div; div.tag = "div"; div.classes = "title";
    MockElement span; span.tag = "span";
    check(selectors[0].matches(h1), "h1 selector matches h1");
    check(selectors[1].matches(h2), "h2 selector matches h2");
    check(selectors[2].matches(div), ".title selector matches div.title");
    check(!selectors[0].matches(span), "h1 selector doesn't match span");
}

static void testSpecificity() {
    printf("--- Selector: specificity ---\n");
    check(calculateSpecificity("*") == 0x000000, "* specificity = 0,0,0");
    check(calculateSpecificity("div") == 0x000001, "div specificity = 0,0,1");
    check(calculateSpecificity(".box") == 0x000100, ".box specificity = 0,1,0");
    check(calculateSpecificity("#main") == 0x010000, "#main specificity = 1,0,0");
    check(calculateSpecificity("div.box") == 0x000101, "div.box specificity = 0,1,1");
    check(calculateSpecificity("#main.box div") == 0x010101, "#main.box div = 1,1,1");
    check(calculateSpecificity(".a.b") == 0x000200, ".a.b specificity = 0,2,0");
    check(calculateSpecificity("div > .box") == 0x000101, "div > .box = 0,1,1");
    check(calculateSpecificity("div") < calculateSpecificity(".box"), "tag < class specificity");
    check(calculateSpecificity(".box") < calculateSpecificity("#main"), "class < id specificity");
}

static void testComplexChain() {
    printf("--- Selector: complex chains ---\n");
    MockElement body; body.tag = "body";
    MockElement div; div.tag = "div"; div.classes = "container";
    MockElement ul; ul.tag = "ul"; ul.classes = "nav";
    MockElement li; li.tag = "li"; li.classes = "item";
    MockElement a; a.tag = "a"; a.classes = "link";
    body.addChild(&div);
    div.addChild(&ul);
    ul.addChild(&li);
    li.addChild(&a);

    check(parseSelector("body .container .item a").matches(a), "descendant chain matches");
    check(parseSelector("body > div > ul > li > a").matches(a), "child chain matches");
    check(!parseSelector("body > div > ul > li > span").matches(a), "wrong tag doesn't match");
    check(parseSelector(".container .link").matches(a), "class chain across depth");
    check(!parseSelector("body > .link").matches(a), "non-direct child doesn't match");
}

static void testOnlyChild() {
    printf("--- Selector: :only-child ---\n");
    MockElement parent1; parent1.tag = "div";
    MockElement only; only.tag = "span";
    parent1.addChild(&only);
    MockElement parent2; parent2.tag = "div";
    MockElement c1; c1.tag = "span";
    MockElement c2; c2.tag = "span";
    parent2.addChild(&c1);
    parent2.addChild(&c2);
    check(parseSelector(":only-child").matches(only), ":only-child matches sole child");
    check(!parseSelector(":only-child").matches(c1), ":only-child doesn't match when siblings exist");
}

static void testFirstOfType() {
    printf("--- Selector: :first-of-type / :last-of-type ---\n");
    MockElement parent; parent.tag = "div";
    MockElement span1; span1.tag = "span";
    MockElement p1; p1.tag = "p";
    MockElement span2; span2.tag = "span";
    parent.addChild(&span1);
    parent.addChild(&p1);
    parent.addChild(&span2);

    check(parseSelector("span:first-of-type").matches(span1), "span:first-of-type matches first span");
    check(!parseSelector("span:first-of-type").matches(span2), "span:first-of-type doesn't match second span");
    check(!parseSelector("span:last-of-type").matches(span1), "span:last-of-type doesn't match first span");
    check(parseSelector("span:last-of-type").matches(span2), "span:last-of-type matches last span");
}

void testSelector() {
    testSimpleTag();
    testSimpleClass();
    testSimpleId();
    testUniversal();
    testCompound();
    testMultipleClasses();
    testAttribute();
    testDescendant();
    testChild();
    testAdjacentSibling();
    testGeneralSibling();
    testPseudoClasses();
    testNot();
    testDynamic();
    testEmpty();
    testCommaList();
    testSpecificity();
    testComplexChain();
    testOnlyChild();
    testFirstOfType();
}
