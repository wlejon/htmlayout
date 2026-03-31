#include "css/tokenizer.h"
#include "css/parser.h"
#include "css/selector.h"
#include "css/cascade.h"
#include "css/properties.h"
#include "layout/box.h"
#include "layout/formatting_context.h"

// Gumbo HTML parser
#include <gumbo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>

static int g_passed = 0;
static int g_failed = 0;

static void check(bool cond, const char* name) {
    if (cond) {
        printf("  PASS: %s\n", name);
        g_passed++;
    } else {
        printf("  FAIL: %s\n", name);
        g_failed++;
    }
}

// ========== Foundation Tests ==========

static void testFoundation() {
    printf("--- Foundation ---\n");

    const char* html = "<html><body><div class=\"box\">Hello</div></body></html>";
    GumboOutput* output = gumbo_parse(html);
    check(output && output->root, "gumbo: parse HTML");

    GumboNode* root = output->root;
    GumboNode* body = nullptr;
    for (unsigned i = 0; i < root->v.element.children.length; i++) {
        GumboNode* child = static_cast<GumboNode*>(root->v.element.children.data[i]);
        if (child->type == GUMBO_NODE_ELEMENT &&
            child->v.element.tag == GUMBO_TAG_BODY) {
            body = child;
            break;
        }
    }
    check(body != nullptr, "gumbo: find <body>");
    gumbo_destroy_output(&kGumboDefaultOptions, output);

    check(htmlayout::css::knownProperties().size() > 0, "properties: known properties exist");
    check(htmlayout::css::isInherited("color") == true, "properties: color is inherited");
    check(htmlayout::css::isInherited("margin-top") == false, "properties: margin-top not inherited");

    auto sel = htmlayout::css::parseSelector(".box");
    check(sel.raw == ".box", "selector: parse returns raw text");

    check(sizeof(htmlayout::layout::LayoutBox) > 0, "layout: LayoutBox defined");
    check(sizeof(htmlayout::layout::Rect) > 0, "layout: Rect defined");
}

// ========== Tokenizer Tests ==========

using namespace htmlayout::css;

static void testTokenizerBasicPunctuation() {
    printf("--- Tokenizer: punctuation ---\n");
    auto tokens = tokenize("{}[]():;,");
    check(tokens.size() == 10, "9 punctuation tokens + EOF");
    check(tokens[0].type == TokenType::LeftBrace, "LeftBrace");
    check(tokens[1].type == TokenType::RightBrace, "RightBrace");
    check(tokens[2].type == TokenType::LeftBracket, "LeftBracket");
    check(tokens[3].type == TokenType::RightBracket, "RightBracket");
    check(tokens[4].type == TokenType::LeftParen, "LeftParen");
    check(tokens[5].type == TokenType::RightParen, "RightParen");
    check(tokens[6].type == TokenType::Colon, "Colon");
    check(tokens[7].type == TokenType::Semicolon, "Semicolon");
    check(tokens[8].type == TokenType::Comma, "Comma");
    check(tokens[9].type == TokenType::EndOfFile, "EOF");
}

static void testTokenizerIdents() {
    printf("--- Tokenizer: identifiers ---\n");
    auto tokens = tokenize("color margin-top _private");
    check(tokens.size() == 6, "3 idents + 2 whitespace + EOF");
    check(tokens[0].type == TokenType::Ident && tokens[0].value == "color", "ident: color");
    check(tokens[2].type == TokenType::Ident && tokens[2].value == "margin-top", "ident: margin-top");
    check(tokens[4].type == TokenType::Ident && tokens[4].value == "_private", "ident: _private");
}

static void testTokenizerNumbers() {
    printf("--- Tokenizer: numbers ---\n");
    auto tokens = tokenize("42 3.14 10px 50% 2em");
    check(tokens[0].type == TokenType::Number, "42 is Number");
    check(tokens[0].numeric == 42.0, "42 numeric value");
    check(tokens[2].type == TokenType::Number, "3.14 is Number");
    check(std::abs(tokens[2].numeric - 3.14) < 0.001, "3.14 numeric value");
    check(tokens[4].type == TokenType::Dimension, "10px is Dimension");
    check(tokens[4].numeric == 10.0, "10px numeric = 10");
    check(tokens[4].unit == "px", "10px unit = px");
    check(tokens[6].type == TokenType::Percentage, "50% is Percentage");
    check(tokens[6].numeric == 50.0, "50% numeric = 50");
    check(tokens[8].type == TokenType::Dimension, "2em is Dimension");
    check(tokens[8].unit == "em", "2em unit = em");
}

static void testTokenizerStrings() {
    printf("--- Tokenizer: strings ---\n");
    auto tokens = tokenize("\"hello\" 'world'");
    check(tokens[0].type == TokenType::String, "double-quoted string");
    check(tokens[0].value == "hello", "double-quoted value");
    check(tokens[2].type == TokenType::String, "single-quoted string");
    check(tokens[2].value == "world", "single-quoted value");
}

static void testTokenizerHash() {
    printf("--- Tokenizer: hash ---\n");
    auto tokens = tokenize("#myId #ff0000");
    check(tokens[0].type == TokenType::Hash, "hash token");
    check(tokens[0].value == "myId", "hash value = myId");
    check(tokens[2].type == TokenType::Hash, "hex color hash");
    check(tokens[2].value == "ff0000", "hex color value");
}

static void testTokenizerDelimiters() {
    printf("--- Tokenizer: delimiters ---\n");
    auto tokens = tokenize(".box > .child + .sibling ~ .cousin * ");
    check(tokens[0].type == TokenType::Delim && tokens[0].value == ".", "dot delim");
    check(tokens[1].type == TokenType::Ident && tokens[1].value == "box", "ident after dot");
    bool foundGt = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Delim && t.value == ">") foundGt = true;
    }
    check(foundGt, "greater-than delim");
    bool foundStar = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Delim && t.value == "*") foundStar = true;
    }
    check(foundStar, "star delim");
}

static void testTokenizerAtKeyword() {
    printf("--- Tokenizer: at-keyword ---\n");
    auto tokens = tokenize("@media @import");
    check(tokens[0].type == TokenType::AtKeyword && tokens[0].value == "media", "@media");
    check(tokens[2].type == TokenType::AtKeyword && tokens[2].value == "import", "@import");
}

static void testTokenizerFunction() {
    printf("--- Tokenizer: functions ---\n");
    auto tokens = tokenize("rgb(255, 0, 0) calc(100% - 20px)");
    check(tokens[0].type == TokenType::Function && tokens[0].value == "rgb", "rgb function");
    check(tokens[1].type == TokenType::Number, "255 inside rgb");
}

