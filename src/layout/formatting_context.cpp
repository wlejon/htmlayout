#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/inline.h"
#include "layout/flex.h"
#include "layout/table.h"
#include "layout/grid.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <algorithm>

namespace htmlayout::layout {

namespace {

// Get a style value with fallback
const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

} // anonymous namespace

// Resolve a single CSS length token (number + unit) to pixels.
static float resolveSingleLength(const std::string& value, float referenceSize, float fontSize) {
    if (value.empty() || value == "auto" || value == "none" || value == "normal") {
        return 0.0f;
    }

    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;

    auto [ptr, ec] = std::from_chars(begin, end, num);
    if (ec != std::errc()) {
        if (value == "thin") return 1.0f;
        if (value == "medium") return 3.0f;
        if (value == "thick") return 5.0f;
        return 0.0f;
    }

    std::string unit(ptr, end);

    if (unit.empty() || unit == "px") return num;
    if (unit == "em") return num * fontSize;
    if (unit == "rem") return num * 16.0f;
    if (unit == "%") return num * referenceSize / 100.0f;
    if (unit == "vw") return num * referenceSize / 100.0f;
    if (unit == "vh") return num * referenceSize / 100.0f;
    if (unit == "vmin") return num * referenceSize / 100.0f;
    if (unit == "vmax") return num * referenceSize / 100.0f;
    if (unit == "pt") return num * 96.0f / 72.0f;
    if (unit == "ch") return num * fontSize * 0.5f;
    if (unit == "ex") return num * fontSize * 0.5f;
    if (unit == "cm") return num * 96.0f / 2.54f;
    if (unit == "mm") return num * 96.0f / 25.4f;
    if (unit == "in") return num * 96.0f;
    if (unit == "pc") return num * 96.0f / 6.0f;

    return num;
}

// Tokenize a calc() expression into numbers-with-units and operators.
// Supports +, -, *, / and nested parentheses.
static float evalCalc(const std::string& expr, float referenceSize, float fontSize) {
    // Simple recursive-descent parser for calc() expressions.
    // Supports: number+unit, +, -, *, /, parentheses, nested calc().
    struct CalcParser {
        const std::string& s;
        size_t pos;
        float refSize, fontSz;

        void skipSpaces() {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        }

        // Parse a primary: number+unit, (expr), or calc(expr)
        float parsePrimary() {
            skipSpaces();
            if (pos >= s.size()) return 0.0f;

            // Handle nested calc(...)
            if (pos + 5 <= s.size() && s.substr(pos, 5) == "calc(") {
                pos += 5;
                float val = parseExpr();
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') pos++;
                return val;
            }

            // Handle parenthesized sub-expression
            if (s[pos] == '(') {
                pos++;
                float val = parseExpr();
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') pos++;
                return val;
            }

            // Parse number + optional unit
            size_t numStart = pos;
            // Handle leading sign
            if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
            // Digits and decimal
            while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) pos++;

            std::string numStr = s.substr(numStart, pos - numStart);

            // Parse unit
            size_t unitStart = pos;
            while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) pos++;
            // Handle %
            if (pos < s.size() && s[pos] == '%') pos++;

            std::string token = s.substr(numStart, pos - numStart);
            return resolveSingleLength(token, refSize, fontSz);
        }

        // Parse multiplicative: primary (* or / primary)*
        float parseMul() {
            float left = parsePrimary();
            while (true) {
                skipSpaces();
                if (pos >= s.size()) break;
                if (s[pos] == '*') {
                    pos++; skipSpaces();
                    left *= parsePrimary();
                } else if (s[pos] == '/') {
                    pos++; skipSpaces();
                    float right = parsePrimary();
                    if (right != 0.0f) left /= right;
                } else {
                    break;
                }
            }
            return left;
        }

        // Parse additive: mul (+ or - mul)*
        float parseExpr() {
            float left = parseMul();
            while (true) {
                skipSpaces();
                if (pos >= s.size()) break;
                if (s[pos] == '+' && (pos + 1 < s.size() && s[pos + 1] == ' ')) {
                    pos++; skipSpaces();
                    left += parseMul();
                } else if (s[pos] == '-' && (pos + 1 < s.size() && s[pos + 1] == ' ')) {
                    pos++; skipSpaces();
                    left -= parseMul();
                } else if (s[pos] == '+' || s[pos] == '-') {
                    // Could be a signed number (no spaces around operator)
                    // In valid calc(), + and - require spaces, so try as operator
                    char op = s[pos];
                    pos++; skipSpaces();
                    float right = parseMul();
                    if (op == '+') left += right;
                    else left -= right;
                } else {
                    break;
                }
            }
            return left;
        }
    };

    CalcParser parser{expr, 0, referenceSize, fontSize};
    return parser.parseExpr();
}

bool isIntrinsicSizingKeyword(const std::string& value) {
    return value == "min-content" || value == "max-content" || value == "fit-content";
}

float computeMinContentWidth(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(style, "font-family");
    const std::string& fontWeight = styleVal(style, "font-weight");

    float maxChildMin = 0.0f;

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Min-content: each word on its own line, take the widest word
            std::string text = child->textContent();
            std::string word;
            float widestWord = 0.0f;
            for (size_t i = 0; i <= text.size(); i++) {
                char c = (i < text.size()) ? text[i] : ' ';
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!word.empty()) {
                        float w = metrics.measureWidth(word, fontFamily, fontSize, fontWeight);
                        widestWord = std::max(widestWord, w);
                        word.clear();
                    }
                } else {
                    word += c;
                }
            }
            maxChildMin = std::max(maxChildMin, widestWord);
        } else {
            auto& cs = child->computedStyle();
            if (styleVal(cs, "display") == "none") continue;
            float childMin = computeMinContentWidth(child, metrics);
            // Add padding/border
            float ph = resolveLength(styleVal(cs, "padding-left"), 0, fontSize) +
                       resolveLength(styleVal(cs, "padding-right"), 0, fontSize);
            float bh = resolveLength(styleVal(cs, "border-left-width"), 0, fontSize) +
                       resolveLength(styleVal(cs, "border-right-width"), 0, fontSize);
            maxChildMin = std::max(maxChildMin, childMin + ph + bh);
        }
    }
    return maxChildMin;
}

