#include "layout/inline.h"
#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/style_util.h"
#include "layout/text.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace htmlayout::layout {

using layout::styleVal;

namespace {

float resolveDim(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

// An item on a line: either a text run or an inline/inline-block element
struct LineItem {
    float width = 0;
    float height = 0;
    float baseline = 0;     // distance from top to baseline
    LayoutNode* node = nullptr;  // non-null for inline-block elements
    std::string text;       // non-empty for text items
    bool isInlineBlock = false;
    bool forceBreak = false;  // true for <br>: forces a new line at this point
    // Source byte range of the run within `node->textContent()` (text items
    // only) — preserved so placed runs can be recorded for caret/selection.
    int  srcStart = 0;
    int  srcEnd   = 0;
    // Break opportunity flags on each side — see TextRun::canBreakBefore.
    bool canBreakBefore = false;
    bool canBreakAfter  = false;
    // Line-sizing extents about the baseline when they differ from the
    // box geometry (nested non-replaced inline elements: the box is only
    // the font strip, but tall children still grow the line). Negative
    // means "use baseline / height - baseline".
    float sizeAscent  = -1.0f;
    float sizeDescent = -1.0f;
};

struct LineBox {
    std::vector<LineItem> items;
    float totalWidth = 0;
    float maxHeight = 0;
    float maxBaseline = 0;
    // Deepest extent below the baseline among the items (height - baseline).
    // After baseline alignment the line box must span
    // maxBaseline + maxDescent, which can exceed the tallest single item.
    float maxDescent = 0;
};

// Distribute items into line boxes that fit within availableWidth.
//
// Width-driven wraps only land on a real break opportunity: whitespace at
// the item boundary (either side) or a canBreak flag set by the word
// splitter. If an overflow falls inside an atomic run (e.g. "Citadel" +
// adjacent "." with no whitespace between them), the builder retreats to
// the most recent break opportunity on the current line rather than
// splitting the unit.
std::vector<LineBox> buildLineBoxes(
    const std::vector<LineItem>& items,
    float availableWidth,
    bool noWrap = false)
{
    auto endsInSpace = [](const LineItem& it) {
        return it.canBreakAfter ||
               (!it.text.empty() && std::isspace(
                    static_cast<unsigned char>(it.text.back())));
    };
    auto startsInSpace = [](const LineItem& it) {
        return it.canBreakBefore ||
               (!it.text.empty() && std::isspace(
                    static_cast<unsigned char>(it.text.front())));
    };

    // Ascent/descent an item contributes to line sizing: the explicit
    // sizeAscent/sizeDescent when set (strip-boxed inline elements),
    // otherwise derived from the box geometry.
    auto itemAscent = [](const LineItem& it) {
        return it.sizeAscent >= 0 ? it.sizeAscent : it.baseline;
    };
    auto itemDescent = [](const LineItem& it) {
        return it.sizeDescent >= 0 ? it.sizeDescent : it.height - it.baseline;
    };

    auto rebuildBounds = [&](LineBox& line) {
        line.totalWidth = 0;
        line.maxHeight = 0;
        line.maxBaseline = 0;
        line.maxDescent = 0;
        for (auto& it : line.items) {
            line.totalWidth += it.width;
            line.maxHeight   = std::max(line.maxHeight, itemAscent(it) + itemDescent(it));
            line.maxBaseline = std::max(line.maxBaseline, itemAscent(it));
            line.maxDescent  = std::max(line.maxDescent, itemDescent(it));
        }
        line.maxHeight = std::max(line.maxHeight, line.maxBaseline + line.maxDescent);
    };

    std::vector<LineBox> lines;
    LineBox currentLine;

    for (auto& item : items) {
        if (item.forceBreak) {
            // <br>: end current line, start a new one. If line is empty, emit an
            // empty line of at least the break's height so the gap is visible.
            if (currentLine.items.empty()) {
                currentLine.maxHeight = std::max(currentLine.maxHeight, item.height);
                currentLine.maxBaseline = std::max(currentLine.maxBaseline, item.baseline);
            }
            lines.push_back(std::move(currentLine));
            currentLine = LineBox{};
            continue;
        }
        if (!noWrap && !currentLine.items.empty() &&
            currentLine.totalWidth + item.width > availableWidth) {
            // Would overflow. Look for the most recent break opportunity in
            // currentLine; if the overflowing item itself can break on its
            // leading edge that's already valid (handled by k == size()).
            auto& cl = currentLine.items;
            size_t k = cl.size();
            while (k > 0) {
                const LineItem& prev = cl[k - 1];
                bool boundaryOk = (k == cl.size())
                    ? (endsInSpace(prev) || startsInSpace(item))
                    : (endsInSpace(prev) || startsInSpace(cl[k]));
                if (boundaryOk) break;
                --k;
            }
            if (k == 0) {
                // No valid break on the line — overflow rather than split
                // an atomic unit.
                lines.push_back(std::move(currentLine));
                currentLine = LineBox{};
            } else {
                // Split: keep [0, k) on the current line, push [k, end) onto
                // a new line along with the incoming item.
                LineBox next;
                next.items.assign(cl.begin() + k, cl.end());
                cl.erase(cl.begin() + k, cl.end());
                rebuildBounds(currentLine);
                lines.push_back(std::move(currentLine));
                currentLine = std::move(next);
                rebuildBounds(currentLine);
            }
        }

        currentLine.items.push_back(item);
        currentLine.totalWidth += item.width;
        currentLine.maxHeight = std::max(currentLine.maxHeight,
                                         itemAscent(item) + itemDescent(item));
        currentLine.maxBaseline = std::max(currentLine.maxBaseline, itemAscent(item));
        currentLine.maxDescent = std::max(currentLine.maxDescent, itemDescent(item));
        currentLine.maxHeight = std::max(currentLine.maxHeight,
                                         currentLine.maxBaseline + currentLine.maxDescent);
    }

    if (!currentLine.items.empty()) {
        lines.push_back(std::move(currentLine));
    }

    return lines;
}

// Apply text-align to position items within a line.
// Returns the starting x offset. For justify, distributes extra space between items.
float alignLine(const LineBox& line, float availableWidth, const std::string& textAlign,
                bool isLastLine = false) {
    float extraSpace = availableWidth - line.totalWidth;
    if (extraSpace <= 0) return 0;

    if (textAlign == "center") return extraSpace / 2.0f;
    if (textAlign == "right" || textAlign == "end") return extraSpace;
    // "justify" is handled at the caller level (adjusts gaps between items)
    return 0; // left/start is default
}

// Calculate per-gap spacing for justify alignment.
// Returns 0 if not justifying or only one item.
float justifyGap(const LineBox& line, float availableWidth, bool isLastLine) {
    if (isLastLine || line.items.size() <= 1) return 0;
    float extraSpace = availableWidth - line.totalWidth;
    if (extraSpace <= 0) return 0;
    return extraSpace / static_cast<float>(line.items.size() - 1);
}

} // anonymous namespace

void layoutInline(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;
    const std::string& fontFamily = styleVal(style, "font-family");
    const std::string& fontWeight = styleVal(style, "font-weight");
    const std::string& whiteSpace = styleVal(style, "white-space");
    const std::string& textAlign = styleVal(style, "text-align");
    const std::string& lineHeightVal = styleVal(style, "line-height");
    const std::string& direction = styleVal(style, "direction");
    bool isRtl = (direction == "rtl");
    const std::string& display = styleVal(style, "display");

    // An inline-block with block-level children is a full block formatting
    // context: floats, clear/clearance, and margin collapsing all apply
    // inside it. Delegate to the block engine (which shrink-to-fits an
    // auto-width inline-block) instead of the simple vertical stacker below.
    if (display == "inline-block") {
        float iw = 0, ih = 0;
        bool intrinsic = node->intrinsicSize(iw, ih, availableWidth);
        bool hasBlockChild = false;
        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            const std::string& d = styleVal(cs, "display");
            if (d == "none") continue;
            const std::string& cp = styleVal(cs, "position");
            if (cp == "absolute" || cp == "fixed") continue;
            if (d != "inline" && d != "inline-block" &&
                d != "inline-flex" && d != "inline-grid")
                hasBlockChild = true;
        }
        if (!intrinsic && hasBlockChild) {
            layoutBlock(node, availableWidth, metrics);
            return;
        }
    }

