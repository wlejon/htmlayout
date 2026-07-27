#include "layout/formatting_context.h"
#include "layout/text.h"
#include "../from_chars_compat.h"
#include "layout/block.h"
#include "layout/inline.h"
#include "layout/flex.h"
#include "layout/table.h"
#include "layout/grid.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <optional>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

// Current viewport for vw/vh/vmin/vmax resolution, set by layoutTree() before each
// pass. Layout is single-threaded, so a file-scoped value is the simplest way to
// reach the workhorse resolver (resolveSingleLength) which is called from hundreds
// of sites without a node/context in hand. 0 means "unknown" — see setLayoutViewport.
static float g_viewportWidth = 0.0f;
static float g_viewportHeight = 0.0f;

// Root element (<html>) font-size, used to resolve rem units. Threaded the same
// file-scoped way as the viewport since rem, like vw/vh, needs a document-global
// reference the per-site resolver has no node to reach. Defaults to 16 (the
// initial font-size) until layoutTree() sets the real root size.
static float g_rootFontSize = 16.0f;

void setLayoutViewport(float width, float height) {
    g_viewportWidth = width;
    g_viewportHeight = height;
}

void setRootFontSize(float px) {
    g_rootFontSize = (px > 0.0f) ? px : 16.0f;
}

// ch and ex are font-metric units: 1ch is the advance of "0" in the element's
// font, 1ex its x-height. Both depend on the font family AND size, which vary
// per element, so they can't be a once-per-pass global like the viewport. Layout
// sets this context (absolute px for the element's current font) right before it
// resolves that element's own lengths. 0 means "unset" — the resolver then falls
// back to the CSS-defined 0.5em approximation so callers without a font in hand
// (intrinsic-width probes, standalone resolveLength) keep working.
static float g_chPx = 0.0f;
static float g_exPx = 0.0f;

void setLengthFontContext(float chPx, float exPx) {
    g_chPx = chPx;
    g_exPx = exPx;
}

// Resolve a single CSS length token (number + unit) to pixels.
// Both resolveLength() overloads screen out the keywords before calling this, and
// from_chars rejects them anyway (falling through to the thin/medium/thick ladder,
// which returns 0 for them) — so re-testing them here would just be a second round
// of strlen-per-literal on the hottest path in layout.
static float resolveSingleLength(const std::string& value, float referenceSize, float fontSize) {
    if (value.empty()) return 0.0f;

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

    // The unit is a view of the tail, not a copy of it: this runs on every length
    // of every node of every pass, and building a std::string here to compare it
    // against a ladder of literals was most of what the ladder cost.
    //
    // Dispatched on length first, so `px` — far and away the common case — is
    // reached in one compare instead of walking past thirteen others.
    std::string_view unit(ptr, static_cast<size_t>(end - ptr));
    // Viewport units resolve against the real viewport (set by layoutTree). When a
    // viewport dimension is unknown (0), fall back to the percentage reference so
    // behavior matches the pre-viewport resolver rather than collapsing to 0.
    const auto vwRef = [&] { return g_viewportWidth  > 0.0f ? g_viewportWidth  : referenceSize; };
    const auto vhRef = [&] { return g_viewportHeight > 0.0f ? g_viewportHeight : referenceSize; };

    switch (unit.size()) {
    case 0:
        return num;
    case 1:
        if (unit[0] == '%') return num * referenceSize / 100.0f;
        break;
    case 2:
        if (unit == "px") return num;
        if (unit == "em") return num * fontSize;
        if (unit == "vw") return num * vwRef() / 100.0f;
        if (unit == "vh") return num * vhRef() / 100.0f;
        if (unit == "pt") return num * 96.0f / 72.0f;
        if (unit == "ch") return num * (g_chPx > 0.0f ? g_chPx : fontSize * 0.5f);
        if (unit == "ex") return num * (g_exPx > 0.0f ? g_exPx : fontSize * 0.5f);
        if (unit == "cm") return num * 96.0f / 2.54f;
        if (unit == "mm") return num * 96.0f / 25.4f;
        if (unit == "in") return num * 96.0f;
        if (unit == "pc") return num * 96.0f / 6.0f;
        break;
    case 3:
        if (unit == "rem") return num * g_rootFontSize;
        break;
    case 4:
        if (unit == "vmin") return num * std::min(vwRef(), vhRef()) / 100.0f;
        if (unit == "vmax") return num * std::max(vwRef(), vhRef()) / 100.0f;
        break;
    default:
        break;
    }

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

        // Parse a comma-separated argument list up to the closing ')'.
        // Consumes the ')'. Each argument is a full additive expression.
        std::vector<float> parseArgs() {
            std::vector<float> args;
            while (true) {
                args.push_back(parseExpr());
                skipSpaces();
                if (pos < s.size() && s[pos] == ',') { pos++; continue; }
                break;
            }
            skipSpaces();
            if (pos < s.size() && s[pos] == ')') pos++;
            return args;
        }

        // Case-insensitive match of a function name + '(' at the cursor.
        bool matchFunc(const char* name) {
            size_t n = std::strlen(name);
            if (pos + n + 1 > s.size()) return false;
            for (size_t i = 0; i < n; i++)
                if (std::tolower(static_cast<unsigned char>(s[pos + i])) != name[i])
                    return false;
            return s[pos + n] == '(';
        }

        // Parse a primary: math function, number+unit, (expr), or calc(expr)
        float parsePrimary() {
            skipSpaces();
            if (pos >= s.size()) return 0.0f;

            // CSS math functions: calc/min/max/clamp (nestable, case-insensitive).
            if (matchFunc("calc")) {
                pos += 5; // "calc("
                float val = parseExpr();
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') pos++;
                return val;
            }
            if (matchFunc("min")) {
                pos += 4; // "min("
                auto args = parseArgs();
                if (args.empty()) return 0.0f;
                float v = args[0];
                for (float a : args) v = std::min(v, a);
                return v;
            }
            if (matchFunc("max")) {
                pos += 4; // "max("
                auto args = parseArgs();
                if (args.empty()) return 0.0f;
                float v = args[0];
                for (float a : args) v = std::max(v, a);
                return v;
            }
            if (matchFunc("clamp")) {
                pos += 6; // "clamp("
                auto args = parseArgs();
                // clamp(MIN, VAL, MAX) == max(MIN, min(VAL, MAX)).
                if (args.size() < 3) return args.empty() ? 0.0f : args[0];
                float lo = args[0], val = args[1], hi = args[2];
                return std::max(lo, std::min(val, hi));
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

// True if the value is a CSS math function — calc()/min()/max()/clamp() — at the
// top level. These all flow through evalCalc, whose primary parser handles each
// function (and their nesting) directly, so the whole string is passed through.
static bool isMathFunction(const std::string& v) {
    // Every length resolution asks this, and almost every one of them is a plain
    // `16px` that has to walk all four names to say no. A math function is
    // `name(...)`, so a value that doesn't end in ')' cannot be one — check that
    // first and the common case leaves after a single byte compare.
    if (v.size() < 5 || v.back() != ')') return false;
    auto starts = [&](const char* f) {
        size_t n = std::strlen(f);
        if (v.size() < n + 1) return false;
        for (size_t i = 0; i < n; i++) {
            // ASCII-lower by hand: std::tolower is locale-aware, which means a
            // function call and a table lookup per character, on a path this hot.
            char c = v[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != f[i]) return false;
        }
        return v[n] == '(';
    };
    return starts("calc") || starts("min") || starts("max") || starts("clamp");
}

bool isIntrinsicSizingKeyword(const std::string& value) {
    return value == "min-content" || value == "max-content" || value == "fit-content";
}

// The uncached walks. Callers go through the caching wrappers below — the
// recursive per-child calls inside these bodies do too, so a cold pass fills
// the cache bottom-up and later passes only descend into invalidated subtrees.
static float computeMinContentWidthImpl(LayoutNode* node, TextMetrics& metrics);
static float computeMaxContentWidthImpl(LayoutNode* node, TextMetrics& metrics);

/**
 * Does this white-space value forbid a soft wrap inside a text run?
 *
 * CSS Sizing 3 §5.1: the min-content size of a string is the width of its
 * widest word ONLY where line breaking is allowed. Under `nowrap` and `pre`
 * there is no break opportunity at a space, so min-content equals max-content.
 * `pre-wrap` and `pre-line` preserve or collapse whitespace differently but
 * both still wrap, so they keep the widest-word rule.
 */
static bool whiteSpaceForbidsWrap(const std::string& ws) {
    return ws == "nowrap" || ws == "pre";
}

/**
 * Width of one unbreakable text run, measured as a single piece — the same
 * measurement the max-content path makes for nowrap/pre, factored out so the
 * two cannot drift.
 *
 * @param collapse  fold runs of whitespace to one space and trim the tail
 *                  (true for `nowrap`, false for `pre`, which keeps them)
 */
static float measureUnbreakableRun(std::string_view text, bool collapse,
                                   const std::string& fontFamily, float fontSize,
                                   const std::string& fontWeight,
                                   float letterSpacing, float wordSpacing,
                                   const std::string& textTransform,
                                   TextMetrics& metrics) {
    std::string piece;
    int spaceCount = 0;
    if (collapse) {
        bool lastSpace = false;
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!lastSpace && !piece.empty()) { piece += ' '; lastSpace = true; ++spaceCount; }
            } else {
                piece += c; lastSpace = false;
            }
        }
        if (!piece.empty() && piece.back() == ' ') { piece.pop_back(); --spaceCount; }
    } else {
        piece.assign(text);
        for (char c : piece) if (c == ' ') ++spaceCount;
    }
    piece = applyTextTransform(piece, textTransform);
    float w = metrics.measureWidth(piece, fontFamily, fontSize, fontWeight);
    if (letterSpacing != 0 && piece.size() > 1)
        w += letterSpacing * static_cast<float>(piece.size() - 1);
    if (wordSpacing != 0 && spaceCount > 0)
        w += wordSpacing * static_cast<float>(spaceCount);
    return w;
}

