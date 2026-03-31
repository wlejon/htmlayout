#include "css/parser.h"
#include <algorithm>
#include <charconv>
#include <cctype>

namespace htmlayout::css {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : m_tokens(tokens), m_pos(0) {}

    Stylesheet parseStylesheet() {
        Stylesheet sheet;
        skipWhitespace();
        while (!atEnd()) {
            if (peek().type == TokenType::AtKeyword) {
                if (peek().value == "media") {
                    auto mediaBlock = parseMediaRule();
                    if (!mediaBlock.condition.empty() || !mediaBlock.rules.empty()) {
                        sheet.mediaBlocks.push_back(std::move(mediaBlock));
                    }
                } else {
                    consumeAtRule();
                }
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

    MediaBlock parseMediaRule() {
        MediaBlock block;
        advance(); // skip @media keyword
        skipWhitespace();

        // Collect condition tokens until '{'
        std::string condition;
        while (!atEnd() && peek().type != TokenType::LeftBrace) {
            condition += tokenToString(advance());
        }
        block.condition = trim(condition);

        if (atEnd() || peek().type != TokenType::LeftBrace) return block;
        advance(); // skip '{'

        // Parse rules inside the media block
        skipWhitespace();
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            if (peek().type == TokenType::AtKeyword) {
                consumeAtRule();
                skipWhitespace();
                continue;
            }
            auto rule = parseRule();
            if (!rule.selector.empty()) {
                block.rules.push_back(std::move(rule));
            }
            skipWhitespace();
        }
        if (!atEnd() && peek().type == TokenType::RightBrace) advance();
        return block;
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

namespace {

// Parse a length value like "768px" to pixels
float parseMediaLength(const std::string& s) {
    float num = 0;
    const char* begin = s.data();
    const char* end = begin + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, num);
    if (ec != std::errc()) return 0;
    // unit is ignored for now (assume px)
    return num;
}

// Evaluate a single media feature like "(min-width: 768px)"
bool evaluateMediaFeature(const std::string& feature, const MediaContext& ctx) {
    // Strip parens and trim
    std::string f = feature;
    if (!f.empty() && f.front() == '(') f.erase(0, 1);
    if (!f.empty() && f.back() == ')') f.pop_back();

    // Trim
    size_t s = f.find_first_not_of(" \t\n\r\f");
    size_t e = f.find_last_not_of(" \t\n\r\f");
    if (s == std::string::npos) return false;
    f = f.substr(s, e - s + 1);

    auto colonPos = f.find(':');
    if (colonPos == std::string::npos) {
        // Boolean feature, e.g. (color)
        return true;
    }

    std::string name = f.substr(0, colonPos);
    std::string value = f.substr(colonPos + 1);
    // Trim both
    auto tn = [](std::string& str) {
        size_t a = str.find_first_not_of(" \t"); size_t b = str.find_last_not_of(" \t");
        str = (a == std::string::npos) ? "" : str.substr(a, b - a + 1);
    };
    tn(name); tn(value);

    float val = parseMediaLength(value);

    if (name == "min-width") return ctx.viewportWidth >= val;
    if (name == "max-width") return ctx.viewportWidth <= val;
    if (name == "min-height") return ctx.viewportHeight >= val;
    if (name == "max-height") return ctx.viewportHeight <= val;
    if (name == "width") return ctx.viewportWidth == val;
    if (name == "height") return ctx.viewportHeight == val;
    if (name == "orientation") {
        if (value == "portrait") return ctx.viewportHeight >= ctx.viewportWidth;
        if (value == "landscape") return ctx.viewportWidth > ctx.viewportHeight;
    }

    return false;
}

} // anonymous namespace

bool evaluateMediaQuery(const std::string& condition, const MediaContext& ctx) {
    if (condition.empty() || condition == "all") return true;

    // Handle media type prefixes: "screen and (...)", "print", etc.
    std::string cond = condition;

    // Simple "not" prefix
    bool negate = false;
    if (cond.size() > 4 && cond.substr(0, 4) == "not ") {
        negate = true;
        cond = cond.substr(4);
    }

    // Check for media type
    std::string mediaType;
    auto andPos = cond.find(" and ");
    if (andPos != std::string::npos && cond[0] != '(') {
        mediaType = cond.substr(0, andPos);
        cond = cond.substr(andPos + 5);
    } else if (cond[0] != '(') {
        mediaType = cond;
        cond.clear();
    }

    // Trim media type
    if (!mediaType.empty()) {
        for (auto& c : mediaType) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t s = mediaType.find_first_not_of(" \t");
        size_t e = mediaType.find_last_not_of(" \t");
        if (s != std::string::npos) mediaType = mediaType.substr(s, e - s + 1);

        if (mediaType != "all" && mediaType != ctx.mediaType) {
            return negate;
        }
    }

    if (cond.empty()) return !negate;

    // Evaluate features connected by "and"
    // Split by " and " outside parens
    bool result = true;
    size_t i = 0;
    while (i < cond.size()) {
        // Find next feature (...)
        auto paren = cond.find('(', i);
        if (paren == std::string::npos) break;

        int depth = 0;
        size_t featureStart = paren;
        size_t j = paren;
        while (j < cond.size()) {
            if (cond[j] == '(') depth++;
            else if (cond[j] == ')') { depth--; if (depth == 0) break; }
            j++;
        }
        std::string feature = cond.substr(featureStart, j - featureStart + 1);
        if (!evaluateMediaFeature(feature, ctx)) {
            result = false;
            break;
        }
        i = j + 1;
    }

    return negate ? !result : result;
}

} // namespace htmlayout::css