    // Fresh layout pass: forget any baseline recorded by a previous layout.
    node->box.baselineOffset = -1.0f;
    node->box.inlineExtentAbove = -1.0f;
    node->box.inlineExtentBelow = -1.0f;

    // Resolve margin, padding, border for the node itself
    node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
    node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);

    Edges borderWidth{};
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

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;

    // Check for intrinsic size (replaced elements like <input>)
    float intrW = 0, intrH = 0;
    bool hasIntrinsic = node->intrinsicSize(intrW, intrH, availableWidth - paddingH - borderH);

    // For inline-block or inline replaced elements: resolve explicit width/height
    if (display == "inline-block" || hasIntrinsic) {
        float specW = resolveLength(styleVal(style, "width"), availableWidth, fontSize);
        const std::string& widthVal = styleVal(style, "width");
        const std::string& heightVal = styleVal(style, "height");
        // A percentage height on an inline-block / inline replaced element
        // resolves against the containing block's *definite* height, propagated
        // here as node->availableHeight (the same value the block path uses).
        // When that height is indefinite (<= 0), CSS treats a percentage height
        // as auto (CSS2 §10.5) — fall back to the intrinsic height below rather
        // than collapsing the box to 0. Previously the percentage basis was
        // hard-coded to 0, so e.g. an inline <iframe height:100%> vanished.
        float heightRef = node->availableHeight;
        bool heightPctIndefinite = !heightVal.empty() && heightVal.back() == '%' &&
                                   heightRef <= 0.0f;
        float specH = resolveLength(heightVal, heightRef, fontSize);

        float contentAvail = availableWidth - paddingH - borderH;

        if (widthVal != "auto" && !widthVal.empty()) {
            const std::string& boxSizing = styleVal(style, "box-sizing");
            if (boxSizing == "border-box") {
                node->box.contentRect.width = specW - paddingH - borderH;
                if (node->box.contentRect.width < 0) node->box.contentRect.width = 0;
            } else {
                node->box.contentRect.width = specW;
            }
            contentAvail = node->box.contentRect.width;
        } else if (hasIntrinsic) {
            node->box.contentRect.width = intrW;
            contentAvail = intrW;
        } else if (display == "inline-block") {
            // Shrink-to-fit: content width = min(max-content, available).
            // Without this, block-level children (divs) laid out at full
            // `contentAvail` would expand the inline-block to parent width,
            // breaking horizontal flow of sibling inline-blocks.
            float maxContent = computeMaxContentWidth(node, metrics);
            float fitAvail = std::min(maxContent, contentAvail);
            // Honor min-width so fit-content doesn't shrink below it.
            const std::string& minWVal = styleVal(style, "min-width");
            if (!minWVal.empty() && minWVal != "auto") {
                float minW = resolveLength(minWVal, availableWidth, fontSize);
                if (styleVal(style, "box-sizing") == "border-box") {
                    minW -= paddingH + borderH;
                }
                if (fitAvail < minW) fitAvail = minW;
            }
            if (fitAvail < 0) fitAvail = 0;
            contentAvail = fitAvail;
            node->box.contentRect.width = fitAvail;
        }

        if (heightVal != "auto" && !heightVal.empty() && !heightPctIndefinite) {
            const std::string& boxSizing = styleVal(style, "box-sizing");
            float paddingV = node->box.padding.top + node->box.padding.bottom;
            float borderV = node->box.border.top + node->box.border.bottom;
            if (boxSizing == "border-box") {
                node->box.contentRect.height = specH - paddingV - borderV;
                if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
            } else {
                node->box.contentRect.height = specH;
            }
        } else if (hasIntrinsic) {
            node->box.contentRect.height = intrH;
        }

        // Replaced elements with intrinsic size don't need child layout
        if (hasIntrinsic) {
            return;
        }

        // Layout children inside inline-block
        float cursorY = 0;
        float maxContentW = 0;

        // Check if children are all inline-level
        bool ibAllInline = true;
        bool ibHasContent = false;
        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) {
                std::string_view t = child->textContent();
                for (char c : t) {
                    if (!std::isspace(static_cast<unsigned char>(c))) { ibHasContent = true; break; }
                }
            } else {
                auto& cs = child->computedStyle();
                const std::string& d = styleVal(cs, "display");
                if (d == "none") continue;
                ibHasContent = true;
                if (d != "inline" && d != "inline-block" && d != "inline-flex" && d != "inline-grid") ibAllInline = false;
            }
        }

        if (ibHasContent && ibAllInline) {
            // Inline content in inline-block: measure text and inline children
            float ibLineHeight = resolveLineHeight(lineHeightVal, fontSize,
                fontFamily, fontWeight, metrics);
            // Font ascent for baseline bookkeeping: text runs are hung on a
            // baseline that sits half-leading + ascent below the line top.
            float ibAscent = metrics.ascent(fontFamily, fontSize, fontWeight);
            float cursorX = 0, lineMaxH = 0;
            for (auto* child : getLayoutChildren(node)) {
                if (child->isTextNode()) {
                    float ls = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
                    float ws = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
                    auto runs = breakTextIntoRuns(std::string(child->textContent()), contentAvail,
                        fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                        "normal", "normal", ls, ws, styleVal(style, "text-transform"));
                    bool firstRun = true;
                    child->box.textRuns.clear();
                    for (auto& run : runs) {
                        if (run.text.empty() && run.width == 0) continue;
                        float h = std::max(run.height, ibLineHeight);
                        if (cursorX > 0 && cursorX + run.width > contentAvail) {
                            maxContentW = std::max(maxContentW, cursorX);
                            cursorY += lineMaxH;
                            cursorX = 0;
                            lineMaxH = 0;
                        }
                        // Center the font's natural box in the line via
                        // half-leading; the glyph baseline then sits at
                        // placed.y + ascent, and the box's reported baseline
                        // (for the parent line box to align against) tracks
                        // the LAST placed run — CSS2 §10.8.1 gives an
                        // inline-block the baseline of its last line box.
                        float halfLead = (h - run.height) * 0.5f;
                        if (halfLead < 0) halfLead = 0;
                        PlacedTextRun placed;
                        placed.x = cursorX;
                        placed.y = cursorY + halfLead;
                        placed.width = run.width;
                        placed.height = run.height;
                        placed.text = run.text;
                        placed.srcStart = run.srcStart;
                        placed.srcEnd   = run.srcEnd;
                        float runAscent = (ibAscent > 0 && ibAscent < run.height)
                            ? ibAscent : run.height * 0.8f;
                        node->box.baselineOffset = placed.y + runAscent;
                        // Record first run's position on contentRect so the
                        // draw traversal knows the text node's origin.
                        if (firstRun) {
                            child->box.contentRect.x = placed.x;
                            child->box.contentRect.y = placed.y;
                            child->box.contentRect.width = placed.width;
                            child->box.contentRect.height = placed.height;
                            firstRun = false;
                        } else {
                            // Extend to union of placed runs.
                            float right = std::max(
                                child->box.contentRect.x + child->box.contentRect.width,
                                placed.x + placed.width);
                            float bottom = std::max(
                                child->box.contentRect.y + child->box.contentRect.height,
                                placed.y + placed.height);
                            child->box.contentRect.width  = right  - child->box.contentRect.x;
                            child->box.contentRect.height = bottom - child->box.contentRect.y;
                        }
                        child->box.textRuns.push_back(std::move(placed));
                        cursorX += run.width;
                        lineMaxH = std::max(lineMaxH, h);
                        // Hard line break preserved by white-space: pre /
                        // pre-wrap / pre-line — advance to the next line.
                        if (run.forceBreakAfter) {
                            maxContentW = std::max(maxContentW, cursorX);
                            cursorY += lineMaxH;
                            cursorX = 0;
                            lineMaxH = 0;
                        }
                    }
                } else if ((child->tagName() == "br" || child->tagName() == "BR")) {
                    // Forced line break: end current line, start a new one.
                    float brH = std::max(ibLineHeight,
                        metrics.lineHeight(fontFamily, fontSize, fontWeight));
                    if (cursorX == 0 && lineMaxH == 0) lineMaxH = brH;
                    maxContentW = std::max(maxContentW, cursorX);
                    cursorY += lineMaxH;
                    cursorX = 0;
                    lineMaxH = 0;
                    child->box.contentRect = {};
                } else {
                    auto& cs = child->computedStyle();
                    if (styleVal(cs, "display") == "none") { child->box = LayoutBox{}; continue; }
                    const std::string& childPos = styleVal(cs, "position");
                    if (childPos == "absolute" || childPos == "fixed") continue;
                    layoutNode(child, contentAvail, metrics);
                    float cw = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
                    float ch = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;
                    if (cursorX > 0 && cursorX + cw > contentAvail) {
                        maxContentW = std::max(maxContentW, cursorX);
                        cursorY += lineMaxH;
                        cursorX = 0;
                        lineMaxH = 0;
                    }
                    child->box.contentRect.x = cursorX + child->box.margin.left +
                        child->box.padding.left + child->box.border.left;
                    child->box.contentRect.y = cursorY + child->box.margin.top +
                        child->box.padding.top + child->box.border.top;
                    cursorX += cw;
                    lineMaxH = std::max(lineMaxH, ch);
                }
            }
            if (cursorX > 0) {
                maxContentW = std::max(maxContentW, cursorX);
                cursorY += lineMaxH;
            }

        } else {
            // Block children inside inline-block
            for (auto* child : getLayoutChildren(node)) {
                if (child->isTextNode()) continue;
                auto& cs = child->computedStyle();
                if (styleVal(cs, "display") == "none") { child->box = LayoutBox{}; continue; }
                const std::string& childPos = styleVal(cs, "position");
                if (childPos == "absolute" || childPos == "fixed") continue;
                layoutNode(child, contentAvail, metrics);
                child->box.contentRect.x = child->box.margin.left + child->box.padding.left + child->box.border.left;
                child->box.contentRect.y = cursorY + child->box.padding.top + child->box.border.top;
                float childFullW = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
                maxContentW = std::max(maxContentW, childFullW);
                cursorY += child->box.margin.top + child->box.border.top + child->box.padding.top +
                           child->box.contentRect.height +
                           child->box.padding.bottom + child->box.border.bottom + child->box.margin.bottom;
            }
        }

        if (heightVal != "auto" && !heightVal.empty()) {
            const std::string& boxSizing = styleVal(style, "box-sizing");
            float paddingV = node->box.padding.top + node->box.padding.bottom;
            float borderV = node->box.border.top + node->box.border.bottom;
            if (boxSizing == "border-box") {
                node->box.contentRect.height = specH - paddingV - borderV;
                if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
            } else {
                node->box.contentRect.height = specH;
            }
        } else {
            node->box.contentRect.height = cursorY;
        }

        if (widthVal == "auto" || widthVal.empty()) {
            // Shrink-wrap to content for inline-block with auto width
            node->box.contentRect.width = (maxContentW > 0) ? maxContentW : contentAvail;
        }

        return;
    }

    // Pure inline element: collect all children into line items
    float contentAvail = availableWidth - paddingH - borderH;
    std::vector<LineItem> allItems;

    // Font ascent for this element's own text runs: their baseline within
    // the run box (natural font height) sits `ascent` below the run top.
    float ownAscent = metrics.ascent(fontFamily, fontSize, fontWeight);

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Break text into runs
            const std::string& owrap = styleVal(style, "overflow-wrap");
            const std::string& wbreak = styleVal(style, "word-break");
            float ls = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
            float ws = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
            auto runs = breakTextIntoRuns(
                std::string(child->textContent()), contentAvail,
                fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                owrap, wbreak, ls, ws, styleVal(style, "text-transform"));

            // Fresh layout pass: clear any previously placed runs so we don't
            // accumulate stale geometry from the prior layout.
            child->box.textRuns.clear();
            for (auto& run : runs) {
                LineItem item;
                item.text = run.text;
                item.width = run.width;
                item.height = run.height;
                item.baseline = (ownAscent > 0 && ownAscent < run.height)
                    ? ownAscent : run.height * 0.8f;
                item.node = child;
                item.srcStart = run.srcStart;
                item.srcEnd   = run.srcEnd;
                item.canBreakBefore = run.canBreakBefore;
                item.canBreakAfter  = run.canBreakAfter;
                allItems.push_back(std::move(item));
                // Preserved newline (white-space: pre/pre-wrap/pre-line)
                // becomes a synthetic break-only item after the run, the
                // same shape <br> takes. Avoids placement skipping the
                // text run itself when forceBreak is set on it.
                if (run.forceBreakAfter) {
                    LineItem brk;
                    brk.forceBreak = true;
                    brk.height = run.height;
                    brk.baseline = run.height * 0.8f;
                    brk.node = child;
                    allItems.push_back(std::move(brk));
                }
            }
        } else {
            auto& childStyle = child->computedStyle();
            const std::string& childDisplay = styleVal(childStyle, "display");

            if (childDisplay == "none") {
                child->box = LayoutBox{};
                continue;
            }

            const std::string& childPos = styleVal(childStyle, "position");
            if (childPos == "absolute" || childPos == "fixed") continue;

            if ((child->tagName() == "br" || child->tagName() == "BR")) {
                // Forced line break
                LineItem item;
                item.forceBreak = true;
                item.height = metrics.lineHeight(fontFamily, fontSize, fontWeight);
                item.baseline = item.height * 0.8f;
                item.node = child;
                child->box.contentRect = {};
                allItems.push_back(std::move(item));
                continue;
            }
            if (childDisplay == "inline-block" || childDisplay == "inline-flex" || childDisplay == "inline-grid") {
                // Layout as atomic inline-level box, then add to line items
                layoutNode(child, contentAvail, metrics);
                LineItem item;
                item.width = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
                item.height = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;
                // CSS2 §10.8.1: an inline-block's baseline is its last line
                // box's baseline when it has in-flow content and visible
                // overflow, else its bottom margin edge.
                const std::string& ov = styleVal(childStyle, "overflow");
                bool visibleOv = ov.empty() || ov == "visible";
                if (visibleOv && child->box.baselineOffset >= 0) {
                    item.baseline = std::min(item.height,
                        child->box.margin.top + child->box.border.top +
                        child->box.padding.top + child->box.baselineOffset);
                } else {
                    item.baseline = item.height;
                }
                item.node = child;
                item.isInlineBlock = true;
                allItems.push_back(std::move(item));
            } else if (childDisplay == "inline") {
                // Recursive inline: layout the child inline
                layoutInline(child, contentAvail, metrics);
                LineItem item;
                item.width = child->box.fullWidth();
                item.height = child->box.fullHeight();
                item.baseline = (child->box.baselineOffset >= 0)
                    ? child->box.baselineOffset : item.height * 0.8f;
                if (child->box.inlineExtentAbove >= 0) {
                    // Strip-boxed inline: the box is only the font strip.
                    // Line sizing uses the child's leaded (line-height) box
                    // unioned with its content extents (tall inline-block
                    // grandchildren, wrapped lines) about the baseline.
                    float cfs = resolveLength(styleVal(childStyle, "font-size"),
                                              fontSize, fontSize);
                    if (cfs <= 0) cfs = fontSize;
                    const std::string& cff = styleVal(childStyle, "font-family");
                    const std::string& cfw = styleVal(childStyle, "font-weight");
                    float cNatural = metrics.lineHeight(cff, cfs, cfw);
                    if (cNatural <= 0) cNatural = cfs * 1.2f;
                    float cAscent = metrics.ascent(cff, cfs, cfw);
                    if (cAscent <= 0 || cAscent >= cNatural) cAscent = cNatural * 0.8f;
                    float cLH = resolveLineHeight(styleVal(childStyle, "line-height"),
                                                  cfs, cff, cfw, metrics);
                    float cLead = cLH - cNatural;
                    float cHalf = std::floor(cLead * 0.5f);
                    item.sizeAscent = std::max(cAscent + cHalf,
                                               child->box.inlineExtentAbove);
                    item.sizeDescent = std::max((cNatural - cAscent) + (cLead - cHalf),
                                                child->box.inlineExtentBelow);
                }
                item.node = child;
                allItems.push_back(std::move(item));
            } else {
                // Block-level child inside inline context — treat as block
                layoutBlock(child, contentAvail, metrics);
                LineItem item;
                item.width = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
                item.height = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;
                item.baseline = item.height;
                item.node = child;
                allItems.push_back(std::move(item));
            }
        }
    }

    // Build line boxes. Under white-space: nowrap, cross-item wraps between
    // separate inline children/text runs must not happen either — only an
    // explicit <br> (forceBreak) should start a new line. Without this,
    // multi-child inline content (e.g. text interleaved with <b> spans)
    // would wrap once it exceeds availableWidth even though nowrap says the
    // whole line should overflow instead.
    auto lineBoxes = buildLineBoxes(allItems, contentAvail, whiteSpace == "nowrap");

    // Resolve text-align with direction
    // "start" -> "left" for LTR, "right" for RTL
    // "end" -> "right" for LTR, "left" for RTL
    std::string resolvedAlign = textAlign;
    if (resolvedAlign == "start" || resolvedAlign.empty()) {
        resolvedAlign = isRtl ? "right" : "left";
    } else if (resolvedAlign == "end") {
        resolvedAlign = isRtl ? "left" : "right";
    }

    // Resolve text-indent (applies to first line only)
    float textIndent = resolveLength(styleVal(style, "text-indent"), contentAvail, fontSize);

    // Position items within line boxes
    //
    // While placing, track the content extents that must be reported to the
    // parent line box (box.inlineExtentAbove/Below): element children always
    // contribute (their boxes overflow the font strip this element's own box
    // shrinks to below); the element's own text contributes only on wrapped
    // lines — first-line text is covered by the parent's leaded (line-height)
    // contribution for this element, which with a negative half-leading is
    // deliberately SMALLER than the glyphs.
    float extMinY = 0.0f, extMaxY = 0.0f;
    bool anyExtent = false;
    auto noteExtent = [&](float top, float bottom) {
        if (!anyExtent) { extMinY = top; extMaxY = bottom; anyExtent = true; return; }
        extMinY = std::min(extMinY, top);
        extMaxY = std::max(extMaxY, bottom);
    };
    float cursorY = 0;
    for (size_t lineIdx = 0; lineIdx < lineBoxes.size(); lineIdx++) {
        auto& line = lineBoxes[lineIdx];
        bool isLastLine = (lineIdx == lineBoxes.size() - 1);
        // text-align only applies to block containers, not inline elements.
        // When layoutInline is called recursively for a <span> (display:inline),
        // skip alignment — the parent block handles positioning.
        float xOffset = (display != "inline") ? alignLine(line, contentAvail, resolvedAlign, isLastLine) : 0;
        float gap = (display != "inline" && resolvedAlign == "justify") ? justifyGap(line, contentAvail, isLastLine) : 0;
        float cursorX = xOffset;
        if (lineIdx == 0) cursorX += textIndent;

        for (size_t itemIdx = 0; itemIdx < line.items.size(); itemIdx++) {
            auto& item = line.items[itemIdx];
            if (item.node && (item.isInlineBlock || !item.node->isTextNode())) {
                // Position inline-block/inline element
                const std::string& va = styleVal(item.node->computedStyle(), "vertical-align");
                float yPos = cursorY;

                if (va == "middle") {
                    yPos = cursorY + (line.maxHeight - item.height) / 2.0f;
                } else if (va == "bottom") {
                    yPos = cursorY + line.maxHeight - item.height;
                } else if (va == "top") {
                    yPos = cursorY;
                } else {
                    // baseline (default): align baselines
                    yPos = cursorY + (line.maxBaseline - item.baseline);
                }

                item.node->box.contentRect.x = cursorX + item.node->box.margin.left +
                    item.node->box.padding.left + item.node->box.border.left;
                item.node->box.contentRect.y = yPos + item.node->box.margin.top +
                    item.node->box.padding.top + item.node->box.border.top;
                if (!item.forceBreak) {
                    float extTop = yPos, extBottom = yPos + item.height;
                    if (item.sizeAscent >= 0) {
                        // Strip-boxed nested inline: extent about its baseline.
                        extTop = yPos + item.baseline - item.sizeAscent;
                        extBottom = yPos + item.baseline + item.sizeDescent;
                    }
                    noteExtent(extTop, extBottom);
                }
            } else if (item.node && item.node->isTextNode()) {
                // Position text run so the draw traversal knows where to draw
                // it and record it in the text node's placed-runs list for
                // selection/caret geometry queries.
                float yPos = cursorY + (line.maxBaseline - item.baseline);
                if (lineIdx > 0) noteExtent(yPos, yPos + item.height);
                PlacedTextRun placed;
                placed.x = cursorX;
                placed.y = yPos;
                placed.width = item.width;
                placed.height = item.height;
                placed.text = item.text;
                placed.srcStart = item.srcStart;
                placed.srcEnd   = item.srcEnd;
                auto& runs = item.node->box.textRuns;
                if (runs.empty()) {
                    item.node->box.contentRect.x = cursorX;
                    item.node->box.contentRect.y = yPos;
                    item.node->box.contentRect.width = item.width;
                    item.node->box.contentRect.height = item.height;
                } else {
                    // Extend contentRect to bound every placed run.
                    float left = std::min(item.node->box.contentRect.x, cursorX);
                    float top  = std::min(item.node->box.contentRect.y, yPos);
                    float right = std::max(
                        item.node->box.contentRect.x + item.node->box.contentRect.width,
                        cursorX + item.width);
                    float bottom = std::max(
                        item.node->box.contentRect.y + item.node->box.contentRect.height,
                        yPos + item.height);
                    item.node->box.contentRect.x = left;
                    item.node->box.contentRect.y = top;
                    item.node->box.contentRect.width = right - left;
                    item.node->box.contentRect.height = bottom - top;
                }
                runs.push_back(std::move(placed));
            }
            cursorX += item.width + gap;
        }

        cursorY += line.maxHeight;
    }

    // Set node dimensions
    // Width: for pure inline, shrink-wrap to content
    float maxLineWidth = 0;
    for (auto& line : lineBoxes) {
        maxLineWidth = std::max(maxLineWidth, line.totalWidth);
    }
    node->box.contentRect.width = maxLineWidth;

    if (!lineBoxes.empty()) {
        // A non-replaced inline element's own box is its font strip: from
        // first-line baseline - ascent down to last-line baseline + descent
        // (Chromium's inline fragment geometry). Content taller than the
        // strip — inline-block children, bigger-font descendants — overflows
        // it without growing it; the extents recorded below let parent line
        // boxes still size around that content.
        float naturalH = metrics.lineHeight(fontFamily, fontSize, fontWeight);
        if (naturalH <= 0) naturalH = fontSize * 1.2f;
        float ascent = (ownAscent > 0 && ownAscent < naturalH)
            ? ownAscent : naturalH * 0.8f;
        float firstBase = lineBoxes.front().maxBaseline;
        float lastBase = (cursorY - lineBoxes.back().maxHeight) +
                         lineBoxes.back().maxBaseline;
        float stripTop = firstBase - ascent;
        float stripBottom = lastBase + (naturalH - ascent);

        node->box.inlineExtentAbove = anyExtent
            ? std::max(0.0f, firstBase - extMinY) : 0.0f;
        node->box.inlineExtentBelow = anyExtent
            ? std::max(0.0f, extMaxY - firstBase) : 0.0f;

        if (stripTop != 0.0f) {
            // Move the box top to the strip top; shift in-flow children the
            // opposite way so their absolute positions are unchanged.
            for (auto* child : getLayoutChildren(node)) {
                if (child->isTextNode()) {
                    child->box.contentRect.y -= stripTop;
                    for (auto& run : child->box.textRuns) run.y -= stripTop;
                    continue;
                }
                auto& cs2 = child->computedStyle();
                if (styleVal(cs2, "display") == "none") continue;
                const std::string& cp2 = styleVal(cs2, "position");
                if (cp2 == "absolute" || cp2 == "fixed") continue;
                child->box.contentRect.y -= stripTop;
            }
        }
        node->box.contentRect.height = stripBottom - stripTop;
        // The strip hangs its ascent above the baseline by construction; the
        // parent line box aligns this element by it.
        node->box.baselineOffset = ascent;
    } else {
        node->box.contentRect.height = cursorY;
    }

    // Text-overflow detection: mark if content was truncated
    const std::string& textOverflow = styleVal(style, "text-overflow");
    const std::string& overflow = styleVal(style, "overflow");
    if (textOverflow == "ellipsis" && (overflow == "hidden" || overflow == "scroll" || overflow == "auto")) {
        // Check if any line exceeds the available width
        for (auto& line : lineBoxes) {
            if (line.totalWidth > contentAvail) {
                node->box.textTruncated = true;
                break;
            }
        }
    }
}

} // namespace htmlayout::layout
