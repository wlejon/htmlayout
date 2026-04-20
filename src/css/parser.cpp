#include "css/parser.h"
#include "../from_chars_compat.h"
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
                } else if (peek().value == "supports") {
                    // @supports works similarly to @media but evaluates CSS feature support
                    auto supportsBlock = parseSupportsRule();
                    // Include rules if the condition evaluates to true
                    if (!supportsBlock.rules.empty()) {
                        for (auto& rule : supportsBlock.rules) {
                            sheet.rules.push_back(std::move(rule));
                        }
                    }
                } else if (peek().value == "layer") {
                    parseLayerRule(sheet);
                } else if (peek().value == "container") {
                    auto containerBlock = parseContainerRule();
                    if (!containerBlock.rules.empty()) {
                        sheet.containerBlocks.push_back(std::move(containerBlock));
                    }
                } else if (peek().value == "import") {
                    auto importRule = parseImportRule();
                    if (!importRule.url.empty()) {
                        sheet.imports.push_back(std::move(importRule));
                    }
                } else if (peek().value == "keyframes" || peek().value == "-webkit-keyframes") {
                    auto kf = parseKeyframesRule();
                    if (!kf.name.empty()) {
                        sheet.keyframes.push_back(std::move(kf));
                    }
                } else if (peek().value == "font-face") {
                    auto ff = parseFontFaceRule();
                    if (!ff.family.empty() && !ff.src.empty()) {
                        sheet.fontFaces.push_back(std::move(ff));
                    }
                } else {
                    // @charset, etc. — skip gracefully
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

    // Parse @supports rule: evaluate the condition and include rules if supported
    MediaBlock parseSupportsRule() {
        MediaBlock block; // reuse MediaBlock structure
        advance(); // skip @supports keyword
        skipWhitespace();

        // Collect condition tokens until '{'
        std::string condition;
        while (!atEnd() && peek().type != TokenType::LeftBrace) {
            condition += tokenToString(advance());
        }
        block.condition = trim(condition);

        if (atEnd() || peek().type != TokenType::LeftBrace) return block;
        advance(); // skip '{'

        // Evaluate @supports condition: we support most CSS properties
        // For simplicity, if the condition contains a known property, we support it
        bool supported = evaluateSupportsCondition(block.condition);

        // Parse rules inside
        skipWhitespace();
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            if (peek().type == TokenType::AtKeyword) {
                consumeAtRule();
                skipWhitespace();
                continue;
            }
            auto rule = parseRule();
            if (!rule.selector.empty() && supported) {
                block.rules.push_back(std::move(rule));
            }
            skipWhitespace();
        }
        if (!atEnd() && peek().type == TokenType::RightBrace) advance();
        return block;
    }

    // Simple @supports condition evaluator
    static bool evaluateSupportsCondition(const std::string& condition) {
        // Check for (property: value) syntax
        // We'll accept any condition with a property:value pair inside parens
        auto paren = condition.find('(');
        if (paren == std::string::npos) return true; // no condition = support

        auto close = condition.find(')', paren);
        if (close == std::string::npos) return true;

        std::string inner = condition.substr(paren + 1, close - paren - 1);
        auto colon = inner.find(':');
        if (colon != std::string::npos) {
            // Has property: value — we broadly support CSS properties
            return true;
        }

        // "not" prefix
        if (condition.find("not") == 0) return false;

        return true; // default: assume supported
    }

    // Parse @layer rule — two forms:
    // 1. @layer name { rules }           — layer block
    // 2. @layer name1, name2, ...;       — layer ordering declaration
    void parseLayerRule(Stylesheet& sheet) {
        advance(); // skip @layer keyword
        skipWhitespace();

        // Collect tokens until '{' or ';'
        std::string nameStr;
        while (!atEnd() && peek().type != TokenType::LeftBrace && peek().type != TokenType::Semicolon) {
            nameStr += tokenToString(advance());
        }
        nameStr = trim(nameStr);

        if (!atEnd() && peek().type == TokenType::Semicolon) {
            // Layer ordering declaration: @layer name1, name2;
            advance(); // skip ';'
            // Split by comma and record order
            std::string current;
            for (char c : nameStr) {
                if (c == ',') {
                    std::string layerName = trim(current);
                    if (!layerName.empty()) sheet.layerOrder.push_back(layerName);
                    current.clear();
                } else {
                    current += c;
                }
            }
            std::string last = trim(current);
            if (!last.empty()) sheet.layerOrder.push_back(last);
            return;
        }

        if (atEnd() || peek().type != TokenType::LeftBrace) return;
        advance(); // skip '{'

        LayerBlock layer;
        layer.name = nameStr;

        // Parse rules inside the layer block
        skipWhitespace();
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            if (peek().type == TokenType::AtKeyword) {
                if (peek().value == "media") {
                    auto mediaBlock = parseMediaRule();
                    if (!mediaBlock.condition.empty() || !mediaBlock.rules.empty()) {
                        layer.mediaBlocks.push_back(std::move(mediaBlock));
                    }
                } else {
                    consumeAtRule();
                }
                skipWhitespace();
                continue;
            }
            auto rule = parseRule();
            if (!rule.selector.empty()) {
                layer.rules.push_back(std::move(rule));
            }
            skipWhitespace();
        }
        if (!atEnd() && peek().type == TokenType::RightBrace) advance();

        sheet.layerBlocks.push_back(std::move(layer));
    }

    // Parse @container rule: @container [name] (condition) { rules }
    ContainerBlock parseContainerRule() {
        ContainerBlock block;
        advance(); // skip @container keyword
        skipWhitespace();

        // Collect tokens until '{'
        std::string prelude;
        while (!atEnd() && peek().type != TokenType::LeftBrace) {
            prelude += tokenToString(advance());
        }
        prelude = trim(prelude);

        // Parse prelude: optional name followed by condition in parens
        // e.g., "sidebar (min-width: 400px)" or "(min-width: 400px)"
        auto parenPos = prelude.find('(');
        if (parenPos != std::string::npos) {
            std::string before = trim(prelude.substr(0, parenPos));
            if (!before.empty()) {
                block.name = before;
            }
            block.condition = trim(prelude.substr(parenPos));
        } else {
            block.condition = prelude;
        }

        if (atEnd() || peek().type != TokenType::LeftBrace) return block;
        advance(); // skip '{'

        // Parse rules inside the container block
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

    ImportRule parseImportRule() {
        ImportRule rule;
        advance(); // skip @import
        skipWhitespace();

        // Extract URL: either a bare string token or url("...")
        if (peek().type == TokenType::String) {
            rule.url = peek().value;
            advance();
        } else if (peek().type == TokenType::Function && peek().value == "url") {
            advance(); // skip url(
            skipWhitespace();
            if (peek().type == TokenType::String) {
                rule.url = peek().value;
                advance();
            } else {
                // Unquoted URL: collect tokens until ')'
                std::string url;
                while (!atEnd() && peek().type != TokenType::RightParen) {
                    url += peek().value;
                    advance();
                }
                rule.url = trim(url);
            }
            skipWhitespace();
            if (!atEnd() && peek().type == TokenType::RightParen) advance();
        } else {
            // Malformed @import — skip to semicolon
            while (!atEnd() && peek().type != TokenType::Semicolon) advance();
            if (!atEnd()) advance();
            return rule;
        }

        skipWhitespace();

        // Parse optional layer and/or media qualifiers before ';'
        // Possible forms: layer, layer(name), supports(...), media condition
        if (!atEnd() && peek().type != TokenType::Semicolon) {
            if (peek().type == TokenType::Ident && peek().value == "layer") {
                advance(); // skip "layer"
                skipWhitespace();
                if (!atEnd() && peek().type == TokenType::Function && peek().value == "layer") {
                    // Shouldn't happen after ident, but handle gracefully
                    advance();
                } else if (!atEnd() && peek().type == TokenType::LeftParen) {
                    // layer(name) — but tokenizer would emit Function token for "layer("
                    // This branch handles if layer and ( are separate tokens
                    advance(); // skip (
                    skipWhitespace();
                    std::string layerName;
                    while (!atEnd() && peek().type != TokenType::RightParen) {
                        layerName += peek().value;
                        advance();
                    }
                    if (!atEnd()) advance(); // skip )
                    rule.layer = trim(layerName);
                } else {
                    rule.layer = "";  // anonymous layer import
                }
                skipWhitespace();
            } else if (peek().type == TokenType::Function && peek().value == "layer") {
                advance(); // skip layer(
                skipWhitespace();
                std::string layerName;
                while (!atEnd() && peek().type != TokenType::RightParen) {
                    layerName += peek().value;
                    advance();
                }
                if (!atEnd()) advance(); // skip )
                rule.layer = trim(layerName);
                skipWhitespace();
            }
        }

        // Remaining tokens before ';' are the media condition
        if (!atEnd() && peek().type != TokenType::Semicolon) {
            std::string media;
            while (!atEnd() && peek().type != TokenType::Semicolon) {
                media += tokenToString(advance());
            }
            rule.mediaCondition = trim(media);
        }

        // Consume the semicolon
        if (!atEnd() && peek().type == TokenType::Semicolon) advance();

        return rule;
    }

    // Parse @font-face { font-family: ...; src: url(...); ... }
    FontFaceRule parseFontFaceRule() {
        FontFaceRule rule;
        advance(); // skip @font-face
        skipWhitespace();

        if (atEnd() || peek().type != TokenType::LeftBrace) {
            consumeAtRule();
            return rule;
        }
        advance(); // skip '{'

        // Collect tokens until matching '}'
        std::vector<Token> declTokens;
        int depth = 1;
        while (!atEnd() && depth > 0) {
            if (peek().type == TokenType::LeftBrace) ++depth;
            else if (peek().type == TokenType::RightBrace) {
                --depth;
                if (depth <= 0) { advance(); break; }
            }
            declTokens.push_back(peek());
            advance();
        }
        declTokens.push_back(Token{TokenType::EndOfFile, "", 0.0, ""});
        Parser declParser(declTokens);
        auto decls = declParser.parseDeclarationList();

        for (auto& d : decls) {
            if (d.property == "font-family") {
                // Strip quotes
                rule.family = d.value;
                if (!rule.family.empty() &&
                    (rule.family.front() == '"' || rule.family.front() == '\''))
                    rule.family = rule.family.substr(1, rule.family.size() - 2);
            } else if (d.property == "src") {
                // Extract url(...) from src value
                auto pos = d.value.find("url(");
                if (pos != std::string::npos) {
                    auto start = pos + 4;
                    auto end = d.value.find(')', start);
                    if (end != std::string::npos) {
                        rule.src = d.value.substr(start, end - start);
                        // Strip quotes
                        if (!rule.src.empty() &&
                            (rule.src.front() == '"' || rule.src.front() == '\''))
                            rule.src = rule.src.substr(1, rule.src.size() - 2);
                    }
                }
            } else if (d.property == "font-weight") {
                if (d.value == "bold") rule.weight = 700;
                else if (d.value == "normal") rule.weight = 400;
                else {
                    char* end = nullptr;
                    int v = static_cast<int>(std::strtof(d.value.c_str(), &end));
                    if (end != d.value.c_str() && v > 0) rule.weight = v;
                }
            } else if (d.property == "font-style") {
                rule.italic = (d.value == "italic" || d.value == "oblique");
            }
        }
        return rule;
    }

    // Parse @keyframes name { from/to/% { declarations } ... }
    KeyframeBlock parseKeyframesRule() {
        KeyframeBlock block;
        advance(); // skip @keyframes / @-webkit-keyframes
        skipWhitespace();

        // Parse animation name (ident or string)
        if (!atEnd()) {
            if (peek().type == TokenType::String) {
                block.name = peek().value;
                advance();
            } else if (peek().type == TokenType::Ident) {
                block.name = peek().value;
                advance();
            }
        }
        skipWhitespace();

        // Expect opening brace
        if (atEnd() || peek().type != TokenType::LeftBrace) {
            consumeAtRule();
            return block;
        }
        advance(); // skip '{'
        skipWhitespace();

        // Parse keyframe stops
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            skipWhitespace();
            if (atEnd() || peek().type == TokenType::RightBrace) break;

            // Parse offset(s): "from", "to", or "50%", or comma-separated list
            std::vector<float> offsets;
            while (!atEnd() && peek().type != TokenType::LeftBrace) {
                skipWhitespace();
                if (peek().type == TokenType::Ident) {
                    if (peek().value == "from") offsets.push_back(0.0f);
                    else if (peek().value == "to") offsets.push_back(1.0f);
                    advance();
                } else if (peek().type == TokenType::Percentage) {
                    offsets.push_back(static_cast<float>(peek().numeric / 100.0));
                    advance();
                } else if (peek().type == TokenType::Number) {
                    // Handle "0" without %
                    offsets.push_back(static_cast<float>(peek().numeric / 100.0));
                    advance();
                } else if (peek().type == TokenType::Comma) {
                    advance();
                } else {
                    advance(); // skip unexpected
                }
                skipWhitespace();
            }

            // Parse declarations block
            if (!atEnd() && peek().type == TokenType::LeftBrace) {
                advance(); // skip '{'
                // Collect tokens until matching '}'
                std::vector<Token> declTokens;
                int depth = 1;
                while (!atEnd() && depth > 0) {
                    if (peek().type == TokenType::LeftBrace) ++depth;
                    else if (peek().type == TokenType::RightBrace) {
                        --depth;
                        if (depth <= 0) { advance(); break; }
                    }
                    declTokens.push_back(peek());
                    advance();
                }
                // Parse declarations from collected tokens
                declTokens.push_back(Token{TokenType::EndOfFile, "", 0.0, ""});
                Parser declParser(declTokens);
                auto decls = declParser.parseDeclarationList();

                // Create a stop for each offset
                for (float offset : offsets) {
                    block.stops.push_back({offset, decls});
                }
            }
            skipWhitespace();
        }

        if (!atEnd() && peek().type == TokenType::RightBrace) advance();

        // Sort stops by offset
        std::sort(block.stops.begin(), block.stops.end(),
            [](const KeyframeStop& a, const KeyframeStop& b) {
                return a.offset < b.offset;
            });

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
        // Enhanced error recovery: skip to next semicolon or closing brace,
        // respecting nested blocks so we don't consume too much.
        int braceDepth = 0;
        while (!atEnd()) {
            auto type = peek().type;
            if (type == TokenType::LeftBrace) {
                braceDepth++;
                advance();
                continue;
            }
            if (type == TokenType::RightBrace) {
                if (braceDepth > 0) {
                    braceDepth--;
                    advance();
                    continue;
                }
                // Don't consume the closing brace of our containing block
                return;
            }
            if (type == TokenType::Semicolon && braceDepth == 0) {
                advance(); // consume the semicolon
                return;
            }
            advance();
        }
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
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
    if (ec != std::errc()) return 0;
    // unit is ignored for now (assume px)
    return num;
}

