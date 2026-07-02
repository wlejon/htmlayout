#include "css/tokenizer.h"
#include "../from_chars_compat.h"
#include <cctype>
#include <cmath>
#include <charconv>
#include <cstdlib>

namespace htmlayout::css {

namespace {

class Tokenizer {
public:
    explicit Tokenizer(const std::string& input) : m_input(input), m_pos(0) {}

    std::vector<Token> run() {
        std::vector<Token> tokens;
        while (m_pos < m_input.size()) {
            Token tok = consumeToken();
            tokens.push_back(std::move(tok));
        }
        tokens.push_back(Token{TokenType::EndOfFile, "", 0.0, ""});
        return tokens;
    }

private:
    const std::string& m_input;
    size_t m_pos;

    char peek(size_t offset = 0) const {
        size_t idx = m_pos + offset;
        return idx < m_input.size() ? m_input[idx] : '\0';
    }

    char advance() {
        return m_pos < m_input.size() ? m_input[m_pos++] : '\0';
    }

    bool atEnd() const { return m_pos >= m_input.size(); }

    static bool isNameStart(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) >= 0x80);
    }

    static bool isNameChar(char c) {
        return isNameStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-';
    }

    static bool isDigit(char c) {
        return std::isdigit(static_cast<unsigned char>(c));
    }

