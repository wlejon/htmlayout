#include "layout/formatting_context.h"
#include "../from_chars_compat.h"
#include "layout/block.h"
#include "layout/inline.h"
#include "layout/flex.h"
#include "layout/table.h"
#include "layout/grid.h"
#include "layout/style_util.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <algorithm>

namespace htmlayout::layout {

using layout::styleVal;

// Resolve a single CSS length token (number + unit) to pixels.
static float resolveSingleLength(const std::string& value, float referenceSize, float fontSize) {
    if (value.empty() || value == "auto" || value == "none" || value == "normal") {
        return 0.0f;
    }

    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;

    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
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

    // Replaced elements (<input>, <canvas>, <svg>, etc.) report their content
    // size via intrinsicSize() rather than having children to measure. Without
    // this, a childless <input type="button"> would report min-content 0 and
    // the flex algorithm would shrink it to padding-only width.
    {
        float iw = 0, ih = 0;
        if (node->intrinsicSize(iw, ih, 0.0f)) return iw;
    }

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(style, "font-family");
    const std::string& fontWeight = styleVal(style, "font-weight");
    // letter-spacing inflates per-char width; the inline layout in text.cpp
    // adds it once per character, so intrinsic measurement must match or
    // parents grant too little width and force unwanted wraps.
    float letterSpacing = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);

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
                        if (letterSpacing != 0) w += letterSpacing * static_cast<float>(word.size());
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