// Trim whitespace helper
void trimInPlace(std::string& str) {
    size_t a = str.find_first_not_of(" \t\n\r\f");
    size_t b = str.find_last_not_of(" \t\n\r\f");
    str = (a == std::string::npos) ? "" : str.substr(a, b - a + 1);
}

// Compare helper: apply a comparison operator
bool applyComparison(float lhs, const std::string& op, float rhs) {
    if (op == ">") return lhs > rhs;
    if (op == ">=") return lhs >= rhs;
    if (op == "<") return lhs < rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == "=") return lhs == rhs;
    return false;
}

// Resolve a media feature name to its value from the context
float resolveMediaFeatureValue(const std::string& name, const MediaContext& ctx) {
    if (name == "width") return ctx.viewportWidth;
    if (name == "height") return ctx.viewportHeight;
    return -1;
}

// Try to parse range syntax: "width > 500px", "width >= 500px",
// "500px < width", "500px <= width <= 800px"
// Returns true if parsed as range, false if not range syntax.
bool tryEvaluateRange(const std::string& f, const MediaContext& ctx, bool& result) {
    // Look for comparison operators: >=, <=, >, <, =
    // Possible forms:
    //   feature > value
    //   feature >= value
    //   value < feature
    //   value <= feature <= value2

    // Find comparison operators
    struct CompPart { std::string token; bool isOp; };
    std::vector<CompPart> parts;
    size_t i = 0;
    std::string current;
    while (i < f.size()) {
        if ((f[i] == '>' || f[i] == '<' || f[i] == '=') && i > 0) {
            if (!current.empty()) {
                std::string t = current; trimInPlace(t);
                if (!t.empty()) parts.push_back({t, false});
                current.clear();
            }
            std::string op(1, f[i]);
            if (i + 1 < f.size() && f[i + 1] == '=') { op += '='; i++; }
            parts.push_back({op, true});
        } else {
            current += f[i];
        }
        i++;
    }
    if (!current.empty()) {
        std::string t = current; trimInPlace(t);
        if (!t.empty()) parts.push_back({t, false});
    }

    // Need at least: value op value (3 parts) with at least one operator
    if (parts.size() < 3) return false;

    // Check if this looks like range syntax (has comparison operators)
    bool hasOp = false;
    for (auto& p : parts) if (p.isOp) { hasOp = true; break; }
    if (!hasOp) return false;

    // Simple range: feature op value  or  value op feature
    if (parts.size() == 3 && parts[1].isOp) {
        float featureVal = resolveMediaFeatureValue(parts[0].token, ctx);
        if (featureVal >= 0) {
            // feature op value
            float rhs = parseMediaLength(parts[2].token);
            result = applyComparison(featureVal, parts[1].token, rhs);
            return true;
        }
        featureVal = resolveMediaFeatureValue(parts[2].token, ctx);
        if (featureVal >= 0) {
            // value op feature
            float lhs = parseMediaLength(parts[0].token);
            result = applyComparison(lhs, parts[1].token, featureVal);
            return true;
        }
    }

    // Chained range: value op feature op value2 (5 parts)
    if (parts.size() == 5 && parts[1].isOp && parts[3].isOp) {
        float featureVal = resolveMediaFeatureValue(parts[2].token, ctx);
        if (featureVal >= 0) {
            float lhs = parseMediaLength(parts[0].token);
            float rhs = parseMediaLength(parts[4].token);
            result = applyComparison(lhs, parts[1].token, featureVal) &&
                     applyComparison(featureVal, parts[3].token, rhs);
            return true;
        }
    }

    return false;
}

