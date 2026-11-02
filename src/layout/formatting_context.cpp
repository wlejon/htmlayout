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
#include <limits>
#include <optional>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

// Current viewport for vw/vh/vmin/vmax resolution, set by layoutTree() before each
// pass. Thread-local so concurrent layout passes on separate documents/threads do
// not race on viewport dimensions. 0 means "unknown" — see setLayoutViewport.
static thread_local float g_viewportWidth = 0.0f;
static thread_local float g_viewportHeight = 0.0f;

// Root element (<html>) font-size, used to resolve rem units. Threaded the same
// thread-local way as the viewport since rem, like vw/vh, needs a document-global
// reference the per-site resolver has no node to reach. Defaults to 16 (the
// initial font-size) until layoutTree() sets the real root size.
static thread_local float g_rootFontSize = 16.0f;

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
// sets this thread-local context (absolute px for the element's current font) right
// before it resolves that element's own lengths. 0 means "unset" — the resolver then
// falls back to the CSS-defined 0.5em approximation so callers without a font in hand
// (intrinsic-width probes, standalone resolveLength) keep working.
static thread_local float g_chPx = 0.0f;
static thread_local float g_exPx = 0.0f;

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

            // Parse unit
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

        // Parse additive: mul (+ or - mul)*. Every nesting level (parens,
        // calc/min/max/clamp) comes back through here, so the depth bound
        // keeps a hostile `((((…` from running the stack out.
        int depth = 0;
        float parseExpr() {
            if (depth >= 64) { pos = s.size(); return 0.0f; }
            ++depth;
            float left = parseExprBody();
            --depth;
            return left;
        }
        float parseExprBody() {
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
bool hasContainment(const LayoutNode* node, const std::string& type) {
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
    node->visitPresetW = std::numeric_limits<float>::quiet_NaN();
    node->visitPresetH = std::numeric_limits<float>::quiet_NaN();

    node->box = LayoutBox{};
    node->box.dirty = false;
    for (auto* child : node->children())
        if (child->isTextNode()) child->box = LayoutBox{};
    if (auto* p = node->pseudoBefore()) markSubtreeDirty(p);
    if (auto* p = node->pseudoAfter())  markSubtreeDirty(p);
}

void claimForParentFlow(LayoutNode* node) {
    if (!node) return;
    // NaN keys: never equal, so the next pass cannot mistake this box for a
    // reusable layoutNode() result.
    claimLayoutNode(node, std::numeric_limits<float>::quiet_NaN());
    buildStyleCache(node);
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
        //
        // And when the repeat visit IS the same question — same keyed inputs,
        // and the same preset content size the container wrote into the box —
        // the box already holds the answer, and the subtree with it. This is
        // the same trust beginLayoutNode() places in a clean subtree across
        // passes, with the preset added because the container writes it after
        // claiming the node and before this call. Without it a node at depth d
        // under nested flex containers was laid out 2^d times per pass.
        if (!node->box.dirty &&
            node->cachedAvailWidth == availableWidth &&
            node->cachedAvailHeight == node->availableHeight &&
            node->cachedOverrideWidth == node->overrideContentWidth &&
            node->visitPresetW == node->box.contentRect.width &&
            node->visitPresetH == node->box.contentRect.height) {
            layoutStatsMut().revisitsSkipped++;
            return;
        }
        node->cachedAvailWidth = availableWidth;
        node->cachedAvailHeight = node->availableHeight;
        node->cachedOverrideWidth = node->overrideContentWidth;
        node->box.dirty = false;
    }
    node->visitPresetW = node->box.contentRect.width;
    node->visitPresetH = node->box.contentRect.height;

    layoutStatsMut().visits++;
    layoutNodeInner(node, availableWidth, metrics);
}

// The layout itself: pick the formatting context from `display` and run it.
// layoutNode() above owns the incremental bookkeeping and calls this.
static void layoutNodeInner(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    const std::string& display = styleVal(node, Prop::Display);

    if (display == "none") {
        // Hidden — zero-size box, skip children. Stays clean: it will be reused
        // (as a zero box) until something restyles it back into the flow.
        clearHiddenBox(node);
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

void centerButtonContent(LayoutNode* node, float flowHeight) {
    if (!node || node->isTextNode()) return;
    const std::string_view tag = node->tagName();
    if (tag != "button" && tag != "BUTTON") return;
    const std::string& d = styleVal(node, Prop::Display);
    if (d == "flex" || d == "inline-flex" || d == "grid" || d == "inline-grid") return;
    const float spare = node->box.contentRect.height - flowHeight;
    if (spare > 0.5f) shiftContentY(node, spare / 2.0f);
}

void shiftContentY(LayoutNode* node, float dy) {
    if (!node || dy == 0.0f) return;
    for (auto* child : getLayoutChildren(node)) {
        if (!child) continue;
        child->box.contentRect.y += dy;
        if (child->isTextNode())
            for (auto& tr : child->box.textRuns) tr.y += dy;
    }
}

} // namespace htmlayout::layout