// Does the first (or last) text this inline subtree contributes begin (or end)
// with collapsible white space?
//
// It matters because an inline box's intrinsic width is measured with its own
// edge white space trimmed — the measurement cannot know whether that space
// will survive — so `<span>AAA</span><span> BBB</span>` measures as two boxes
// of "AAA" and "BBB" and loses the space between them, while layout keeps it
// and needs a space more room than the measurement asked for. Under a parent
// that is wider than its content nobody notices; a flex item is sized *to* its
// max-content width, so it came out one space too narrow and wrapped its own
// second half onto a second line.
//
// nullopt means this subtree contributes no text at that edge, so the walk
// should keep looking at the next sibling.
static std::optional<bool> subtreeEdgeSpace(LayoutNode* node, bool leading) {
    if (!node) return std::nullopt;
    if (node->isTextNode()) {
        std::string_view text = node->textContent();
        if (text.empty()) return std::nullopt;
        char edge = leading ? text.front() : text.back();
        return std::isspace(static_cast<unsigned char>(edge)) != 0;
    }
    const std::string& display = styleVal(node, Prop::Display);
    if (display == "none") return std::nullopt;
    const std::string& pos = styleVal(node, Prop::Position);
    if (pos == "absolute" || pos == "fixed") return std::nullopt;
    // Only a non-replaced inline box shares an inline formatting context with
    // what surrounds it. An atomic inline (inline-block and friends) or a
    // block-level child is a wall white space does not collapse across, so its
    // edge is a hard edge whatever its text says.
    if (!(display.empty() || display == "inline" || display == "contents"))
        return false;
    const std::string& ws = styleVal(node, Prop::WhiteSpace);
    if (!(ws.empty() || ws == "normal" || ws == "nowrap" || ws == "pre-line"))
        return false;                       // pre/pre-wrap: the space is kept, not collapsed

    auto children = getLayoutChildren(node);
    for (size_t i = 0; i < children.size(); ++i) {
        LayoutNode* child = children[leading ? i : children.size() - 1 - i];
        auto answer = subtreeEdgeSpace(child, leading);
        if (answer.has_value()) return answer;
    }
    return std::nullopt;
}

float computeMinContentWidth(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;
    if (node->cachedMinContentW >= 0.0f) return node->cachedMinContentW;
    float w = computeMinContentWidthImpl(node, metrics);
    node->cachedMinContentW = w;
    return w;
}

float computeMaxContentWidth(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;
    if (node->cachedMaxContentW >= 0.0f) return node->cachedMaxContentW;
    float w = computeMaxContentWidthImpl(node, metrics);
    node->cachedMaxContentW = w;
    return w;
}

