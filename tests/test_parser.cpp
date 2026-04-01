#include "test_parser.h"
#include "test_helpers.h"
#include "css/tokenizer.h"
#include "css/parser.h"

using namespace htmlayout::css;

static void testSingleRule() {
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

static void testMultipleRules() {
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

static void testImportant() {
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

static void testCompoundSelector() {
    printf("--- Parser: compound selectors ---\n");
    auto sheet = parse("div.box#main > .child { display: flex; }");
    check(sheet.rules.size() == 1, "1 rule");
    check(sheet.rules[0].selector == "div.box#main > .child", "compound selector preserved");
}

static void testInlineStyle() {
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

static void testEmptyInput() {
    printf("--- Parser: empty/edge cases ---\n");
    auto sheet1 = parse("");
    check(sheet1.rules.empty(), "empty string -> 0 rules");
    auto sheet2 = parse("   \n\t  ");
    check(sheet2.rules.empty(), "whitespace only -> 0 rules");
    auto decls = parseInlineStyle("");
    check(decls.empty(), "empty inline style -> 0 declarations");
}

static void testNoSemicolonLastDecl() {
    printf("--- Parser: no trailing semicolon ---\n");
    auto sheet = parse("p { color: red; font-size: 14px }");
    check(sheet.rules.size() == 1, "1 rule");
    check(sheet.rules[0].declarations.size() == 2, "2 declarations (no trailing ;)");
    check(sheet.rules[0].declarations[1].value == "14px", "last decl value ok");
}

static void testAtRuleSkipped() {
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

static void testFunctionValues() {
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

static void testCommaSelector() {
    printf("--- Parser: comma-separated selector ---\n");
    auto sheet = parse("h1, h2, h3 { font-weight: bold; }");
    check(sheet.rules.size() == 1, "1 rule (grouped selector)");
    check(sheet.rules[0].selector == "h1, h2, h3", "comma selector preserved");
}

static void testMultiValueProperty() {
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
    for (auto& d : sheet.rules[2].declarations)
        if (d.property == "display" && d.value == "flex") foundFlex = true;
    check(foundFlex, "header display: flex");

    bool foundMaxWidth = false;
    for (auto& d : sheet.rules[1].declarations)
        if (d.property == "max-width" && d.value == "1200px") foundMaxWidth = true;
    check(foundMaxWidth, "container max-width: 1200px");
}

static void testImportBasicString() {
    printf("--- Parser: @import with string URL ---\n");
    auto sheet = parse("@import \"reset.css\";");
    check(sheet.imports.size() == 1, "1 import");
    check(sheet.imports[0].url == "reset.css", "import URL is reset.css");
    check(sheet.imports[0].mediaCondition.empty(), "no media condition");
    check(sheet.imports[0].layer.empty(), "no layer");
}

static void testImportUrlFunction() {
    printf("--- Parser: @import url() ---\n");
    auto sheet = parse("@import url(\"theme.css\");");
    check(sheet.imports.size() == 1, "1 import");
    check(sheet.imports[0].url == "theme.css", "import URL is theme.css");
}

static void testImportWithMedia() {
    printf("--- Parser: @import with media condition ---\n");
    auto sheet = parse("@import \"print.css\" print;");
    check(sheet.imports.size() == 1, "1 import");
    check(sheet.imports[0].url == "print.css", "import URL");
    check(sheet.imports[0].mediaCondition == "print", "media condition is print");
}

static void testImportWithLayer() {
    printf("--- Parser: @import with layer ---\n");
    auto sheet = parse("@import \"base.css\" layer(base);");
    check(sheet.imports.size() == 1, "1 import");
    check(sheet.imports[0].url == "base.css", "import URL");
    check(sheet.imports[0].layer == "base", "layer name is base");
}

static void testImportWithLayerAndMedia() {
    printf("--- Parser: @import with layer and media ---\n");
    auto sheet = parse("@import \"responsive.css\" layer(utils) (max-width: 600px);");
    check(sheet.imports.size() == 1, "1 import");
    check(sheet.imports[0].url == "responsive.css", "import URL");
    check(sheet.imports[0].layer == "utils", "layer name is utils");
    check(sheet.imports[0].mediaCondition == "(max-width: 600px)", "media condition");
}

static void testMultipleImports() {
    printf("--- Parser: multiple @import rules ---\n");
    auto sheet = parse(
        "@import \"reset.css\";\n"
        "@import \"base.css\";\n"
        "div { color: red; }\n"
    );
    check(sheet.imports.size() == 2, "2 imports");
    check(sheet.imports[0].url == "reset.css", "first import URL");
    check(sheet.imports[1].url == "base.css", "second import URL");
    check(sheet.rules.size() == 1, "1 rule after imports");
}

void testParser() {
    testSingleRule();
    testMultipleRules();
    testImportant();
    testCompoundSelector();
    testInlineStyle();
    testEmptyInput();
    testNoSemicolonLastDecl();
    testAtRuleSkipped();
    testFunctionValues();
    testCommaSelector();
    testMultiValueProperty();
    testRoundTrip();
    testImportBasicString();
    testImportUrlFunction();
    testImportWithMedia();
    testImportWithLayer();
    testImportWithLayerAndMedia();
    testMultipleImports();
}