static void testTokenizerComments() {
    printf("--- Tokenizer: comments ---\n");
    auto tokens = tokenize("color /* this is a comment */ : red");
    check(tokens[0].type == TokenType::Ident && tokens[0].value == "color", "ident before comment");
    bool foundComment = false;
    for (auto& t : tokens) {
        if (t.value.find("comment") != std::string::npos && t.type != TokenType::Ident) {
            foundComment = true;
        }
    }
    check(!foundComment, "comment text not in tokens");
    bool foundColon = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Colon) foundColon = true;
    }
    check(foundColon, "colon present after comment");
}

static void testTokenizerNegativeNumbers() {
    printf("--- Tokenizer: negative numbers ---\n");
    auto tokens = tokenize("-10px -3.5em");
    check(tokens[0].type == TokenType::Dimension, "-10px is Dimension");
    check(tokens[0].numeric == -10.0, "-10px numeric = -10");
    check(tokens[0].unit == "px", "-10px unit = px");
    check(tokens[2].type == TokenType::Dimension, "-3.5em is Dimension");
    check(std::abs(tokens[2].numeric - (-3.5)) < 0.001, "-3.5em numeric = -3.5");
}

static void testTokenizerFullRule() {
    printf("--- Tokenizer: full rule ---\n");
    auto tokens = tokenize(".box { color: red; }");
    check(tokens.size() > 0, "non-empty token list");
    check(tokens[0].type == TokenType::Delim && tokens[0].value == ".", "starts with dot");
    check(tokens[1].type == TokenType::Ident && tokens[1].value == "box", "class name");
    bool foundBrace = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::LeftBrace) foundBrace = true;
    }
    check(foundBrace, "has left brace");
}

// ========== Parser Tests ==========

static void testParserSingleRule() {
    printf("--- Parser: single rule ---\n");
    auto sheet = parse(".box { color: red; margin: 10px; }");
    check(sheet.rules.size() == 1, "1 rule");
    check(sheet.rules[0].selector == ".box", "selector is .box");
    check(sheet.rules[0].declarations.size() == 2, "2 declarations");
    check(sheet.rules[0].declarations[0].property == "color", "first prop is color");
    check(sheet.rules[0].declarations[0].value == "red", "color value is red");
    check(sheet.rules[0].declarations[1].property == "margin", "second prop is margin");
    check(sheet.rules[0].declarations[1].value == "10px", "margin value is 10px");
}

static void testParserMultipleRules() {
    printf("--- Parser: multiple rules ---\n");
    auto sheet = parse(
        "h1 { font-size: 24px; }\n"
        ".container { width: 960px; margin: 0 auto; }\n"
        "#main { padding: 20px; }"
    );
    check(sheet.rules.size() == 3, "3 rules");
    check(sheet.rules[0].selector == "h1", "first selector: h1");
    check(sheet.rules[1].selector == ".container", "second selector: .container");
    check(sheet.rules[2].selector == "#main", "third selector: #main");
    check(sheet.rules[1].declarations.size() == 2, "container has 2 declarations");
}

static void testParserImportant() {
    printf("--- Parser: !important ---\n");
    auto sheet = parse(".urgent { color: red !important; font-size: 16px; }");
    check(sheet.rules.size() == 1, "1 rule");
    auto& decls = sheet.rules[0].declarations;
    check(decls.size() == 2, "2 declarations");
    check(decls[0].property == "color", "color property");
    check(decls[0].value == "red", "color value (without !important)");
    check(decls[0].important == true, "color is !important");
    check(decls[1].important == false, "font-size is not !important");
}

static void testParserCompoundSelector() {
    printf("--- Parser: compound selectors ---\n");
    auto sheet = parse("div.box#main > .child { display: flex; }");
    check(sheet.rules.size() == 1, "1 rule");
    check(sheet.rules[0].selector == "div.box#main > .child", "compound selector preserved");
}

static void testParserInlineStyle() {
    printf("--- Parser: inline style ---\n");
    auto decls = parseInlineStyle("color: blue; font-weight: bold; margin: 10px 20px");
    check(decls.size() == 3, "3 inline declarations");
    check(decls[0].property == "color", "inline: color");
    check(decls[0].value == "blue", "inline: blue");
    check(decls[1].property == "font-weight", "inline: font-weight");
    check(decls[1].value == "bold", "inline: bold");
    check(decls[2].property == "margin", "inline: margin");
    check(decls[2].value == "10px 20px", "inline: 10px 20px");
}

static void testParserEmptyInput() {
    printf("--- Parser: empty/edge cases ---\n");
    auto sheet1 = parse("");
    check(sheet1.rules.empty(), "empty string -> 0 rules");
    auto sheet2 = parse("   \n\t  ");
    check(sheet2.rules.empty(), "whitespace only -> 0 rules");
    auto decls = parseInlineStyle("");
    check(decls.empty(), "empty inline style -> 0 declarations");
}

static void testParserNoSemicolonLastDecl() {
    printf("--- Parser: no trailing semicolon ---\n");
    auto sheet = parse("p { color: red; font-size: 14px }");
    check(sheet.rules.size() == 1, "1 rule");
    check(sheet.rules[0].declarations.size() == 2, "2 declarations (no trailing ;)");
    check(sheet.rules[0].declarations[1].value == "14px", "last decl value ok");
}

static void testParserAtRuleSkipped() {
    printf("--- Parser: at-rules skipped ---\n");
    auto sheet = parse(
        "@charset \"UTF-8\";\n"
        ".box { color: red; }\n"
        "@media screen { .x { display: none; } }\n"
        "p { margin: 0; }"
    );
    check(sheet.rules.size() == 2, "2 rules (at-rules skipped)");
    check(sheet.rules[0].selector == ".box", "first rule is .box");
    check(sheet.rules[1].selector == "p", "second rule is p");
}

static void testParserFunctionValues() {
    printf("--- Parser: function values ---\n");
    auto sheet = parse(".bg { background: rgb(255, 128, 0); width: calc(100% - 20px); }");
    check(sheet.rules.size() == 1, "1 rule");
    auto& decls = sheet.rules[0].declarations;
    check(decls.size() == 2, "2 declarations");
    check(decls[0].property == "background", "background property");
    check(decls[0].value.find("rgb") != std::string::npos, "background value contains rgb");
    check(decls[1].property == "width", "width property");
    check(decls[1].value.find("calc") != std::string::npos, "width value contains calc");
}

static void testParserCommaSelector() {
    printf("--- Parser: comma-separated selector ---\n");
    auto sheet = parse("h1, h2, h3 { font-weight: bold; }");
    check(sheet.rules.size() == 1, "1 rule (grouped selector)");
    check(sheet.rules[0].selector == "h1, h2, h3", "comma selector preserved");
}