static float computeMinContentWidthImpl(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;

    // Replaced elements (<input>, <canvas>, <svg>, etc.) report their content
    // size via intrinsicSize() rather than having children to measure. Without
    // this, a childless <input type="button"> would report min-content 0 and
    // the flex algorithm would shrink it to padding-only width.
    //
    // But when the element has an explicit CSS width, that overrides the
    // intrinsic. Per CSS Sizing 3 §5.2.1, percentage widths contribute 0 to
    // a parent's min-content (they can't be resolved without a containing
    // block); a length contributes that length. Without this gate, a
    // <canvas width="N"> with CSS width:100% feeds N back as min-content,
    // which propagates up through ancestors and (in a flex context) makes
    // an ancestor wider than its container — and on the next frame, JS
    // resizes the canvas to the new larger rect, growing without bound.
    {
        float iw = 0, ih = 0;
        if (node->intrinsicSize(iw, ih, 0.0f)) {
            const std::string& wVal = styleVal(node, Prop::Width);
            if (wVal.empty() || wVal == "auto") return iw;
            if (wVal.find('%') != std::string::npos) return 0.0f;
            float fs = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
            if (fs <= 0.0f) fs = 16.0f;
            return resolveLength(wVal, 0.0f, fs);
        }
    }

    auto& style = node->computedStyle();

    // Tables size by columns, not by the widest descendant: a table-row lays
    // its cells side by side, so the generic max-of-children walk below would
    // report the widest single cell instead of the column sum. Run the real
    // column algorithm (CSS2 §17.5.2.2) instead.
    {
        const std::string& d = styleVal(node, Prop::Display);
        if (d == "table" || d == "inline-table") {
            float minW = 0, maxW = 0;
            computeTableIntrinsicWidths(node, metrics, minW, maxW);
            return minW;
        }
    }

    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(node, Prop::FontFamily);
    const std::string& fontWeight = styleVal(node, Prop::FontWeight);
    // letter-spacing inflates per-char width; the inline layout in text.cpp
    // adds it once per character, so intrinsic measurement must match or
    // parents grant too little width and force unwanted wraps.
    float letterSpacing = resolveLength(styleVal(node, Prop::LetterSpacing), 0, fontSize);

    float maxChildMin = 0.0f;

    const std::string& displayMin = styleVal(node, Prop::Display);
    bool isFlexContainerMin = (displayMin == "flex" || displayMin == "inline-flex");

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Flex containers discard whitespace-only text nodes (CSS Flexbox §4)
            if (isFlexContainerMin) {
                bool ws = true;
                for (char c : child->textContent())
                    if (!std::isspace(static_cast<unsigned char>(c))) { ws = false; break; }
                if (ws) continue;
            }
            std::string_view text = child->textContent();

            // Where the containing element forbids wrapping, there is no
            // "widest word" to fall back to — the run is indivisible, so its
            // min-content size is its whole measured width. Without this a
            // `white-space: nowrap` box reports a min-content narrower than
            // the text it will actually paint, and anything sizing to
            // min-content (a shrink-to-fit table column, a flex item's
            // automatic minimum) comes out too small and the text spills.
            {
                const std::string& ws = styleVal(node, Prop::WhiteSpace);
                if (whiteSpaceForbidsWrap(ws)) {
                    float wordSpacing = resolveLength(
                        styleVal(node, Prop::WordSpacing), 0, fontSize);
                    const std::string& tt = styleVal(node, Prop::TextTransform);
                    float w;
                    if (ws == "pre") {
                        // `pre` still breaks at a newline, so the widest line wins.
                        w = 0.0f;
                        size_t start = 0;
                        for (size_t i = 0; i <= text.size(); i++) {
                            if (i == text.size() || text[i] == '\n') {
                                w = std::max(w, measureUnbreakableRun(
                                    text.substr(start, i - start), false, fontFamily,
                                    fontSize, fontWeight, letterSpacing, wordSpacing,
                                    tt, metrics));
                                start = i + 1;
                            }
                        }
                    } else {
                        w = measureUnbreakableRun(text, true, fontFamily, fontSize,
                            fontWeight, letterSpacing, wordSpacing, tt, metrics);
                    }
                    maxChildMin = std::max(maxChildMin, w);
                    continue;
                }
            }

            // Min-content: each word on its own line, take the widest word
            std::string word;
            float widestWord = 0.0f;
            for (size_t i = 0; i <= text.size(); i++) {
                char c = (i < text.size()) ? text[i] : ' ';
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!word.empty()) {
                        float w = metrics.measureWidth(word, fontFamily, fontSize, fontWeight);
                        // Letter-spacing on every slot incl. the trailing one,
                        // matching breakTextIntoRuns' measureWithSpacing (the
                        // width block.cpp gives each word item). A word measured
                        // narrower here than the item it becomes would let a box
                        // sized to min-content wrap its own widest word.
                        if (letterSpacing != 0 && !word.empty())
                            w += letterSpacing * static_cast<float>(word.size());
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
            if (styleVal(child, Prop::Display) == "none") continue;
            // Out-of-flow children contribute nothing to intrinsic sizes.
            const std::string& cpos = styleVal(child, Prop::Position);
            if (cpos == "absolute" || cpos == "fixed") continue;
            float childFontSize = resolveLength(styleVal(child, Prop::FontSize), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            float ph = resolveLength(styleVal(child, Prop::PaddingLeft), 0, childFontSize) +
                       resolveLength(styleVal(child, Prop::PaddingRight), 0, childFontSize);
            // Border widths only count when the side has a style (a styleless
            // border's used width is 0 even though the computed width is
            // "medium" = 3px).
            float bh = 0;
            if (styleVal(child, Prop::BorderLeftStyle) != "none")
                bh += resolveLength(styleVal(child, Prop::BorderLeftWidth), 0, childFontSize);
            if (styleVal(child, Prop::BorderRightStyle) != "none")
                bh += resolveLength(styleVal(child, Prop::BorderRightWidth), 0, childFontSize);
            // A border-collapse table's used border is zero: the collapsed
            // edge half-borders live inside its content width (they're part
            // of computeTableIntrinsicWidths' result), so adding the computed
            // border widths on top would double-count them.
            {
                const std::string& cd = styleVal(child, Prop::Display);
                if ((cd == "table" || cd == "inline-table") &&
                    styleVal(child, Prop::BorderCollapse) == "collapse") {
                    bh = 0;
                }
            }
            float mh = resolveLength(styleVal(child, Prop::MarginLeft), 0, childFontSize) +
                       resolveLength(styleVal(child, Prop::MarginRight), 0, childFontSize);
            // A child with a definite (non-percentage) width contributes that
            // width, not its content's min-content — same rule as the
            // max-content path below. Percentages can't be resolved against an
            // intrinsic size, so they fall back to the content measurement.
            const std::string& wVal = styleVal(child, Prop::Width);
            bool definiteW = !wVal.empty() && wVal != "auto" &&
                             wVal.find('%') == std::string::npos &&
                             !isIntrinsicSizingKeyword(wVal);
            float childMin;
            if (definiteW) {
                float w = resolveLength(wVal, 0, childFontSize);
                childMin = (styleVal(child, Prop::BoxSizing) == "border-box")
                    ? w + mh : w + ph + bh + mh;
            } else {
                childMin = computeMinContentWidth(child, metrics) + ph + bh + mh;
            }
            // Definite min-width floors the contribution; max-width caps it.
            const std::string& minWVal = styleVal(child, Prop::MinWidth);
            if (!minWVal.empty() && minWVal != "auto" &&
                minWVal.find('%') == std::string::npos) {
                float v = resolveLength(minWVal, 0, childFontSize);
                float t = (styleVal(child, Prop::BoxSizing) == "border-box")
                    ? v + mh : v + ph + bh + mh;
                if (childMin < t) childMin = t;
            }
            const std::string& maxWVal = styleVal(child, Prop::MaxWidth);
            if (!maxWVal.empty() && maxWVal != "none" &&
                maxWVal.find('%') == std::string::npos) {
                float v = resolveLength(maxWVal, 0, childFontSize);
                float t = (styleVal(child, Prop::BoxSizing) == "border-box")
                    ? v + mh : v + ph + bh + mh;
                if (childMin > t) childMin = t;
            }
            maxChildMin = std::max(maxChildMin, childMin);
        }
    }
    return maxChildMin;
}

