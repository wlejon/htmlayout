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

// ========== Foundation Tests (existing) ==========

static void testFoundation() {
    printf("--- Foundation ---\n");

    // Gumbo HTML parsing
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

    // Properties
    check(htmlayout::css::knownProperties().size() > 0, "properties: known properties exist");
    check(htmlayout::css::isInherited("color") == true, "properties: color is inherited");
    check(htmlayout::css::isInherited("margin-top") == false, "properties: margin-top not inherited");

    // Selector stub
    auto sel = htmlayout::css::parseSelector(".box");
    check(sel.raw == ".box", "selector: parse returns raw text");

    // Layout types
    check(sizeof(htmlayout::layout::LayoutBox) > 0, "layout: LayoutBox defined");
    check(sizeof(htmlayout::layout::Rect) > 0, "layout: Rect defined");
}

// ========== Tokenizer Tests ==========

using namespace htmlayout::css;

static void testTokenizerBasicPunctuation() {
    printf("--- Tokenizer: punctuation ---\n");
    auto tokens = tokenize("{}[]():;,");
    // Expect: { } [ ] ( ) : ; , EOF
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
    // Ident WS Ident WS Ident EOF
    check(tokens.size() == 6, "3 idents + 2 whitespace + EOF");
    check(tokens[0].type == TokenType::Ident && tokens[0].value == "color", "ident: color");
    check(tokens[2].type == TokenType::Ident && tokens[2].value == "margin-top", "ident: margin-top");
    check(tokens[4].type == TokenType::Ident && tokens[4].value == "_private", "ident: _private");
}

static void testTokenizerNumbers() {
    printf("--- Tokenizer: numbers ---\n");
    auto tokens = tokenize("42 3.14 10px 50% 2em");
    // Number WS Number WS Dimension WS Percentage WS Dimension EOF
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
    // > is a Delim
    bool foundGt = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Delim && t.value == ">") foundGt = true;
    }
    check(foundGt, "greater-than delim");
    // * is a Delim
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
    // Comments should be stripped; we should get: Ident WS Colon WS Ident EOF
    check(tokens[0].type == TokenType::Ident && tokens[0].value == "color", "ident before comment");
    bool foundComment = false;
    for (auto& t : tokens) {
        if (t.value.find("comment") != std::string::npos && t.type != TokenType::Ident) {
            foundComment = true;
        }
    }
    check(!foundComment, "comment text not in tokens");
    // Should have colon somewhere after the comment
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
    // Delim(.) Ident(box) WS LeftBrace WS Ident(color) Colon WS Ident(red) Semicolon WS RightBrace EOF
    check(tokens.size() > 0, "non-empty token list");
    check(tokens[0].type == TokenType::Delim && tokens[0].value == ".", "starts with dot");
    check(tokens[1].type == TokenType::Ident && tokens[1].value == "box", "class name");
    // Find LeftBrace
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
    // The value should contain the function call reconstructed
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

// ========== Round-trip / Integration Tests ==========

static void testRoundTrip() {
    printf("--- Round-trip tests ---\n");

    // A realistic stylesheet
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

    // Verify each rule
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

    // Spot-check declaration values
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

// ========== Main ==========

int main() {
    printf("=== htmlayout Phase 1 tests ===\n\n");

    // Foundation
    testFoundation();
    printf("\n");

    // Tokenizer
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

    // Parser
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
    printf("\n");

    // Round-trip
    testRoundTrip();
    printf("\n");

    // Summary
    printf("=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