// Evaluate a single media feature like "(min-width: 768px)" or "(width > 500px)"
bool evaluateMediaFeature(const std::string& feature, const MediaContext& ctx) {
    // Strip parens and trim
    std::string f = feature;
    if (!f.empty() && f.front() == '(') f.erase(0, 1);
    if (!f.empty() && f.back() == ')') f.pop_back();
    trimInPlace(f);

    if (f.empty()) return false;

    // Try range syntax first
    bool rangeResult = false;
    if (tryEvaluateRange(f, ctx, rangeResult)) {
        return rangeResult;
    }

    auto colonPos = f.find(':');
    if (colonPos == std::string::npos) {
        // Boolean feature, e.g. (color)
        return true;
    }

    std::string name = f.substr(0, colonPos);
    std::string value = f.substr(colonPos + 1);
    trimInPlace(name); trimInPlace(value);

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

    // Extract parenthesized features and connecting keywords (and/or)
    std::vector<std::string> features;
    std::vector<std::string> connectors; // "and" or "or" between features

    size_t i = 0;
    while (i < cond.size()) {
        auto paren = cond.find('(', i);
        if (paren == std::string::npos) break;

        // Check for connector keyword between previous feature and this one
        if (!features.empty()) {
            std::string between = cond.substr(i, paren - i);
            trimInPlace(between);
            if (between == "or") connectors.push_back("or");
            else connectors.push_back("and"); // default is "and"
        }

        int depth = 0;
        size_t j = paren;
        while (j < cond.size()) {
            if (cond[j] == '(') depth++;
            else if (cond[j] == ')') { depth--; if (depth == 0) break; }
            j++;
        }
        features.push_back(cond.substr(paren, j - paren + 1));
        i = j + 1;
    }

    // Evaluate with and/or logic
    bool result = true;
    if (!features.empty()) {
        result = evaluateMediaFeature(features[0], ctx);
        for (size_t fi = 1; fi < features.size(); fi++) {
            bool val = evaluateMediaFeature(features[fi], ctx);
            if (fi - 1 < connectors.size() && connectors[fi - 1] == "or") {
                result = result || val;
            } else {
                result = result && val;
            }
        }
    }

    return negate ? !result : result;
}

} // namespace htmlayout::css