static void testParserMultiValueProperty() {
    printf("--- Parser: multi-value properties ---\n");
    auto sheet = parse(".box { border: 1px solid black; margin: 10px 20px 10px 20px; }");
    check(sheet.rules.size() == 1, "1 rule");
    auto& decls = sheet.rules[0].declarations;
    check(decls[0].value == "1px solid black", "border shorthand value");
    check(decls[1].value == "10px 20px 10px 20px", "margin 4-value shorthand");
}

static void testRoundTrip() {
    printf("--- Round-trip tests ---\n");
    const char* css =
        "body {\n"
        "    margin: 0;\n"
        "    padding: 0;\n"
        "    font-family: Arial, sans-serif;\n"
        "    font-size: 16px;\n"
        "    color: #333;\n"
        "}\n"
        "\n"
        ".container {\n"
        "    max-width: 1200px;\n"
        "    margin: 0 auto;\n"
        "    padding: 0 20px;\n"
        "}\n"
        "\n"
        ".header {\n"
        "    display: flex;\n"
        "    justify-content: space-between;\n"
        "    align-items: center;\n"
        "    height: 60px;\n"
        "    background-color: #fff;\n"
        "    border-bottom: 1px solid #eee;\n"
        "}\n"
        "\n"
        ".nav a {\n"
        "    text-decoration: none;\n"
        "    color: inherit;\n"
        "    padding: 8px 16px;\n"
        "}\n"
        "\n"
        ".nav a:hover {\n"
        "    color: #0066cc;\n"
        "}\n";

    auto tokens = tokenize(css);
    check(tokens.size() > 50, "realistic CSS produces many tokens");

    auto sheet = parse(css);
    check(sheet.rules.size() == 5, "5 rules in realistic stylesheet");
    check(sheet.rules[0].selector == "body", "rule 0: body");
    check(sheet.rules[0].declarations.size() == 5, "body has 5 declarations");
    check(sheet.rules[1].selector == ".container", "rule 1: .container");
    check(sheet.rules[1].declarations.size() == 3, "container has 3 declarations");
    check(sheet.rules[2].selector == ".header", "rule 2: .header");
    check(sheet.rules[2].declarations.size() == 6, "header has 6 declarations");
    check(sheet.rules[3].selector == ".nav a", "rule 3: .nav a");
    check(sheet.rules[3].declarations.size() == 3, "nav a has 3 declarations");
    check(sheet.rules[4].selector == ".nav a:hover", "rule 4: .nav a:hover");
    check(sheet.rules[4].declarations.size() == 1, "nav a:hover has 1 declaration");

    bool foundFlex = false;
    for (auto& d : sheet.rules[2].declarations) {
        if (d.property == "display" && d.value == "flex") foundFlex = true;
    }
    check(foundFlex, "header display: flex");

    bool foundMaxWidth = false;
    for (auto& d : sheet.rules[1].declarations) {
        if (d.property == "max-width" && d.value == "1200px") foundMaxWidth = true;
    }
    check(foundMaxWidth, "container max-width: 1200px");
}

// ========== Mock DOM for Selector Tests ==========

// A simple mock element for testing selector matching.
struct MockElement : public ElementRef {
    std::string tag;
    std::string elemId;
    std::string classes;  // space-separated
    std::unordered_map<std::string, std::string> attrs;
    MockElement* parentElem = nullptr;
    std::vector<MockElement*> childElems;
    bool hovered = false;
    bool focused = false;
    bool active = false;
    void* scopePtr = nullptr;

    std::string tagName() const override { return tag; }
    std::string id() const override { return elemId; }
    std::string className() const override { return classes; }

    std::string getAttribute(const std::string& name) const override {
        auto it = attrs.find(name);
        return it != attrs.end() ? it->second : "";
    }
    bool hasAttribute(const std::string& name) const override {
        return attrs.count(name) > 0;
    }
    ElementRef* parent() const override { return parentElem; }
    std::vector<ElementRef*> children() const override {
        return {childElems.begin(), childElems.end()};
    }
    int childIndex() const override {
        if (!parentElem) return 0;
        for (int i = 0; i < static_cast<int>(parentElem->childElems.size()); i++) {
            if (parentElem->childElems[i] == this) return i;
        }
        return 0;
    }
    int childIndexOfType() const override {
        if (!parentElem) return 0;
        int idx = 0;
        for (auto* c : parentElem->childElems) {
            if (c == this) return idx;
            if (c->tag == tag) idx++;
        }
        return 0;
    }
    int siblingCount() const override {
        if (!parentElem) return 1;
        return static_cast<int>(parentElem->childElems.size());
    }
    int siblingCountOfType() const override {
        if (!parentElem) return 1;
        int count = 0;
        for (auto* c : parentElem->childElems) {
            if (c->tag == tag) count++;
        }
        return count;
    }
    bool isHovered() const override { return hovered; }
    bool isFocused() const override { return focused; }
    bool isActive() const override { return active; }
    void* scope() const override { return scopePtr; }

    void addChild(MockElement* child) {
        child->parentElem = this;
        childElems.push_back(child);
    }
};

// ========== Selector Tests ==========

static void testSelectorSimpleTag() {
    printf("--- Selector: simple tag ---\n");
    MockElement div; div.tag = "div";
    MockElement span; span.tag = "span";

    auto sel = parseSelector("div");
    check(sel.matches(div), "div matches div");
    check(!sel.matches(span), "div doesn't match span");
}

static void testSelectorSimpleClass() {
    printf("--- Selector: simple class ---\n");
    MockElement e; e.tag = "div"; e.classes = "box highlight";
    MockElement e2; e2.tag = "div"; e2.classes = "other";

    auto sel = parseSelector(".box");
    check(sel.matches(e), ".box matches element with class 'box highlight'");
    check(!sel.matches(e2), ".box doesn't match element with class 'other'");

    auto sel2 = parseSelector(".highlight");
    check(sel2.matches(e), ".highlight matches element with class 'box highlight'");
}

static void testSelectorSimpleId() {
    printf("--- Selector: simple id ---\n");
    MockElement e; e.tag = "div"; e.elemId = "main";
    MockElement e2; e2.tag = "div"; e2.elemId = "sidebar";

    auto sel = parseSelector("#main");
    check(sel.matches(e), "#main matches element with id 'main'");
    check(!sel.matches(e2), "#main doesn't match element with id 'sidebar'");
}