    // Replaced elements report their own content width via intrinsicSize().
    // See the same note in computeMinContentWidth above.
    {
        float iw = 0, ih = 0;
        if (node->intrinsicSize(iw, ih, 0.0f)) return iw;
    }

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(style, "font-family");
    const std::string& fontWeight = styleVal(style, "font-weight");
    // letter-spacing / word-spacing inflate the laid-out width; intrinsic
    // measurement must match what text.cpp will produce or callers wrap.
    float letterSpacing = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
    float wordSpacing   = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);

    // Determine if this container lays out children horizontally (sum) vs vertically (max)
    const std::string& display = styleVal(style, "display");
    const std::string& flexDir = styleVal(style, "flex-direction");
    bool isHorizontal = (display == "flex" || display == "inline-flex") &&
                        (flexDir.empty() || flexDir == "row" || flexDir == "row-reverse");

    float maxChildMax = 0.0f;
    float sumChildMax = 0.0f;

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Max-content: no wrapping, measure the whole text as one line
            std::string text = child->textContent();
            // Collapse whitespace
            std::string collapsed;
            bool lastSpace = false;
            int spaceCount = 0;
            for (char c : text) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!lastSpace && !collapsed.empty()) {
                        collapsed += ' '; lastSpace = true; ++spaceCount;
                    }
                } else {
                    collapsed += c; lastSpace = false;
                }
            }
            if (!collapsed.empty() && collapsed.back() == ' ') {
                collapsed.pop_back();
                --spaceCount;
            }
            float w = metrics.measureWidth(collapsed, fontFamily, fontSize, fontWeight);
            if (letterSpacing != 0 && !collapsed.empty())
                w += letterSpacing * static_cast<float>(collapsed.size());
            if (wordSpacing != 0 && spaceCount > 0)
                w += wordSpacing * static_cast<float>(spaceCount);
            maxChildMax = std::max(maxChildMax, w);
            sumChildMax += w;
        } else {
            auto& cs = child->computedStyle();
            if (styleVal(cs, "display") == "none") continue;
            float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            float ph = resolveLength(styleVal(cs, "padding-left"), 0, childFontSize) +
                       resolveLength(styleVal(cs, "padding-right"), 0, childFontSize);
            float bh = resolveLength(styleVal(cs, "border-left-width"), 0, childFontSize) +
                       resolveLength(styleVal(cs, "border-right-width"), 0, childFontSize);
            float mh = resolveLength(styleVal(cs, "margin-left"), 0, childFontSize) +
                       resolveLength(styleVal(cs, "margin-right"), 0, childFontSize);
            // Use explicit width if set, otherwise recurse for intrinsic size
            const std::string& wVal = styleVal(cs, "width");
            float childMax;
            if (!wVal.empty() && wVal != "auto") {
                float w = resolveLength(wVal, 0, childFontSize);
                if (styleVal(cs, "box-sizing") == "border-box")
                    childMax = w + mh;
                else
                    childMax = w + ph + bh + mh;
            } else {
                childMax = computeMaxContentWidth(child, metrics) + ph + bh + mh;
            }
            // Apply min-width (specifies minimum content width)
            const std::string& minWVal = styleVal(cs, "min-width");
            if (!minWVal.empty() && minWVal != "auto") {
                float minW = resolveLength(minWVal, 0, childFontSize);
                float minTotal = (styleVal(cs, "box-sizing") == "border-box")
                    ? minW + mh : minW + ph + bh + mh;
                if (childMax < minTotal) childMax = minTotal;
            }
            maxChildMax = std::max(maxChildMax, childMax);
            sumChildMax += childMax;
        }
    }
    // Flex-row: children are side-by-side, so sum their widths.
    // Block/flex-column: children stack, so use the widest.
    if (isHorizontal) {
        // Add gaps between children
        float gap = resolveLength(styleVal(style, "column-gap"), 0, fontSize);
        int childCount = 0;
        for (auto* child : getLayoutChildren(node)) {
            if (!child->isTextNode()) {
                auto& cs = child->computedStyle();
                if (styleVal(cs, "display") != "none") childCount++;
            } else {
                childCount++;
            }
        }
        if (childCount > 1) sumChildMax += gap * (childCount - 1);
        return sumChildMax;
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
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
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
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
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

// Parse contain property to check for specific containment types.
static bool hasContainment(const css::ComputedStyle& style, const std::string& type) {
    auto it = style.find("contain");
    if (it == style.end() || it->second == "none") return false;
    const std::string& val = it->second;
    if (val == "strict") return true; // strict = size layout paint style
    if (val == "content") return type != "size"; // content = layout paint style
    return val.find(type) != std::string::npos;
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

    // CSS Containment L2: content-visibility: hidden acts like display:none
    // but preserves the element's box (it still occupies space per explicit size).
    const std::string& contentVis = styleVal(style, "content-visibility");
    if (contentVis == "hidden") {
        // Skip layout of children but keep the element's own box.
        // Use explicit size if set, otherwise 0.
        float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
        if (fontSize <= 0.0f) fontSize = 16.0f;
        node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
        node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);
        float specW = resolveLength(styleVal(style, "width"), availableWidth, fontSize);
        float specH = resolveLength(styleVal(style, "height"), 0, fontSize);
        node->box.contentRect.width = (specW > 0) ? specW : 0;
        node->box.contentRect.height = (specH > 0) ? specH : 0;
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

    // CSS Containment L2: contain: size — override content-based sizing
    // with explicit dimensions only. If no explicit size, use 0.
    if (hasContainment(style, "size")) {
        float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
        if (fontSize <= 0.0f) fontSize = 16.0f;
        const std::string& wVal = styleVal(style, "width");
        const std::string& hVal = styleVal(style, "height");
        if (wVal == "auto" || wVal.empty()) {
            // size containment with auto width: content width is already set by layout,
            // but for true size containment it should be 0 unless explicit
            // In practice, keep the layout width (block fills available) since that's
            // what browsers do for block-level elements with contain:size
        }
        if (hVal == "auto" || hVal.empty()) {
            node->box.contentRect.height = 0;
        }
    }
}

// ============================================================
// Post-layout pass: position absolute/fixed elements
// ============================================================

namespace {

// Resolve a dimension that returns -1 for auto/none/empty (sentinel)
float resolveDimAbs(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

// Check if a node establishes a containing block for absolute descendants.
// Per CSS spec: position != static, or has transform/filter/perspective,
// or has contain: layout/paint.
bool isContainingBlock(LayoutNode* node) {
    auto& style = node->computedStyle();
    const std::string& pos = styleVal(style, "position");
    if (pos == "relative" || pos == "absolute" || pos == "fixed" || pos == "sticky")
        return true;
    // transform, filter, and perspective also create containing blocks
    const std::string& transform = styleVal(style, "transform");
    if (!transform.empty() && transform != "none") return true;
    const std::string& filter = styleVal(style, "filter");
    if (!filter.empty() && filter != "none") return true;
    // CSS Containment L2: contain: layout or contain: paint creates a containing block
    if (hasContainment(style, "layout") || hasContainment(style, "paint"))
        return true;
    return false;
}

// Find the containing block for an absolute element: nearest positioned ancestor.
// Returns nullptr if none found (meaning use the initial containing block / viewport).
LayoutNode* findContainingBlock(LayoutNode* node) {
    for (LayoutNode* p = node->parent(); p != nullptr; p = p->parent()) {
        if (isContainingBlock(p)) return p;
    }
    return nullptr; // initial containing block
}

// Compute accumulated offset from a node's parent up to (but not including) an ancestor.
// This is the sum of contentRect.x/y of all intermediate nodes between
// the node's DOM parent and the containing block.
struct Offset { float x, y; };

Offset computeOffsetToAncestor(LayoutNode* from, LayoutNode* to) {
    Offset off{0, 0};
    for (LayoutNode* p = from; p != nullptr && p != to; p = p->parent()) {
        off.x += p->box.contentRect.x;
        off.y += p->box.contentRect.y;
    }
    return off;
}

// Layout and position a single absolute/fixed child.
void layoutAbsoluteChild(LayoutNode* child, float cbWidth, float cbHeight,
                         float cbOriginOffsetX, float cbOriginOffsetY,
                         float domParentOffsetX, float domParentOffsetY,
                         float viewportHeight, TextMetrics& metrics) {
    auto& childStyle = child->computedStyle();
    float fontSize = resolveLength(styleVal(childStyle, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    // Set available height for the child's own percentage resolution
    child->availableHeight = cbHeight;

    // Resolve offsets and explicit dimensions
    float left = resolveDimAbs(styleVal(childStyle, "left"), cbWidth, fontSize);
    float right = resolveDimAbs(styleVal(childStyle, "right"), cbWidth, fontSize);
    float specW = resolveDimAbs(styleVal(childStyle, "width"), cbWidth, fontSize);
    float top = resolveDimAbs(styleVal(childStyle, "top"), cbHeight, fontSize);
    float bottom = resolveDimAbs(styleVal(childStyle, "bottom"), cbHeight, fontSize);
    float specH = resolveDimAbs(styleVal(childStyle, "height"), cbHeight, fontSize);

    // Determine available width for layout
    // Shrink-wrap if width:auto and not both left+right set
    bool shrinkWrap = (specW < 0 && !(left >= 0 && right >= 0));
    if (shrinkWrap) {
        float maxCW = computeMaxContentWidth(child, metrics);
        if (maxCW > cbWidth) maxCW = cbWidth;
        layoutNode(child, maxCW + child->box.padding.left + child->box.padding.right +
                   child->box.border.left + child->box.border.right +
                   child->box.margin.left + child->box.margin.right, metrics);
    } else {
        layoutNode(child, cbWidth, metrics);
    }

    // Stretch width if both left and right are set and width is auto
    if (specW < 0 && left >= 0 && right >= 0) {
        float w = cbWidth - left - right -
                  child->box.margin.left - child->box.margin.right -
                  child->box.padding.left - child->box.padding.right -
                  child->box.border.left - child->box.border.right;
        if (w > 0) child->box.contentRect.width = w;
    }

    // Stretch height if both top and bottom are set and height is auto
    if (specH < 0 && top >= 0 && bottom >= 0) {
        float h = cbHeight - top - bottom -
                  child->box.margin.top - child->box.margin.bottom -
                  child->box.padding.top - child->box.padding.bottom -
                  child->box.border.top - child->box.border.bottom;
        if (h > 0) child->box.contentRect.height = h;
    }

    // Compute position in containing-block-relative space
    float xInCB = child->box.margin.left + child->box.padding.left + child->box.border.left;
    float yInCB = child->box.margin.top + child->box.padding.top + child->box.border.top;

    if (left >= 0) {
        xInCB = left + child->box.margin.left + child->box.padding.left + child->box.border.left;
    } else if (right >= 0) {
        xInCB = cbWidth - right - child->box.margin.right -
                child->box.padding.right - child->box.border.right - child->box.contentRect.width;
    }

    if (top >= 0) {
        yInCB = top + child->box.margin.top + child->box.padding.top + child->box.border.top;
    } else if (bottom >= 0) {
        yInCB = cbHeight - bottom - child->box.margin.bottom -
                child->box.padding.bottom - child->box.border.bottom - child->box.contentRect.height;
    }

    // Transform from CB space to DOM-parent-relative space.
    // cbOriginOffset: the CB's padding-box origin in absolute coordinates
    // domParentOffset: the DOM parent's content-area origin in absolute coordinates
    // contentRect must be relative to the DOM parent's content area
    child->box.contentRect.x = xInCB + cbOriginOffsetX - domParentOffsetX;
    child->box.contentRect.y = yInCB + cbOriginOffsetY - domParentOffsetY;
}

// Compute the absolute position of a node's content area origin
// by walking from root and accumulating contentRect offsets.
Offset computeAbsolutePosition(LayoutNode* node) {
    // Build path from root to node
    std::vector<LayoutNode*> path;
    for (LayoutNode* p = node; p != nullptr; p = p->parent()) {
        path.push_back(p);
    }
    // Walk from root (end of vector) toward node, accumulating offsets
    Offset off{0, 0};
    // The root's contentRect is in viewport space, so start from it
    // Each node's contentRect.x/y is relative to its parent's content area
    for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
        off.x += path[i]->box.contentRect.x;
        off.y += path[i]->box.contentRect.y;
    }
    return off;
}

// Recursive tree walk to find and position all absolute/fixed elements.
// Processes in depth-first pre-order so ancestor absolutes are positioned
// before their descendant absolutes.
void layoutAbsoluteElementsRecursive(LayoutNode* node, const Viewport& viewport,
                                      TextMetrics& metrics) {
    for (auto* child : node->children()) {
        if (!child || child->isTextNode()) continue;

        auto& style = child->computedStyle();
        const std::string& display = styleVal(style, "display");
        if (display == "none") continue;

        const std::string& pos = styleVal(style, "position");

        if (pos == "fixed") {
            // Fixed: containing block is the viewport
            float cbWidth = viewport.width;
            float cbHeight = viewport.height;

            // CB origin is (0, 0) in absolute space
            float cbOriginX = 0, cbOriginY = 0;

            // DOM parent's absolute content position
            Offset parentPos = computeAbsolutePosition(node);

            child->viewportHeight = viewport.height;
            layoutAbsoluteChild(child, cbWidth, cbHeight,
                                cbOriginX, cbOriginY,
                                parentPos.x, parentPos.y,
                                viewport.height, metrics);
        } else if (pos == "absolute") {
            // Find the containing block
            LayoutNode* cb = findContainingBlock(child);

            float cbWidth, cbHeight;
            float cbOriginX, cbOriginY;

            if (cb) {
                // Containing block is the padding box of the positioned ancestor
                cbWidth = cb->box.contentRect.width + cb->box.padding.left + cb->box.padding.right;
                cbHeight = cb->box.contentRect.height + cb->box.padding.top + cb->box.padding.bottom;

                // CB's padding-box origin in absolute coordinates
                Offset cbPos = computeAbsolutePosition(cb);
                cbOriginX = cbPos.x - cb->box.padding.left;
                cbOriginY = cbPos.y - cb->box.padding.top;
            } else {
                // No positioned ancestor: use viewport (initial containing block)
                cbWidth = viewport.width;
                cbHeight = viewport.height;
                cbOriginX = 0;
                cbOriginY = 0;
            }

            // DOM parent's absolute content position
            Offset parentPos = computeAbsolutePosition(node);

            child->viewportHeight = viewport.height;
            layoutAbsoluteChild(child, cbWidth, cbHeight,
                                cbOriginX, cbOriginY,
                                parentPos.x, parentPos.y,
                                viewport.height, metrics);
        }

        // Recurse into children (including into absolute elements, which can
        // contain further absolute descendants)
        layoutAbsoluteElementsRecursive(child, viewport, metrics);
    }
}

} // anonymous namespace

void layoutAbsoluteElements(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics) {
    if (!root) return;
    layoutAbsoluteElementsRecursive(root, viewport, metrics);
}

} // namespace htmlayout::layout
