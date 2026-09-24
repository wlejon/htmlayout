#include "layout/block.h"
#include "layout/block_internal.h"
#include "layout/formatting_context.h"
#include "../from_chars_compat.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include "layout/text.h"
#include "layout/bidi_line.h"
#include "layout/line_clamp.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

} // anonymous namespace

// Does this box establish a new block formatting context (CSS2 §9.4.1)?
// BFC roots contain their floats, don't collapse margins with their
// children, and — when sitting beside a float — narrow to the space
// between the float edges instead of overlapping them.
bool nodeEstablishesBFC(LayoutNode* node) {
    if (!node->parent()) return true; // root element: the initial BFC
    auto& style = node->computedStyle();
    const std::string& ov = styleVal(node, Prop::Overflow);
    const std::string& ovx = styleVal(node, Prop::OverflowX);
    const std::string& ovy = styleVal(node, Prop::OverflowY);
    if ((ov != "visible" && !ov.empty()) ||
        (ovx != "visible" && !ovx.empty()) ||
        (ovy != "visible" && !ovy.empty()))
        return true;
    const std::string& disp = styleVal(node, Prop::Display);
    if (disp == "inline-block" || disp == "flex" || disp == "inline-flex" ||
        disp == "grid" || disp == "inline-grid" || disp == "flow-root" ||
        disp == "table-cell" || disp == "table-caption" ||
        disp == "-webkit-box")  // (-webkit-inline-box reads as inline-block)
        return true;
    const std::string& position = styleVal(node, Prop::Position);
    if (position == "absolute" || position == "fixed")
        return true;
    const std::string& flt = styleVal(node, Prop::Float);
    if (flt == "left" || flt == "right")
        return true;
    // A flex or grid item establishes an independent formatting context
    // (Flexbox §4, Grid §6.1).
    if (LayoutNode* p = node->parent()) {
        const std::string& pd = styleVal(p, Prop::Display);
        if (pd == "flex" || pd == "inline-flex" ||
            pd == "grid" || pd == "inline-grid")
            return true;
    }
    // Multi-column containers are formatting-context roots too.
    const std::string& mcCount = styleVal(node, Prop::ColumnCount);
    const std::string& mcWidth = styleVal(node, Prop::ColumnWidth);
    if ((!mcCount.empty() && mcCount != "auto") ||
        (!mcWidth.empty() && mcWidth != "auto"))
        return true;
    // Layout containment makes the box an independent formatting context
    // (css-contain §2). container-type: size / inline-size implies it.
    const std::string& ctype = styleVal(node, Prop::ContainerType);
    if (ctype.find("size") != std::string::npos)
        return true;
    const std::string& contain = styleVal(node, Prop::Contain);
    if (contain.find("layout") != std::string::npos ||
        contain.find("paint") != std::string::npos ||
        contain == "strict" || contain == "content")
        return true;
    return false;
}