    static bool isWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    }

    static bool isHexDigit(char c) {
        return std::isxdigit(static_cast<unsigned char>(c));
    }

    static void appendUtf8(std::string& out, unsigned long cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // Consume a CSS "escaped code point" (spec section 4.3.7), assuming the
    // leading backslash has already been consumed. 1-6 hex digits decode as
    // a Unicode code point (UTF-8 encoded into `out`), consuming one
    // trailing whitespace character as the escape's terminator; anything
    // else is a literal single-character escape (e.g. "\." in a selector).
    void consumeEscapedCodePoint(std::string& out) {
        if (atEnd()) return;
        if (isHexDigit(peek())) {
            std::string hex;
            for (int i = 0; i < 6 && isHexDigit(peek()); i++) hex += advance();
            if (!atEnd() && isWhitespace(peek())) advance();
            unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                cp = 0xFFFD; // REPLACEMENT CHARACTER
            }
            appendUtf8(out, cp);
        } else {
            out += advance();
        }
    }

    // Check if next chars start a number: optional sign, then digit or dot+digit
    bool startsNumber(size_t offset = 0) const {
        char c = peek(offset);
        if (isDigit(c)) return true;
        if (c == '.' && isDigit(peek(offset + 1))) return true;
        if ((c == '+' || c == '-')) {
            char next = peek(offset + 1);
            if (isDigit(next)) return true;
            if (next == '.' && isDigit(peek(offset + 2))) return true;
        }
        return false;
    }

    // Check if next chars start an identifier
    bool startsIdentifier(size_t offset = 0) const {
        char c = peek(offset);
        if (isNameStart(c)) return true;
        if (c == '-') {
            char next = peek(offset + 1);
            return isNameStart(next) || next == '-';
        }
        if (c == '\\' && peek(offset + 1) != '\n') return true;
        return false;
    }

    void consumeWhitespace() {
        while (!atEnd() && isWhitespace(peek())) advance();
    }

    // Consume a CSS comment /* ... */
    bool tryConsumeComment() {
        if (peek(0) == '/' && peek(1) == '*') {
            m_pos += 2;
            while (!atEnd()) {
                if (peek(0) == '*' && peek(1) == '/') {
                    m_pos += 2;
                    return true;
                }
                advance();
            }
            return true; // unterminated comment, consume to end
        }
        return false;
    }

    std::string consumeName() {
        std::string name;
        while (!atEnd()) {
            char c = peek();
            if (isNameChar(c)) {
                name += advance();
            } else if (c == '\\' && peek(1) != '\n') {
                advance(); // skip backslash
                consumeEscapedCodePoint(name);
            } else {
                break;
            }
        }
        return name;
    }

    // Consume a numeric value, returns {integer_or_float, string_repr}
    std::pair<double, std::string> consumeNumber() {
        std::string repr;
        // optional sign
        if (peek() == '+' || peek() == '-') repr += advance();
        // integer part
        while (isDigit(peek())) repr += advance();
        // decimal part
        if (peek() == '.' && isDigit(peek(1))) {
            repr += advance(); // '.'
            while (isDigit(peek())) repr += advance();
        }
        // exponent
        if ((peek() == 'e' || peek() == 'E') &&
            (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
            repr += advance(); // 'e'/'E'
            if (peek() == '+' || peek() == '-') repr += advance();
            while (isDigit(peek())) repr += advance();
        }
        double val = 0.0;
        auto [ptr, ec] = htmlayout::from_chars_fp(repr.data(), repr.data() + repr.size(), val);
        if (ec != std::errc()) val = 0.0;
        return {val, repr};
    }

    Token consumeNumericToken() {
        auto [num, repr] = consumeNumber();
        if (startsIdentifier()) {
            std::string unit = consumeName();
            return Token{TokenType::Dimension, repr, num, unit};
        }
        if (peek() == '%') {
            advance();
            return Token{TokenType::Percentage, repr, num, ""};
        }
        return Token{TokenType::Number, repr, num, ""};
    }

    Token consumeStringToken(char quote) {
        std::string value;
        while (!atEnd()) {
            char c = advance();
            if (c == quote) {
                return Token{TokenType::String, value, 0.0, ""};
            }
            if (c == '\\') {
                if (atEnd()) continue;
                char next = peek();
                if (next == '\n') {
                    advance(); // escaped newline, consume and continue
                } else {
                    consumeEscapedCodePoint(value);
                }
            } else if (c == '\n') {
                // Unescaped newline in string is a parse error; end the string
                return Token{TokenType::String, value, 0.0, ""};
            } else {
                value += c;
            }
        }
        // unterminated string
        return Token{TokenType::String, value, 0.0, ""};
    }

    Token consumeIdentLikeToken() {
        std::string name = consumeName();
        if (peek() == '(') {
            advance();
            return Token{TokenType::Function, name, 0.0, ""};
        }
        return Token{TokenType::Ident, name, 0.0, ""};
    }

    Token consumeToken() {
        // Skip comments
        while (tryConsumeComment()) {}

        if (atEnd()) {
            return Token{TokenType::EndOfFile, "", 0.0, ""};
        }

        char c = peek();

        // Whitespace
        if (isWhitespace(c)) {
            consumeWhitespace();
            return Token{TokenType::Whitespace, " ", 0.0, ""};
        }

        // Strings
        if (c == '"' || c == '\'') {
            advance();
            return consumeStringToken(c);
        }

        // Hash
        if (c == '#') {
            advance();
            if (isNameChar(peek()) || peek() == '\\') {
                std::string name = consumeName();
                return Token{TokenType::Hash, name, 0.0, ""};
            }
            return Token{TokenType::Delim, "#", 0.0, ""};
        }

        // Number starting with + or digit
        if (isDigit(c)) {
            return consumeNumericToken();
        }
        if (c == '+') {
            if (startsNumber()) return consumeNumericToken();
            advance();
            return Token{TokenType::Delim, "+", 0.0, ""};
        }
        if (c == '-') {
            if (startsNumber()) return consumeNumericToken();
            // CDC -->
            if (peek(1) == '-' && peek(2) == '>') {
                m_pos += 3;
                return Token{TokenType::CDC, "-->", 0.0, ""};
            }
            if (startsIdentifier()) return consumeIdentLikeToken();
            advance();
            return Token{TokenType::Delim, "-", 0.0, ""};
        }
        if (c == '.') {
            if (startsNumber()) return consumeNumericToken();
            advance();
            return Token{TokenType::Delim, ".", 0.0, ""};
        }

        // CDO <!--
        if (c == '<' && peek(1) == '!' && peek(2) == '-' && peek(3) == '-') {
            m_pos += 4;
            return Token{TokenType::CDO, "<!--", 0.0, ""};
        }

        // At-keyword
        if (c == '@') {
            advance();
            if (startsIdentifier()) {
                std::string name = consumeName();
                return Token{TokenType::AtKeyword, name, 0.0, ""};
            }
            return Token{TokenType::Delim, "@", 0.0, ""};
        }

        // Backslash (escaped identifier)
        if (c == '\\') {
            if (peek(1) != '\n') {
                return consumeIdentLikeToken();
            }
            advance();
            return Token{TokenType::Delim, "\\", 0.0, ""};
        }

        // Identifier
        if (isNameStart(c)) {
            return consumeIdentLikeToken();
        }

        // Single-character tokens
        advance();
        switch (c) {
            case '(': return Token{TokenType::LeftParen, "(", 0.0, ""};
            case ')': return Token{TokenType::RightParen, ")", 0.0, ""};
            case '[': return Token{TokenType::LeftBracket, "[", 0.0, ""};
            case ']': return Token{TokenType::RightBracket, "]", 0.0, ""};
            case '{': return Token{TokenType::LeftBrace, "{", 0.0, ""};
            case '}': return Token{TokenType::RightBrace, "}", 0.0, ""};
            case ':': return Token{TokenType::Colon, ":", 0.0, ""};
            case ';': return Token{TokenType::Semicolon, ";", 0.0, ""};
            case ',': return Token{TokenType::Comma, ",", 0.0, ""};
            default:
                return Token{TokenType::Delim, std::string(1, c), 0.0, ""};
        }
    }
};

} // anonymous namespace

std::vector<Token> tokenize(const std::string& css) {
    Tokenizer tokenizer(css);
    return tokenizer.run();
}

} // namespace htmlayout::css
