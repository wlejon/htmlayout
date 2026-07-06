#include "layout/block.h"
#include "layout/formatting_context.h"
#include "../from_chars_compat.h"
#include "layout/style_util.h"
#include "layout/text.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace htmlayout::layout {

using layout::styleVal;

namespace {

float resolveDimension(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f; // sentinel: auto
    if (value == "min-content") return SIZING_MIN_CONTENT;
    if (value == "max-content") return SIZING_MAX_CONTENT;
    if (value == "fit-content") return SIZING_FIT_CONTENT;
    return resolveLength(value, available, fontSize);
}

// Parse `aspect-ratio` value. Returns ratio = width/height as a positive float,
// or -1.0f if value is auto/none/empty/invalid. Accepts forms like "16 / 9",
// "1.5", or "auto 16/9" (auto prefix is ignored — fallback to ratio for
// non-replaced elements).
float parseAspectRatio(const std::string& value) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    const char* s = value.c_str();
    // Skip leading "auto" (with optional whitespace) — replaced elements use
    // intrinsic ratio when one is provided alongside, but for non-replaced
    // boxes the explicit ratio still applies.
    if (value.size() >= 4 && std::strncmp(s, "auto", 4) == 0 &&
        (value.size() == 4 || std::isspace(static_cast<unsigned char>(s[4])))) {
        s += 4;
        while (*s && std::isspace(static_cast<unsigned char>(*s))) ++s;
        if (!*s) return -1.0f;
    }
    char* end = nullptr;
    double w = std::strtod(s, &end);
    if (end == s || w <= 0) return -1.0f;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end != '/') return static_cast<float>(w);
    ++end;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    char* end2 = nullptr;
    double h = std::strtod(end, &end2);
    if (end2 == end || h <= 0) return -1.0f;
    return static_cast<float>(w / h);
}

// CSS2 §10.8 strut for a block's inline formatting context: a zero-width
// inline box with the block's own font and line-height. The font's natural
// box (ascent + descent) is distributed in the line-height by half-leading;
// Blink floors the ascent-side half (CalculateLeadingSpace) and the descent
// side takes the remainder, so an odd leading puts the extra half-pixel
// below the baseline. font-size:0 produces an empty strut — the font
// backend is not consulted (it may clamp degenerate sizes).
struct StrutMetrics {
    float above = 0;      // line-top to baseline when only the strut is present
    float below = 0;      // baseline to line-bottom
    float ascent = 0;     // font natural ascent (no leading)
    float xHeight = 0;    // for vertical-align: middle
    float lineHeight = 0; // used line-height
};

StrutMetrics computeStrut(const css::ComputedStyle& style, float fontSize,
                          TextMetrics& metrics) {
    const std::string& fam = styleVal(style, "font-family");
    const std::string& wt = styleVal(style, "font-weight");
    StrutMetrics s;
    s.lineHeight = resolveLineHeight(styleVal(style, "line-height"), fontSize,
                                     fam, wt, metrics);
    float natural = fontSize > 0 ? metrics.lineHeight(fam, fontSize, wt) : 0.0f;
    if (natural <= 0) natural = fontSize * 1.2f;
    float asc = fontSize > 0 ? metrics.ascent(fam, fontSize, wt) : 0.0f;
    if (fontSize > 0 && (asc <= 0 || asc >= natural)) asc = natural * 0.8f;
    float leading = s.lineHeight - natural;
    float half = std::floor(leading * 0.5f);
    s.ascent = asc;
    s.above = asc + half;
    s.below = (natural - asc) + (leading - half);
    s.xHeight = fontSize > 0 ? metrics.xHeight(fam, fontSize, wt) : 0.0f;
    return s;
}

// Baseline geometry of an atomic inline-level box (inline-block, replaced
// element…) that has already been laid out. `height` is the margin-box
// height. Returns the distance from the margin-box top to the box's
// baseline plus the vertical-align keyword (0 = baseline, 1 = top,
// 2 = middle, 3 = bottom) — CSS2 §10.8.1.
struct AtomicInlineGeom {
    float baseline = 0;
    int valign = 0;
};

AtomicInlineGeom atomicInlineGeometry(LayoutNode* child, float height,
                                      float strutBelow, float availWidth) {
    AtomicInlineGeom g;
    auto& styleRef = child->computedStyle();
    const std::string& va = styleVal(styleRef, "vertical-align");
    if (va == "top" || va == "text-top") g.valign = 1;
    else if (va == "middle") g.valign = 2;
    else if (va == "bottom" || va == "text-bottom") g.valign = 3;

    float iw = 0, ih = 0;
    if (child->intrinsicSize(iw, ih, availWidth)) {
        if (child->hasIntrinsicRatio()) {
            // Replaced media (img/canvas/video/svg): baseline is the bottom
            // margin edge.
            g.baseline = height;
        } else {
            // Form controls: browsers align them by the control's internal
            // text baseline, approximated as a strut-descent above the
            // bottom margin edge.
            float below = std::min(strutBelow, height);
            if (below < 0) below = 0;
            g.baseline = height - below;
        }
    } else {
        // Atomic inline: baseline of the last line box when it has in-flow
        // content and visible overflow; otherwise the bottom margin edge.
        const std::string& ov = styleVal(styleRef, "overflow");
        bool visibleOv = ov.empty() || ov == "visible";
        if (visibleOv && child->box.baselineOffset >= 0) {
            g.baseline = child->box.margin.top + child->box.border.top +
                child->box.padding.top + child->box.baselineOffset;
            if (g.baseline > height) g.baseline = height;
        } else {
            g.baseline = height;
        }
    }
    return g;
}

} // anonymous namespace