static void testSelectorUniversal() {
    printf("--- Selector: universal ---\n");
    MockElement div; div.tag = "div";
    MockElement span; span.tag = "span";

    auto sel = parseSelector("*");
    check(sel.matches(div), "* matches div");
    check(sel.matches(span), "* matches span");
}

static void testSelectorCompound() {
    printf("--- Selector: compound ---\n");
    MockElement e; e.tag = "div"; e.classes = "box"; e.elemId = "main";
    MockElement e2; e2.tag = "span"; e2.classes = "box"; e2.elemId = "main";
    MockElement e3; e3.tag = "div"; e3.classes = "other"; e3.elemId = "main";

    auto sel = parseSelector("div.box#main");
    check(sel.matches(e), "div.box#main matches matching element");
    check(!sel.matches(e2), "div.box#main doesn't match span");
    check(!sel.matches(e3), "div.box#main doesn't match wrong class");
}

static void testSelectorMultipleClasses() {
    printf("--- Selector: multiple classes ---\n");
    MockElement e; e.tag = "div"; e.classes = "a b c";
    MockElement e2; e2.tag = "div"; e2.classes = "a c";

    auto sel = parseSelector(".a.b");
    check(sel.matches(e), ".a.b matches element with 'a b c'");
    check(!sel.matches(e2), ".a.b doesn't match element with 'a c'");
}

static void testSelectorAttribute() {
    printf("--- Selector: attribute ---\n");
    MockElement e; e.tag = "a";
    e.attrs["href"] = "https://example.com";
    e.attrs["data-type"] = "link primary";
    e.attrs["lang"] = "en-US";

    // [attr] exists
    auto sel1 = parseSelector("[href]");
    check(sel1.matches(e), "[href] matches element with href");

    MockElement e2; e2.tag = "span";
    check(!sel1.matches(e2), "[href] doesn't match element without href");

    // [attr=val]
    auto sel2 = parseSelector("[href=\"https://example.com\"]");
    check(sel2.matches(e), "[href=url] exact match");

    // [attr~=val] includes
    auto sel3 = parseSelector("[data-type~=\"primary\"]");
    check(sel3.matches(e), "[data-type~=primary] includes match");

    // [attr|=val] dash match
    auto sel4 = parseSelector("[lang|=\"en\"]");
    check(sel4.matches(e), "[lang|=en] matches en-US");

    // [attr^=val] prefix
    auto sel5 = parseSelector("[href^=\"https\"]");
    check(sel5.matches(e), "[href^=https] prefix match");

    // [attr$=val] suffix
    auto sel6 = parseSelector("[href$=\".com\"]");
    check(sel6.matches(e), "[href$=.com] suffix match");

    // [attr*=val] substring
    auto sel7 = parseSelector("[href*=\"example\"]");
    check(sel7.matches(e), "[href*=example] substring match");
}

static void testSelectorDescendant() {
    printf("--- Selector: descendant combinator ---\n");
    // Build: div > ul > li
    MockElement div; div.tag = "div"; div.classes = "nav";
    MockElement ul; ul.tag = "ul";
    MockElement li; li.tag = "li"; li.classes = "item";

    div.addChild(&ul);
    ul.addChild(&li);

    auto sel = parseSelector("div li");
    check(sel.matches(li), "div li matches descendant");
    check(!sel.matches(ul), "div li doesn't match ul");

    auto sel2 = parseSelector(".nav .item");
    check(sel2.matches(li), ".nav .item matches nested descendant");
}

static void testSelectorChild() {
    printf("--- Selector: child combinator ---\n");
    MockElement div; div.tag = "div";
    MockElement ul; ul.tag = "ul";
    MockElement li; li.tag = "li";

    div.addChild(&ul);
    ul.addChild(&li);

    auto sel = parseSelector("div > ul");
    check(sel.matches(ul), "div > ul matches direct child");

    auto sel2 = parseSelector("div > li");
    check(!sel2.matches(li), "div > li doesn't match grandchild");

    auto sel3 = parseSelector("ul > li");
    check(sel3.matches(li), "ul > li matches direct child");
}

static void testSelectorAdjacentSibling() {
    printf("--- Selector: adjacent sibling ---\n");
    MockElement parent; parent.tag = "div";
    MockElement h1; h1.tag = "h1";
    MockElement p1; p1.tag = "p"; p1.classes = "first";
    MockElement p2; p2.tag = "p"; p2.classes = "second";

    parent.addChild(&h1);
    parent.addChild(&p1);
    parent.addChild(&p2);

    auto sel = parseSelector("h1 + p");
    check(sel.matches(p1), "h1 + p matches adjacent p");
    check(!sel.matches(p2), "h1 + p doesn't match non-adjacent p");
}

static void testSelectorGeneralSibling() {
    printf("--- Selector: general sibling ---\n");
    MockElement parent; parent.tag = "div";
    MockElement h1; h1.tag = "h1";
    MockElement p1; p1.tag = "p";
    MockElement p2; p2.tag = "p";

    parent.addChild(&h1);
    parent.addChild(&p1);
    parent.addChild(&p2);

    auto sel = parseSelector("h1 ~ p");
    check(sel.matches(p1), "h1 ~ p matches first p sibling");
    check(sel.matches(p2), "h1 ~ p matches second p sibling");
    check(!sel.matches(h1), "h1 ~ p doesn't match h1 itself");
}

static void testSelectorPseudoClasses() {
    printf("--- Selector: pseudo-classes ---\n");
    MockElement parent; parent.tag = "ul";
    MockElement li1; li1.tag = "li"; li1.classes = "first";
    MockElement li2; li2.tag = "li"; li2.classes = "second";
    MockElement li3; li3.tag = "li"; li3.classes = "third";

    parent.addChild(&li1);
    parent.addChild(&li2);
    parent.addChild(&li3);

    auto selFirst = parseSelector(":first-child");
    check(selFirst.matches(li1), ":first-child matches first li");
    check(!selFirst.matches(li2), ":first-child doesn't match second li");

    auto selLast = parseSelector(":last-child");
    check(selLast.matches(li3), ":last-child matches last li");
    check(!selLast.matches(li1), ":last-child doesn't match first li");

    // :nth-child
    auto selNth2 = parseSelector(":nth-child(2)");
    check(!selNth2.matches(li1), ":nth-child(2) doesn't match 1st");
    check(selNth2.matches(li2), ":nth-child(2) matches 2nd");
    check(!selNth2.matches(li3), ":nth-child(2) doesn't match 3rd");

    auto selOdd = parseSelector(":nth-child(odd)");
    check(selOdd.matches(li1), ":nth-child(odd) matches 1st");
    check(!selOdd.matches(li2), ":nth-child(odd) doesn't match 2nd");
    check(selOdd.matches(li3), ":nth-child(odd) matches 3rd");

    auto selEven = parseSelector(":nth-child(even)");
    check(!selEven.matches(li1), ":nth-child(even) doesn't match 1st");
    check(selEven.matches(li2), ":nth-child(even) matches 2nd");
}

