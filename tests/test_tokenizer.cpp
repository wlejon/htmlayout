#include "test_tokenizer.h"
#include "test_helpers.h"
#include "css/tokenizer.h"
#include <cmath>

using namespace htmlayout::css;

static void testBasicPunctuation() {
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

static void testIdents() {
    printf("--- Tokenizer: identifiers ---\n");
    auto tokens = tokenize("color margin-top _private");
    check(tokens.size() == 6, "3 idents + 2 whitespace + EOF");
    check(tokens[0].type == TokenType::Ident && tokens[0].value == "color", "ident: color");
    check(tokens[2].type == TokenType::Ident && tokens[2].value == "margin-top", "ident: margin-top");
    check(tokens[4].type == TokenType::Ident && tokens[4].value == "_private", "ident: _private");
}

static void testNumbers() {
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

static void testStrings() {
    printf("--- Tokenizer: strings ---\n");
    auto tokens = tokenize("\"hello\" 'world'");
    check(tokens[0].type == TokenType::String, "double-quoted string");
    check(tokens[0].value == "hello", "double-quoted value");
    check(tokens[2].type == TokenType::String, "single-quoted string");
    check(tokens[2].value == "world", "single-quoted value");
}

static void testHash() {
    printf("--- Tokenizer: hash ---\n");
    auto tokens = tokenize("#myId #ff0000");
    check(tokens[0].type == TokenType::Hash, "hash token");
    check(tokens[0].value == "myId", "hash value = myId");
    check(tokens[2].type == TokenType::Hash, "hex color hash");
    check(tokens[2].value == "ff0000", "hex color value");
}

static void testDelimiters() {
    printf("--- Tokenizer: delimiters ---\n");
    auto tokens = tokenize(".box > .child + .sibling ~ .cousin * ");
    check(tokens[0].type == TokenType::Delim && tokens[0].value == ".", "dot delim");
    check(tokens[1].type == TokenType::Ident && tokens[1].value == "box", "ident after dot");
    bool foundGt = false, foundStar = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Delim && t.value == ">") foundGt = true;
        if (t.type == TokenType::Delim && t.value == "*") foundStar = true;
    }
    check(foundGt, "greater-than delim");
    check(foundStar, "star delim");
}

static void testAtKeyword() {
    printf("--- Tokenizer: at-keyword ---\n");
    auto tokens = tokenize("@media @import");
    check(tokens[0].type == TokenType::AtKeyword && tokens[0].value == "media", "@media");
    check(tokens[2].type == TokenType::AtKeyword && tokens[2].value == "import", "@import");
}

static void testFunction() {
    printf("--- Tokenizer: functions ---\n");
    auto tokens = tokenize("rgb(255, 0, 0) calc(100% - 20px)");
    check(tokens[0].type == TokenType::Function && tokens[0].value == "rgb", "rgb function");
    check(tokens[1].type == TokenType::Number, "255 inside rgb");
}

static void testComments() {
    printf("--- Tokenizer: comments ---\n");
    auto tokens = tokenize("color /* this is a comment */ : red");
    check(tokens[0].type == TokenType::Ident && tokens[0].value == "color", "ident before comment");
    bool foundComment = false;
    for (auto& t : tokens) {
        if (t.value.find("comment") != std::string::npos && t.type != TokenType::Ident)
            foundComment = true;
    }
    check(!foundComment, "comment text not in tokens");
    bool foundColon = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Colon) foundColon = true;
    }
    check(foundColon, "colon present after comment");
}

static void testNegativeNumbers() {
    printf("--- Tokenizer: negative numbers ---\n");
    auto tokens = tokenize("-10px -3.5em");
    check(tokens[0].type == TokenType::Dimension, "-10px is Dimension");
    check(tokens[0].numeric == -10.0, "-10px numeric = -10");
    check(tokens[0].unit == "px", "-10px unit = px");
    check(tokens[2].type == TokenType::Dimension, "-3.5em is Dimension");
    check(std::abs(tokens[2].numeric - (-3.5)) < 0.001, "-3.5em numeric = -3.5");
}

static void testFullRule() {
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

void testTokenizer() {
    testBasicPunctuation();
    testIdents();
    testNumbers();
    testStrings();
    testHash();
    testDelimiters();
    testAtKeyword();
    testFunction();
    testComments();
    testNegativeNumbers();
    testFullRule();
}
