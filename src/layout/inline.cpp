#include "layout/inline.h"
#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/text.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace htmlayout::layout {

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

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

    // For inline-block: resolve explicit width/height
    if (display == "inline-block") {
        float specW = resolveLength(styleVal(style, "width"), availableWidth, fontSize);
        float specH = resolveLength(styleVal(style, "height"), 0, fontSize);
        const std::string& widthVal = styleVal(style, "width");
        const std::string& heightVal = styleVal(style, "height");

        float contentAvail = availableWidth - paddingH - borderH;

        // Check for intrinsic size (replaced elements like <input>)
        float intrW = 0, intrH = 0;
        bool hasIntrinsic = node->intrinsicSize(intrW, intrH, contentAvail);

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
        }

        if (heightVal != "auto" && !heightVal.empty()) {
            // handled below
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
        std::vector<LayoutNode*> absChildren;

        // Check if children are all inline-level
        bool ibAllInline = true;
        bool ibHasContent = false;
        for (auto* child : node->children()) {
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
                if (d != "inline" && d != "inline-block") ibAllInline = false;
            }
        }

        if (ibHasContent && ibAllInline) {
            // Inline content in inline-block: measure text and inline children
            float ibLineHeight = resolveLineHeight(lineHeightVal, fontSize);
            float cursorX = 0, lineMaxH = 0;
            for (auto* child : node->children()) {
                if (child->isTextNode()) {
                    auto runs = breakTextIntoRuns(child->textContent(), contentAvail,
                        fontFamily, fontSize, fontWeight, whiteSpace, metrics);
                    for (auto& run : runs) {
                        if (run.text.empty() && run.width == 0) continue;
                        float h = std::max(run.height, ibLineHeight);
                        if (cursorX > 0 && cursorX + run.width > contentAvail) {
                            maxContentW = std::max(maxContentW, cursorX);
                            cursorY += lineMaxH;
                            cursorX = 0;
                            lineMaxH = 0;
                        }
                        cursorX += run.width;
                        lineMaxH = std::max(lineMaxH, h);
                    }
                } else {
                    auto& cs = child->computedStyle();
                    if (styleVal(cs, "display") == "none") { child->box = LayoutBox{}; continue; }
                    const std::string& childPos = styleVal(cs, "position");
                    if (childPos == "absolute" || childPos == "fixed") {
                        absChildren.push_back(child);
                        continue;
                    }
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
            for (auto* child : node->children()) {
                if (child->isTextNode()) continue;
                auto& cs = child->computedStyle();
                if (styleVal(cs, "display") == "none") { child->box = LayoutBox{}; continue; }
                const std::string& childPos = styleVal(cs, "position");
                if (childPos == "absolute" || childPos == "fixed") {
                    absChildren.push_back(child);
                    continue;
                }
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

        // Position absolutely/fixed positioned children against this container
        {
            float cbWidth = node->box.contentRect.width;
            float cbHeight = node->box.contentRect.height;

            for (auto* child : absChildren) {
                auto& childStyle = child->computedStyle();
                float childFontSize = resolveLength(styleVal(childStyle, "font-size"), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;

                float left = resolveDim(styleVal(childStyle, "left"), cbWidth, childFontSize);
                float right = resolveDim(styleVal(childStyle, "right"), cbWidth, childFontSize);
                float specAbsW = resolveDim(styleVal(childStyle, "width"), cbWidth, childFontSize);

                bool shrinkWrap = (specAbsW < 0 && !(left >= 0 && right >= 0));
                if (shrinkWrap) {
                    float maxCW = computeMaxContentWidth(child, metrics);
                    if (maxCW > cbWidth) maxCW = cbWidth;
                    layoutNode(child, maxCW + child->box.padding.left + child->box.padding.right +
                               child->box.border.left + child->box.border.right +
                               child->box.margin.left + child->box.margin.right, metrics);
                } else {
                    layoutNode(child, cbWidth, metrics);
                }

                float top = resolveDim(styleVal(childStyle, "top"), cbHeight, childFontSize);
                float bottom = resolveDim(styleVal(childStyle, "bottom"), cbHeight, childFontSize);

                if (specAbsW < 0 && left >= 0 && right >= 0) {
                    float w = cbWidth - left - right -
                              child->box.margin.left - child->box.margin.right -
                              child->box.padding.left - child->box.padding.right -
                              child->box.border.left - child->box.border.right;
                    if (w > 0) child->box.contentRect.width = w;
                }

                float specAbsH = resolveDim(styleVal(childStyle, "height"), cbHeight, childFontSize);
                if (specAbsH < 0 && top >= 0 && bottom >= 0) {
                    float h = cbHeight - top - bottom -
                              child->box.margin.top - child->box.margin.bottom -
                              child->box.padding.top - child->box.padding.bottom -
                              child->box.border.top - child->box.border.bottom;
                    if (h > 0) child->box.contentRect.height = h;
                }

                float xPos = child->box.margin.left + child->box.padding.left + child->box.border.left;
                float yPos = child->box.margin.top + child->box.padding.top + child->box.border.top;

                if (left >= 0) {
                    xPos = left + child->box.margin.left + child->box.padding.left + child->box.border.left;
                } else if (right >= 0) {
                    xPos = cbWidth - right - child->box.margin.right -
                           child->box.padding.right - child->box.border.right - child->box.contentRect.width;
                }

                if (top >= 0) {
                    yPos = top + child->box.margin.top + child->box.padding.top + child->box.border.top;
                } else if (bottom >= 0) {
                    yPos = cbHeight - bottom - child->box.margin.bottom -
                           child->box.padding.bottom - child->box.border.bottom - child->box.contentRect.height;
                }

                child->box.contentRect.x = xPos;
                child->box.contentRect.y = yPos;
            }
        }

        return;
    }

    // Pure inline element: collect all children into line items
    float contentAvail = availableWidth - paddingH - borderH;
    std::vector<LineItem> allItems;
    std::vector<LayoutNode*> inlineAbsChildren;

    for (auto* child : node->children()) {
        if (child->isTextNode()) {
            // Break text into runs
            const std::string& owrap = styleVal(style, "overflow-wrap");
            const std::string& wbreak = styleVal(style, "word-break");
            auto runs = breakTextIntoRuns(
                child->textContent(), contentAvail,
                fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                owrap, wbreak);

            for (auto& run : runs) {
                LineItem item;
                item.text = run.text;
                item.width = run.width;
                item.height = run.height;
                item.baseline = run.height * 0.8f; // approximate baseline at 80% of height
                item.node = child;
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
            if (childPos == "absolute" || childPos == "fixed") {
                inlineAbsChildren.push_back(child);
                continue;
            }

            if (childDisplay == "inline-block") {
                // Layout as inline-block, then add to line items
                layoutInline(child, contentAvail, metrics);
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

    // Position items within line boxes
    float cursorY = 0;
    for (size_t lineIdx = 0; lineIdx < lineBoxes.size(); lineIdx++) {
        auto& line = lineBoxes[lineIdx];
        bool isLastLine = (lineIdx == lineBoxes.size() - 1);
        float xOffset = alignLine(line, contentAvail, resolvedAlign, isLastLine);
        float gap = (resolvedAlign == "justify") ? justifyGap(line, contentAvail, isLastLine) : 0;
        float cursorX = xOffset;

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

    // Position absolutely/fixed positioned children against this inline container
    {
        float cbWidth = node->box.contentRect.width;
        float cbHeight = node->box.contentRect.height;

        for (auto* child : inlineAbsChildren) {
            auto& childStyle = child->computedStyle();
            float childFontSize = resolveLength(styleVal(childStyle, "font-size"), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;

            float left = resolveDim(styleVal(childStyle, "left"), cbWidth, childFontSize);
            float right = resolveDim(styleVal(childStyle, "right"), cbWidth, childFontSize);
            float specAbsW = resolveDim(styleVal(childStyle, "width"), cbWidth, childFontSize);

            bool shrinkWrap = (specAbsW < 0 && !(left >= 0 && right >= 0));
            if (shrinkWrap) {
                float maxCW = computeMaxContentWidth(child, metrics);
                if (maxCW > cbWidth) maxCW = cbWidth;
                layoutNode(child, maxCW + child->box.padding.left + child->box.padding.right +
                           child->box.border.left + child->box.border.right +
                           child->box.margin.left + child->box.margin.right, metrics);
            } else {
                layoutNode(child, cbWidth, metrics);
            }

            float top = resolveDim(styleVal(childStyle, "top"), cbHeight, childFontSize);
            float bottom = resolveDim(styleVal(childStyle, "bottom"), cbHeight, childFontSize);

            if (specAbsW < 0 && left >= 0 && right >= 0) {
                float w = cbWidth - left - right -
                          child->box.margin.left - child->box.margin.right -
                          child->box.padding.left - child->box.padding.right -
                          child->box.border.left - child->box.border.right;
                if (w > 0) child->box.contentRect.width = w;
            }

            float specAbsH = resolveDim(styleVal(childStyle, "height"), cbHeight, childFontSize);
            if (specAbsH < 0 && top >= 0 && bottom >= 0) {
                float h = cbHeight - top - bottom -
                          child->box.margin.top - child->box.margin.bottom -
                          child->box.padding.top - child->box.padding.bottom -
                          child->box.border.top - child->box.border.bottom;
                if (h > 0) child->box.contentRect.height = h;
            }

            float xPos = child->box.margin.left + child->box.padding.left + child->box.border.left;
            float yPos = child->box.margin.top + child->box.padding.top + child->box.border.top;

            if (left >= 0) {
                xPos = left + child->box.margin.left + child->box.padding.left + child->box.border.left;
            } else if (right >= 0) {
                xPos = cbWidth - right - child->box.margin.right -
                       child->box.padding.right - child->box.border.right - child->box.contentRect.width;
            }

            if (top >= 0) {
                yPos = top + child->box.margin.top + child->box.padding.top + child->box.border.top;
            } else if (bottom >= 0) {
                yPos = cbHeight - bottom - child->box.margin.bottom -
                       child->box.padding.bottom - child->box.border.bottom - child->box.contentRect.height;
            }

            child->box.contentRect.x = xPos;
            child->box.contentRect.y = yPos;
        }
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