static float computeMaxContentWidthImpl(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;

    // Replaced elements report their own content width via intrinsicSize().
    // See the same note in computeMinContentWidth above.
    {
        float iw = 0, ih = 0;
        if (node->intrinsicSize(iw, ih, 0.0f)) {
            const std::string& wVal = styleVal(node, Prop::Width);
            if (wVal.empty() || wVal == "auto") return iw;
            if (wVal.find('%') != std::string::npos) return 0.0f;
            float fs = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
            if (fs <= 0.0f) fs = 16.0f;
            return resolveLength(wVal, 0.0f, fs);
        }
    }

    auto& style = node->computedStyle();

    // Tables size by columns — see the matching note in computeMinContentWidth.
    {
        const std::string& d = styleVal(node, Prop::Display);
        if (d == "table" || d == "inline-table") {
            float minW = 0, maxW = 0;
            computeTableIntrinsicWidths(node, metrics, minW, maxW);
            return maxW;
        }
        // Grids size by column tracks: the generic "widest child" fallback
        // below would collapse a fixed-track grid (auto-width items measure
        // 0) or undercount a multi-column one.
        if (d == "grid" || d == "inline-grid") {
            return gridMaxContentWidth(node, metrics);
        }
    }

    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(node, Prop::FontFamily);
    const std::string& fontWeight = styleVal(node, Prop::FontWeight);
    // letter-spacing / word-spacing inflate the laid-out width; intrinsic
    // measurement must match what text.cpp will produce or callers wrap.
    float letterSpacing = resolveLength(styleVal(node, Prop::LetterSpacing), 0, fontSize);
    float wordSpacing   = resolveLength(styleVal(node, Prop::WordSpacing), 0, fontSize);

    // Determine if this container lays out children horizontally (sum) vs vertically (max)
    const std::string& display = styleVal(node, Prop::Display);
    const std::string& flexDir = styleVal(node, Prop::FlexDirection);
    bool isHorizontal = (display == "flex" || display == "inline-flex") &&
                        (flexDir.empty() || flexDir == "row" || flexDir == "row-reverse");
    bool isFlexContainer = (display == "flex" || display == "inline-flex");

    float maxChildMax = 0.0f;
    float sumChildMax = 0.0f;

    // Track whether every in-flow child is inline-level. A block container
    // whose children are all inline-level (text, inline, inline-block, and
    // inline ::before/::after) forms a single inline formatting context, so
    // its max-content width is the whole content laid out on one unbroken
    // line — the SUM of the pieces — not the widest child. With a block-level
    // child present, children stack and the widest wins (the fallback below).
    bool allInline = true;
    int inflowCount = 0;

    auto isWhitespaceOnly = [](std::string_view s) {
        for (char c : s) {
            if (!std::isspace(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    // A pure-whitespace text node between inline-level siblings collapses to
    // ONE space in layout (block.cpp emits a synthetic space item), so the
    // single-line (max-content) sum must count it too — table cells were
    // measuring exactly (N-1) space advances narrower than Chromium for a
    // run of N inline-blocks and wrapping content that fits. The space only
    // counts when inline content both precedes AND follows (mirrors layout's
    // leading-skip and trailing-trim), hence the pending buffer.
    float spaceW = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight)
        + letterSpacing + wordSpacing;
    float pendingSpace = 0.0f;
    bool prevInlineContent = false;
    const std::string& wsModeOuter = styleVal(node, Prop::WhiteSpace);
    bool collapsingWs = wsModeOuter.empty() || wsModeOuter == "normal" ||
                        wsModeOuter == "nowrap" || wsModeOuter == "pre-line";

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Flex containers discard whitespace-only text nodes (CSS Flexbox §4)
            if (isFlexContainer && isWhitespaceOnly(child->textContent())) continue;
            if (collapsingWs && isWhitespaceOnly(child->textContent())) {
                if (prevInlineContent) pendingSpace = spaceW;
                continue;
            }
            std::string_view text = child->textContent();
            // The container's white-space mode decides how the FINAL layout
            // sizes this text. When it collapses and wraps at word boundaries
            // (white-space: normal, not break-word/break-all), block.cpp lays
            // the line out as per-word runs + synthetic space items; its
            // reconstructed single-line width is a hair wider than one whole-
            // string measurement, so max-content must be reconstructed the same
            // way or a cell sized to it wraps its own text (see block.cpp's
            // word-mode item construction / measureWordModeIntrinsics).
            const std::string& wsMode = styleVal(node, Prop::WhiteSpace);
            const std::string& oWrap  = styleVal(node, Prop::OverflowWrap);
            const std::string& wBreak = styleVal(node, Prop::WordBreak);
            bool wordMode = (wsMode.empty() || wsMode == "normal") &&
                            !(oWrap == "break-word" || oWrap == "anywhere" ||
                              wBreak == "break-all");
            float w;
            if (wordMode) {
                float mn, mx;
                measureWordModeIntrinsics(std::string(text), fontFamily,
                    fontSize, fontWeight, letterSpacing, wordSpacing,
                    styleVal(node, Prop::TextTransform), metrics, mn, mx);
                w = mx;
            } else {
                // nowrap / pre*: the line is a single whole-string measurement,
                // so measure the collapsed string as one piece. Shared with the
                // min-content path, which needs the identical number whenever
                // wrapping is forbidden.
                w = measureUnbreakableRun(text, /*collapse=*/true, fontFamily,
                    fontSize, fontWeight, letterSpacing, wordSpacing,
                    styleVal(node, Prop::TextTransform), metrics);
            }
            // Both measurements above trim this run's own edge white space —
            // they have to, since a run cannot know what sits beside it. So a
            // space where this run meets an inline sibling is counted here,
            // exactly as it is for a whitespace-only text node between two
            // siblings, and for the same reason: it is real width on the line.
            const bool spaceBefore =
                collapsingWs && std::isspace(static_cast<unsigned char>(text.front()));
            const bool spaceAfter =
                collapsingWs && std::isspace(static_cast<unsigned char>(text.back()));
            if (spaceBefore && prevInlineContent) pendingSpace = spaceW;

            sumChildMax += pendingSpace;
            pendingSpace = 0.0f;
            prevInlineContent = true;
            maxChildMax = std::max(maxChildMax, w);
            sumChildMax += w;
            if (spaceAfter) pendingSpace = spaceW;   // only if something follows
            ++inflowCount;   // text runs are inline-level
        } else {
            auto& cs = child->computedStyle();
            if (styleVal(child, Prop::Display) == "none") continue;
            // Out-of-flow children contribute nothing to intrinsic sizes.
            const std::string& cpos = styleVal(child, Prop::Position);
            if (cpos == "absolute" || cpos == "fixed") continue;
            const std::string& cdisp = styleVal(child, Prop::Display);
            const bool inlineLevel = cdisp == "inline" || cdisp == "inline-block" ||
                                     cdisp == "inline-flex" || cdisp == "inline-grid" ||
                                     cdisp == "inline-table";
            if (!inlineLevel) allInline = false;
            ++inflowCount;
            // White space where this inline box meets the previous one. The
            // box measures itself with its own edge trimmed, so the space
            // between the two belongs to neither and has to be added here.
            if (inlineLevel && collapsingWs && prevInlineContent &&
                subtreeEdgeSpace(child, /*leading=*/true).value_or(false))
                pendingSpace = spaceW;
            float childFontSize = resolveLength(styleVal(child, Prop::FontSize), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            float ph = resolveLength(styleVal(child, Prop::PaddingLeft), 0, childFontSize) +
                       resolveLength(styleVal(child, Prop::PaddingRight), 0, childFontSize);
            // Styleless borders have used width 0 (computed "medium" = 3px
            // must not inflate the contribution).
            float bh = 0;
            if (styleVal(child, Prop::BorderLeftStyle) != "none")
                bh += resolveLength(styleVal(child, Prop::BorderLeftWidth), 0, childFontSize);
            if (styleVal(child, Prop::BorderRightStyle) != "none")
                bh += resolveLength(styleVal(child, Prop::BorderRightWidth), 0, childFontSize);
            // A border-collapse table's used border is zero — see the
            // matching note in computeMinContentWidth.
            {
                const std::string& cd = styleVal(child, Prop::Display);
                if ((cd == "table" || cd == "inline-table") &&
                    styleVal(child, Prop::BorderCollapse) == "collapse") {
                    bh = 0;
                }
            }
            float mh = resolveLength(styleVal(child, Prop::MarginLeft), 0, childFontSize) +
                       resolveLength(styleVal(child, Prop::MarginRight), 0, childFontSize);
            // Use explicit width if set, otherwise recurse for intrinsic size
            const std::string& wVal = styleVal(child, Prop::Width);
            float childMax;
            if (!wVal.empty() && wVal != "auto") {
                float w = resolveLength(wVal, 0, childFontSize);
                if (styleVal(child, Prop::BoxSizing) == "border-box")
                    childMax = w + mh;
                else
                    childMax = w + ph + bh + mh;
            } else {
                childMax = computeMaxContentWidth(child, metrics) + ph + bh + mh;
            }
            // Apply min-width (specifies minimum content width)
            const std::string& minWVal = styleVal(child, Prop::MinWidth);
            if (!minWVal.empty() && minWVal != "auto") {
                float minW = resolveLength(minWVal, 0, childFontSize);
                float minTotal = (styleVal(child, Prop::BoxSizing) == "border-box")
                    ? minW + mh : minW + ph + bh + mh;
                if (childMax < minTotal) childMax = minTotal;
            }
            sumChildMax += pendingSpace;
            pendingSpace = 0.0f;
            prevInlineContent = true;
            maxChildMax = std::max(maxChildMax, childMax);
            sumChildMax += childMax;
            // And white space at this box's far edge, which counts only if
            // inline content follows — the same pending buffer a whitespace-
            // only text node between siblings uses, and deliberately the same
            // variable: two collapsible spaces meeting at a boundary are one
            // space, not two.
            if (inlineLevel && collapsingWs &&
                subtreeEdgeSpace(child, /*leading=*/false).value_or(false))
                pendingSpace = spaceW;
        }
    }
    // Flex-row: children are side-by-side, so sum their widths.
    // Block/flex-column: children stack, so use the widest.
    if (isHorizontal) {
        // Add gaps between children
        float gap = resolveLength(styleVal(node, Prop::ColumnGap), 0, fontSize);
        int childCount = 0;
        for (auto* child : getLayoutChildren(node)) {
            if (!child->isTextNode()) {
                auto& cs = child->computedStyle();
                if (styleVal(child, Prop::Display) == "none") continue;
                const std::string& cpos = styleVal(child, Prop::Position);
                if (cpos == "absolute" || cpos == "fixed") continue;
                childCount++;
            } else if (!isFlexContainer || !isWhitespaceOnly(child->textContent())) {
                childCount++;
            }
        }
        if (childCount > 1) sumChildMax += gap * (childCount - 1);
        return sumChildMax;
    }
    // Pure inline formatting context: all in-flow children share one line,
    // so max-content is their combined width, not the widest single piece.
    if (allInline && inflowCount > 1) return sumChildMax;
    return maxChildMax;
}

// A length starts with a digit, a sign or a dot; a keyword and a math function
// both start with a letter. Deciding on the first byte keeps `16px` — which is
// nearly every length in a document — out of the keyword comparisons and out of
// isMathFunction entirely. Each of those is a strlen against a literal, and this
// is the most-called function in layout.
static bool startsNumeric(const std::string& v) {
    char c = v[0];
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
}

float resolveLength(const std::string& value, float referenceSize, float fontSize) {
    layoutStatsMut().lengthResolves++;
    if (value.empty()) return 0.0f;
    if (!startsNumeric(value)) {
        if (value == "auto" || value == "none" || value == "normal") return 0.0f;
        // calc()/min()/max()/clamp(): the whole string goes to evalCalc, whose
        // primary parser recognizes each function name.
        if (isMathFunction(value)) return evalCalc(value, referenceSize, fontSize);
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
        if (unit == "%") {
            // Percentage line-height resolves against the element's own
            // font-size (CSS2 10.8.1).
            return num / 100.0f * fontSize;
        }
    }

    // Otherwise, resolve as a regular length
    return resolveLength(value, 0, fontSize);
}

float resolveLineHeight(const std::string& value, float fontSize,
                        const std::string& fontFamily,
                        const std::string& fontWeight,
                        TextMetrics& metrics) {
    if (value.empty() || value == "normal") {
        // font-size:0 → normal line-height is 0; don't ask the font backend,
        // which may clamp degenerate sizes to a usable minimum.
        if (fontSize <= 0) return 0.0f;
        float h = metrics.lineHeight(fontFamily, fontSize, fontWeight);
        if (h > 0) return h;
        return fontSize * 1.2f;
    }
    return resolveLineHeight(value, fontSize);
}

float resolveLength(const std::string& value, float referenceSize, float fontSize,
                    float viewportWidth, float viewportHeight) {
    layoutStatsMut().lengthResolves++;
    if (value.empty()) return 0.0f;
    if (!startsNumeric(value)) {
        if (value == "auto" || value == "none" || value == "normal") return 0.0f;
        // calc()/min()/max()/clamp(). vw/vh inside resolve against the file-scoped
        // viewport (set by layoutTree), so referenceSize suffices here.
        if (isMathFunction(value)) return evalCalc(value, referenceSize, fontSize);
    }

    // For vw/vh/vmin/vmax, use actual viewport dimensions
    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
    if (ec != std::errc()) {
        return resolveSingleLength(value, referenceSize, fontSize);
    }
    std::string_view unit(ptr, static_cast<size_t>(end - ptr));
    if (unit == "vw") return num * viewportWidth / 100.0f;
    if (unit == "vh") return num * viewportHeight / 100.0f;
    if (unit == "vmin") return num * std::min(viewportWidth, viewportHeight) / 100.0f;
    if (unit == "vmax") return num * std::max(viewportWidth, viewportHeight) / 100.0f;

    return resolveSingleLength(value, referenceSize, fontSize);
}

Edges resolveEdges(const LayoutNode* node, const EdgeProps& props,
                   float referenceWidth, float fontSize) {
    Edges e;
    e.top = resolveLength(styleVal(node, props.top), referenceWidth, fontSize);
    e.right = resolveLength(styleVal(node, props.right), referenceWidth, fontSize);
    e.bottom = resolveLength(styleVal(node, props.bottom), referenceWidth, fontSize);
    e.left = resolveLength(styleVal(node, props.left), referenceWidth, fontSize);
    return e;
}

Edges resolveBorders(const LayoutNode* node, float referenceWidth, float fontSize) {
    // CSS2 §8.5.3: border-width is ignored unless border-style is set, and the
    // initial style is `none`. So a box with `border-width: 4px` and no style has
    // no border at all.
    static constexpr EdgeProps kStyle{Prop::BorderTopStyle, Prop::BorderRightStyle,
                                      Prop::BorderBottomStyle, Prop::BorderLeftStyle};
    static constexpr EdgeProps kWidth{Prop::BorderTopWidth, Prop::BorderRightWidth,
                                      Prop::BorderBottomWidth, Prop::BorderLeftWidth};
    Edges e;
    const Prop styleProps[4] = {kStyle.top, kStyle.right, kStyle.bottom, kStyle.left};
    const Prop widthProps[4] = {kWidth.top, kWidth.right, kWidth.bottom, kWidth.left};
    float* out[4] = {&e.top, &e.right, &e.bottom, &e.left};
    for (int i = 0; i < 4; i++)
        if (styleVal(node, styleProps[i]) != "none")
            *out[i] = resolveLength(styleVal(node, widthProps[i]), referenceWidth, fontSize);
    return e;
}

// Parse contain property to check for specific containment types.
static bool hasContainment(const LayoutNode* node, const std::string& type) {
    const std::string& val = styleVal(node, Prop::Contain);
    if (val.empty() || val == "none") return false;
    if (val == "strict") return true; // strict = size layout paint style
    if (val == "content") return type != "size"; // content = layout paint style
    return val.find(type) != std::string::npos;
}

static void layoutNodeInner(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Take ownership of a node for this pass: record the inputs it is about to be
// laid out under and clear the geometry the previous pass left in its box.
//
// The clear matters because layout reads the box back as an input in places —
// layoutBlock's "did an outer pass already resolve a definite height for me"
// probe, layoutAbsoluteChild's shrink-wrap — so a stale value silently drifts
// the result. Direct text children are cleared with the node: their boxes are
// written by this node's inline layout, and they are never passed to layoutNode
// themselves, so this is their only reset. Pseudo-elements re-lay out with their
// host, since their content and style live on it and the wrapper carries no
// dirty bit of its own to say either changed.
static void claimLayoutNode(LayoutNode* node, float availableWidth) {
    node->lastLayoutPass = currentLayoutPass();
    node->cachedAvailWidth = availableWidth;
    node->cachedAvailHeight = node->availableHeight;
    node->cachedOverrideWidth = node->overrideContentWidth;

    node->box = LayoutBox{};
    node->box.dirty = false;
    for (auto* child : node->children())
        if (child->isTextNode()) child->box = LayoutBox{};
    if (auto* p = node->pseudoBefore()) markSubtreeDirty(p);
    if (auto* p = node->pseudoAfter())  markSubtreeDirty(p);
}

bool beginLayoutNode(LayoutNode* node, float availableWidth) {
    if (!node) return false;
    if (node->lastLayoutPass == currentLayoutPass()) return true;  // already ours
    if (!node->box.dirty &&
        node->cachedAvailWidth == availableWidth &&
        node->cachedAvailHeight == node->availableHeight &&
        node->cachedOverrideWidth == node->overrideContentWidth) {
        layoutStatsMut().reused++;
        return false;   // cached subtree still valid — don't touch the box
    }
    if (node->box.dirty)                                           layoutStatsMut().reuseFailDirty++;
    else if (!(node->cachedAvailWidth == availableWidth)) layoutStatsMut().reuseFailAvailW++;
    else if (!(node->cachedAvailHeight == node->availableHeight))  layoutStatsMut().reuseFailAvailH++;
    else                                                           layoutStatsMut().reuseFailOverride++;
    layoutStatsMut().laidOut++;
    claimLayoutNode(node, availableWidth);
    // This node is about to be laid out, which means its style is about to be read
    // a couple of hundred times. That is the only condition under which projecting
    // it into a flat array pays for itself: the projection costs one hash per
    // property the node holds, so a node that is merely *glanced* at — the
    // hit-bounds walk asks every node in the tree for `overflow` and `position`,
    // laid out or not — would pay to build a cache it then reads five times.
    // Building on first read instead of here made a single-leaf reflow 46% slower
    // for exactly that reason. Everyone else reads the live map; see styleVal.
    buildStyleCache(node);
    return true;
}

void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    if (node->lastLayoutPass != currentLayoutPass()) {
        // Incremental layout. Nothing about this node changed (box.dirty is
        // clear) and the space it is being laid out into is the same as last
        // time, so it would recompute the box it already holds — and so would
        // every descendant, recursively. Hand the cached subtree back untouched.
        // This is what turns a one-element change from an O(document) pass into
        // an O(changed) one.
        if (!beginLayoutNode(node, availableWidth)) return;
    } else {
        // A repeat visit in the same pass. Layout does this deliberately: a flex
        // or grid container lays an item out to measure it, resolves its tracks,
        // writes the item's used size into its box, and lays it out again to push
        // that size through the item's own children. So: re-lay it out, and keep
        // the box it was re-entered with — by now the box carries the container's
        // decision about this item and is itself an input.
        //
        // Re-key the cache to THIS call's inputs. The final call of the sequence
        // is what produced the geometry the box will hold, and a container that
        // re-lays with unchanged resolved sizes re-issues that same final call —
        // so a next-pass first call matching these keys means the whole sequence's
        // result is unchanged and the subtree can be handed back. (Poisoning the
        // cache here instead — the old behavior — meant a node that was ever
        // re-visited could never be reused again: every ancestor container
        // re-lay swept the entire subtree, every pass, forever.)
        //
        // The contract this rests on: everything a container decides for the
        // node between visits must arrive through the keyed channels
        // (availableWidth, availableHeight, overrideContentWidth) or be
        // derivable from them plus the node's own style. Callers whose
        // sequence writes other inputs straight into the box (grid's track
        // stretch, multicol balancing, flex auto-margin distribution) poison
        // the cache at their call site to keep their always-relay semantics.
        node->cachedAvailWidth = availableWidth;
        node->cachedAvailHeight = node->availableHeight;
        node->cachedOverrideWidth = node->overrideContentWidth;
        node->box.dirty = false;
    }

    layoutStatsMut().visits++;
    layoutNodeInner(node, availableWidth, metrics);
}

// The layout itself: pick the formatting context from `display` and run it.
// layoutNode() above owns the incremental bookkeeping and calls this.
static void layoutNodeInner(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    auto& style = node->computedStyle();
    const std::string& display = styleVal(node, Prop::Display);

    if (display == "none") {
        // Hidden — zero-size box, skip children. Stays clean: it will be reused
        // (as a zero box) until something restyles it back into the flow.
        node->box = LayoutBox{};
        node->box.dirty = false;
        return;
    }

    // CSS Containment L2: content-visibility: hidden acts like display:none
    // but preserves the element's box (it still occupies space per explicit size).
    const std::string& contentVis = styleVal(node, Prop::ContentVisibility);
    if (contentVis == "hidden") {
        // Skip layout of children but keep the element's own box.
        // Use explicit size if set, otherwise 0.
        float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
        if (fontSize <= 0.0f) fontSize = 16.0f;
        node->box.margin = resolveEdges(node, kMarginProps, availableWidth, fontSize);
        node->box.padding = resolveEdges(node, kPaddingProps, availableWidth, fontSize);
        float specW = resolveLength(styleVal(node, Prop::Width), availableWidth, fontSize);
        float specH = resolveLength(styleVal(node, Prop::Height), 0, fontSize);
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
    if (hasContainment(node, "size")) {
        float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
        if (fontSize <= 0.0f) fontSize = 16.0f;
        const std::string& wVal = styleVal(node, Prop::Width);
        const std::string& hVal = styleVal(node, Prop::Height);
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

// Resolve a dimension: nullopt means auto/none/empty (unspecified).
// Specified-ness must be kept separate from the value — offsets like
// top:-40px are valid negative lengths and must not read as "auto".
std::optional<float> resolveDimAbs(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return std::nullopt;
    return resolveLength(value, available, fontSize);
}

// Check if a node establishes a containing block for absolute descendants.
// Per CSS spec: position != static, or has transform/filter/perspective,
// or has contain: layout/paint.
bool isContainingBlock(LayoutNode* node) {
    auto& style = node->computedStyle();
    const std::string& pos = styleVal(node, Prop::Position);
    if (pos == "relative" || pos == "absolute" || pos == "fixed" || pos == "sticky")
        return true;
    // transform, filter, and perspective also create containing blocks
    const std::string& transform = styleVal(node, Prop::Transform);
    if (!transform.empty() && transform != "none") return true;
    const std::string& filter = styleVal(node, Prop::Filter);
    if (!filter.empty() && filter != "none") return true;
    // CSS Containment L2: contain: layout or contain: paint creates a containing block
    if (hasContainment(node, "layout") || hasContainment(node, "paint"))
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
    float fontSize = resolveLength(styleVal(child, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    // Set available height for the child's own percentage resolution
    child->availableHeight = cbHeight;

    // Resolve offsets and explicit dimensions
    std::optional<float> left = resolveDimAbs(styleVal(child, Prop::Left), cbWidth, fontSize);
    std::optional<float> right = resolveDimAbs(styleVal(child, Prop::Right), cbWidth, fontSize);
    std::optional<float> specW = resolveDimAbs(styleVal(child, Prop::Width), cbWidth, fontSize);
    std::optional<float> top = resolveDimAbs(styleVal(child, Prop::Top), cbHeight, fontSize);
    std::optional<float> bottom = resolveDimAbs(styleVal(child, Prop::Bottom), cbHeight, fontSize);
    std::optional<float> specH = resolveDimAbs(styleVal(child, Prop::Height), cbHeight, fontSize);

    // Determine available width for layout
    // Shrink-wrap if width:auto and not both left+right set
    bool shrinkWrap = (!specW && !(left && right));
    bool stretchW = (!specW && left && right);

    bool stretchH = (!specH && top && bottom);

    // The width the inner layout runs at. Margins, padding and border all
    // resolve from the child's own style here rather than from child->box: the
    // in-flow pass skips absolute/fixed children, so their box holds no resolved
    // edges at this point (and reading it as zero would make the inner layout
    // subtract padding+border from a too-small available width and shrink the
    // children).
    //
    // Stretched width (left+right pinned, width:auto) has to reach the inner
    // layout so flex/grid children resolve their cross size against the real
    // containing-block width, not the raw cbWidth.
    float layoutW = cbWidth;
    if (stretchW) {
        float mh = resolveLength(styleVal(child, Prop::MarginLeft), cbWidth, fontSize) +
                   resolveLength(styleVal(child, Prop::MarginRight), cbWidth, fontSize);
        // layoutNode treats layoutW as the parent content width including
        // padding+border for this child; subtract only the margins here.
        float w = cbWidth - *left - *right - mh;
        if (w > 0) layoutW = w;
    } else if (shrinkWrap) {
        float maxCW = computeMaxContentWidth(child, metrics);
        if (maxCW > cbWidth) maxCW = cbWidth;
        float ph = resolveLength(styleVal(child, Prop::PaddingLeft), cbWidth, fontSize) +
                   resolveLength(styleVal(child, Prop::PaddingRight), cbWidth, fontSize);
        Edges bEdges = resolveBorders(child, cbWidth, fontSize);
        float bh = bEdges.left + bEdges.right;
        float mh = resolveLength(styleVal(child, Prop::MarginLeft), cbWidth, fontSize) +
                   resolveLength(styleVal(child, Prop::MarginRight), cbWidth, fontSize);
        layoutW = maxCW + ph + bh + mh;
    }

    // Claim the node for this layout pass — which clears the geometry the
    // previous pass left in its box — BEFORE pre-writing the stretched height
    // below. layoutNode() would otherwise clear the box on its own first visit
    // of the pass and throw that height away, and the height is an *input*: it
    // is what lets a top+bottom-pinned flex or grid container size its children
    // against a definite cross axis instead of collapsing to content height.
    //
    // beginLayoutNode returns false when the child's cached subtree is still
    // valid for these inputs. Nothing is laid out then — the box already holds
    // the right geometry — and we fall through to reposition it.
    if (beginLayoutNode(child, layoutW)) {
        // Pre-computed stretched height: see above.
        if (stretchH) {
            float marginTop = resolveLength(styleVal(child, Prop::MarginTop), cbHeight, fontSize);
            float marginBottom = resolveLength(styleVal(child, Prop::MarginBottom), cbHeight, fontSize);
            float padTop = resolveLength(styleVal(child, Prop::PaddingTop), cbHeight, fontSize);
            float padBottom = resolveLength(styleVal(child, Prop::PaddingBottom), cbHeight, fontSize);
            float borTop = (styleVal(child, Prop::BorderTopStyle) != "none")
                ? resolveLength(styleVal(child, Prop::BorderTopWidth), cbHeight, fontSize) : 0.0f;
            float borBottom = (styleVal(child, Prop::BorderBottomStyle) != "none")
                ? resolveLength(styleVal(child, Prop::BorderBottomWidth), cbHeight, fontSize) : 0.0f;
            float h = cbHeight - *top - *bottom - marginTop - marginBottom -
                      padTop - padBottom - borTop - borBottom;
            if (h > 0) child->box.contentRect.height = h;
        }
        layoutNode(child, layoutW, metrics);
    }

    // Stretch width if both left and right are set and width is auto
    if (stretchW) {
        float w = cbWidth - *left - *right -
                  child->box.margin.left - child->box.margin.right -
                  child->box.padding.left - child->box.padding.right -
                  child->box.border.left - child->box.border.right;
        if (w > 0) child->box.contentRect.width = w;
    }

    // Re-apply the stretched height in case layoutNode overwrote it from
    // content size (block layout typically does); the absolute-position
    // contract is that top+bottom together pin the box.
    if (stretchH) {
        float h = cbHeight - *top - *bottom -
                  child->box.margin.top - child->box.margin.bottom -
                  child->box.padding.top - child->box.padding.bottom -
                  child->box.border.top - child->box.border.bottom;
        if (h > 0) child->box.contentRect.height = h;
    }

    // Compute position in containing-block-relative space
    float xInCB = child->box.margin.left + child->box.padding.left + child->box.border.left;
    float yInCB = child->box.margin.top + child->box.padding.top + child->box.border.top;

    if (left) {
        xInCB = *left + child->box.margin.left + child->box.padding.left + child->box.border.left;
    } else if (right) {
        xInCB = cbWidth - *right - child->box.margin.right -
                child->box.padding.right - child->box.border.right - child->box.contentRect.width;
    }

    if (top) {
        yInCB = *top + child->box.margin.top + child->box.padding.top + child->box.border.top;
    } else if (bottom) {
        yInCB = cbHeight - *bottom - child->box.margin.bottom -
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
//
// Returns whether any positioned element lives strictly below `node`, and
// caches it in node->subtreeHasPositioned so later passes skip branches with
// none. Branches that do contain one are walked fully every pass — a
// positioned box must re-resolve against its containing block's current
// geometry, which can move without anything in this branch being dirty.
bool layoutAbsoluteElementsRecursive(LayoutNode* node, const Viewport& viewport,
                                      TextMetrics& metrics) {
    if (node->subtreeHasPositioned == 0) return false;
    bool any = false;
    for (auto* child : node->children()) {
        if (!child || child->isTextNode()) continue;

        auto& style = child->computedStyle();
        const std::string& display = styleVal(child, Prop::Display);
        if (display == "none") continue;

        const std::string& pos = styleVal(child, Prop::Position);

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
            any = true;
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
            any = true;
        }

        // Recurse into children (including into absolute elements, which can
        // contain further absolute descendants)
        if (layoutAbsoluteElementsRecursive(child, viewport, metrics)) any = true;
    }
    node->subtreeHasPositioned = any ? 1 : 0;
    return any;
}

} // anonymous namespace

void layoutAbsoluteElements(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics) {
    if (!root) return;
    layoutAbsoluteElementsRecursive(root, viewport, metrics);
}

} // namespace htmlayout::layout