static void testSelectorNot() {
    printf("--- Selector: :not() ---\n");
    MockElement div; div.tag = "div"; div.classes = "box";
    MockElement span; span.tag = "span"; span.classes = "box";

    auto sel = parseSelector("div:not(.hidden)");
    check(sel.matches(div), "div:not(.hidden) matches div.box");

    MockElement divHidden; divHidden.tag = "div"; divHidden.classes = "hidden";
    check(!sel.matches(divHidden), "div:not(.hidden) doesn't match div.hidden");

    auto sel2 = parseSelector(":not(span)");
    check(sel2.matches(div), ":not(span) matches div");
    check(!sel2.matches(span), ":not(span) doesn't match span");
}

static void testSelectorDynamic() {
    printf("--- Selector: dynamic pseudo-classes ---\n");
    MockElement a; a.tag = "a"; a.hovered = true;
    MockElement a2; a2.tag = "a";

    auto sel = parseSelector("a:hover");
    check(sel.matches(a), "a:hover matches hovered a");
    check(!sel.matches(a2), "a:hover doesn't match non-hovered a");

    MockElement input; input.tag = "input"; input.focused = true;
    auto sel2 = parseSelector("input:focus");
    check(sel2.matches(input), "input:focus matches focused input");
}

static void testSelectorEmpty() {
    printf("--- Selector: :empty ---\n");
    MockElement empty; empty.tag = "div";
    MockElement notEmpty; notEmpty.tag = "div";
    MockElement child; child.tag = "span";
    notEmpty.addChild(&child);

    auto sel = parseSelector(":empty");
    check(sel.matches(empty), ":empty matches childless element");
    check(!sel.matches(notEmpty), ":empty doesn't match element with children");
}

static void testSelectorCommaList() {
    printf("--- Selector: comma-separated list ---\n");
    auto selectors = parseSelectorList("h1, h2, .title");
    check(selectors.size() == 3, "3 selectors from comma list");

    MockElement h1; h1.tag = "h1";
    MockElement h2; h2.tag = "h2";
    MockElement div; div.tag = "div"; div.classes = "title";
    MockElement span; span.tag = "span";

    // Check each selector
    check(selectors[0].matches(h1), "h1 selector matches h1");
    check(selectors[1].matches(h2), "h2 selector matches h2");
    check(selectors[2].matches(div), ".title selector matches div.title");
    check(!selectors[0].matches(span), "h1 selector doesn't match span");
}

static void testSelectorSpecificity() {
    printf("--- Selector: specificity ---\n");

    // * = (0,0,0) = 0
    check(calculateSpecificity("*") == 0x000000, "* specificity = 0,0,0");

    // tag = (0,0,1)
    check(calculateSpecificity("div") == 0x000001, "div specificity = 0,0,1");

    // .class = (0,1,0)
    check(calculateSpecificity(".box") == 0x000100, ".box specificity = 0,1,0");

    // #id = (1,0,0)
    check(calculateSpecificity("#main") == 0x010000, "#main specificity = 1,0,0");

    // tag.class = (0,1,1)
    check(calculateSpecificity("div.box") == 0x000101, "div.box specificity = 0,1,1");

    // #id.class tag = (1,1,1)
    check(calculateSpecificity("#main.box div") == 0x010101, "#main.box div = 1,1,1");

    // .a.b = (0,2,0)
    check(calculateSpecificity(".a.b") == 0x000200, ".a.b specificity = 0,2,0");

    // div > .box = (0,1,1)
    check(calculateSpecificity("div > .box") == 0x000101, "div > .box = 0,1,1");

    // Specificity ordering
    uint32_t specTag = calculateSpecificity("div");
    uint32_t specClass = calculateSpecificity(".box");
    uint32_t specId = calculateSpecificity("#main");
    check(specTag < specClass, "tag < class specificity");
    check(specClass < specId, "class < id specificity");
}

static void testSelectorComplexChain() {
    printf("--- Selector: complex chains ---\n");
    // Build: body > div.container > ul.nav > li.item > a.link
    MockElement body; body.tag = "body";
    MockElement div; div.tag = "div"; div.classes = "container";
    MockElement ul; ul.tag = "ul"; ul.classes = "nav";
    MockElement li; li.tag = "li"; li.classes = "item";
    MockElement a; a.tag = "a"; a.classes = "link";

    body.addChild(&div);
    div.addChild(&ul);
    ul.addChild(&li);
    li.addChild(&a);

    auto sel1 = parseSelector("body .container .item a");
    check(sel1.matches(a), "body .container .item a matches (descendant chain)");

    auto sel2 = parseSelector("body > div > ul > li > a");
    check(sel2.matches(a), "body > div > ul > li > a matches (child chain)");

    auto sel3 = parseSelector("body > div > ul > li > span");
    check(!sel3.matches(a), "wrong tag at end doesn't match");

    auto sel4 = parseSelector(".container .link");
    check(sel4.matches(a), ".container .link matches across depth");

    auto sel5 = parseSelector("body > .link");
    check(!sel5.matches(a), "body > .link doesn't match non-direct child");
}

static void testSelectorOnlyChild() {
    printf("--- Selector: :only-child ---\n");
    MockElement parent1; parent1.tag = "div";
    MockElement only; only.tag = "span";
    parent1.addChild(&only);

    MockElement parent2; parent2.tag = "div";
    MockElement c1; c1.tag = "span";
    MockElement c2; c2.tag = "span";
    parent2.addChild(&c1);
    parent2.addChild(&c2);

    auto sel = parseSelector(":only-child");
    check(sel.matches(only), ":only-child matches sole child");
    check(!sel.matches(c1), ":only-child doesn't match when siblings exist");
}

static void testSelectorFirstOfType() {
    printf("--- Selector: :first-of-type / :last-of-type ---\n");
    MockElement parent; parent.tag = "div";
    MockElement span1; span1.tag = "span";
    MockElement p1; p1.tag = "p";
    MockElement span2; span2.tag = "span";

    parent.addChild(&span1);
    parent.addChild(&p1);
    parent.addChild(&span2);

    auto selFirst = parseSelector("span:first-of-type");
    check(selFirst.matches(span1), "span:first-of-type matches first span");
    check(!selFirst.matches(span2), "span:first-of-type doesn't match second span");

    auto selLast = parseSelector("span:last-of-type");
    check(!selLast.matches(span1), "span:last-of-type doesn't match first span");
    check(selLast.matches(span2), "span:last-of-type matches last span");
}