namespace {

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
                                      float strutBelow, float availWidth,
                                      TextMetrics& metrics) {
    AtomicInlineGeom g;
    auto& styleRef = child->computedStyle();
    const std::string& va = styleVal(child, Prop::VerticalAlign);
    if (va == "top" || va == "text-top") g.valign = 1;
    else if (va == "middle") g.valign = 2;
    else if (va == "bottom" || va == "text-bottom") g.valign = 3;

    float iw = 0, ih = 0;
    if (child->intrinsicSize(iw, ih, availWidth)) {
        std::string_view tag = child->tagName();
        bool isTextarea = (tag == "textarea" || tag == "TEXTAREA");
        if (child->hasIntrinsicRatio() || isTextarea) {
            // Replaced media (img/canvas/video/svg) and the multi-line
            // <textarea> align by the bottom margin edge, so the line strut's
            // descent hangs below the control (Blink).
            g.baseline = height;
        } else {
            std::string_view ctype = child->attribute("type");
            bool isCheckRadio = (tag == "input" || tag == "INPUT") &&
                                (ctype == "checkbox" || ctype == "radio");
            if (isCheckRadio) {
                // Checkbox/radio: align by the bottom of the border box.
                g.baseline = height - child->box.margin.bottom;
            } else {
                // Single-line form controls (input, select, button-type
                // inputs): align by the control's internal text baseline —
                // top border + padding + the control font's ascent.
                float fs = resolveLength(styleVal(child, Prop::FontSize), 16.0f, 16.0f);
                if (fs <= 0.0f) fs = 16.0f;
                const std::string& fam = styleVal(child, Prop::FontFamily);
                const std::string& wt  = styleVal(child, Prop::FontWeight);
                float asc = fs > 0.0f ? metrics.ascent(fam, fs, wt) : 0.0f;
                if (asc <= 0.0f) asc = fs * 0.8f;
                g.baseline = child->box.margin.top + child->box.border.top +
                             child->box.padding.top + asc;
            }
            if (g.baseline > height) g.baseline = height;
            if (g.baseline < 0) g.baseline = 0;
            (void)strutBelow;
        }
    } else {
        // Atomic inline: baseline of the last line box when it has in-flow
        // content and visible overflow; otherwise the bottom margin edge.
        const std::string& ov = styleVal(child, Prop::Overflow);
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
    const std::string& fontSizeStr = styleVal(node, Prop::FontSize);
    float fontSize = resolveLength(fontSizeStr, 16.0f, 16.0f);
    if (fontSize <= 0.0f) {
        // An explicit zero (font-size: 0) is honored — em units and the
        // line-box strut collapse to nothing, matching Chromium. Anything
        // missing, negative or unparseable falls back to the default.
        bool explicitZero = !fontSizeStr.empty() &&
            (fontSizeStr[0] == '0' || fontSizeStr[0] == '.');
        fontSize = explicitZero ? 0.0f : 16.0f;
    }

    // Establish the ch/ex font-metric context for this element before any of
    // its lengths are resolved. ch = advance of "0", ex = x-height, both in the
    // element's own (inherited) font. Nested elements reset this on entry.
    {
        const std::string& fam = styleVal(node, Prop::FontFamily);
        const std::string& wt  = styleVal(node, Prop::FontWeight);
        float chPx = fontSize > 0.0f ? metrics.measureWidth("0", fam, fontSize, wt) : 0.0f;
        float exPx = fontSize > 0.0f ? metrics.xHeight(fam, fontSize, wt) : 0.0f;
        setLengthFontContext(chPx, exPx);
    }

    const std::string& position = styleVal(node, Prop::Position);

    // Fresh layout pass: forget any baseline recorded by a previous layout
    // and any floats handed up to the parent last time.
    node->box.baselineOffset = -1.0f;
    node->box.escapedFloats.clear();

    // line-clamp / -webkit-line-clamp: resolved (and last pass's clamp undone)
    // before the children are laid out; applied once they are placed.
    LineClampSpec lineClamp;
    const bool clampPass = beginLineClamp(node, lineClamp);
    // text-overflow truncates overflowing lines once they are placed, the
    // same way (after the clamp, on the lines it kept).
    std::string textOverflowEllipsis;
    const bool textOverflowPass = beginTextOverflow(node, textOverflowEllipsis);

    // Resolve margin, padding, border
    node->box.margin = resolveEdges(node, kMarginProps, availableWidth, fontSize);
    node->box.padding = resolveEdges(node, kPaddingProps, availableWidth, fontSize);
    node->box.border = resolveBorders(node, availableWidth, fontSize);

    // Resolve width
    float marginH = node->box.margin.left + node->box.margin.right;
    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;

    float specifiedWidth = resolveDimension(styleVal(node, Prop::Width), availableWidth, fontSize);
    // A table cell's 'width' is an input to the table's column algorithm,
    // not the used width — the cell always fills its assigned track span,
    // handed in as availableWidth by table.cpp's final layout pass. Flow as
    // width:auto so inline content (text-align, centering) positions against
    // the real span: a colspan cell is much wider than its specified width.
    if (specifiedWidth >= 0.0f && styleVal(node, Prop::Display) == "table-cell")
        specifiedWidth = -1.0f;
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
        const std::string& boxSizing = styleVal(node, Prop::BoxSizing);
        if (boxSizing == "border-box") {
            contentWidth = specifiedWidth - paddingH - borderH;
            if (contentWidth < 0.0f) contentWidth = 0.0f;
        } else {
            contentWidth = specifiedWidth;
        }
    } else {
        // width: auto — for replaced elements (e.g. <img>) with an intrinsic
        // size, use that. Inline-blocks and floats shrink-to-fit (CSS2
        // §10.3.5). Otherwise fill available space.
        float intrW = 0, intrH = 0;
        const std::string& selfDisp = styleVal(node, Prop::Display);
        const std::string& selfFloat = styleVal(node, Prop::Float);
        if (node->intrinsicSize(intrW, intrH, availableWidth - paddingH - borderH)) {
            contentWidth = intrW;
        } else if (selfDisp == "inline-block" ||
                   selfFloat == "left" || selfFloat == "right") {
            float minC = computeMinContentWidth(node, metrics);
            float maxC = computeMaxContentWidth(node, metrics);
            float avail = availableWidth - marginH - paddingH - borderH;
            if (avail < 0.0f) avail = 0.0f;
            contentWidth = std::min(maxC, std::max(minC, avail));
        } else {
            contentWidth = availableWidth - marginH - paddingH - borderH;
            if (contentWidth < 0.0f) contentWidth = 0.0f;
        }
    }

    // Apply min/max-width constraints
    float minW = resolveDimension(styleVal(node, Prop::MinWidth), availableWidth, fontSize);
    float maxW = resolveDimension(styleVal(node, Prop::MaxWidth), availableWidth, fontSize);
    if (minW >= 0.0f && contentWidth < minW) contentWidth = minW;
    if (maxW >= 0.0f && contentWidth > maxW) contentWidth = maxW;

    // Flex-item override: the flex algorithm already resolved (and min/max-
    // clamped) the used main size; honor it over the style width so children
    // are laid out against the flexed content width.
    if (node->overrideContentWidth >= 0.0f)
        contentWidth = node->overrideContentWidth;

    node->box.contentRect.width = contentWidth;

    // Handle margin: auto for horizontal centering
    const std::string& marginLeftVal = styleVal(node, Prop::MarginLeft);
    const std::string& marginRightVal = styleVal(node, Prop::MarginRight);
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
    } else if (node->overrideContentWidth < 0.0f &&
               node->parent() &&
               styleVal(node->parent(), Prop::Direction) == "rtl" &&
               styleVal(node, Prop::Width) != "auto" &&
               styleVal(node, Prop::Float) == "none" &&
               styleVal(node, Prop::Position) != "absolute" &&
               styleVal(node, Prop::Position) != "fixed" &&
               (styleVal(node, Prop::Display) == "block" ||
                styleVal(node, Prop::Display) == "flow-root" ||
                styleVal(node, Prop::Display) == "list-item")) {
        // Over-constrained (CSS 2.1 §10.3.3): an in-flow block whose width and
        // both margins are specified but do not fill its containing block. The
        // end-side margin is over-specified and recomputed to absorb the slack —
        // margin-left when the CONTAINING BLOCK's direction is rtl (an element
        // carrying dir="rtl" but laid out inside an ltr parent still hugs the
        // left, so it's the parent's direction that matters). In ltr the slack
        // goes to margin-right, which leaves the box position unchanged, so only
        // the rtl case needs handling here. Only widen for positive slack; a box
        // wider than its container overflows without shifting the anchored edge.
        float totalUsed = contentWidth + paddingH + borderH +
                          node->box.margin.left + node->box.margin.right;
        float remaining = availableWidth - totalUsed;
        if (remaining > 0)
            node->box.margin.left += remaining;
    }

    // Available width for children
    float childAvailable = contentWidth;

    // Resolve definite height early so children can use percentage heights.
    // For auto-height parents, children get availableHeight = 0 (per CSS spec,
    // percentage heights only resolve against definite containing-block heights).
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;
    float earlyHeight = resolveDimension(styleVal(node, Prop::Height), node->availableHeight, fontSize);
    // aspect-ratio: when height is auto and width is definite, derive a
    // content-box height from the ratio. The ratio applies to the box that
    // box-sizing selects: content-box (default) → ratio of content widths and
    // heights; border-box → ratio of border boxes.
    // aspectRatioCBH < 0 means "no aspect-ratio override".
    float aspectRatio = parseAspectRatio(styleVal(node, Prop::AspectRatio));
    float aspectRatioCBH = -1.0f;
    if (earlyHeight < 0.0f && aspectRatio > 0.0f && contentWidth >= 0.0f) {
        const std::string& bs = styleVal(node, Prop::BoxSizing);
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
        const std::string& boxSizing = styleVal(node, Prop::BoxSizing);
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
            const std::string& d = styleVal(child, Prop::Display);
            if (d == "none") continue;
            const std::string& cp = styleVal(child, Prop::Position);
            if (cp == "absolute" || cp == "fixed") continue;
            hasVisibleContent = true;
            if (d != "inline" && d != "inline-block" && d != "inline-flex" && d != "inline-grid") {
                allInlineChildren = false;
            }
        }
    }

    if (hasVisibleContent && allInlineChildren) {
        // Inline formatting context: text and inline-level boxes in line
        // boxes (block_inline.cpp).
        layoutBlockInlineContent(node, childAvailable, fontSize, metrics, cursorY);
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
    bool establishesBFC = nodeEstablishesBFC(node);

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
    const std::string& bfcTextAlign = styleVal(node, Prop::TextAlign);
    const std::string& bfcDirection = styleVal(node, Prop::Direction);
    std::string bfcResolvedAlign = bfcTextAlign;
    if (bfcResolvedAlign == "start" || bfcResolvedAlign.empty()) {
        bfcResolvedAlign = (bfcDirection == "rtl") ? "right" : "left";
    } else if (bfcResolvedAlign == "end") {
        bfcResolvedAlign = (bfcDirection == "rtl") ? "left" : "right";
    }

    // The strut for anonymous line boxes in this BFC (same model as the
    // dedicated IFC path — CSS2 §10.8 with Blink's half-leading split).
    StrutMetrics anonStrut = computeStrut(node, fontSize, metrics);

    // Place a floated child with its margin-box top at `atY`, register its
    // exclusion (including any shape-outside: circle geometry), and return.
    auto placeFloat = [&](LayoutNode* child, float atY) {
        auto& childStyle = child->computedStyle();
        const std::string& childFloat = styleVal(child, Prop::Float);
        layoutNode(child, childAvailable, metrics);

        float floatWidth = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
        float floatHeight = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;

        // CSS2 §9.5.1 rule 5: a float's outer top may not be higher than the
        // outer top of any earlier float in the same formatting context.
        for (auto& f : floats) atY = std::max(atY, f.y);

        // Rules 2/3/7: if the float doesn't fit between the exclusions at
        // atY, move it down to the next Y where the set of intersecting
        // floats changes (a float bottom) until it fits — or until no float
        // intersects its band (a float wider than the containing block
        // itself just overflows).
        float bandH = floatHeight > 0 ? floatHeight : 1.0f;
        for (;;) {
            auto [le, re] = getAvailableAtY(atY, floatHeight);
            if (floatWidth <= re - le) break;
            float nextY = std::numeric_limits<float>::max();
            for (auto& f : floats) {
                if (atY + bandH > f.y && atY < f.y + f.height)
                    nextY = std::min(nextY, f.y + f.height);
            }
            if (nextY == std::numeric_limits<float>::max()) break;
            atY = nextY;
        }

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
        const std::string& shapeOutside = styleVal(child, Prop::ShapeOutside);
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
    // Out-of-flow children seen while an inline run was open. Their static
    // position is only known once the run has been placed, so it is assigned
    // at the end of the flush. See LayoutNode::staticPosX.
    std::vector<LayoutNode*> pendingStatic;
    auto flushInlineRun = [&]() {
        if (pendingInline.empty()) {
            for (LayoutNode* oof : pendingStatic) {
                oof->staticPosX = 0.0f;
                oof->staticPosY = cursorY;
                oof->staticPosPass = currentLayoutPass();
            }
            pendingStatic.clear();
            return;
        }

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
            // For text runs: display string + source byte range (mirrors the
            // dedicated IFC path) so wrapped segments become PlacedTextRuns.
            std::string text;
            int srcStart = 0;
            int srcEnd = 0;
            // Soft-wrap opportunities (word-boundary splitter for text,
            // always-breakable for atomic inline elements).
            bool canBreakBefore = false;
            bool canBreakAfter = false;
            // Line-sizing extents about the baseline for strip-boxed inline
            // elements (see layoutInline) — the box is only the font strip,
            // but tall children still grow the line. Negative = use
            // baseline / height - baseline.
            float sizeAscent = -1.0f;
            float sizeDescent = -1.0f;
        };
        std::vector<AnonItem> anonItems;
        float anonSpaceWidth = metrics.measureWidth(" ", styleVal(node, Prop::FontFamily),
            fontSize, styleVal(node, Prop::FontWeight));

        for (auto* inl : pendingInline) {
            if (inl->isTextNode()) {
                float ls2 = resolveLength(styleVal(node, Prop::LetterSpacing), 0, fontSize);
                float ws2 = resolveLength(styleVal(node, Prop::WordSpacing), 0, fontSize);
                // Collapsing white-space wraps here, in the float-aware line
                // builder — request word-granularity runs (the splitter
                // greedily packs to the given width, so a tiny width yields
                // one word per run) and re-insert the collapsed inter-word
                // spaces as separate items below. Non-wrapping modes keep
                // the splitter's own line packing.
                const std::string& wsMode = styleVal(node, Prop::WhiteSpace);
                const std::string& oWrap2 = styleVal(node, Prop::OverflowWrap);
                const std::string& wBreak2 = styleVal(node, Prop::WordBreak);
                bool canBreakWord2 = oWrap2 == "break-word" ||
                    oWrap2 == "anywhere" || wBreak2 == "break-all";
                bool wordMode = (wsMode.empty() || wsMode == "normal") &&
                                !canBreakWord2;
                auto runs = breakTextIntoRuns(std::string(inl->textContent()),
                    wordMode ? 0.5f : childAvailable,
                    styleVal(node, Prop::FontFamily), fontSize, styleVal(node, Prop::FontWeight),
                    wsMode, metrics,
                    oWrap2, wBreak2, ls2, ws2, styleVal(node, Prop::TextTransform));
                inl->box = LayoutBox{};
                // Pure-whitespace text node: collapses to a single space
                // between inline-level siblings (skipped at the run's start,
                // and dropped entirely next to a float — Chromium renders no
                // space on either side of a floated box).
                if (runs.empty()) {
                    bool anyWs = false;
                    for (char c : inl->textContent()) {
                        if (std::isspace(static_cast<unsigned char>(c))) { anyWs = true; break; }
                    }
                    bool anyContent = false;
                    for (auto& prev : anonItems) {
                        if (!prev.isFloat) { anyContent = true; break; }
                    }
                    if (anyWs && anyContent && !anonItems.back().isFloat) {
                        AnonItem it{};
                        it.node = inl; it.width = anonSpaceWidth; it.height = 0;
                        it.isText = true; it.baseline = anonStrut.ascent;
                        it.text = " ";
                        it.canBreakBefore = true; it.canBreakAfter = true;
                        anonItems.push_back(std::move(it));
                    }
                    continue;
                }
                // One item per run so the float-aware line builder can wrap
                // text into the shortened line boxes beside floats.
                size_t emitted = 0;
                int prevSrcEnd = 0;
                for (auto& run : runs) {
                    if (run.text.empty() && run.width == 0) continue;
                    if (wordMode && emitted > 0 && run.canBreakBefore) {
                        // The collapsed whitespace between two words: its own
                        // item so wrap decisions exclude the trailing space
                        // and line-edge trimming can drop it.
                        AnonItem sp{};
                        // Real height, for the same reason as the IFC path
                        // above: selection geometry discards a zero-height run.
                        // Contextual gap width, as in the IFC path above.
                        sp.node = inl;
                        sp.width = run.spaceBefore > 0.0f ? run.spaceBefore
                                                          : anonSpaceWidth;
                        sp.height = run.height;
                        sp.isText = true; sp.baseline = anonStrut.ascent;
                        sp.text = " ";
                        sp.srcStart = prevSrcEnd; sp.srcEnd = run.srcStart;
                        sp.canBreakBefore = true; sp.canBreakAfter = true;
                        anonItems.push_back(std::move(sp));
                    }
                    AnonItem it{};
                    it.node = inl;
                    it.width = run.width;
                    it.height = run.height;
                    it.isText = true;
                    it.baseline = anonStrut.ascent;
                    it.text = run.text;
                    it.srcStart = run.srcStart;
                    it.srcEnd = run.srcEnd;
                    it.canBreakBefore = run.canBreakBefore;
                    it.canBreakAfter = run.canBreakAfter;
                    prevSrcEnd = run.srcEnd;
                    anonItems.push_back(std::move(it));
                    ++emitted;
                }
            } else if (inl->tagName() == "br" || inl->tagName() == "BR") {
                inl->box = LayoutBox{};
                // Record the natural font line-height (not the CSS line-height)
                // as the BR's bounding rect height — matches Chromium's
                // getBoundingClientRect for forced-break inlines.
                inl->box.contentRect.height = fontSize > 0 ? metrics.lineHeight(
                    styleVal(node, Prop::FontFamily), fontSize,
                    styleVal(node, Prop::FontWeight)) : 0.0f;
                AnonItem it{};
                it.node = inl; it.height = anonStrut.lineHeight;
                it.forceBreak = true;
                anonItems.push_back(std::move(it));
            } else {
                const std::string& fd = styleVal(inl, Prop::Float);
                if (fd == "left" || fd == "right") {
                    // A collapsed space immediately before a float renders no
                    // gap in Chromium — drop it.
                    if (!anonItems.empty() && anonItems.back().isText &&
                        anonItems.back().text == " ")
                        anonItems.pop_back();
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
                                                          childAvailable, metrics);
                AnonItem it{};
                it.node = inl; it.width = cw; it.height = ch;
                it.baseline = g.baseline; it.valign = g.valign;
                if (inl->box.inlineExtentAbove >= 0) {
                    // Strip-boxed inline: size the line to its leaded box
                    // unioned with its content extents, not the strip.
                    auto& ics = inl->computedStyle();
                    float cfs = resolveLength(styleVal(inl, Prop::FontSize),
                                              fontSize, fontSize);
                    if (cfs <= 0) cfs = fontSize;
                    const std::string& cff = styleVal(inl, Prop::FontFamily);
                    const std::string& cfw = styleVal(inl, Prop::FontWeight);
                    float cNat = metrics.lineHeight(cff, cfs, cfw);
                    if (cNat <= 0) cNat = cfs * 1.2f;
                    float cAsc = metrics.ascent(cff, cfs, cfw);
                    if (cAsc <= 0 || cAsc >= cNat) cAsc = cNat * 0.8f;
                    float cLH = resolveLineHeight(styleVal(inl, Prop::LineHeight),
                                                  cfs, cff, cfw, metrics);
                    float cLead = cLH - cNat;
                    float cHalf = std::floor(cLead * 0.5f);
                    it.sizeAscent = std::max(cAsc + cHalf,
                                             inl->box.inlineExtentAbove);
                    it.sizeDescent = std::max((cNat - cAsc) + (cLead - cHalf),
                                              inl->box.inlineExtentBelow);
                }
                // Atomic inline elements offer a soft-wrap opportunity on
                // both sides (CSS Text §5.3).
                it.canBreakBefore = true;
                it.canBreakAfter = true;
                anonItems.push_back(std::move(it));
            }
        }

        // Strip leading collapsible whitespace from the first text run —
        // it collapses against the start of the inline run (floats ahead of
        // it don't count as content).
        for (auto& it : anonItems) {
            if (it.isFloat) continue;
            if (it.isText && !it.text.empty() && it.text.front() == ' ') {
                it.text.erase(it.text.begin());
                it.width = std::max(0.0f, it.width - anonSpaceWidth);
            }
            break;
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
                    a = std::max(a, it.sizeAscent >= 0 ? it.sizeAscent
                                                       : it.baseline);
                    b = std::max(b, it.sizeDescent >= 0 ? it.sizeDescent
                                                        : it.height - it.baseline);
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
            float estLineH = resolveLineHeight(styleVal(node, Prop::LineHeight), fontSize,
                styleVal(node, Prop::FontFamily), styleVal(node, Prop::FontWeight), metrics);
            bool anonNoWrap = styleVal(node, Prop::WhiteSpace) == "nowrap";

            float lineY = cursorY;
            auto band = [&](float y) {
                auto [le, re] = getAvailableAtY(y, estLineH);
                return std::pair<float, float>{le, re};
            };
            auto [curLE, curRE] = band(lineY);

            size_t ls = 0;
            float cx = 0;
            auto lineWidth = [&](size_t s, size_t e) {
                float w = 0;
                for (size_t k = s; k < e; ++k)
                    if (!anonItems[k].isFloat && !anonItems[k].forceBreak)
                        w += anonItems[k].width;
                return w;
            };
            auto closeLine = [&](size_t end, bool brk) {
                auto [la, lb] = anonLineExtents(ls, end);
                float lh = la + lb;
                anonLines.push_back({ls, end, lineWidth(ls, end), lh, brk,
                                     curLE, curRE - curLE, la});
                lineY += (lh > 0 ? lh : estLineH);
                ls = end; cx = 0;
                auto b = band(lineY);
                curLE = b.first; curRE = b.second;
            };
            // A soft wrap may only land on a real break opportunity: either
            // a canBreak flag or whitespace on one side of the boundary. A
            // float boundary always allows a break (the float isn't line
            // content).
            auto canBreakBetweenAnon = [&](size_t p, size_t n) {
                const AnonItem& a = anonItems[p];
                const AnonItem& b = anonItems[n];
                if (a.isFloat || b.isFloat) return true;
                bool after = a.canBreakAfter || (!a.text.empty() && std::isspace(
                    static_cast<unsigned char>(a.text.back())));
                bool before = b.canBreakBefore || (!b.text.empty() && std::isspace(
                    static_cast<unsigned char>(b.text.front())));
                return after || before;
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
                    // Retreat to the latest break opportunity on the line so
                    // an atomic unit isn't split across lines.
                    size_t breakIdx = i;
                    while (breakIdx > ls && !canBreakBetweenAnon(breakIdx - 1, breakIdx))
                        --breakIdx;
                    if (breakIdx == ls) breakIdx = i;
                    closeLine(breakIdx, false);
                    for (size_t k = breakIdx; k < i; ++k)
                        if (!anonItems[k].isFloat && !anonItems[k].forceBreak)
                            cx += anonItems[k].width;
                }
                cx += anonItems[i].width;
            }
            if (ls < anonItems.size()) {
                auto [la, lb] = anonLineExtents(ls, anonItems.size());
                anonLines.push_back({ls, anonItems.size(), lineWidth(ls, anonItems.size()),
                                     la + lb, false, curLE, curRE - curLE, la});
            }
        }

        // Collapsible whitespace at a soft-wrap boundary is removed: the
        // first text run on a line drops its leading spaces and the last
        // drops its trailing spaces (mirrors the dedicated IFC path).
        {
            const std::string& wsProp = styleVal(node, Prop::WhiteSpace);
            if (wsProp != "pre" && wsProp != "pre-wrap") {
                for (auto& line : anonLines) {
                    for (size_t k = line.start; k < line.end; ++k) {
                        AnonItem& it = anonItems[k];
                        if (it.isFloat) continue;
                        if (it.isText && !it.forceBreak) {
                            while (!it.text.empty() && it.text.front() == ' ') {
                                it.text.erase(it.text.begin());
                                it.width = std::max(0.0f, it.width - anonSpaceWidth);
                                line.totalWidth = std::max(0.0f, line.totalWidth - anonSpaceWidth);
                            }
                        }
                        break;
                    }
                    for (size_t k = line.end; k > line.start; --k) {
                        AnonItem& it = anonItems[k - 1];
                        if (it.isFloat) continue;
                        if (it.isText && !it.forceBreak) {
                            while (!it.text.empty() && it.text.back() == ' ') {
                                it.text.pop_back();
                                it.width = std::max(0.0f, it.width - anonSpaceWidth);
                                line.totalWidth = std::max(0.0f, line.totalWidth - anonSpaceWidth);
                            }
                        }
                        break;
                    }
                }
            }
        }

        // A line box doesn't collapse margins: if this run produces real
        // line content, the previous block sibling's pending margin-bottom
        // resolves in full before the first line, and a following block's
        // margin-top won't see it. Whitespace-only runs (no rendered line)
        // must leave the pending margin alone so blocks separated by a
        // whitespace text node still collapse normally.
        {
            bool anyLine = false;
            for (auto& line : anonLines) {
                if (line.totalWidth > 0 || line.endsWithBreak) { anyLine = true; break; }
            }
            if (anyLine) {
                if (!firstChild) cursorY += prevMarginBottom;
                prevMarginBottom = 0;
                firstChild = false;
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

            // Bidi visual reordering, exactly as the dedicated IFC does it —
            // an anonymous block wrapping a paragraph of Hebrew is the same
            // problem and deserves the same answer. Floats are excluded: they
            // were placed while the line was being built and are not part of
            // its inline sequence.
            const bool anonRtlBase = (bfcDirection == "rtl");
            std::vector<size_t> anonOrder;
            anonOrder.reserve(line.end - line.start);
            {
                std::vector<BidiItem> bidiItems;
                bidiItems.reserve(line.end - line.start);
                for (size_t i = line.start; i < line.end; i++) {
                    const AnonItem& it = anonItems[i];
                    BidiItem bi;
                    bi.excluded = it.forceBreak || it.isFloat;
                    if (!bi.excluded && it.node) {
                        if (it.isText) bi.text = it.text;
                        else           bi.subtree = it.node;
                        bi.opposesBase =
                            ((styleVal(it.node, Prop::Direction) == "rtl") != anonRtlBase);
                    }
                    bidiItems.push_back(bi);
                }
                for (int idx : visualOrderForLine(bidiItems, anonRtlBase, metrics)) {
                    anonOrder.push_back(line.start + static_cast<size_t>(idx));
                }
            }

            for (size_t oi = 0; oi < anonOrder.size(); oi++) {
                size_t i = anonOrder[oi];
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
                    // Record the placed run (mirrors the dedicated IFC path)
                    // so painting and selection geometry follow the wrapped
                    // segments; the text node's rect is the union.
                    if (!(ai.text.empty() && ai.width == 0)) {
                        PlacedTextRun placed;
                        placed.x = cx;
                        placed.y = lineBaseline - ai.baseline;
                        placed.width = ai.width;
                        placed.height = ai.height;
                        placed.text = ai.text;
                        placed.srcStart = ai.srcStart;
                        placed.srcEnd = ai.srcEnd;
                        auto& trs = ai.node->box.textRuns;
                        if (trs.empty()) {
                            ai.node->box.contentRect.x = placed.x;
                            ai.node->box.contentRect.y = placed.y;
                            ai.node->box.contentRect.width = placed.width;
                            ai.node->box.contentRect.height = placed.height;
                        } else {
                            float left = std::min(ai.node->box.contentRect.x, placed.x);
                            float top = std::min(ai.node->box.contentRect.y, placed.y);
                            float right = std::max(
                                ai.node->box.contentRect.x + ai.node->box.contentRect.width,
                                placed.x + placed.width);
                            float bottom = std::max(
                                ai.node->box.contentRect.y + ai.node->box.contentRect.height,
                                placed.y + placed.height);
                            ai.node->box.contentRect.x = left;
                            ai.node->box.contentRect.y = top;
                            ai.node->box.contentRect.width = right - left;
                            ai.node->box.contentRect.height = bottom - top;
                        }
                        trs.push_back(std::move(placed));
                    }
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
            node->box.lineBoxes.push_back(
                {cursorY, line.maxHeight, line.xStart, line.availWidth});
            cursorY += line.maxHeight;
        }
        pendingInline.clear();
        // Out-of-flow boxes that appeared inside this run take their static
        // position from where the flow stands now — below the lines that
        // preceded them. A block-level hypothetical box would have broken the
        // line anyway; an inline-level one keeps the line's left edge rather
        // than its inline cursor, which is the one simplification here.
        for (LayoutNode* oof : pendingStatic) {
            oof->staticPosX = 0.0f;
            oof->staticPosY = cursorY;
            oof->staticPosPass = currentLayoutPass();
        }
        pendingStatic.clear();
    };

    // Fieldset with a rendered legend (CSS rendering §fieldset): the first
    // in-flow legend child straddles the block-start border — shrink-to-fit,
    // positioned at the fieldset's border-box top — and the fieldset content
    // is pushed down below it (the legend's margin box replaces the block-
    // start border in the flow). Laid out here, then skipped in the loop.
    LayoutNode* fieldsetLegend = nullptr;
    {
        std::string_view ntag = node->tagName();
        if (ntag == "fieldset" || ntag == "FIELDSET") {
            for (auto* c : getLayoutChildren(node)) {
                if (c->isTextNode()) continue;
                if (styleVal(c, Prop::Display) == "none") continue;
                std::string_view ctag = c->tagName();
                if (ctag == "legend" || ctag == "LEGEND") fieldsetLegend = c;
                break; // only the first in-flow child can be the legend
            }
        }
        if (fieldsetLegend) {
            layoutNode(fieldsetLegend, childAvailable, metrics);
            auto& lb = fieldsetLegend->box;
            float padBorderH = lb.padding.left + lb.padding.right +
                               lb.border.left + lb.border.right;
            float legMax = computeMaxContentWidth(fieldsetLegend, metrics);
            float legContent = std::min(legMax,
                std::max(0.0f, childAvailable - padBorderH));
            if (legContent < 0) legContent = 0;
            lb.contentRect.width = legContent;
            // Inset horizontally to the fieldset's content-left; pull the
            // border-box top up to the fieldset border-box top.
            lb.contentRect.x = lb.margin.left + lb.border.left + lb.padding.left;
            lb.contentRect.y = -(node->box.border.top + node->box.padding.top) +
                               lb.margin.top + lb.border.top + lb.padding.top;
            float legMarginH = lb.fullHeight() + lb.margin.top + lb.margin.bottom;
            float push = legMarginH - node->box.border.top;
            if (push > 0.0f) cursorY += push;
        }
    }

    // -x-flow-collapse: collapse (UA-internal) — the child is laid out at
    // its normal flow position but the run of consecutive collapsed children
    // contributes nothing to the flow: the cursor/margin state is snapshotted
    // when the run starts and restored when it ends, so following siblings
    // and the parent's height behave as if the run were empty. This models
    // Chromium's closed-<details> content (a content-visibility:hidden
    // ::details-content wrapper): geometry queries see real laid-out boxes,
    // but they take no space, don't paint and don't hit-test.
    bool inCollapsedRun = false;
    struct CollapsedSave {
        float cursorY = 0, prevMarginBottom = 0, firstBlockChildMarginTop = 0;
        bool firstChild = true, hadFirstBlockChild = false;
        size_t nFloats = 0;
    } collapsedSave;

    for (auto* child : getLayoutChildren(node)) {
        auto& childStyle = child->computedStyle();

        if (child == fieldsetLegend) continue; // already placed above

        if (child->isTextNode()) {
            pendingInline.push_back(child);
            continue;
        }

        const std::string& childDisplay = styleVal(child, Prop::Display);
        if (childDisplay == "none") {
            clearHiddenBox(child);
            continue;
        }

        const std::string& childPos = styleVal(child, Prop::Position);

        // Absolutely and fixed positioned children are out of flow
        // (positioned by the post-layout absolute positioning pass). Record
        // the static position on the way past: with both offsets on an axis
        // `auto`, that pass has nothing else to place the box against.
        if (childPos == "absolute" || childPos == "fixed") {
            if (pendingInline.empty()) {
                child->staticPosX = 0.0f;
                child->staticPosY = cursorY;
                child->staticPosPass = currentLayoutPass();
            } else {
                // Mid-run: the lines above it have not been placed yet.
                pendingStatic.push_back(child);
            }
            continue;
        }

        bool childCollapsed =
            styleVal(child, Prop::XFlowCollapse) == "collapse";
        if (childCollapsed != inCollapsedRun) {
            flushInlineRun();
            if (childCollapsed) {
                collapsedSave = {cursorY, prevMarginBottom,
                                 firstBlockChildMarginTop,
                                 firstChild, hadFirstBlockChild,
                                 floats.size()};
            } else {
                cursorY = collapsedSave.cursorY;
                prevMarginBottom = collapsedSave.prevMarginBottom;
                firstBlockChildMarginTop = collapsedSave.firstBlockChildMarginTop;
                firstChild = collapsedSave.firstChild;
                hadFirstBlockChild = collapsedSave.hadFirstBlockChild;
                if (floats.size() > collapsedSave.nFloats)
                    floats.resize(collapsedSave.nFloats);
            }
            inCollapsedRun = childCollapsed;
        }

        // Collect inline/inline-block children for horizontal layout
        if (childDisplay == "inline" || childDisplay == "inline-block" ||
            childDisplay == "inline-flex" || childDisplay == "inline-grid") {
            pendingInline.push_back(child);
            continue;
        }

        const std::string& childFloat = styleVal(child, Prop::Float);

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

        const std::string& childClear = styleVal(child, Prop::Clear);

        // clear: the bottom edge of the floats the child must move past
        // (CSS2 §9.5.2). Clearance is applied against the child's
        // hypothetical position — after normal margin collapsing — so a
        // cleared box's margin-top is swallowed by the clearance when it is
        // introduced, not stacked below the floats.
        bool childHasClear = (childClear == "left" || childClear == "right" ||
                              childClear == "both");
        float clearY = 0.0f;
        if (childClear == "left" || childClear == "both") {
            for (auto& f : floats) {
                if (f.isLeft) clearY = std::max(clearY, f.y + f.height);
            }
        }
        if (childClear == "right" || childClear == "both") {
            for (auto& f : floats) {
                if (!f.isLeft) clearY = std::max(clearY, f.y + f.height);
            }
        }

        // Handle float: left/right
        if (childFloat == "left" || childFloat == "right") {
            placeFloat(child, childHasClear ? std::max(cursorY, clearY) : cursorY);
            continue; // floats don't advance cursorY
        }

        // In-flow block boxes keep the containing block's full width — only
        // their line boxes shorten beside floats (CSS2 §9.5). A child that
        // establishes a new BFC must not overlap floats, so it narrows to
        // the space between the float edges and shifts beside them.
        bool childBFC = nodeEstablishesBFC(child);
        float childLeftOffset = 0.0f;
        float inFlowAvail = childAvailable;
        if (childBFC) {
            float probeY = childHasClear ? std::max(cursorY, clearY) : cursorY;
            auto [leftEdge, rightEdge] = getAvailableAtY(probeY, 0);
            inFlowAvail = rightEdge - leftEdge;
            if (inFlowAvail < 0) inFlowAvail = 0;
            childLeftOffset = leftEdge;
        }

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
            if (childHasClear && cursorY < clearY) {
                // Clearance on a self-collapsing box: its top edge moves to
                // the floats' bottom and following content flows from there
                // (the clearfix pattern — the parent's height now reaches
                // the floats). Clearance stops the margin pass-through.
                cursorY = clearY;
                child->box.contentRect.x = (childBFC ? childLeftOffset : 0.0f) +
                    child->box.margin.left + child->box.padding.left + child->box.border.left;
                child->box.contentRect.y = cursorY;
                prevMarginBottom = 0;
                continue;
            }
            child->box.contentRect.x = (childBFC ? childLeftOffset : 0.0f) +
                child->box.margin.left + child->box.padding.left + child->box.border.left;
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

        // Clearance (CSS2 §9.5.2): if the hypothetical top — the position
        // after normal margin collapsing — is still above the floats being
        // cleared, move the box down to the floats' bottom. The difference
        // is clearance; the margin is NOT re-applied below the floats.
        if (childHasClear && cursorY < clearY) cursorY = clearY;

        // Position the child's content rect (a BFC child shifts beside the
        // floats it narrowed for; a plain block keeps the full width)
        if (childBFC) {
            auto [le2, re2] = getAvailableAtY(cursorY, child->box.fullHeight());
            childLeftOffset = le2;
        }
        child->box.contentRect.x = childLeftOffset + child->box.margin.left + child->box.padding.left + child->box.border.left;
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
            float childFontSize = resolveLength(styleVal(child, Prop::FontSize), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            const std::string& topVal = styleVal(child, Prop::Top);
            const std::string& leftVal = styleVal(child, Prop::Left);
            const std::string& bottomVal = styleVal(child, Prop::Bottom);
            const std::string& rightVal = styleVal(child, Prop::Right);

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

    // A collapsed run that reaches the end of the child list: roll the flow
    // state back after flushing so the run's boxes keep their positions but
    // contribute nothing to the parent's height.
    if (inCollapsedRun) {
        cursorY = collapsedSave.cursorY;
        prevMarginBottom = collapsedSave.prevMarginBottom;
        firstChild = collapsedSave.firstChild;
        hadFirstBlockChild = collapsedSave.hadFirstBlockChild;
        firstBlockChildMarginTop = collapsedSave.firstBlockChildMarginTop;
        if (floats.size() > collapsedSave.nFloats)
            floats.resize(collapsedSave.nFloats);
        inCollapsedRun = false;
    }

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
            // Shift all children up by the first child's margin that was applied to cursorY.
            // A text child's placed runs sit in this box's content space, not
            // relative to the text node's own rect, so they move explicitly —
            // otherwise text laid out beside the collapsing child (anonymous
            // lines after it) keeps its pre-collapse position while its rect
            // and line box move up.
            for (auto* child : getLayoutChildren(node)) {
                child->box.contentRect.y -= firstBlockChildMarginTop;
                if (child->isTextNode())
                    for (auto& run : child->box.textRuns) run.y -= firstBlockChildMarginTop;
            }
            for (auto& lb : node->box.lineBoxes) lb.top -= firstBlockChildMarginTop;
            for (auto& ef : node->box.escapedFloats) {
                ef.y -= firstBlockChildMarginTop;
                if (ef.shapeR >= 0) ef.shapeCy -= firstBlockChildMarginTop;
            }
            cursorY -= firstBlockChildMarginTop;
        }

        // Bottom margin collapsing: last child's bottom margin escapes through the parent
        // (only for auto height)
        float heightRef0 = node->availableHeight;
        float specH0 = resolveDimension(styleVal(node, Prop::Height), heightRef0, fontSize);
        if (node->box.padding.bottom == 0 && node->box.border.bottom == 0 &&
            specH0 < 0 && prevMarginBottom > 0) {
            float collapsed = std::max(node->box.margin.bottom, prevMarginBottom);
            node->box.margin.bottom = collapsed;
            cursorY -= prevMarginBottom;
        }
    }

    } // end BFC else block

    if (clampPass) cursorY = applyLineClamp(node, lineClamp, cursorY, metrics);
    if (textOverflowPass) applyTextOverflow(node, textOverflowEllipsis, metrics);

    // Resolve height using available height from containing block
    float heightRef = node->availableHeight;
    float specifiedHeight = resolveDimension(styleVal(node, Prop::Height), heightRef, fontSize);
    if (specifiedHeight >= 0.0f) {
        const std::string& boxSizing = styleVal(node, Prop::BoxSizing);
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
    // Pure flow height, without the explicit-height fold — table cells
    // center content against this (see LayoutBox::flowHeight).
    node->box.flowHeight = cursorY;

    // Apply min/max-height constraints. A percentage min/max-height resolves
    // against the containing block's height; when that height is indefinite
    // (heightRef <= 0) CSS treats the percentage as 'none'/'auto' (CSS2 §10.5,
    // §10.7), NOT as 0. Resolving it to 0 would collapse the box — e.g.
    // max-height:100% on a replaced <iframe> (or any element) inside an
    // auto-height flex item clamps its height to 0 and it vanishes.
    auto pctAgainstIndefiniteH = [&](const std::string& v) {
        return heightRef <= 0.0f && !v.empty() && v.back() == '%';
    };
    const std::string& minHVal = styleVal(node, Prop::MinHeight);
    const std::string& maxHVal = styleVal(node, Prop::MaxHeight);
    float minH = pctAgainstIndefiniteH(minHVal) ? -1.0f
                 : resolveDimension(minHVal, heightRef, fontSize);
    float maxH = pctAgainstIndefiniteH(maxHVal) ? -1.0f
                 : resolveDimension(maxHVal, heightRef, fontSize);
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
            const std::string& cp = styleVal(child, Prop::Position);
            if (cp != "absolute" && cp != "fixed") {
                child->availableHeight = childAvailableHeight;
            }
        }
    }

    // Multi-column layout: redistribute children into columns if column-count or column-width is set
    const std::string& colCountStr = styleVal(node, Prop::ColumnCount);
    const std::string& colWidthStr = styleVal(node, Prop::ColumnWidth);

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
        const std::string& colGapStr = styleVal(node, Prop::ColumnGap);
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
            if (styleVal(child, Prop::Display) == "none") continue;
            const std::string& cp = styleVal(child, Prop::Position);
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
            if (styleVal(child, Prop::ColumnSpan) == "all") {
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
                // Lay the segment out as a sequence of fragmentation-atomic
                // items — block children stay whole (htmlayout never splits
                // a box across columns, which also satisfies break-inside:
                // avoid), and runs of atomic inline-level children are
                // packed into line boxes at the column width so inline
                // content fragments by lines like Chromium.
                struct FragItem {
                    std::vector<LayoutNode*> nodes; // 1 for blocks, n per line
                    std::vector<float> nodeX;       // line: margin-box x in column
                    std::vector<float> nodeY;       // line: margin-box y in item
                    float height = 0;               // border-box / line height
                    float marginTop = 0, marginBottom = 0; // blocks only
                    bool isLine = false;
                    bool breakBefore = false, breakAfter = false;
                };
                std::vector<FragItem> frag;
                StrutMetrics colStrut = computeStrut(node, fontSize, metrics);

                auto isInlineLevel = [](LayoutNode* c) {
                    const std::string& d = styleVal(c, Prop::Display);
                    return d == "inline" || d == "inline-block" ||
                           d == "inline-flex" || d == "inline-grid";
                };

                size_t ci = 0;
                while (ci < seg.children.size()) {
                    LayoutNode* child = seg.children[ci];
                    if (isInlineLevel(child)) {
                        size_t cj = ci;
                        while (cj < seg.children.size() &&
                               isInlineLevel(seg.children[cj])) ++cj;
                        struct LItem { LayoutNode* n; float w, h; AtomicInlineGeom g; };
                        std::vector<LItem> li;
                        for (size_t k = ci; k < cj; ++k) {
                            LayoutNode* c = seg.children[k];
                            layoutNode(c, actualColWidth, metrics);
                            float cw = c->box.fullWidth() + c->box.margin.left + c->box.margin.right;
                            float ch = c->box.fullHeight() + c->box.margin.top + c->box.margin.bottom;
                            li.push_back({c, cw, ch,
                                atomicInlineGeometry(c, ch, colStrut.below, actualColWidth, metrics)});
                        }
                        size_t ls = 0;
                        while (ls < li.size()) {
                            float cx = 0;
                            size_t le = ls;
                            while (le < li.size()) {
                                if (le > ls && cx + li[le].w > actualColWidth) break;
                                cx += li[le].w;
                                ++le;
                            }
                            // Line vertical geometry: union of the strut and
                            // the items' baseline-aligned extents (same model
                            // as the IFC path).
                            float a = colStrut.above, b = colStrut.below;
                            for (size_t k = ls; k < le; ++k) {
                                if (li[k].g.valign == 0) {
                                    a = std::max(a, li[k].g.baseline);
                                    b = std::max(b, li[k].h - li[k].g.baseline);
                                } else if (li[k].g.valign == 2) {
                                    float ia = li[k].h * 0.5f + colStrut.xHeight * 0.5f;
                                    a = std::max(a, ia);
                                    b = std::max(b, li[k].h - ia);
                                }
                            }
                            for (size_t k = ls; k < le; ++k) {
                                if ((li[k].g.valign == 1 || li[k].g.valign == 3) &&
                                    li[k].h > a + b) b = li[k].h - a;
                            }
                            FragItem fi;
                            fi.isLine = true;
                            fi.height = a + b;
                            float x = 0;
                            for (size_t k = ls; k < le; ++k) {
                                float yTop;
                                switch (li[k].g.valign) {
                                    case 1: yTop = 0; break;
                                    case 2: yTop = a - (li[k].h * 0.5f +
                                        colStrut.xHeight * 0.5f); break;
                                    case 3: yTop = (a + b) - li[k].h; break;
                                    default: yTop = a - li[k].g.baseline; break;
                                }
                                fi.nodes.push_back(li[k].n);
                                fi.nodeX.push_back(x);
                                fi.nodeY.push_back(yTop);
                                x += li[k].w;
                            }
                            frag.push_back(std::move(fi));
                            ls = le;
                        }
                        ci = cj;
                    } else {
                        layoutNode(child, actualColWidth, metrics);
                        auto& cs = child->computedStyle();
                        FragItem fi;
                        fi.nodes.push_back(child);
                        fi.height = child->box.fullHeight();
                        fi.marginTop = child->box.margin.top;
                        fi.marginBottom = child->box.margin.bottom;
                        const std::string& bb = styleVal(child, Prop::BreakBefore);
                        const std::string& ba = styleVal(child, Prop::BreakAfter);
                        fi.breakBefore = (bb == "column" || bb == "always");
                        fi.breakAfter = (ba == "column" || ba == "always");
                        frag.push_back(std::move(fi));
                        ++ci;
                    }
                }

                // Greedy fragmentation at column height H: items stack with
                // collapsed sibling margins; margins are truncated at a
                // column top (CSS Fragmentation §adjoining margins). Returns
                // {columns used, tallest column content height}.
                std::vector<int> itemCol(frag.size(), 0);
                std::vector<float> itemY(frag.size(), 0.0f);
                auto fill = [&](float H, bool commit) {
                    int col = 0;
                    float y = 0, maxBottom = 0, prevMB = 0;
                    bool colEmpty = true;
                    // Margins truncate only at UNFORCED breaks (css-break
                    // §5.2). The first column's top and forced break-before/
                    // after columns keep the leading margin — the multicol
                    // container is a BFC, so it doesn't collapse away.
                    bool keepTopMargin = true;
                    for (size_t k = 0; k < frag.size(); ++k) {
                        auto& it = frag[k];
                        if (it.breakBefore && !colEmpty) {
                            ++col; y = 0; colEmpty = true; prevMB = 0;
                            keepTopMargin = true;
                        }
                        float spacing = colEmpty ?
                            (keepTopMargin ? it.marginTop : 0.0f) :
                            std::max({prevMB, it.marginTop, 0.0f}) +
                            std::min({prevMB, it.marginTop, 0.0f});
                        float top = y + spacing;
                        if (!colEmpty && top + it.height > H + 0.001f) {
                            ++col; top = 0; colEmpty = true;
                        }
                        if (commit) { itemCol[k] = col; itemY[k] = top; }
                        y = top + it.height;
                        maxBottom = std::max(maxBottom, y);
                        colEmpty = false;
                        prevMB = it.marginBottom;
                        if (it.breakAfter) {
                            ++col; y = 0; colEmpty = true; prevMB = 0;
                            keepTopMargin = true;
                        }
                    }
                    int used = frag.empty() ? 1 : (colEmpty ? col : col + 1);
                    return std::pair<int, float>{used, maxBottom};
                };

                const float unbounded = std::numeric_limits<float>::max() * 0.25f;
                const bool fillAuto = (styleVal(node, Prop::ColumnFill) == "auto");
                float H;
                if (specifiedHeight >= 0.0f && fillAuto) {
                    // column-fill: auto with a definite height: columns fill to
                    // it sequentially and overflow into extra columns when the
                    // content doesn't fit.
                    H = std::max(1.0f, node->box.contentRect.height);
                } else if (columnCount <= 1) {
                    H = fill(unbounded, false).second;
                } else {
                    // column-fill: balance (the default) — find the minimal
                    // column height that still fits within columnCount
                    // columns. Feasibility is monotonic in H, so bisect,
                    // then tighten H to the tallest column actually used.
                    // The floor is the tallest unbreakable item: a smaller H
                    // can look "feasible" by spreading one oversized item
                    // per column, but the container gets no shorter and the
                    // distribution stops matching Chromium's.
                    float total = fill(unbounded, false).second;
                    float tallest = 0.0f;
                    for (auto& it : frag) tallest = std::max(tallest, it.height);
                    float lo = tallest, hi = std::max({total, tallest, 1.0f});
                    for (int iter = 0; iter < 60 && hi - lo > 0.0005f; ++iter) {
                        float mid = (lo + hi) * 0.5f;
                        if (fill(mid, false).first <= columnCount) hi = mid;
                        else lo = mid;
                    }
                    H = hi;
                    // A definite height caps the balanced column height:
                    // balancing may want a shorter column (honored), but never a
                    // taller one — overflow spills into extra columns instead.
                    if (specifiedHeight >= 0.0f)
                        H = std::min(H, std::max(1.0f, node->box.contentRect.height));
                }
                float maxBottom = fill(H, true).second;

                for (size_t k = 0; k < frag.size(); ++k) {
                    auto& it = frag[k];
                    float colX = itemCol[k] * (actualColWidth + columnGap);
                    if (it.isLine) {
                        for (size_t m = 0; m < it.nodes.size(); ++m) {
                            LayoutNode* c = it.nodes[m];
                            c->box.contentRect.x = colX + it.nodeX[m] +
                                c->box.margin.left + c->box.padding.left +
                                c->box.border.left;
                            c->box.contentRect.y = totalY + itemY[k] + it.nodeY[m] +
                                c->box.margin.top + c->box.padding.top +
                                c->box.border.top;
                        }
                    } else {
                        LayoutNode* c = it.nodes[0];
                        c->box.contentRect.x = colX + c->box.margin.left +
                            c->box.padding.left + c->box.border.left;
                        c->box.contentRect.y = totalY + itemY[k] +
                            c->box.padding.top + c->box.border.top;
                        if (c->box.contentRect.width > actualColWidth) {
                            c->box.contentRect.width = actualColWidth;
                        }
                    }
                }
                totalY += maxBottom;
            }
        }
        if (specifiedHeight < 0.0f) node->box.contentRect.height = totalY;
    }
}

} // namespace htmlayout::layout
