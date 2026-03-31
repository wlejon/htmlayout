#pragma once
#include <string>
#include <vector>
#include <variant>

namespace htmlayout::css {

// CSS token types per W3C CSS Syntax Module Level 3
enum class TokenType {
    Ident,          // keyword or property name
    Function,       // function-name(
    AtKeyword,      // @keyword
    Hash,           // #name
    String,         // "..." or '...'
    Number,         // 42, 3.14
    Percentage,     // 50%
    Dimension,      // 10px, 2em
    Whitespace,
    Colon,          // :
    Semicolon,      // ;
    Comma,          // ,
    LeftBracket,    // [
    RightBracket,   // ]
    LeftParen,      // (
    RightParen,     // )
    LeftBrace,      // {
    RightBrace,     // }
    Delim,          // any other single character (., >, +, ~, *, etc.)
    CDO,            // <!--
    CDC,            // -->
    EndOfFile,
};

struct Token {
    TokenType type;
    std::string value;      // the text content
    double numeric = 0.0;   // for Number/Percentage/Dimension
    std::string unit;       // for Dimension (px, em, etc.)
};

// Tokenize a CSS string into a vector of tokens
std::vector<Token> tokenize(const std::string& css);

} // namespace htmlayout::css