float computeMaxContentWidth(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(style, "font-family");
    const std::string& fontWeight = styleVal(style, "font-weight");

    float maxChildMax = 0.0f;

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Max-content: no wrapping, measure the whole text as one line
            std::string text = child->textContent();
            // Collapse whitespace
            std::string collapsed;
            bool lastSpace = false;
            for (char c : text) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!lastSpace && !collapsed.empty()) { collapsed += ' '; lastSpace = true; }
                } else {
                    collapsed += c; lastSpace = false;
                }
            }
            if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
            float w = metrics.measureWidth(collapsed, fontFamily, fontSize, fontWeight);
            maxChildMax = std::max(maxChildMax, w);
        } else {
            auto& cs = child->computedStyle();
            if (styleVal(cs, "display") == "none") continue;
            float childMax = computeMaxContentWidth(child, metrics);
            float ph = resolveLength(styleVal(cs, "padding-left"), 0, fontSize) +
                       resolveLength(styleVal(cs, "padding-right"), 0, fontSize);
            float bh = resolveLength(styleVal(cs, "border-left-width"), 0, fontSize) +
                       resolveLength(styleVal(cs, "border-right-width"), 0, fontSize);
            maxChildMax = std::max(maxChildMax, childMax + ph + bh);
        }
    }
    return maxChildMax;
}

float resolveLength(const std::string& value, float referenceSize, float fontSize) {
    if (value.empty() || value == "auto" || value == "none" || value == "normal") {
        return 0.0f;
    }

    // Handle calc() expressions
    if (value.size() > 5 && value.substr(0, 5) == "calc(") {
        // Extract the expression inside calc(...)
        std::string expr = value.substr(5);
        if (!expr.empty() && expr.back() == ')') expr.pop_back();
        return evalCalc(expr, referenceSize, fontSize);
    }

    return resolveSingleLength(value, referenceSize, fontSize);
}

float resolveLineHeight(const std::string& value, float fontSize) {
    if (value.empty() || value == "normal") {
        return fontSize * 1.2f;
    }

    // Unitless number: multiplier of font-size
    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;
    auto [ptr, ec] = std::from_chars(begin, end, num);
    if (ec == std::errc()) {
        std::string unit(ptr, end);
        if (unit.empty()) {
            // Unitless: treat as multiplier
            return num * fontSize;
        }
    }

    // Otherwise, resolve as a regular length
    return resolveLength(value, 0, fontSize);
}

float resolveLength(const std::string& value, float referenceSize, float fontSize,
                    float viewportWidth, float viewportHeight) {
    if (value.empty() || value == "auto" || value == "none" || value == "normal") {
        return 0.0f;
    }

    // Handle calc() with viewport dimensions
    if (value.size() > 5 && value.substr(0, 5) == "calc(") {
        std::string expr = value.substr(5);
        if (!expr.empty() && expr.back() == ')') expr.pop_back();
        // For calc, we still use the basic evalCalc which uses referenceSize for vw/vh
        // A more complete implementation would thread viewport through
        return evalCalc(expr, referenceSize, fontSize);
    }

    // For vw/vh/vmin/vmax, use actual viewport dimensions
    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;
    auto [ptr, ec] = std::from_chars(begin, end, num);
    if (ec != std::errc()) {
        return resolveSingleLength(value, referenceSize, fontSize);
    }
    std::string unit(ptr, end);
    if (unit == "vw") return num * viewportWidth / 100.0f;
    if (unit == "vh") return num * viewportHeight / 100.0f;
    if (unit == "vmin") return num * std::min(viewportWidth, viewportHeight) / 100.0f;
    if (unit == "vmax") return num * std::max(viewportWidth, viewportHeight) / 100.0f;

    return resolveSingleLength(value, referenceSize, fontSize);
}

Edges resolveEdges(const css::ComputedStyle& style,
                   const std::string& prefix,
                   float referenceWidth,
                   float fontSize) {
    Edges e;
    e.top = resolveLength(styleVal(style, prefix + "-top"), referenceWidth, fontSize);
    e.right = resolveLength(styleVal(style, prefix + "-right"), referenceWidth, fontSize);
    e.bottom = resolveLength(styleVal(style, prefix + "-bottom"), referenceWidth, fontSize);
    e.left = resolveLength(styleVal(style, prefix + "-left"), referenceWidth, fontSize);
    return e;
}

void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    const std::string& display = styleVal(style, "display");

    if (display == "none") {
        // Hidden — zero-size box, skip children
        node->box = LayoutBox{};
        return;
    }

    if (display == "flex" || display == "inline-flex") {
        layoutFlex(node, availableWidth, metrics);
    } else if (display == "grid" || display == "inline-grid") {
        layoutGrid(node, availableWidth, metrics);
    } else if (display == "inline" || display == "inline-block") {
        layoutInline(node, availableWidth, metrics);
    } else if (display == "table" || display == "inline-table") {
        layoutTable(node, availableWidth, metrics);
    } else {
        // block, list-item, or anything else defaults to block layout
        layoutBlock(node, availableWidth, metrics);
    }
}

} // namespace htmlayout::layout
