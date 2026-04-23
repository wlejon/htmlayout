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
};

struct LineBox {
    std::vector<LineItem> items;
    float totalWidth = 0;
    float maxHeight = 0;
    float maxBaseline = 0;
};

// Distribute items into line boxes that fit within availableWidth
std::vector<LineBox> buildLineBoxes(
    const std::vector<LineItem>& items,
    float availableWidth)
{
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
        // Check if item fits on current line
        if (!currentLine.items.empty() &&
            currentLine.totalWidth + item.width > availableWidth) {
            // Wrap: finalize current line, start new one
            lines.push_back(std::move(currentLine));
            currentLine = LineBox{};
        }

        currentLine.items.push_back(item);
        currentLine.totalWidth += item.width;
        currentLine.maxHeight = std::max(currentLine.maxHeight, item.height);
        currentLine.maxBaseline = std::max(currentLine.maxBaseline, item.baseline);
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
        float specH = resolveLength(styleVal(style, "height"), 0, fontSize);
        const std::string& widthVal = styleVal(style, "width");
        const std::string& heightVal = styleVal(style, "height");

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
                const std::string& t = child->textContent();
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
            float ibLineHeight = resolveLineHeight(lineHeightVal, fontSize);
            float cursorX = 0, lineMaxH = 0;
            for (auto* child : getLayoutChildren(node)) {
                if (child->isTextNode()) {
                    float ls = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
                    float ws = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
                    auto runs = breakTextIntoRuns(child->textContent(), contentAvail,
                        fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                        "normal", "normal", ls, ws);
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
                        PlacedTextRun placed;
                        placed.x = cursorX;
                        placed.y = cursorY;
                        placed.width = run.width;
                        placed.height = h;
                        placed.text = run.text;
                        placed.srcStart = run.srcStart;
                        placed.srcEnd   = run.srcEnd;
                        // Record first run's position on contentRect so the
                        // draw traversal knows the text node's origin.
                        if (firstRun) {
                            child->box.contentRect.x = cursorX;
                            child->box.contentRect.y = cursorY;
                            child->box.contentRect.width = run.width;
                            child->box.contentRect.height = h;
                            firstRun = false;
                        } else {
                            // Extend to union of placed runs.
                            float right = std::max(
                                child->box.contentRect.x + child->box.contentRect.width,
                                cursorX + run.width);
                            float bottom = std::max(
                                child->box.contentRect.y + child->box.contentRect.height,
                                cursorY + h);
                            child->box.contentRect.width  = right  - child->box.contentRect.x;
                            child->box.contentRect.height = bottom - child->box.contentRect.y;
                        }
                        child->box.textRuns.push_back(std::move(placed));
                        cursorX += run.width;
                        lineMaxH = std::max(lineMaxH, h);
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

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Break text into runs
            const std::string& owrap = styleVal(style, "overflow-wrap");
            const std::string& wbreak = styleVal(style, "word-break");
            float ls = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
            float ws = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
            auto runs = breakTextIntoRuns(
                child->textContent(), contentAvail,
                fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                owrap, wbreak, ls, ws);

            // Fresh layout pass: clear any previously placed runs so we don't
            // accumulate stale geometry from the prior layout.
            child->box.textRuns.clear();
            for (auto& run : runs) {
                LineItem item;
                item.text = run.text;
                item.width = run.width;
                item.height = run.height;
                item.baseline = run.height * 0.8f; // approximate baseline at 80% of height
                item.node = child;
                item.srcStart = run.srcStart;
                item.srcEnd   = run.srcEnd;
                allItems.push_back(std::move(item));
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
                item.baseline = item.height * 0.8f;
                item.node = child;
                item.isInlineBlock = true;
                allItems.push_back(std::move(item));
            } else if (childDisplay == "inline") {
                // Recursive inline: layout the child inline
                layoutInline(child, contentAvail, metrics);
                LineItem item;
                item.width = child->box.fullWidth();
                item.height = child->box.fullHeight();
                item.baseline = item.height * 0.8f;
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

    // Build line boxes
    auto lineBoxes = buildLineBoxes(allItems, contentAvail);

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
            } else if (item.node && item.node->isTextNode()) {
                // Position text run so the draw traversal knows where to draw
                // it and record it in the text node's placed-runs list for
                // selection/caret geometry queries.
                float yPos = cursorY + (line.maxBaseline - item.baseline);
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
    node->box.contentRect.height = cursorY;

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