// ========== Cascade Tests ==========

static void testCascadeBasicResolve() {
    printf("--- Cascade: basic resolve ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; font-size: 20px; }"));

    MockElement div; div.tag = "div";
    auto style = cascade.resolve(div);

    check(style["color"] == "red", "cascade: color resolved to red");
    check(style["font-size"] == "20px", "cascade: font-size resolved to 20px");
    // Non-set property gets initial value
    check(style["display"] == "inline", "cascade: display gets initial value 'inline'");
}

static void testCascadeSpecificityOrder() {
    printf("--- Cascade: specificity ordering ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        "div { color: red; }\n"
        ".box { color: blue; }\n"
        "#main { color: green; }\n"
    ));

    // Element matches all three rules; #id wins
    MockElement e; e.tag = "div"; e.classes = "box"; e.elemId = "main";
    auto style = cascade.resolve(e);
    check(style["color"] == "green", "cascade: #id specificity wins over .class and tag");
}

static void testCascadeSourceOrder() {
    printf("--- Cascade: source order ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".a { color: red; }\n"
        ".a { color: blue; }\n"
    ));

    MockElement e; e.tag = "div"; e.classes = "a";
    auto style = cascade.resolve(e);
    check(style["color"] == "blue", "cascade: later source order wins at equal specificity");
}

static void testCascadeImportant() {
    printf("--- Cascade: !important ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        "#main { color: green; }\n"
        ".box { color: red !important; }\n"
    ));

    MockElement e; e.tag = "div"; e.classes = "box"; e.elemId = "main";
    auto style = cascade.resolve(e);
    check(style["color"] == "red", "cascade: !important beats higher specificity");
}

static void testCascadeInlineStyle() {
    printf("--- Cascade: inline style ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("#main { color: green; font-size: 20px; }"));

    MockElement e; e.tag = "div"; e.elemId = "main";
    auto style = cascade.resolve(e, "color: blue; margin-top: 5px");
    check(style["color"] == "blue", "cascade: inline style overrides #id rule");
    check(style["font-size"] == "20px", "cascade: non-inline property still from stylesheet");
    check(style["margin-top"] == "5px", "cascade: inline-only property applied");
}

static void testCascadeInheritance() {
    printf("--- Cascade: inheritance ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".parent { color: red; font-size: 18px; margin-top: 10px; }"
    ));

    MockElement parent; parent.tag = "div"; parent.classes = "parent";
    MockElement child; child.tag = "span";
    parent.addChild(&child);

    auto parentStyle = cascade.resolve(parent);
    auto childStyle = cascade.resolve(child, {}, &parentStyle);

    // color is inherited
    check(childStyle["color"] == "red", "cascade: child inherits color from parent");
    // font-size is inherited
    check(childStyle["font-size"] == "18px", "cascade: child inherits font-size from parent");
    // margin-top is NOT inherited - should get initial value
    check(childStyle["margin-top"] == "0", "cascade: child gets initial margin-top, not inherited");
}

static void testCascadeInitialValues() {
    printf("--- Cascade: initial values ---\n");
    Cascade cascade;
    // No stylesheets - everything should be initial values
    MockElement e; e.tag = "div";
    auto style = cascade.resolve(e);

    check(style["display"] == "inline", "cascade: initial display = inline");
    check(style["color"] == "black", "cascade: initial color = black");
    check(style["position"] == "static", "cascade: initial position = static");
    check(style["opacity"] == "1", "cascade: initial opacity = 1");
    check(style["font-size"] == "16px", "cascade: initial font-size = 16px");
}

static void testCascadeMultipleStylesheets() {
    printf("--- Cascade: multiple stylesheets ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; font-size: 14px; }"));
    cascade.addStylesheet(parse("div { color: blue; }"));

    MockElement e; e.tag = "div";
    auto style = cascade.resolve(e);
    check(style["color"] == "blue", "cascade: later stylesheet wins for color");
    check(style["font-size"] == "14px", "cascade: earlier stylesheet property preserved");
}

static void testCascadeShadowDOMScoping() {
    printf("--- Cascade: shadow DOM scoping ---\n");
    int shadowRoot = 42; // dummy scope pointer

    Cascade cascade;
    // Document-level styles
    cascade.addStylesheet(parse("div { color: red; }"));
    // Shadow-scoped styles
    cascade.addStylesheet(parse("div { color: blue; }"), &shadowRoot);

    // Element in document scope
    MockElement docElem; docElem.tag = "div";
    auto docStyle = cascade.resolve(docElem);
    check(docStyle["color"] == "red", "cascade: doc-scoped element gets doc rules");

    // Element in shadow scope
    MockElement shadowElem; shadowElem.tag = "div"; shadowElem.scopePtr = &shadowRoot;
    auto shadowStyle = cascade.resolve(shadowElem);
    check(shadowStyle["color"] == "blue", "cascade: shadow-scoped element gets shadow rules");
}

