#include "layout/formatting_context.h"
#include "layout/text.h"
#include "layout/table.h"
#include "layout/grid.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace htmlayout::layout {

using layout::styleVal;

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
    // One advance per code point, the trailing one included — what
    // breakTextIntoRuns gives the laid-out run.
    if (letterSpacing != 0 && !piece.empty())
        w += letterSpacing * static_cast<float>(letterSpacingSlots(piece));
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
    FontFeatureScope features(metrics, fontFeaturesOf(node));
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
                            w += letterSpacing * static_cast<float>(letterSpacingSlots(word));
                        widestWord = std::max(widestWord, w);
                        word.clear();
                    }
                } else {
                    word += c;
                }
            }
            maxChildMin = std::max(maxChildMin, widestWord);
        } else {
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
    FontFeatureScope features(metrics, fontFeaturesOf(node));
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
            // overflow-wrap / word-break do not change how a line is
            // reconstructed: a word is only ever cut once it overflows.
            const std::string& wsMode = styleVal(node, Prop::WhiteSpace);
            bool wordMode = wsMode.empty() || wsMode == "normal";
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
            // A definite width is the contribution; otherwise recurse for the
            // content. A percentage resolves against the very size being
            // measured, so it behaves as auto here (CSS Sizing 3 §5.2.1) — a
            // `width: 100%` menu row otherwise added nothing, and the menu
            // sized to its min-width and cut every label.
            const std::string& wVal = styleVal(child, Prop::Width);
            const bool definiteW = !wVal.empty() && wVal != "auto" &&
                                   wVal.find('%') == std::string::npos &&
                                   !isIntrinsicSizingKeyword(wVal);
            float childMax;
            if (definiteW) {
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

} // namespace htmlayout::layout
