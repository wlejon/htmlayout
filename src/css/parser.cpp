#include "css/parser.h"
#include <algorithm>

namespace htmlayout::css {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : m_tokens(tokens), m_pos(0) {}

    Stylesheet parseStylesheet() {
        Stylesheet sheet;
        skipWhitespace();
        while (!atEnd()) {
            // Skip at-rules (consume until next semicolon or matching braces)
            if (peek().type == TokenType::AtKeyword) {
                consumeAtRule();
                skipWhitespace();
                continue;
            }
            // Try to parse a qualified rule (selector { declarations })
            auto rule = parseRule();
            if (!rule.selector.empty()) {
                sheet.rules.push_back(std::move(rule));
            }
            skipWhitespace();
        }
        return sheet;
    }

    std::vector<Declaration> parseDeclarationList() {
        std::vector<Declaration> decls;
        skipWhitespace();
        while (!atEnd()) {
            skipWhitespace();
            if (atEnd()) break;
            if (peek().type == TokenType::Semicolon) {
                advance();
                continue;
            }
            auto decl = parseDeclaration();
            if (!decl.property.empty()) {
                decls.push_back(std::move(decl));
            }
        }
        return decls;
    }

private:
    const std::vector<Token>& m_tokens;
    size_t m_pos;

    bool atEnd() const {
        return m_pos >= m_tokens.size() || m_tokens[m_pos].type == TokenType::EndOfFile;
    }

    const Token& peek() const {
        static const Token eof{TokenType::EndOfFile, "", 0.0, ""};
        return m_pos < m_tokens.size() ? m_tokens[m_pos] : eof;
    }

    const Token& advance() {
        const Token& tok = peek();
        if (m_pos < m_tokens.size()) m_pos++;
        return tok;
    }

    void skipWhitespace() {
        while (!atEnd() && peek().type == TokenType::Whitespace) advance();
    }

    void consumeAtRule() {
        advance(); // skip @keyword
        int braceDepth = 0;
        while (!atEnd()) {
            auto type = peek().type;
            if (type == TokenType::Semicolon && braceDepth == 0) {
                advance();
                return;
            }
            if (type == TokenType::LeftBrace) {
                braceDepth++;
            } else if (type == TokenType::RightBrace) {
                braceDepth--;
                if (braceDepth <= 0) {
                    advance();
                    return;
                }
            }
            advance();
        }
    }

    // Reconstruct the selector text from tokens up to '{'
    Rule parseRule() {
        Rule rule;
        std::string selector;
        // Consume tokens until we hit '{' to form the selector
        while (!atEnd() && peek().type != TokenType::LeftBrace) {
            selector += tokenToString(advance());
        }
        // Trim whitespace from selector
        rule.selector = trim(selector);

        if (atEnd() || peek().type != TokenType::LeftBrace) {
            return rule; // malformed
        }
        advance(); // skip '{'

        // Parse declarations until '}'
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            skipWhitespace();
            if (atEnd() || peek().type == TokenType::RightBrace) break;
            if (peek().type == TokenType::Semicolon) {
                advance();
                continue;
            }
            auto decl = parseDeclaration();
            if (!decl.property.empty()) {
                rule.declarations.push_back(std::move(decl));
            }
        }
        if (!atEnd() && peek().type == TokenType::RightBrace) {
            advance(); // skip '}'
        }
        return rule;
    }

    Declaration parseDeclaration() {
        Declaration decl;
        skipWhitespace();

        // Property name (must be an ident)
        if (peek().type != TokenType::Ident) {
            // Skip to next semicolon or brace
            skipToRecovery();
            return decl;
        }
        decl.property = advance().value;

        skipWhitespace();

        // Expect ':'
        if (peek().type != TokenType::Colon) {
            skipToRecovery();
            return Declaration{};
        }
        advance(); // skip ':'

        skipWhitespace();

        // Collect value tokens until ';' or '}' or EOF
        std::string value;
        while (!atEnd() && peek().type != TokenType::Semicolon && peek().type != TokenType::RightBrace) {
            value += tokenToString(advance());
        }

        // Consume the semicolon if present
        if (!atEnd() && peek().type == TokenType::Semicolon) {
            advance();
        }

        value = trim(value);

        // Check for !important
        decl.important = checkAndStripImportant(value);
        decl.value = value;

        return decl;
    }

    void skipToRecovery() {
        while (!atEnd() && peek().type != TokenType::Semicolon && peek().type != TokenType::RightBrace) {
            advance();
        }
        if (!atEnd() && peek().type == TokenType::Semicolon) advance();
    }

    static bool checkAndStripImportant(std::string& value) {
        // Look for "!important" at the end
        const std::string marker = "!important";
        // Find last non-whitespace
        size_t end = value.find_last_not_of(" \t\n\r\f");
        if (end == std::string::npos) return false;

        size_t len = end + 1;
        if (len >= marker.size()) {
            std::string tail = value.substr(len - marker.size(), marker.size());
            // Case-insensitive compare
            for (auto& ch : tail) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (tail == marker) {
                value = trim(value.substr(0, len - marker.size()));
                return true;
            }
        }

        // Also handle "! important" (with space after !)
        // Find last occurrence of '!'
        auto excl = value.rfind('!');
        if (excl != std::string::npos) {
            std::string after = value.substr(excl + 1);
            // trim and lowercase
            after = trim(after);
            for (auto& ch : after) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (after == "important") {
                value = trim(value.substr(0, excl));
                return true;
            }
        }
        return false;
    }

    static std::string tokenToString(const Token& tok) {
        switch (tok.type) {
            case TokenType::Whitespace: return " ";
            case TokenType::Colon: return ":";
            case TokenType::Semicolon: return ";";
            case TokenType::Comma: return ",";
            case TokenType::LeftBrace: return "{";
            case TokenType::RightBrace: return "}";
            case TokenType::LeftBracket: return "[";
            case TokenType::RightBracket: return "]";
            case TokenType::LeftParen: return "(";
            case TokenType::RightParen: return ")";
            case TokenType::Hash: return "#" + tok.value;
            case TokenType::AtKeyword: return "@" + tok.value;
            case TokenType::Delim: return tok.value;
            case TokenType::String: return "\"" + tok.value + "\"";
            case TokenType::Function: return tok.value + "(";
            case TokenType::Dimension: return tok.value + tok.unit;
            case TokenType::Percentage: return tok.value + "%";
            case TokenType::Number: return tok.value;
            case TokenType::CDO: return "<!--";
            case TokenType::CDC: return "-->";
            default: return tok.value;
        }
    }

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r\f");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r\f");
        return s.substr(start, end - start + 1);
    }
};

} // anonymous namespace

Stylesheet parse(const std::string& css) {
    auto tokens = tokenize(css);
    Parser parser(tokens);
    return parser.parseStylesheet();
}

std::vector<Declaration> parseInlineStyle(const std::string& style) {
    auto tokens = tokenize(style);
    Parser parser(tokens);
    return parser.parseDeclarationList();
}

} // namespace htmlayout::css