static void testCascadeCommaSelectors() {
    printf("--- Cascade: comma-separated selectors ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("h1, h2, h3 { font-weight: bold; color: navy; }"));

    MockElement h1; h1.tag = "h1";
    MockElement h2; h2.tag = "h2";
    MockElement h3; h3.tag = "h3";
    MockElement p; p.tag = "p";

    auto s1 = cascade.resolve(h1);
    auto s2 = cascade.resolve(h2);
    auto s3 = cascade.resolve(h3);
    auto sp = cascade.resolve(p);

    check(s1["font-weight"] == "bold", "cascade: h1 gets bold from h1,h2,h3 rule");
    check(s2["color"] == "navy", "cascade: h2 gets navy from h1,h2,h3 rule");
    check(s3["font-weight"] == "bold", "cascade: h3 gets bold from h1,h2,h3 rule");
    check(sp["font-weight"] == "normal", "cascade: p doesn't match h1,h2,h3 rule");
}

static void testCascadeNoMatch() {
    printf("--- Cascade: no matching rules ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".special { color: red; }"));

    MockElement e; e.tag = "div";
    auto style = cascade.resolve(e);
    check(style["color"] == "black", "cascade: unmatched element gets initial color");
}

static void testCascadeInheritanceChain() {
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

    // color inherits through the chain
    check(leafStyle["color"] == "red", "cascade: color inherits through 3 levels");
    // font-family inherits through the chain
    check(leafStyle["font-family"] == "monospace", "cascade: font-family inherits through 3 levels");
    // font-size was set on middle, inherits to leaf
    check(leafStyle["font-size"] == "20px", "cascade: font-size inherits from middle to leaf");
}

static void testCascadeImportantVsInline() {
    printf("--- Cascade: !important vs inline ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(".forced { color: red !important; }"));

    MockElement e; e.tag = "div"; e.classes = "forced";
    // Inline style tries to override, but !important in stylesheet wins
    // Note: per CSS spec, !important in author stylesheet beats normal inline,
    // but !important inline beats !important author. We test the former.
    auto style = cascade.resolve(e, "color: blue");
    // In our implementation, inline has max specificity for normal declarations.
    // !important declarations beat normal ones regardless of specificity.
    // So !important author should beat normal inline.
    check(style["color"] == "red", "cascade: !important author beats normal inline");
}

static void testCascadeClear() {
    printf("--- Cascade: clear ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse("div { color: red; }"));

    MockElement e; e.tag = "div";
    auto s1 = cascade.resolve(e);
    check(s1["color"] == "red", "cascade: before clear, color is red");

    cascade.clear();
    auto s2 = cascade.resolve(e);
    check(s2["color"] == "black", "cascade: after clear, color is initial");
}

// ========== Shorthand Expansion Tests ==========

static void testShorthandMargin() {
    printf("--- Shorthand: margin ---\n");
    // 1 value
    auto r1 = expandShorthand("margin", "10px");
    check(r1.size() == 4, "margin 1-value -> 4 longhands");
    check(r1[0].property == "margin-top" && r1[0].value == "10px", "margin 1v: top");
    check(r1[1].property == "margin-right" && r1[1].value == "10px", "margin 1v: right");
    check(r1[2].property == "margin-bottom" && r1[2].value == "10px", "margin 1v: bottom");
    check(r1[3].property == "margin-left" && r1[3].value == "10px", "margin 1v: left");

    // 2 values
    auto r2 = expandShorthand("margin", "10px 20px");
    check(r2[0].value == "10px" && r2[1].value == "20px", "margin 2v: top/right");
    check(r2[2].value == "10px" && r2[3].value == "20px", "margin 2v: bottom/left");

    // 3 values
    auto r3 = expandShorthand("margin", "10px 20px 30px");
    check(r3[0].value == "10px", "margin 3v: top=10px");
    check(r3[1].value == "20px" && r3[3].value == "20px", "margin 3v: right=left=20px");
    check(r3[2].value == "30px", "margin 3v: bottom=30px");

    // 4 values
    auto r4 = expandShorthand("margin", "1px 2px 3px 4px");
    check(r4[0].value == "1px", "margin 4v: top=1px");
    check(r4[1].value == "2px", "margin 4v: right=2px");
    check(r4[2].value == "3px", "margin 4v: bottom=3px");
    check(r4[3].value == "4px", "margin 4v: left=4px");

    // auto
    auto ra = expandShorthand("margin", "0 auto");
    check(ra[0].value == "0" && ra[1].value == "auto", "margin: 0 auto");
}

static void testShorthandPadding() {
    printf("--- Shorthand: padding ---\n");
    auto r = expandShorthand("padding", "5px 10px");
    check(r.size() == 4, "padding -> 4 longhands");
    check(r[0].property == "padding-top" && r[0].value == "5px", "padding-top");
    check(r[1].property == "padding-right" && r[1].value == "10px", "padding-right");
}

static void testShorthandBorder() {
    printf("--- Shorthand: border ---\n");
    auto r = expandShorthand("border", "1px solid black");
    check(r.size() == 12, "border -> 12 longhands (4 sides x 3 properties)");
    check(r[0].property == "border-top-width" && r[0].value == "1px", "border-top-width");
    check(r[1].property == "border-top-style" && r[1].value == "solid", "border-top-style");
    check(r[2].property == "border-top-color" && r[2].value == "black", "border-top-color");
    check(r[3].property == "border-right-width" && r[3].value == "1px", "border-right-width");
}

static void testShorthandBorderSide() {
    printf("--- Shorthand: border-top ---\n");
    auto r = expandShorthand("border-top", "2px dashed red");
    check(r.size() == 3, "border-top -> 3 longhands");
    check(r[0].property == "border-top-width" && r[0].value == "2px", "border-top-width");
    check(r[1].property == "border-top-style" && r[1].value == "dashed", "border-top-style");
    check(r[2].property == "border-top-color" && r[2].value == "red", "border-top-color");
}

static void testShorthandBorderWidth() {
    printf("--- Shorthand: border-width ---\n");
    auto r = expandShorthand("border-width", "1px 2px 3px 4px");
    check(r.size() == 4, "border-width -> 4 longhands");
    check(r[0].value == "1px" && r[1].value == "2px", "border-width top/right");
    check(r[2].value == "3px" && r[3].value == "4px", "border-width bottom/left");
}

static void testShorthandFlex() {
    printf("--- Shorthand: flex ---\n");
    // flex: none
    auto r1 = expandShorthand("flex", "none");
    check(r1.size() == 3, "flex none -> 3 longhands");
    check(r1[0].value == "0" && r1[1].value == "0" && r1[2].value == "auto", "flex: none = 0 0 auto");

    // flex: auto
    auto r2 = expandShorthand("flex", "auto");
    check(r2[0].value == "1" && r2[1].value == "1" && r2[2].value == "auto", "flex: auto = 1 1 auto");

    // flex: 1
    auto r3 = expandShorthand("flex", "1");
    check(r3[0].value == "1" && r3[2].value == "0", "flex: 1 = grow:1 basis:0");

    // flex: 2 1 100px
    auto r4 = expandShorthand("flex", "2 1 100px");
    check(r4[0].value == "2", "flex 3v: grow=2");
    check(r4[1].value == "1", "flex 3v: shrink=1");
    check(r4[2].value == "100px", "flex 3v: basis=100px");
}

static void testShorthandFlexFlow() {
    printf("--- Shorthand: flex-flow ---\n");
    auto r1 = expandShorthand("flex-flow", "column wrap");
    check(r1[0].property == "flex-direction" && r1[0].value == "column", "flex-flow: direction=column");
    check(r1[1].property == "flex-wrap" && r1[1].value == "wrap", "flex-flow: wrap=wrap");

    auto r2 = expandShorthand("flex-flow", "row-reverse");
    check(r2[0].value == "row-reverse", "flex-flow single: direction=row-reverse");
    check(r2[1].value == "nowrap", "flex-flow single: wrap defaults to nowrap");
}

static void testShorthandGap() {
    printf("--- Shorthand: gap ---\n");
    auto r1 = expandShorthand("gap", "10px");
    check(r1.size() == 2, "gap single -> 2 longhands");
    check(r1[0].property == "row-gap" && r1[0].value == "10px", "gap single: row-gap");
    check(r1[1].property == "column-gap" && r1[1].value == "10px", "gap single: column-gap");

    auto r2 = expandShorthand("gap", "10px 20px");
    check(r2[0].value == "10px" && r2[1].value == "20px", "gap: row=10px col=20px");
}

static void testShorthandFont() {
    printf("--- Shorthand: font ---\n");
    auto r = expandShorthand("font", "italic bold 16px/1.5 Arial, sans-serif");
    check(r.size() == 5, "font -> 5 longhands");
    check(r[0].property == "font-style" && r[0].value == "italic", "font: style=italic");
    check(r[1].property == "font-weight" && r[1].value == "bold", "font: weight=bold");
    check(r[2].property == "font-size" && r[2].value == "16px", "font: size=16px");
    check(r[3].property == "line-height" && r[3].value == "1.5", "font: line-height=1.5");
    check(r[4].property == "font-family", "font: family property");

    // Simple font shorthand
    auto r2 = expandShorthand("font", "14px monospace");
    check(r2[2].value == "14px", "font simple: size=14px");
    check(r2[4].value == "monospace", "font simple: family=monospace");
}

static void testShorthandListStyle() {
    printf("--- Shorthand: list-style ---\n");
    auto r = expandShorthand("list-style", "square inside");
    check(r[0].property == "list-style-type" && r[0].value == "square", "list-style: type=square");
    check(r[1].property == "list-style-position" && r[1].value == "inside", "list-style: position=inside");
}

static void testShorthandNotRecognized() {
    printf("--- Shorthand: non-shorthand passthrough ---\n");
    auto r = expandShorthand("color", "red");
    check(r.size() == 1, "non-shorthand returns 1 entry");
    check(r[0].property == "color" && r[0].value == "red", "non-shorthand passes through");
}

static void testShorthandInCascade() {
    printf("--- Shorthand: integration with cascade ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".box { margin: 10px 20px; padding: 5px; border: 1px solid red; }"
    ));

    MockElement e; e.tag = "div"; e.classes = "box";
    auto style = cascade.resolve(e);

    check(style["margin-top"] == "10px", "cascade shorthand: margin-top=10px");
    check(style["margin-right"] == "20px", "cascade shorthand: margin-right=20px");
    check(style["margin-bottom"] == "10px", "cascade shorthand: margin-bottom=10px");
    check(style["margin-left"] == "20px", "cascade shorthand: margin-left=20px");
    check(style["padding-top"] == "5px", "cascade shorthand: padding-top=5px");
    check(style["border-top-width"] == "1px", "cascade shorthand: border-top-width=1px");
    check(style["border-top-style"] == "solid", "cascade shorthand: border-top-style=solid");
    check(style["border-top-color"] == "red", "cascade shorthand: border-top-color=red");
}

static void testShorthandOverrideLonghand() {
    printf("--- Shorthand: shorthand overrides longhand ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".box { margin-top: 100px; }\n"
        ".box { margin: 20px; }\n"  // later shorthand should override
    ));

    MockElement e; e.tag = "div"; e.classes = "box";
    auto style = cascade.resolve(e);
    check(style["margin-top"] == "20px", "cascade: shorthand margin overrides earlier margin-top");
}

static void testShorthandLonghandOverridesShorthand() {
    printf("--- Shorthand: longhand overrides shorthand ---\n");
    Cascade cascade;
    cascade.addStylesheet(parse(
        ".box { margin: 10px; margin-top: 50px; }"
    ));

    MockElement e; e.tag = "div"; e.classes = "box";
    auto style = cascade.resolve(e);
    check(style["margin-top"] == "50px", "cascade: longhand margin-top overrides shorthand");
    check(style["margin-right"] == "10px", "cascade: other sides from shorthand preserved");
}

// ========== Main ==========

int main() {
    printf("=== htmlayout Phase 1-4 tests ===\n\n");

    // Foundation
    testFoundation();
    printf("\n");

    // Phase 1: Tokenizer
    testTokenizerBasicPunctuation();
    testTokenizerIdents();
    testTokenizerNumbers();
    testTokenizerStrings();
    testTokenizerHash();
    testTokenizerDelimiters();
    testTokenizerAtKeyword();
    testTokenizerFunction();
    testTokenizerComments();
    testTokenizerNegativeNumbers();
    testTokenizerFullRule();
    printf("\n");

    // Phase 1: Parser
    testParserSingleRule();
    testParserMultipleRules();
    testParserImportant();
    testParserCompoundSelector();
    testParserInlineStyle();
    testParserEmptyInput();
    testParserNoSemicolonLastDecl();
    testParserAtRuleSkipped();
    testParserFunctionValues();
    testParserCommaSelector();
    testParserMultiValueProperty();
    testRoundTrip();
    printf("\n");

    // Phase 2: Selectors
    testSelectorSimpleTag();
    testSelectorSimpleClass();
    testSelectorSimpleId();
    testSelectorUniversal();
    testSelectorCompound();
    testSelectorMultipleClasses();
    testSelectorAttribute();
    testSelectorDescendant();
    testSelectorChild();
    testSelectorAdjacentSibling();
    testSelectorGeneralSibling();
    testSelectorPseudoClasses();
    testSelectorNot();
    testSelectorDynamic();
    testSelectorEmpty();
    testSelectorCommaList();
    testSelectorSpecificity();
    testSelectorComplexChain();
    testSelectorOnlyChild();
    testSelectorFirstOfType();
    printf("\n");

    // Phase 3: Cascade
    testCascadeBasicResolve();
    testCascadeSpecificityOrder();
    testCascadeSourceOrder();
    testCascadeImportant();
    testCascadeInlineStyle();
    testCascadeInheritance();
    testCascadeInitialValues();
    testCascadeMultipleStylesheets();
    testCascadeShadowDOMScoping();
    testCascadeCommaSelectors();
    testCascadeNoMatch();
    testCascadeInheritanceChain();
    testCascadeImportantVsInline();
    testCascadeClear();
    printf("\n");

    // Phase 4: Shorthand Expansion
    testShorthandMargin();
    testShorthandPadding();
    testShorthandBorder();
    testShorthandBorderSide();
    testShorthandBorderWidth();
    testShorthandFlex();
    testShorthandFlexFlow();
    testShorthandGap();
    testShorthandFont();
    testShorthandListStyle();
    testShorthandNotRecognized();
    testShorthandInCascade();
    testShorthandOverrideLonghand();
    testShorthandLonghandOverridesShorthand();
    printf("\n");

    // Summary
    printf("=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