void layoutBlock(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    const std::string& fontSizeStr = styleVal(style, "font-size");
    float fontSize = resolveLength(fontSizeStr, 16.0f, 16.0f);
    if (fontSize <= 0.0f) {
        // An explicit zero (font-size: 0) is honored — em units and the
        // line-box strut collapse to nothing, matching Chromium. Anything
        // missing, negative or unparseable falls back to the default.
        bool explicitZero = !fontSizeStr.empty() &&
            (fontSizeStr[0] == '0' || fontSizeStr[0] == '.');
        fontSize = explicitZero ? 0.0f : 16.0f;
    }

    const std::string& position = styleVal(style, "position");

    // Fresh layout pass: forget any baseline recorded by a previous layout
    // and any floats handed up to the parent last time.
    node->box.baselineOffset = -1.0f;
    node->box.escapedFloats.clear();

    // Resolve margin, padding, border
    node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
    node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);

    // Border widths: only apply if border-style is not "none"
    Edges borderWidth;
    const char* sides[] = {"top", "right", "bottom", "left"};
    float* bw[] = {&borderWidth.top, &borderWidth.right, &borderWidth.bottom, &borderWidth.left};
    for (int i = 0; i < 4; i++) {
        std::string styleProp = std::string("border-") + sides[i] + "-style";
        std::string widthProp = std::string("border-") + sides[i] + "-width";
        if (styleVal(style, styleProp) != "none") {
            *bw[i] = resolveLength(styleVal(style, widthProp), availableWidth, fontSize);
        }
    }
    node->box.border = borderWidth;

    // Resolve width
    float marginH = node->box.margin.left + node->box.margin.right;
    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;

    float specifiedWidth = resolveDimension(styleVal(style, "width"), availableWidth, fontSize);
    float contentWidth;
    if (specifiedWidth == SIZING_MIN_CONTENT) {
        contentWidth = computeMinContentWidth(node, metrics);
    } else if (specifiedWidth == SIZING_MAX_CONTENT) {
        contentWidth = computeMaxContentWidth(node, metrics);
    } else if (specifiedWidth == SIZING_FIT_CONTENT) {
        float minC = computeMinContentWidth(node, metrics);
        float maxC = computeMaxContentWidth(node, metrics);
        float avail = availableWidth - marginH - paddingH - borderH;
        if (avail < 0.0f) avail = 0.0f;
        contentWidth = std::min(maxC, std::max(minC, avail));
    } else if (specifiedWidth >= 0.0f) {
        // Box-sizing
        const std::string& boxSizing = styleVal(style, "box-sizing");
        if (boxSizing == "border-box") {
            contentWidth = specifiedWidth - paddingH - borderH;
            if (contentWidth < 0.0f) contentWidth = 0.0f;
        } else {
            contentWidth = specifiedWidth;
        }
    } else {
        // width: auto — for replaced elements (e.g. <img>) with an intrinsic
        // size, use that. Otherwise fill available space.
        float intrW = 0, intrH = 0;
        if (node->intrinsicSize(intrW, intrH, availableWidth - paddingH - borderH)) {
            contentWidth = intrW;
        } else {
            contentWidth = availableWidth - marginH - paddingH - borderH;
            if (contentWidth < 0.0f) contentWidth = 0.0f;
        }
    }

    // Apply min/max-width constraints
    float minW = resolveDimension(styleVal(style, "min-width"), availableWidth, fontSize);
    float maxW = resolveDimension(styleVal(style, "max-width"), availableWidth, fontSize);
    if (minW >= 0.0f && contentWidth < minW) contentWidth = minW;
    if (maxW >= 0.0f && contentWidth > maxW) contentWidth = maxW;

    node->box.contentRect.width = contentWidth;

    // Handle margin: auto for horizontal centering
    const std::string& marginLeftVal = styleVal(style, "margin-left");
    const std::string& marginRightVal = styleVal(style, "margin-right");
    if (marginLeftVal == "auto" || marginRightVal == "auto") {
        float totalUsed = contentWidth + paddingH + borderH;
        float remaining = availableWidth - totalUsed;
        if (remaining < 0) remaining = 0;
        if (marginLeftVal == "auto" && marginRightVal == "auto") {
            node->box.margin.left = remaining / 2.0f;
            node->box.margin.right = remaining / 2.0f;
        } else if (marginLeftVal == "auto") {
            node->box.margin.left = remaining - node->box.margin.right;
            if (node->box.margin.left < 0) node->box.margin.left = 0;
        } else {
            node->box.margin.right = remaining - node->box.margin.left;
            if (node->box.margin.right < 0) node->box.margin.right = 0;
        }
    }

    // Available width for children
    float childAvailable = contentWidth;

    // Resolve definite height early so children can use percentage heights.
    // For auto-height parents, children get availableHeight = 0 (per CSS spec,
    // percentage heights only resolve against definite containing-block heights).
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;
    float earlyHeight = resolveDimension(styleVal(style, "height"), node->availableHeight, fontSize);
    // aspect-ratio: when height is auto and width is definite, derive a
    // content-box height from the ratio. The ratio applies to the box that
    // box-sizing selects: content-box (default) → ratio of content widths and
    // heights; border-box → ratio of border boxes.
    // aspectRatioCBH < 0 means "no aspect-ratio override".
    float aspectRatio = parseAspectRatio(styleVal(style, "aspect-ratio"));
    float aspectRatioCBH = -1.0f;
    if (earlyHeight < 0.0f && aspectRatio > 0.0f && contentWidth >= 0.0f) {
        const std::string& bs = styleVal(style, "box-sizing");
        if (bs == "border-box") {
            float borderBoxW = contentWidth + paddingH + borderH;
            float borderBoxH = borderBoxW / aspectRatio;
            aspectRatioCBH = borderBoxH - paddingV - borderV;
        } else {
            aspectRatioCBH = contentWidth / aspectRatio;
        }
        if (aspectRatioCBH < 0.0f) aspectRatioCBH = 0.0f;
    }
    float earlyChildAvailableHeight = 0.0f;
    if (earlyHeight >= 0.0f) {
        const std::string& boxSizing = styleVal(style, "box-sizing");
        if (boxSizing == "border-box")
            earlyChildAvailableHeight = earlyHeight - paddingV - borderV;
        else
            earlyChildAvailableHeight = earlyHeight;
        if (earlyChildAvailableHeight < 0.0f) earlyChildAvailableHeight = 0.0f;
    } else if (aspectRatioCBH >= 0.0f) {
        earlyChildAvailableHeight = aspectRatioCBH;
    } else if (node->box.contentRect.height > 0) {
        // Outer pass (flex item finalMain, grid track, abs-positioned with
        // inset) already resolved a definite content height into contentRect.
        // Treat that as the definite containing-block height for percentage
        // resolution in children — matches flex.cpp's row-stretch fallback.
        earlyChildAvailableHeight = node->box.contentRect.height;
    } else if (node->viewportHeight > 0 &&
               (!node->parent() || !node->parent()->parent())) {
        // Root element chain (html/body): the initial containing block has
        // viewport dimensions, so propagate viewport height for percentage
        // resolution even when height is auto.
        earlyChildAvailableHeight = node->viewportHeight;
    }

    // Propagate viewport height and available height to all children before layout
    for (auto* child : getLayoutChildren(node)) {
        if (!child->isTextNode()) {
            child->viewportHeight = node->viewportHeight;
            child->availableHeight = earlyChildAvailableHeight;
        }
    }

    // Shared state for both BFC and IFC paths
    float cursorY = 0.0f;

    // Determine if this block contains only inline-level content
    // (text nodes, inline, inline-block — no block children)
    bool allInlineChildren = true;
    bool hasVisibleContent = false;
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            std::string_view text = child->textContent();
            for (char c : text) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    hasVisibleContent = true;
                    break;
                }
            }
        } else {
            auto& cs = child->computedStyle();
            const std::string& d = styleVal(cs, "display");
            if (d == "none") continue;
            const std::string& cp = styleVal(cs, "position");
            if (cp == "absolute" || cp == "fixed") continue;
            hasVisibleContent = true;
            if (d != "inline" && d != "inline-block" && d != "inline-flex" && d != "inline-grid") {
                allInlineChildren = false;
            }
        }
    }

    if (hasVisibleContent && allInlineChildren) {
        // Inline formatting context: lay out text and inline elements in line boxes
        const std::string& fontFamily = styleVal(style, "font-family");
        const std::string& fontWeight = styleVal(style, "font-weight");
        const std::string& whiteSpace = styleVal(style, "white-space");
        // CSS2 §10.8: every line box starts with the block's strut (see
        // computeStrut for the Blink-compatible half-leading model).
        StrutMetrics strut = computeStrut(style, fontSize, metrics);
        float lineHeight = strut.lineHeight;
        float strutAscent = strut.ascent;
        float strutAbove = strut.above;
        float strutBelow = strut.below;

        struct IFCItem {
            float width = 0, height = 0;
            LayoutNode* node = nullptr;
            bool isElement = false;
            bool forceBreak = false;
            // CSS2 §10.8 vertical geometry. Every inline-level item spans
            // [baseline - above, baseline + below] in the line box after
            // baseline alignment; the line box height is the union of these
            // extents (plus the block's strut). `baseline` is the distance
            // from the item's positioned top (run top for text, margin-box
            // top for atomic inlines, content top for non-replaced inline
            // elements — see baselineFromContent) down to its baseline.
            float above = 0;
            float below = 0;
            float baseline = 0;
            // True when `baseline` is measured from the child's contentRect
            // top (non-replaced inline elements — their padding/border sit
            // outside the line-height geometry); false when measured from
            // the margin-box top (inline-blocks and replaced elements).
            bool baselineFromContent = false;
            // vertical-align: 0 = baseline, 1 = top, 2 = middle, 3 = bottom.
            // Non-baseline items don't move the line's baseline; they only
            // grow the line box when taller than it.
            int valign = 0;
            // For text runs: the post-processing display string and the
            // source byte range so we can record PlacedTextRun on the text
            // node after line layout.
            std::string text;
            int srcStart = 0;
            int srcEnd = 0;
            // True when a soft line break is allowed on each side of this
            // item. Text-run items inherit these from the word-boundary
            // splitter; atomic inline elements default to false so the
            // break decision falls back to the adjacent items' whitespace.
            bool canBreakBefore = false;
            bool canBreakAfter  = false;
        };
        std::vector<IFCItem> items;

        // Cached space width for synthetic whitespace runs between
        // inline-level siblings (e.g. <ib> <ib> separated by " \n ").
        float spaceWidth = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight);

        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) {
                float ls = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
                float ws = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
                auto runs = breakTextIntoRuns(std::string(child->textContent()), childAvailable,
                    fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                    "normal", "normal", ls, ws, styleVal(style, "text-transform"));
                // Fresh layout pass — clear any previously placed runs.
                child->box.textRuns.clear();
                // Pure-whitespace text node (scanWords returned no words):
                // collapses to a single space contribution between inline
                // siblings. Skip when there's no prior content so leading
                // whitespace doesn't push the first child rightward.
                if (runs.empty() && (whiteSpace == "normal" || whiteSpace.empty())) {
                    bool anyWs = false;
                    for (char c : child->textContent()) {
                        if (std::isspace(static_cast<unsigned char>(c))) { anyWs = true; break; }
                    }
                    if (anyWs && !items.empty()) {
                        IFCItem it{};
                        it.width = spaceWidth;
                        it.height = 0.0f; // doesn't grow line height
                        it.node = child;
                        it.isElement = false;
                        it.forceBreak = false;
                        it.text = " ";
                        it.canBreakBefore = true;
                        it.canBreakAfter  = true;
                        items.push_back(std::move(it));
                    }
                }
                for (auto& run : runs) {
                    bool emitContent = !(run.text.empty() && run.width == 0);
                    if (emitContent) {
                        IFCItem it{};
                        it.width = run.width;
                        // The run's own box is the font's natural height
                        // (ascent + descent); the line box grows to the
                        // block's line-height via above/below instead.
                        it.height = run.height;
                        it.above = strutAbove;
                        it.below = strutBelow;
                        it.baseline = strutAscent;
                        it.node = child;
                        it.isElement = false;
                        it.forceBreak = false;
                        it.text = run.text;
                        it.srcStart = run.srcStart;
                        it.srcEnd = run.srcEnd;
                        it.canBreakBefore = run.canBreakBefore;
                        it.canBreakAfter  = run.canBreakAfter;
                        items.push_back(std::move(it));
                    }
                    // A preserved newline (white-space: pre/pre-wrap/
                    // pre-line) becomes a synthetic break-only item AFTER
                    // the run's content, mirroring how <br> is handled.
                    // Placement logic skips forceBreak items, so emitting
                    // them as separate sentinels keeps the run visible.
                    if (run.forceBreakAfter) {
                        IFCItem brk{};
                        brk.width = 0;
                        brk.height = lineHeight;
                        brk.above = strutAbove;
                        brk.below = strutBelow;
                        brk.baseline = strutAscent;
                        brk.node = child;
                        brk.isElement = false;
                        brk.forceBreak = true;
                        items.push_back(std::move(brk));
                    }
                }
            } else {
                auto& cs = child->computedStyle();
                const std::string& d = styleVal(cs, "display");
                if (d == "none") { child->box = LayoutBox{}; continue; }
                const std::string& cp = styleVal(cs, "position");
                if (cp == "absolute" || cp == "fixed") continue;
                if ((child->tagName() == "br" || child->tagName() == "BR")) {
                    child->box = LayoutBox{};
                    // Record natural font line-height so getBoundingClientRect
                    // returns the inline content height for the <br>, not 0.
                    child->box.contentRect.height = metrics.lineHeight(
                        fontFamily, fontSize, fontWeight);
                    IFCItem brit{};
                    brit.width = 0.0f;
                    brit.height = lineHeight;
                    brit.above = strutAbove;
                    brit.below = strutBelow;
                    brit.baseline = strutAscent;
                    brit.node = child;
                    brit.isElement = false;
                    brit.forceBreak = true;
                    items.push_back(std::move(brit));
                    continue;
                }
                layoutNode(child, childAvailable, metrics);

                IFCItem it{};
                it.width = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
                it.height = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;
                it.node = child;
                it.isElement = true;

                const std::string& va = styleVal(cs, "vertical-align");
                if (va == "top" || va == "text-top") it.valign = 1;
                else if (va == "middle") it.valign = 2;
                else if (va == "bottom" || va == "text-bottom") it.valign = 3;

                float iw = 0, ih = 0;
                bool replaced = child->intrinsicSize(iw, ih, childAvailable);

                if (replaced) {
                    // Replaced elements keep their replaced-baseline rules
                    // regardless of computed display (an <input> is
                    // inline-block per the UA sheet but has no line boxes of
                    // its own to take a baseline from).
                    if (child->hasIntrinsicRatio()) {
                        // Replaced media (img/canvas/video/svg): baseline is
                        // the bottom margin edge.
                        it.baseline = it.height;
                        it.above = it.height;
                        it.below = 0;
                    } else {
                        // Form controls (input/button/select…): browsers align
                        // them by the control's internal text baseline, which
                        // we approximate as sitting a strut-descent above the
                        // bottom margin edge. This keeps a lone control from
                        // adding descent space under the line (matching
                        // Chromium's reported container heights).
                        float below = std::min(strutBelow, it.height);
                        if (below < 0) below = 0;
                        it.baseline = it.height - below;
                        it.above = it.baseline;
                        it.below = below;
                    }
                } else if (d == "inline-block" || d == "inline-flex" || d == "inline-grid") {
                    // Atomic inline: baseline is the last line box's baseline
                    // when the box has in-flow inline content and visible
                    // overflow; otherwise the bottom margin edge (CSS2
                    // §10.8.1). The whole margin box participates in line
                    // sizing — no half-leading.
                    const std::string& ov = styleVal(cs, "overflow");
                    bool visibleOv = ov.empty() || ov == "visible";
                    if (visibleOv && child->box.baselineOffset >= 0) {
                        it.baseline = child->box.margin.top + child->box.border.top +
                            child->box.padding.top + child->box.baselineOffset;
                        if (it.baseline > it.height) it.baseline = it.height;
                    } else {
                        it.baseline = it.height;
                    }
                    it.above = it.baseline;
                    it.below = it.height - it.baseline;
                } else {
                    // Non-replaced inline element (span/strong/em/…): its
                    // contribution to the line comes from its own font and
                    // line-height (number values multiply the element's own
                    // font-size), NOT from its box height — padding/border on
                    // an inline never grow the line box. Its content is
                    // positioned by aligning its first-line baseline with the
                    // line's baseline.
                    float cfs = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
                    if (cfs <= 0) cfs = fontSize;
                    const std::string& cff = styleVal(cs, "font-family");
                    const std::string& cfw = styleVal(cs, "font-weight");
                    float cNatural = cfs > 0 ? metrics.lineHeight(cff, cfs, cfw) : 0.0f;
                    if (cNatural <= 0) cNatural = cfs * 1.2f;
                    float cAscent = cfs > 0 ? metrics.ascent(cff, cfs, cfw) : 0.0f;
                    if (cfs > 0 && (cAscent <= 0 || cAscent >= cNatural))
                        cAscent = cNatural * 0.8f;
                    float cLH = resolveLineHeight(styleVal(cs, "line-height"),
                        cfs, cff, cfw, metrics);
                    // Blink-compatible half-leading split: floor the ascent
                    // side, remainder below (see the strut above).
                    float cLead = cLH - cNatural;
                    float cHalf = std::floor(cLead * 0.5f);

                    float b = (child->box.baselineOffset >= 0)
                        ? child->box.baselineOffset : cAscent;
                    it.baseline = b;
                    it.baselineFromContent = true;
                    // Nested content taller than the element's own font
                    // (e.g. a bigger-font span inside) still has to fit.
                    it.above = std::max(cAscent + cHalf, b);
                    it.below = std::max((cNatural - cAscent) + (cLead - cHalf),
                                        child->box.contentRect.height - b);
                }
                // Atomic inlines (inline-block, replaced media, controls)
                // always offer a soft-wrap opportunity on both sides (CSS
                // Text §5.3) — adjacent inline-blocks with no whitespace
                // still wrap. Non-replaced inline elements (span/em/…) keep
                // the text-driven break rules so "word<span>.</span>" stays
                // atomic.
                bool atomicInline = replaced || d == "inline-block" ||
                    d == "inline-flex" || d == "inline-grid";
                it.canBreakBefore = atomicInline;
                it.canBreakAfter = atomicInline;
                items.push_back(std::move(it));
            }
        }

        // Drop trailing collapsible whitespace synthetic items.
        while (!items.empty()) {
            auto& back = items.back();
            if (!back.isElement && !back.forceBreak && back.text == " ") {
                items.pop_back();
                continue;
            }
            break;
        }
        // Strip leading whitespace from the first text run (collapses against
        // the IFC's start, matching Chromium).
        if (!items.empty()) {
            auto& front = items.front();
            if (!front.isElement && !front.forceBreak && !front.text.empty() &&
                front.text.front() == ' ') {
                float spW = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight);
                front.text.erase(front.text.begin());
                front.width = std::max(0.0f, front.width - spW);
            }
        }

        // Resolve text-align for line positioning
        const std::string& textAlign = styleVal(style, "text-align");
        const std::string& direction = styleVal(style, "direction");
        std::string resolvedAlign = textAlign;
        if (resolvedAlign == "start" || resolvedAlign.empty()) {
            resolvedAlign = (direction == "rtl") ? "right" : "left";
        } else if (resolvedAlign == "end") {
            resolvedAlign = (direction == "rtl") ? "left" : "right";
        }

        // Build line boxes first, then position with alignment.
        //
        // Wrapping is width-driven but must only land on a real break
        // opportunity: either whitespace on one side of the boundary
        // or a canBreak flag from the word-boundary splitter. When a
        // width overflow would cut an atomic unit (e.g. "Citadel" +
        // "." across an inline boundary with no intervening space),
        // retreat to the most recent break opportunity on the line.
        auto itemEndsInSpace = [](const IFCItem& it) {
            return it.canBreakAfter ||
                   (!it.text.empty() && std::isspace(
                        static_cast<unsigned char>(it.text.back())));
        };
        auto itemStartsInSpace = [](const IFCItem& it) {
            return it.canBreakBefore ||
                   (!it.text.empty() && std::isspace(
                        static_cast<unsigned char>(it.text.front())));
        };
        auto canBreakBetween = [&](size_t prev, size_t next) {
            if (prev >= items.size() || next >= items.size()) return true;
            return itemEndsInSpace(items[prev]) || itemStartsInSpace(items[next]);
        };

        struct LineBounds {
            size_t start; size_t end; float totalWidth;
            float maxHeight = 0;   // final line box height
            float above = 0;       // distance from line top to the baseline
        };
        std::vector<LineBounds> lines;
        {
            size_t lineStart = 0;
            float cursorX = 0;
            auto emitLine = [&](size_t endIdx) {
                float w = 0;
                for (size_t k = lineStart; k < endIdx; ++k) w += items[k].width;
                lines.push_back({lineStart, endIdx, w});
                lineStart = endIdx;
                cursorX = 0;
            };
            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].forceBreak) {
                    // <br>: terminate line after including the break marker so it
                    // advances cursorY by a full line even if the line was empty.
                    lines.push_back({lineStart, i + 1, cursorX});
                    lineStart = i + 1;
                    cursorX = 0;
                    continue;
                }
                if (whiteSpace != "nowrap" && cursorX > 0 && cursorX + items[i].width > childAvailable) {
                    // Find the latest in-range break point at or before i.
                    size_t breakIdx = i;
                    while (breakIdx > lineStart &&
                           !canBreakBetween(breakIdx - 1, breakIdx)) {
                        --breakIdx;
                    }
                    if (breakIdx == lineStart) {
                        // No valid break — let the line overflow rather
                        // than split an atomic unit.
                        breakIdx = i;
                    }
                    emitLine(breakIdx);
                    for (size_t k = breakIdx; k < i; ++k) {
                        cursorX += items[k].width;
                    }
                }
                cursorX += items[i].width;
            }
            if (lineStart < items.size()) {
                lines.push_back({lineStart, items.size(), cursorX});
            }
        }

        // Collapsible whitespace at a soft-wrap boundary is removed: a run
        // that starts a wrapped line drops its leading space and a run that
        // ends any line drops its trailing space (CSS Text §white-space
        // processing). Adjust the line's width so alignment math matches.
        if (whiteSpace != "pre" && whiteSpace != "pre-wrap") {
            float spW = spaceWidth;
            for (auto& line : lines) {
                if (line.start >= line.end) continue;
                IFCItem& first = items[line.start];
                if (!first.isElement && !first.forceBreak) {
                    while (!first.text.empty() && first.text.front() == ' ') {
                        first.text.erase(first.text.begin());
                        first.width = std::max(0.0f, first.width - spW);
                        line.totalWidth = std::max(0.0f, line.totalWidth - spW);
                    }
                }
                IFCItem& last = items[line.end - 1];
                if (!last.isElement && !last.forceBreak) {
                    while (!last.text.empty() && last.text.back() == ' ') {
                        last.text.pop_back();
                        last.width = std::max(0.0f, last.width - spW);
                        line.totalWidth = std::max(0.0f, line.totalWidth - spW);
                    }
                }
            }
        }

        // Resolve each line's vertical geometry (CSS2 §10.8): baseline-align
        // the items, take the union of their [baseline-above, baseline+below]
        // extents together with the block's strut. vertical-align: middle
        // centers a box on baseline + xHeight/2 and its extent participates
        // in the union; a top/bottom-aligned item that is still taller than
        // the line grows it downward (the baseline does not move).
        float strutXHeight = strut.xHeight;
        for (auto& line : lines) {
            float above = strutAbove;
            float below = strutBelow;
            for (size_t k = line.start; k < line.end; ++k) {
                const IFCItem& it = items[k];
                if (it.valign == 0) {
                    above = std::max(above, it.above);
                    below = std::max(below, it.below);
                } else if (it.valign == 2) {
                    float a = it.height * 0.5f + strutXHeight * 0.5f;
                    above = std::max(above, a);
                    below = std::max(below, it.height - a);
                }
            }
            for (size_t k = line.start; k < line.end; ++k) {
                const IFCItem& it = items[k];
                if (it.valign != 1 && it.valign != 3) continue;
                if (it.height > above + below) below = it.height - above;
            }
            line.above = above;
            line.maxHeight = above + below;
        }

        // Resolve text-indent (first line only)
        float textIndent = resolveLength(styleVal(style, "text-indent"), childAvailable, fontSize);

        // Position items per line with text-align offset
        for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
            auto& line = lines[lineIdx];
            bool isLastLine = (lineIdx == lines.size() - 1);
            float extraSpace = childAvailable - line.totalWidth;
            float xOffset = 0;
            float gap = 0;
            if (extraSpace > 0) {
                if (resolvedAlign == "center") xOffset = extraSpace / 2.0f;
                else if (resolvedAlign == "right" || resolvedAlign == "end") xOffset = extraSpace;
                else if (resolvedAlign == "justify" && !isLastLine) {
                    size_t itemCount = line.end - line.start;
                    if (itemCount > 1)
                        gap = extraSpace / static_cast<float>(itemCount - 1);
                }
            }
            float cursorX = xOffset;
            if (lineIdx == 0) cursorX += textIndent;

            float lineBaseline = cursorY + line.above;

            for (size_t i = line.start; i < line.end; i++) {
                auto& item = items[i];
                if (item.forceBreak) {
                    if (item.node) {
                        // Position the <br> at line-end x with its natural
                        // font box hung on the line's baseline so its rect
                        // matches Chromium's getBoundingClientRect.
                        float brY = lineBaseline - strutAscent;
                        if (brY < cursorY) brY = cursorY;
                        item.node->box.contentRect.x = cursorX;
                        item.node->box.contentRect.y = brY;
                        item.node->box.contentRect.width = 0;
                    }
                    continue;
                }
                if (item.node) {
                    if (item.isElement) {
                        // Vertical position: align baselines by default;
                        // vertical-align top/middle/bottom position the
                        // margin box against the line box instead.
                        float yTop;
                        switch (item.valign) {
                            case 1: yTop = cursorY; break;
                            case 2: yTop = lineBaseline -
                                (item.height * 0.5f + strutXHeight * 0.5f); break;
                            case 3: yTop = cursorY + line.maxHeight - item.height; break;
                            default: yTop = lineBaseline - item.baseline; break;
                        }
                        item.node->box.contentRect.x = cursorX + item.node->box.margin.left +
                            item.node->box.padding.left + item.node->box.border.left;
                        if (item.baselineFromContent && item.valign == 0) {
                            // Non-replaced inline: `baseline` is measured
                            // from the content top — padding/border sit
                            // outside the line geometry and don't shift it.
                            item.node->box.contentRect.y = yTop;
                        } else {
                            item.node->box.contentRect.y = yTop + item.node->box.margin.top +
                                item.node->box.padding.top + item.node->box.border.top;
                        }
                    } else {
                        // Record the placed run so caret/selection geometry
                        // queries can map DOM offsets back to (x, y, w, h).
                        PlacedTextRun placed;
                        placed.x = cursorX;
                        placed.y = lineBaseline - item.baseline;
                        placed.width = item.width;
                        placed.height = item.height;
                        placed.text = item.text;
                        placed.srcStart = item.srcStart;
                        placed.srcEnd = item.srcEnd;
                        auto& trs = item.node->box.textRuns;
                        if (trs.empty()) {
                            item.node->box.contentRect.x = placed.x;
                            item.node->box.contentRect.y = placed.y;
                            item.node->box.contentRect.width = placed.width;
                            item.node->box.contentRect.height = placed.height;
                        } else {
                            float left = std::min(item.node->box.contentRect.x, placed.x);
                            float top  = std::min(item.node->box.contentRect.y, placed.y);
                            float right = std::max(
                                item.node->box.contentRect.x + item.node->box.contentRect.width,
                                placed.x + placed.width);
                            float bottom = std::max(
                                item.node->box.contentRect.y + item.node->box.contentRect.height,
                                placed.y + placed.height);
                            item.node->box.contentRect.x = left;
                            item.node->box.contentRect.y = top;
                            item.node->box.contentRect.width = right - left;
                            item.node->box.contentRect.height = bottom - top;
                        }
                        trs.push_back(std::move(placed));
                    }
                }
                cursorX += item.width + gap;
            }
            cursorY += line.maxHeight;
        }

        // The block's own baseline: the last line box's baseline, measured
        // from the content top (used when this block is an inline-block
        // child of another IFC).
        if (!lines.empty()) {
            node->box.baselineOffset =
                (cursorY - lines.back().maxHeight) + lines.back().above;
        }
    } else {
    // Block formatting context: layout children vertically
    float prevMarginBottom = 0.0f;
    bool firstChild = true;
    float firstBlockChildMarginTop = 0.0f;
    bool hadFirstBlockChild = false;

    // Float tracking: left and right float edges. shapeR >= 0 carries a
    // shape-outside: circle() exclusion (center cx/cy, radius r).
    struct FloatRect {
        float x, y, width, height;
        bool isLeft;
        float shapeCx = 0, shapeCy = 0, shapeR = -1.0f;
    };
    std::vector<FloatRect> floats;

    // Does this block establish a new block formatting context? BFC roots
    // contain their floats (CSS2 §10.6.3) and don't collapse margins with
    // children; non-BFC blocks let floats escape to the nearest BFC
    // ancestor. Computed up front so the child loop and the height pass
    // agree.
    bool establishesBFC = false;
    {
        // Root element always establishes the initial BFC
        if (!node->parent()) establishesBFC = true;
        const std::string& ov = styleVal(style, "overflow");
        const std::string& ovx = styleVal(style, "overflow-x");
        const std::string& ovy = styleVal(style, "overflow-y");
        if ((ov != "visible" && !ov.empty()) ||
            (ovx != "visible" && !ovx.empty()) ||
            (ovy != "visible" && !ovy.empty()))
            establishesBFC = true;
        const std::string& disp = styleVal(style, "display");
        if (disp == "inline-block" || disp == "flex" || disp == "inline-flex" ||
            disp == "grid" || disp == "inline-grid" || disp == "flow-root" ||
            disp == "table-cell" || disp == "table-caption")
            establishesBFC = true;
        if (position == "absolute" || position == "fixed")
            establishesBFC = true;
        const std::string& flt = styleVal(style, "float");
        if (flt == "left" || flt == "right")
            establishesBFC = true;
        // Multi-column containers are formatting-context roots too.
        const std::string& mcCount = styleVal(style, "column-count");
        const std::string& mcWidth = styleVal(style, "column-width");
        if ((!mcCount.empty() && mcCount != "auto") ||
            (!mcWidth.empty() && mcWidth != "auto"))
            establishesBFC = true;
    }

    // Get available width at a given Y position accounting for floats.
    // A float with a circle shape (shape-outside) only excludes the chord
    // of the circle covered by the queried band — outside the circle's
    // vertical range content flows past the float box entirely.
    auto getAvailableAtY = [&](float y, float h) -> std::pair<float, float> {
        float leftEdge = 0;
        float rightEdge = childAvailable;
        float effectiveH = h > 0 ? h : 1.0f; // treat zero-height as point query
        for (auto& f : floats) {
            if (!(y + effectiveH > f.y && y < f.y + f.height)) continue;
            if (f.shapeR >= 0) {
                // Vertical distance from the circle's center to the nearest
                // edge of the band (0 when the band covers the center).
                float dy = 0;
                if (f.shapeCy < y) dy = y - f.shapeCy;
                else if (f.shapeCy > y + effectiveH) dy = f.shapeCy - (y + effectiveH);
                if (dy >= f.shapeR) continue; // band misses the circle
                float half = std::sqrt(std::max(0.0f, f.shapeR * f.shapeR - dy * dy));
                if (f.isLeft) {
                    leftEdge = std::max(leftEdge,
                        std::min(f.x + f.width, f.shapeCx + half));
                } else {
                    rightEdge = std::min(rightEdge,
                        std::max(f.x, f.shapeCx - half));
                }
            } else if (f.isLeft) {
                leftEdge = std::max(leftEdge, f.x + f.width);
            } else {
                rightEdge = std::min(rightEdge, f.x);
            }
        }
        return {leftEdge, rightEdge};
    };

    // Resolve text-align for anonymous inline boxes in BFC
    const std::string& bfcTextAlign = styleVal(style, "text-align");
    const std::string& bfcDirection = styleVal(style, "direction");
    std::string bfcResolvedAlign = bfcTextAlign;
    if (bfcResolvedAlign == "start" || bfcResolvedAlign.empty()) {
        bfcResolvedAlign = (bfcDirection == "rtl") ? "right" : "left";
    } else if (bfcResolvedAlign == "end") {
        bfcResolvedAlign = (bfcDirection == "rtl") ? "left" : "right";
    }

    // The strut for anonymous line boxes in this BFC (same model as the
    // dedicated IFC path — CSS2 §10.8 with Blink's half-leading split).
    StrutMetrics anonStrut = computeStrut(style, fontSize, metrics);

    // Place a floated child with its margin-box top at `atY`, register its
    // exclusion (including any shape-outside: circle geometry), and return.
    auto placeFloat = [&](LayoutNode* child, float atY) {
        auto& childStyle = child->computedStyle();
        const std::string& childFloat = styleVal(childStyle, "float");
        layoutNode(child, childAvailable, metrics);

        float floatWidth = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
        float floatHeight = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;

        auto [leftEdge, rightEdge] = getAvailableAtY(atY, floatHeight);

        FloatRect fr{0, atY, floatWidth, floatHeight, childFloat == "left"};
        if (childFloat == "left") {
            child->box.contentRect.x = leftEdge + child->box.margin.left +
                child->box.padding.left + child->box.border.left;
            fr.x = leftEdge;
        } else {
            child->box.contentRect.x = rightEdge - floatWidth + child->box.margin.left +
                child->box.padding.left + child->box.border.left;
            fr.x = rightEdge - floatWidth;
        }
        child->box.contentRect.y = atY + child->box.margin.top +
            child->box.padding.top + child->box.border.top;

        // shape-outside: circle(<r>) — content wraps the circle's chord
        // instead of the margin box. The reference box defaults to the
        // margin box; a percentage radius resolves against
        // sqrt(w² + h²)/√2 (CSS Shapes §basic-shape).
        const std::string& shapeOutside = styleVal(childStyle, "shape-outside");
        if (shapeOutside.rfind("circle(", 0) == 0) {
            std::string arg = shapeOutside.substr(7);
            size_t stop = arg.find_first_of(")");
            size_t at = arg.find(" at ");
            if (at != std::string::npos && at < stop) stop = at;
            arg = arg.substr(0, stop);
            while (!arg.empty() && std::isspace((unsigned char)arg.front())) arg.erase(arg.begin());
            while (!arg.empty() && std::isspace((unsigned char)arg.back())) arg.pop_back();
            float r = -1.0f;
            if (arg.empty() || arg == "closest-side") {
                r = std::min(floatWidth, floatHeight) * 0.5f;
            } else if (arg == "farthest-side") {
                r = std::max(floatWidth, floatHeight) * 0.5f;
            } else if (!arg.empty() && arg.back() == '%') {
                float pct = 0;
                htmlayout::from_chars_fp(arg.data(), arg.data() + arg.size(), pct);
                r = pct * 0.01f * std::sqrt(
                    (floatWidth * floatWidth + floatHeight * floatHeight) * 0.5f);
            } else {
                r = resolveLength(arg, floatWidth, fontSize);
            }
            if (r >= 0) {
                fr.shapeCx = fr.x + floatWidth * 0.5f;
                fr.shapeCy = fr.y + floatHeight * 0.5f;
                fr.shapeR = r;
            }
        }
        floats.push_back(fr);
    };

    // Helper: flush accumulated inline children as an anonymous line box
    std::vector<LayoutNode*> pendingInline;
    auto flushInlineRun = [&]() {
        if (pendingInline.empty()) return;

        // First pass: measure all items and build line structure
        struct AnonItem {
            LayoutNode* node = nullptr;
            float width = 0, height = 0;
            bool isText = false;
            bool forceBreak = false;
            // Deferred float (placed during line building, not a line item)
            bool isFloat = false;
            // Baseline geometry (margin-box space for elements). Text hangs
            // the block's natural font box on the shared baseline.
            float baseline = 0;
            int valign = 0;
        };
        std::vector<AnonItem> anonItems;

        for (auto* inl : pendingInline) {
            if (inl->isTextNode()) {
                float tw = 0, th = 0;
                float ls2 = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
                float ws2 = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
                auto runs = breakTextIntoRuns(std::string(inl->textContent()), childAvailable,
                    styleVal(style, "font-family"), fontSize, styleVal(style, "font-weight"),
                    styleVal(style, "white-space"), metrics,
                    "normal", "normal", ls2, ws2, styleVal(style, "text-transform"));
                for (auto& run : runs) {
                    tw += run.width;
                    th = std::max(th, run.height);
                }
                th = std::max(th, anonStrut.lineHeight);
                AnonItem it{};
                it.node = inl; it.width = tw; it.height = th; it.isText = true;
                it.baseline = anonStrut.ascent;
                anonItems.push_back(std::move(it));
            } else if (inl->tagName() == "br" || inl->tagName() == "BR") {
                inl->box = LayoutBox{};
                // Record the natural font line-height (not the CSS line-height)
                // as the BR's bounding rect height — matches Chromium's
                // getBoundingClientRect for forced-break inlines.
                inl->box.contentRect.height = fontSize > 0 ? metrics.lineHeight(
                    styleVal(style, "font-family"), fontSize,
                    styleVal(style, "font-weight")) : 0.0f;
                AnonItem it{};
                it.node = inl; it.height = anonStrut.lineHeight;
                it.forceBreak = true;
                anonItems.push_back(std::move(it));
            } else {
                const std::string& fd = styleVal(inl->computedStyle(), "float");
                if (fd == "left" || fd == "right") {
                    // Deferred mid-run float: placed while building lines.
                    AnonItem it{};
                    it.node = inl; it.isFloat = true;
                    anonItems.push_back(std::move(it));
                    continue;
                }
                layoutNode(inl, childAvailable, metrics);
                float cw = inl->box.fullWidth() + inl->box.margin.left + inl->box.margin.right;
                float ch = inl->box.fullHeight() + inl->box.margin.top + inl->box.margin.bottom;
                AtomicInlineGeom g = atomicInlineGeometry(inl, ch, anonStrut.below,
                                                          childAvailable);
                AnonItem it{};
                it.node = inl; it.width = cw; it.height = ch;
                it.baseline = g.baseline; it.valign = g.valign;
                anonItems.push_back(std::move(it));
            }
        }

        // Build lines. Each line is float-aware: its available width and left
        // start come from getAvailableAtY() at the line's Y, so inline content
        // wraps narrower (and shifts right) where a float intrudes. xStart and
        // availWidth are carried into the positioning pass below. With no
        // overlapping float, leftEdge=0 and rightEdge=childAvailable, so this
        // reduces to the plain full-width behavior.
        struct AnonLine { size_t start; size_t end; float totalWidth; float maxHeight;
                          bool endsWithBreak; float xStart; float availWidth;
                          float above; };
        std::vector<AnonLine> anonLines;
        // Union of the strut with the items' baseline-aligned extents
        // (CSS2 §10.8): returns {above, below} for the line's items.
        auto anonLineExtents = [&](size_t s, size_t e) {
            float a = anonStrut.above, b = anonStrut.below;
            for (size_t k = s; k < e; ++k) {
                const AnonItem& it = anonItems[k];
                if (it.forceBreak || it.isFloat) continue;
                if (it.isText) continue; // text == strut extents
                if (it.valign == 0) {
                    a = std::max(a, it.baseline);
                    b = std::max(b, it.height - it.baseline);
                } else if (it.valign == 2) {
                    float ia = it.height * 0.5f + anonStrut.xHeight * 0.5f;
                    a = std::max(a, ia);
                    b = std::max(b, it.height - ia);
                }
            }
            for (size_t k = s; k < e; ++k) {
                const AnonItem& it = anonItems[k];
                if (it.forceBreak || it.isText || it.isFloat) continue;
                if (it.valign != 1 && it.valign != 3) continue;
                if (it.height > a + b) b = it.height - a;
            }
            return std::pair<float, float>{a, b};
        };
        {
            // Height estimate for the per-line float band query (the line's own
            // height isn't known until it's filled). The natural font
            // line-height matches the common all-text/inline-word case.
            float estLineH = resolveLineHeight(styleVal(style, "line-height"), fontSize,
                styleVal(style, "font-family"), styleVal(style, "font-weight"), metrics);
            bool anonNoWrap = styleVal(style, "white-space") == "nowrap";

            float lineY = cursorY;
            auto band = [&](float y) {
                auto [le, re] = getAvailableAtY(y, estLineH);
                return std::pair<float, float>{le, re};
            };
            auto [curLE, curRE] = band(lineY);

            size_t ls = 0;
            float cx = 0;
            auto closeLine = [&](size_t end, bool brk) {
                auto [la, lb] = anonLineExtents(ls, end);
                float lh = la + lb;
                anonLines.push_back({ls, end, cx, lh, brk, curLE, curRE - curLE, la});
                lineY += (lh > 0 ? lh : estLineH);
                ls = end; cx = 0;
                auto b = band(lineY);
                curLE = b.first; curRE = b.second;
            };
            for (size_t i = 0; i < anonItems.size(); i++) {
                if (anonItems[i].isFloat) {
                    // A float breaking into the middle of a line is placed
                    // below the line under construction (its top may not sit
                    // above the current line box, CSS2 §9.5.1 rule 6); at a
                    // line start it sits at the line's own top.
                    float placeY = lineY;
                    if (cx > 0) {
                        auto [la, lb] = anonLineExtents(ls, i);
                        placeY = lineY + la + lb;
                    }
                    placeFloat(anonItems[i].node, placeY);
                    if (cx == 0) {
                        // The new float narrows the line being started.
                        auto b = band(lineY);
                        curLE = b.first; curRE = b.second;
                    }
                    continue;
                }
                if (anonItems[i].forceBreak) {
                    closeLine(i + 1, true);
                    continue;
                }
                if (!anonNoWrap && cx > 0 && cx + anonItems[i].width > (curRE - curLE)) {
                    closeLine(i, false);
                }
                cx += anonItems[i].width;
            }
            if (ls < anonItems.size()) {
                auto [la, lb] = anonLineExtents(ls, anonItems.size());
                anonLines.push_back({ls, anonItems.size(), cx, la + lb, false,
                                     curLE, curRE - curLE, la});
            }
        }

        // Position with text-align (skip zero-width lines from whitespace,
        // but preserve explicit <br>-terminated lines so blank lines render)
        for (auto& line : anonLines) {
            if (line.totalWidth <= 0 && !line.endsWithBreak) continue;
            float extra = line.availWidth - line.totalWidth;
            float xOff = 0;
            if (extra > 0) {
                if (bfcResolvedAlign == "center") xOff = extra / 2.0f;
                else if (bfcResolvedAlign == "right" || bfcResolvedAlign == "end") xOff = extra;
            }
            // line.xStart is the float-aware left edge of this line.
            float cx = line.xStart + xOff;
            float lineBaseline = cursorY + line.above;
            for (size_t i = line.start; i < line.end; i++) {
                auto& ai = anonItems[i];
                if (ai.isFloat) continue; // placed during line building
                if (ai.forceBreak) {
                    // Position the BR at the line's current x (post-content)
                    // and vertically center within the line box, matching
                    // Chromium's getBoundingClientRect for <br>.
                    float brH = ai.node->box.contentRect.height;
                    float halfLeading = (line.maxHeight - brH) * 0.5f;
                    if (halfLeading < 0) halfLeading = 0;
                    ai.node->box.contentRect.x = cx;
                    ai.node->box.contentRect.y = cursorY + halfLeading;
                    ai.node->box.contentRect.width = 0;
                    continue;
                }
                if (ai.isText) {
                    ai.node->box.contentRect.x = cx;
                    ai.node->box.contentRect.y = cursorY;
                    ai.node->box.contentRect.width = ai.width;
                    ai.node->box.contentRect.height = ai.height;
                } else {
                    // Vertical position by alignment: baseline items align
                    // baselines; middle centers on baseline + xHeight/2;
                    // top/bottom pin the margin box to the line edges.
                    float yTop;
                    switch (ai.valign) {
                        case 1: yTop = cursorY; break;
                        case 2: yTop = lineBaseline -
                            (ai.height * 0.5f + anonStrut.xHeight * 0.5f); break;
                        case 3: yTop = cursorY + line.maxHeight - ai.height; break;
                        default: yTop = lineBaseline - ai.baseline; break;
                    }
                    ai.node->box.contentRect.x = cx + ai.node->box.margin.left +
                        ai.node->box.padding.left + ai.node->box.border.left;
                    ai.node->box.contentRect.y = yTop + ai.node->box.margin.top +
                        ai.node->box.padding.top + ai.node->box.border.top;
                }
                cx += ai.width;
            }
            cursorY += line.maxHeight;
        }
        pendingInline.clear();
    };

    for (auto* child : getLayoutChildren(node)) {
        auto& childStyle = child->computedStyle();

        if (child->isTextNode()) {
            pendingInline.push_back(child);
            continue;
        }

        const std::string& childDisplay = styleVal(childStyle, "display");
        if (childDisplay == "none") {
            child->box = LayoutBox{};
            continue;
        }

        const std::string& childPos = styleVal(childStyle, "position");

        // Absolutely and fixed positioned children are out of flow
        // (positioned by the post-layout absolute positioning pass)
        if (childPos == "absolute" || childPos == "fixed") continue;

        // Collect inline/inline-block children for horizontal layout
        if (childDisplay == "inline" || childDisplay == "inline-block" ||
            childDisplay == "inline-flex" || childDisplay == "inline-grid") {
            pendingInline.push_back(child);
            continue;
        }

        const std::string& childFloat = styleVal(childStyle, "float");

        // A float that interrupts an inline run does NOT break the line
        // (CSS2 §9.5): defer it into the run — flushInlineRun places it
        // below the line being built and the inline content continues.
        if ((childFloat == "left" || childFloat == "right") &&
            !pendingInline.empty()) {
            pendingInline.push_back(child);
            continue;
        }

        // Block child encountered — flush any pending inline items first
        flushInlineRun();

        const std::string& childClear = styleVal(childStyle, "clear");

        // Handle clear: move past floats on the specified side(s)
        if (childClear == "left" || childClear == "both") {
            for (auto& f : floats) {
                if (f.isLeft) cursorY = std::max(cursorY, f.y + f.height);
            }
        }
        if (childClear == "right" || childClear == "both") {
            for (auto& f : floats) {
                if (!f.isLeft) cursorY = std::max(cursorY, f.y + f.height);
            }
        }

        // Handle float: left/right
        if (childFloat == "left" || childFloat == "right") {
            placeFloat(child, cursorY);
            continue; // floats don't advance cursorY
        }

        // Recursively layout the child (with available width reduced by floats)
        auto [leftEdge, rightEdge] = getAvailableAtY(cursorY, 0);
        float inFlowAvail = rightEdge - leftEdge;
        if (inFlowAvail < 0) inFlowAvail = 0;

        layoutNode(child, inFlowAvail, metrics);

        float childMarginTop = child->box.margin.top;
        float childMarginBottom = child->box.margin.bottom;

        // Check if this child is an "empty box" — zero height, no padding, no border.
        // Empty boxes have their top and bottom margins collapse together.
        bool isEmptyBox = (child->box.contentRect.height == 0 &&
                           child->box.padding.top == 0 && child->box.padding.bottom == 0 &&
                           child->box.border.top == 0 && child->box.border.bottom == 0 &&
                           getLayoutChildren(child).empty());

        float effectiveMargin;
        if (isEmptyBox) {
            // Margins collapse through empty box: top and bottom collapse together,
            // then collapse with adjacent margins
            float selfCollapsed = std::max(childMarginTop, childMarginBottom);
            if (firstChild) {
                effectiveMargin = selfCollapsed;
                firstChild = false;
            } else {
                effectiveMargin = std::max(prevMarginBottom, selfCollapsed);
            }
            // The empty box doesn't advance the cursor beyond the collapsed margin
            prevMarginBottom = effectiveMargin;

            // Position the empty box at current cursor
            cursorY += effectiveMargin;
            auto [le2, re2] = getAvailableAtY(cursorY, 0);
            child->box.contentRect.x = le2 + child->box.margin.left + child->box.padding.left + child->box.border.left;
            child->box.contentRect.y = cursorY;
            // Reset cursor: the margin is "passed through" to the next sibling
            cursorY -= effectiveMargin;
            continue;
        }

        // Margin collapsing: adjacent vertical margins collapse. Per CSS 2.1
        // §8.3.1, the resulting margin is the largest of the positive
        // operands plus the smallest (most negative) of the negatives.
        // For two values a, b that simplifies to:
        //   max(a, b, 0) + min(a, b, 0)
        float collapsedMargin;
        if (firstChild) {
            collapsedMargin = childMarginTop;
            firstBlockChildMarginTop = childMarginTop;
            hadFirstBlockChild = true;
            firstChild = false;
        } else {
            float pos = std::max({prevMarginBottom, childMarginTop, 0.0f});
            float neg = std::min({prevMarginBottom, childMarginTop, 0.0f});
            collapsedMargin = pos + neg;
        }

        cursorY += collapsedMargin;

        // Position the child's content rect (offset by float margins)
        auto [le2, re2] = getAvailableAtY(cursorY, child->box.fullHeight());
        child->box.contentRect.x = le2 + child->box.margin.left + child->box.padding.left + child->box.border.left;
        child->box.contentRect.y = cursorY + child->box.padding.top + child->box.border.top;

        // Adopt floats that escaped the child (it isn't a BFC root, so its
        // floats belong to this formatting context and keep excluding
        // content beside the following siblings).
        for (const auto& ef : child->box.escapedFloats) {
            FloatRect fr{ef.x + child->box.contentRect.x,
                         ef.y + child->box.contentRect.y,
                         ef.width, ef.height, ef.isLeft};
            if (ef.shapeR >= 0) {
                fr.shapeCx = ef.shapeCx + child->box.contentRect.x;
                fr.shapeCy = ef.shapeCy + child->box.contentRect.y;
                fr.shapeR = ef.shapeR;
            }
            floats.push_back(fr);
        }

        // Apply position: relative/sticky offset after normal positioning
        // (sticky behaves like relative during layout; scroll clamping is a paint-time concern)
        if (childPos == "relative" || childPos == "sticky") {
            float childFontSize = resolveLength(styleVal(childStyle, "font-size"), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            const std::string& topVal = styleVal(childStyle, "top");
            const std::string& leftVal = styleVal(childStyle, "left");
            const std::string& bottomVal = styleVal(childStyle, "bottom");
            const std::string& rightVal = styleVal(childStyle, "right");

            if (topVal != "auto" && !topVal.empty()) {
                child->box.contentRect.y += resolveLength(topVal, 0, childFontSize);
            } else if (bottomVal != "auto" && !bottomVal.empty()) {
                child->box.contentRect.y -= resolveLength(bottomVal, 0, childFontSize);
            }
            if (leftVal != "auto" && !leftVal.empty()) {
                child->box.contentRect.x += resolveLength(leftVal, childAvailable, childFontSize);
            } else if (rightVal != "auto" && !rightVal.empty()) {
                child->box.contentRect.x -= resolveLength(rightVal, childAvailable, childFontSize);
            }
        }

        // Advance cursor past the child's full box height
        cursorY += child->box.border.top + child->box.padding.top +
                   child->box.contentRect.height +
                   child->box.padding.bottom + child->box.border.bottom;

        prevMarginBottom = childMarginBottom;
    }

    // Flush any remaining inline items at end of BFC
    flushInlineRun();

    // Add the last child's bottom margin
    if (!firstChild) {
        cursorY += prevMarginBottom;
    }

    // A BFC root contains its floats (CSS2 §10.6.3); a non-BFC block does
    // NOT — they escape to the nearest BFC ancestor and keep excluding
    // content beside the following siblings, so hand them up instead.
    node->box.escapedFloats.clear();
    if (establishesBFC) {
        for (auto& f : floats) {
            cursorY = std::max(cursorY, f.y + f.height);
        }
    } else {
        node->box.escapedFloats.reserve(floats.size());
        for (auto& f : floats) {
            LayoutBox::EscapedFloat ef;
            ef.x = f.x; ef.y = f.y; ef.width = f.width; ef.height = f.height;
            ef.isLeft = f.isLeft;
            ef.shapeCx = f.shapeCx; ef.shapeCy = f.shapeCy; ef.shapeR = f.shapeR;
            node->box.escapedFloats.push_back(ef);
        }
    }

    // Parent-child margin collapsing (CSS 2.1 §8.3.1):
    // A parent's top/bottom margin collapses with its first/last in-flow block child's
    // margin if:
    //   - No top/bottom padding or border separating them
    //   - The parent doesn't establish a new BFC
    //   - For bottom: parent has auto height
    if (!establishesBFC && hadFirstBlockChild) {
        // Top margin collapsing: first child's top margin escapes through the parent
        if (node->box.padding.top == 0 && node->box.border.top == 0 &&
            firstBlockChildMarginTop > 0) {
            float collapsed = std::max(node->box.margin.top, firstBlockChildMarginTop);
            node->box.margin.top = collapsed;
            // Shift all children up by the first child's margin that was applied to cursorY
            for (auto* child : getLayoutChildren(node)) {
                child->box.contentRect.y -= firstBlockChildMarginTop;
            }
            for (auto& ef : node->box.escapedFloats) {
                ef.y -= firstBlockChildMarginTop;
                if (ef.shapeR >= 0) ef.shapeCy -= firstBlockChildMarginTop;
            }
            cursorY -= firstBlockChildMarginTop;
        }

        // Bottom margin collapsing: last child's bottom margin escapes through the parent
        // (only for auto height)
        float heightRef0 = node->availableHeight;
        float specH0 = resolveDimension(styleVal(style, "height"), heightRef0, fontSize);
        if (node->box.padding.bottom == 0 && node->box.border.bottom == 0 &&
            specH0 < 0 && prevMarginBottom > 0) {
            float collapsed = std::max(node->box.margin.bottom, prevMarginBottom);
            node->box.margin.bottom = collapsed;
            cursorY -= prevMarginBottom;
        }
    }

    } // end BFC else block

    // Resolve height using available height from containing block
    float heightRef = node->availableHeight;
    float specifiedHeight = resolveDimension(styleVal(style, "height"), heightRef, fontSize);
    if (specifiedHeight >= 0.0f) {
        const std::string& boxSizing = styleVal(style, "box-sizing");
        if (boxSizing == "border-box") {
            float paddingV = node->box.padding.top + node->box.padding.bottom;
            float borderV = node->box.border.top + node->box.border.bottom;
            node->box.contentRect.height = specifiedHeight - paddingV - borderV;
            if (node->box.contentRect.height < 0.0f) node->box.contentRect.height = 0.0f;
        } else {
            node->box.contentRect.height = specifiedHeight;
        }
    } else if (aspectRatioCBH >= 0.0f && cursorY <= aspectRatioCBH) {
        // aspect-ratio: derive height from width when height is auto.
        // Per spec the ratio applies to the border box; if content overflows,
        // content height takes precedence.
        node->box.contentRect.height = aspectRatioCBH;
    } else {
        // height: auto — for replaced elements (e.g. <img>) with intrinsic
        // size, use that; otherwise shrink to fit content.
        float intrW = 0, intrH = 0;
        float ctW = node->box.contentRect.width;
        if (cursorY == 0.0f && node->intrinsicSize(intrW, intrH, ctW)) {
            if (node->hasIntrinsicRatio() && intrW > 0.0f && intrH > 0.0f &&
                ctW != intrW) {
                // The used width was constrained away from intrinsic (max-width,
                // min-width, or a specified width). A replaced element with a
                // fixed intrinsic ratio scales its auto height to match, so the
                // box keeps the media's aspect ratio (CSS2 §10.4) instead of
                // squashing a 1120×240 canvas into 768×240.
                node->box.contentRect.height = intrH * (ctW / intrW);
            } else {
                node->box.contentRect.height = intrH;
            }
        } else {
            node->box.contentRect.height = cursorY;
        }
    }

    // Store natural height before clamping (for scroll extent calculation)
    node->box.naturalHeight = std::max(cursorY, node->box.contentRect.height);

    // Apply min/max-height constraints
    float minH = resolveDimension(styleVal(style, "min-height"), heightRef, fontSize);
    float maxH = resolveDimension(styleVal(style, "max-height"), heightRef, fontSize);
    if (minH >= 0.0f && node->box.contentRect.height < minH) node->box.contentRect.height = minH;
    if (maxH >= 0.0f && node->box.contentRect.height > maxH) node->box.contentRect.height = maxH;

    // Propagate available height to in-flow children for percentage height resolution.
    // Children can use percentage heights only when the parent has a definite height.
    float childAvailableHeight = (specifiedHeight >= 0.0f) ? node->box.contentRect.height : 0.0f;
    // Root element chain: propagate viewport height even with auto height
    if (childAvailableHeight == 0.0f && specifiedHeight < 0.0f && node->viewportHeight > 0 &&
        (!node->parent() || !node->parent()->parent())) {
        childAvailableHeight = node->box.contentRect.height;
    }
    for (auto* child : getLayoutChildren(node)) {
        if (!child->isTextNode()) {
            auto& cs = child->computedStyle();
            const std::string& cp = styleVal(cs, "position");
            if (cp != "absolute" && cp != "fixed") {
                child->availableHeight = childAvailableHeight;
            }
        }
    }

    // Multi-column layout: redistribute children into columns if column-count or column-width is set
    const std::string& colCountStr = styleVal(style, "column-count");
    const std::string& colWidthStr = styleVal(style, "column-width");

    bool hasMulticol = (!colCountStr.empty() && colCountStr != "auto") ||
                       (!colWidthStr.empty() && colWidthStr != "auto");

    if (hasMulticol) {
        int columnCount = 1;
        float columnWidth = 0.0f;

        if (!colCountStr.empty() && colCountStr != "auto") {
            try { columnCount = std::stoi(colCountStr); } catch (...) { columnCount = 1; }
            if (columnCount < 1) columnCount = 1;
        }

        if (!colWidthStr.empty() && colWidthStr != "auto") {
            columnWidth = resolveLength(colWidthStr, contentWidth, fontSize);
        }

        // Resolve column gap
        const std::string& colGapStr = styleVal(style, "column-gap");
        float columnGap = 0.0f;
        if (!colGapStr.empty() && colGapStr != "normal") {
            columnGap = resolveLength(colGapStr, contentWidth, fontSize);
        }

        // Determine actual column count and width
        if (columnWidth > 0 && (colCountStr.empty() || colCountStr == "auto")) {
            // Only column-width specified: compute count from available width
            columnCount = std::max(1, static_cast<int>((contentWidth + columnGap) / (columnWidth + columnGap)));
        }
        // Compute actual column width from count
        float actualColWidth = (contentWidth - columnGap * (columnCount - 1)) / columnCount;
        if (actualColWidth < 0) actualColWidth = 0;

        // Compute total content height (already computed as cursorY or specifiedHeight)
        float totalHeight = node->box.contentRect.height;

        // Target column height: divide total evenly
        float targetColHeight = totalHeight / columnCount;
        if (targetColHeight < 1.0f) targetColHeight = 1.0f;

        // Redistribute in-flow children into columns
        // Collect in-flow children (not absolute, not display:none)
        std::vector<LayoutNode*> inFlowChildren;
        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            if (styleVal(cs, "display") == "none") continue;
            const std::string& cp = styleVal(cs, "position");
            if (cp == "absolute" || cp == "fixed") continue;
            inFlowChildren.push_back(child);
        }

        // Split children into segments separated by column-span:all elements.
        // Each segment is laid out in columns; spanners get full width between segments.
        struct Segment {
            std::vector<LayoutNode*> children;
            bool isSpanner = false; // true = column-span:all element
        };
        std::vector<Segment> segments;
        Segment current;

        for (auto* child : inFlowChildren) {
            auto& cs = child->computedStyle();
            if (styleVal(cs, "column-span") == "all") {
                if (!current.children.empty()) {
                    segments.push_back(std::move(current));
                    current = Segment{};
                }
                Segment spanner;
                spanner.isSpanner = true;
                spanner.children.push_back(child);
                segments.push_back(std::move(spanner));
            } else {
                current.children.push_back(child);
            }
        }
        if (!current.children.empty()) segments.push_back(std::move(current));

        float totalY = 0.0f;

        for (auto& seg : segments) {
            if (seg.isSpanner) {
                // Layout spanner at full container width
                auto* child = seg.children[0];
                layoutNode(child, contentWidth, metrics);
                child->box.contentRect.x = child->box.margin.left +
                    child->box.padding.left + child->box.border.left;
                child->box.contentRect.y = totalY + child->box.margin.top +
                    child->box.padding.top + child->box.border.top;
                float childFullH = child->box.margin.top + child->box.border.top +
                    child->box.padding.top + child->box.contentRect.height +
                    child->box.padding.bottom + child->box.border.bottom +
                    child->box.margin.bottom;
                totalY += childFullH;
            } else {
                // Layout segment children into columns
                // First compute total height to determine target
                float segTotalH = 0;
                for (auto* child : seg.children) {
                    layoutNode(child, actualColWidth, metrics);
                    segTotalH += child->box.margin.top + child->box.border.top +
                        child->box.padding.top + child->box.contentRect.height +
                        child->box.padding.bottom + child->box.border.bottom +
                        child->box.margin.bottom;
                }
                float segTargetH = segTotalH / columnCount;
                if (segTargetH < 1.0f) segTargetH = 1.0f;

                int currentCol = 0;
                float colY = 0.0f;
                float maxColHeight = 0.0f;

                for (auto* child : seg.children) {
                    auto& cs = child->computedStyle();

                    // break-before: column
                    const std::string& breakBefore = styleVal(cs, "break-before");
                    if ((breakBefore == "column" || breakBefore == "always") &&
                        colY > 0 && currentCol < columnCount - 1) {
                        maxColHeight = std::max(maxColHeight, colY);
                        currentCol++;
                        colY = 0.0f;
                    }

                    layoutNode(child, actualColWidth, metrics);

                    float childFullH = child->box.margin.top + child->box.border.top +
                        child->box.padding.top + child->box.contentRect.height +
                        child->box.padding.bottom + child->box.border.bottom +
                        child->box.margin.bottom;

                    // Check if child would overflow current column
                    if (colY > 0 && colY + childFullH > segTargetH && currentCol < columnCount - 1) {
                        // break-inside: avoid — keep element whole if possible
                        maxColHeight = std::max(maxColHeight, colY);
                        currentCol++;
                        colY = 0.0f;
                    }

                    float colX = currentCol * (actualColWidth + columnGap);

                    child->box.contentRect.x = colX + child->box.margin.left +
                        child->box.padding.left + child->box.border.left;
                    child->box.contentRect.y = totalY + colY + child->box.margin.top +
                        child->box.padding.top + child->box.border.top;

                    if (child->box.contentRect.width > actualColWidth) {
                        child->box.contentRect.width = actualColWidth;
                    }

                    colY += childFullH;

                    // break-after: column
                    const std::string& breakAfter = styleVal(cs, "break-after");
                    if ((breakAfter == "column" || breakAfter == "always") &&
                        currentCol < columnCount - 1) {
                        maxColHeight = std::max(maxColHeight, colY);
                        currentCol++;
                        colY = 0.0f;
                    }
                }
                maxColHeight = std::max(maxColHeight, colY);
                totalY += maxColHeight;
            }
        }
        node->box.contentRect.height = totalY;
    }
}

} // namespace htmlayout::layout
