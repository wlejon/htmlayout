#include "layout/block.h"
#include "layout/formatting_context.h"
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

float resolveDimension(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f; // sentinel: auto
    if (value == "min-content") return SIZING_MIN_CONTENT;
    if (value == "max-content") return SIZING_MAX_CONTENT;
    if (value == "fit-content") return SIZING_FIT_CONTENT;
    return resolveLength(value, available, fontSize);
}

} // anonymous namespace

void layoutBlock(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    const std::string& position = styleVal(style, "position");

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
        // width: auto — fill available space
        contentWidth = availableWidth - marginH - paddingH - borderH;
        if (contentWidth < 0.0f) contentWidth = 0.0f;
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

    // Shared state for both BFC and IFC paths
    std::vector<LayoutNode*> absChildren;
    float cursorY = 0.0f;

    // Determine if this block contains only inline-level content
    // (text nodes, inline, inline-block — no block children)
    bool allInlineChildren = true;
    bool hasVisibleContent = false;
    for (auto* child : node->children()) {
        if (child->isTextNode()) {
            const std::string& text = child->textContent();
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
            if (d != "inline" && d != "inline-block") {
                allInlineChildren = false;
            }
        }
    }

    if (hasVisibleContent && allInlineChildren) {
        // Inline formatting context: lay out text and inline elements in line boxes
        const std::string& fontFamily = styleVal(style, "font-family");
        const std::string& fontWeight = styleVal(style, "font-weight");
        const std::string& whiteSpace = styleVal(style, "white-space");
        float lineHeight = resolveLineHeight(styleVal(style, "line-height"), fontSize);

        struct IFCItem {
            float width = 0, height = 0;
            LayoutNode* node = nullptr;
            bool isElement = false;
        };
        std::vector<IFCItem> items;

        for (auto* child : node->children()) {
            if (child->isTextNode()) {
                auto runs = breakTextIntoRuns(child->textContent(), childAvailable,
                    fontFamily, fontSize, fontWeight, whiteSpace, metrics);
                for (auto& run : runs) {
                    if (run.text.empty() && run.width == 0) continue;
                    items.push_back({run.width, std::max(run.height, lineHeight), child, false});
                }
            } else {
                auto& cs = child->computedStyle();
                const std::string& d = styleVal(cs, "display");
                if (d == "none") { child->box = LayoutBox{}; continue; }
                const std::string& cp = styleVal(cs, "position");
                if (cp == "absolute" || cp == "fixed") { absChildren.push_back(child); continue; }
                layoutNode(child, childAvailable, metrics);
                items.push_back({
                    child->box.fullWidth() + child->box.margin.left + child->box.margin.right,
                    child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom,
                    child, true});
            }
        }

        // Position items in line boxes
        float cursorX = 0, lineMaxH = 0;
        for (auto& item : items) {
            if (cursorX > 0 && cursorX + item.width > childAvailable) {
                cursorY += lineMaxH;
                cursorX = 0;
                lineMaxH = 0;
            }
            if (item.node) {
                if (item.isElement) {
                    item.node->box.contentRect.x = cursorX + item.node->box.margin.left +
                        item.node->box.padding.left + item.node->box.border.left;
                    item.node->box.contentRect.y = cursorY + item.node->box.margin.top +
                        item.node->box.padding.top + item.node->box.border.top;
                } else {
                    // Store first text run position for drawing
                    // (subsequent runs from same text node keep the first position)
                    if (item.node->box.contentRect.width == 0) {
                        item.node->box.contentRect.x = cursorX;
                        item.node->box.contentRect.y = cursorY;
                        item.node->box.contentRect.width = item.width;
                        item.node->box.contentRect.height = item.height;
                    }
                }
            }
            cursorX += item.width;
            lineMaxH = std::max(lineMaxH, item.height);
        }
        if (cursorX > 0) cursorY += lineMaxH;
    } else {
    // Block formatting context: layout children vertically
    float prevMarginBottom = 0.0f;
    bool firstChild = true;

    // Float tracking: left and right float edges
    struct FloatRect {
        float x, y, width, height;
        bool isLeft;
    };
    std::vector<FloatRect> floats;

    // Get available width at a given Y position accounting for floats
    auto getAvailableAtY = [&](float y, float h) -> std::pair<float, float> {
        float leftEdge = 0;
        float rightEdge = childAvailable;
        float effectiveH = h > 0 ? h : 1.0f; // treat zero-height as point query
        for (auto& f : floats) {
            if (y + effectiveH > f.y && y < f.y + f.height) {
                if (f.isLeft) {
                    leftEdge = std::max(leftEdge, f.x + f.width);
                } else {
                    rightEdge = std::min(rightEdge, f.x);
                }
            }
        }
        return {leftEdge, rightEdge};
    };

    // Helper: flush accumulated inline children as an anonymous line box
    std::vector<LayoutNode*> pendingInline;
    auto flushInlineRun = [&]() {
        if (pendingInline.empty()) return;

        // Lay out each inline/inline-block child, then position horizontally
        float inlineX = 0, lineMaxH = 0;
        for (auto* inl : pendingInline) {
            if (inl->isTextNode()) {
                // Measure text run
                float tw = 0, th = 0;
                auto runs = breakTextIntoRuns(inl->textContent(), childAvailable,
                    styleVal(style, "font-family"), fontSize, styleVal(style, "font-weight"),
                    styleVal(style, "white-space"), metrics);
                for (auto& run : runs) {
                    tw += run.width;
                    th = std::max(th, run.height);
                }
                float lineHeight = resolveLineHeight(styleVal(style, "line-height"), fontSize);
                th = std::max(th, lineHeight);
                inl->box.contentRect.x = inlineX;
                inl->box.contentRect.y = cursorY;
                inl->box.contentRect.width = tw;
                inl->box.contentRect.height = th;
                inlineX += tw;
                lineMaxH = std::max(lineMaxH, th);
            } else {
                layoutNode(inl, childAvailable, metrics);
                float cw = inl->box.fullWidth() + inl->box.margin.left + inl->box.margin.right;
                float ch = inl->box.fullHeight() + inl->box.margin.top + inl->box.margin.bottom;
                if (inlineX > 0 && inlineX + cw > childAvailable) {
                    cursorY += lineMaxH;
                    inlineX = 0;
                    lineMaxH = 0;
                }
                inl->box.contentRect.x = inlineX + inl->box.margin.left +
                    inl->box.padding.left + inl->box.border.left;
                inl->box.contentRect.y = cursorY + inl->box.margin.top +
                    inl->box.padding.top + inl->box.border.top;
                inlineX += cw;
                lineMaxH = std::max(lineMaxH, ch);
            }
        }
        if (inlineX > 0) cursorY += lineMaxH;
        pendingInline.clear();
    };

    for (auto* child : node->children()) {
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
        if (childPos == "absolute" || childPos == "fixed") {
            absChildren.push_back(child);
            continue;
        }

        // Collect inline/inline-block children for horizontal layout
        if (childDisplay == "inline" || childDisplay == "inline-block") {
            pendingInline.push_back(child);
            continue;
        }

        // Block child encountered — flush any pending inline items first
        flushInlineRun();

        const std::string& childFloat = styleVal(childStyle, "float");
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
            layoutNode(child, childAvailable, metrics);

            float floatWidth = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
            float floatHeight = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;

            auto [leftEdge, rightEdge] = getAvailableAtY(cursorY, floatHeight);

            if (childFloat == "left") {
                child->box.contentRect.x = leftEdge + child->box.margin.left +
                    child->box.padding.left + child->box.border.left;
                child->box.contentRect.y = cursorY + child->box.margin.top +
                    child->box.padding.top + child->box.border.top;
                floats.push_back({leftEdge, cursorY, floatWidth, floatHeight, true});
            } else {
                child->box.contentRect.x = rightEdge - floatWidth + child->box.margin.left +
                    child->box.padding.left + child->box.border.left;
                child->box.contentRect.y = cursorY + child->box.margin.top +
                    child->box.padding.top + child->box.border.top;
                floats.push_back({rightEdge - floatWidth, cursorY, floatWidth, floatHeight, false});
            }
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
                           child->children().empty());

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

        // Margin collapsing: adjacent vertical margins collapse to the larger value
        float collapsedMargin;
        if (firstChild) {
            collapsedMargin = childMarginTop;
            firstChild = false;
        } else {
            collapsedMargin = std::max(prevMarginBottom, childMarginTop);
        }

        cursorY += collapsedMargin;

        // Position the child's content rect (offset by float margins)
        auto [le2, re2] = getAvailableAtY(cursorY, child->box.fullHeight());
        child->box.contentRect.x = le2 + child->box.margin.left + child->box.padding.left + child->box.border.left;
        child->box.contentRect.y = cursorY + child->box.padding.top + child->box.border.top;

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

    // Container must also contain all floats
    for (auto& f : floats) {
        cursorY = std::max(cursorY, f.y + f.height);
    }
    } // end BFC else block

    // Resolve height
    float specifiedHeight = resolveDimension(styleVal(style, "height"), 0.0f, fontSize);
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
    } else {
        // height: auto — shrink to fit content
        node->box.contentRect.height = cursorY;
    }

    // Apply min/max-height constraints
    float minH = resolveDimension(styleVal(style, "min-height"), 0.0f, fontSize);
    float maxH = resolveDimension(styleVal(style, "max-height"), 0.0f, fontSize);
    if (minH >= 0.0f && node->box.contentRect.height < minH) node->box.contentRect.height = minH;
    if (maxH >= 0.0f && node->box.contentRect.height > maxH) node->box.contentRect.height = maxH;

    // Now layout absolutely positioned children against this containing block
    float cbWidth = node->box.contentRect.width;
    float cbHeight = node->box.contentRect.height;

    for (auto* child : absChildren) {
        auto& childStyle = child->computedStyle();
        float childFontSize = resolveLength(styleVal(childStyle, "font-size"), fontSize, fontSize);
        if (childFontSize <= 0) childFontSize = fontSize;

        // Resolve offsets and explicit width to determine layout available width
        float absLeft = resolveDimension(styleVal(childStyle, "left"), cbWidth, childFontSize);
        float absRight = resolveDimension(styleVal(childStyle, "right"), cbWidth, childFontSize);
        float absSpecW = resolveDimension(styleVal(childStyle, "width"), cbWidth, childFontSize);

        // For absolutely positioned elements with width:auto, shrink-wrap to content
        // unless both left and right are set (which stretches to fill).
        bool shrinkWrap = (absSpecW < 0 && !(absLeft >= 0 && absRight >= 0));
        if (shrinkWrap) {
            // Compute max-content width (intrinsic preferred width)
            float maxCW = computeMaxContentWidth(child, metrics);
            // Cap to container width
            if (maxCW > cbWidth) maxCW = cbWidth;
            // Layout at the shrink-wrapped width
            layoutNode(child, maxCW + child->box.padding.left + child->box.padding.right +
                       child->box.border.left + child->box.border.right +
                       child->box.margin.left + child->box.margin.right, metrics);
        } else {
            layoutNode(child, cbWidth, metrics);
        }

        // Resolve offsets: top/right/bottom/left
        float top = resolveDimension(styleVal(childStyle, "top"), cbHeight, childFontSize);
        float right = absRight;
        float bottom = resolveDimension(styleVal(childStyle, "bottom"), cbHeight, childFontSize);
        float left = absLeft;

        // Resolve width for absolute: if left and right are both set and width is auto
        if (absSpecW < 0 && left >= 0 && right >= 0) {
            float w = cbWidth - left - right -
                      child->box.margin.left - child->box.margin.right -
                      child->box.padding.left - child->box.padding.right -
                      child->box.border.left - child->box.border.right;
            if (w > 0) child->box.contentRect.width = w;
        }

        // Resolve height for absolute: if top and bottom are both set and height is auto
        float specH = resolveDimension(styleVal(childStyle, "height"), cbHeight, childFontSize);
        if (specH < 0 && top >= 0 && bottom >= 0) {
            float h = cbHeight - top - bottom -
                      child->box.margin.top - child->box.margin.bottom -
                      child->box.padding.top - child->box.padding.bottom -
                      child->box.border.top - child->box.border.bottom;
            if (h > 0) child->box.contentRect.height = h;
        }

        // Position: prefer top/left, fall back to bottom/right
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
        for (auto* child : node->children()) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            if (styleVal(cs, "display") == "none") continue;
            const std::string& cp = styleVal(cs, "position");
            if (cp == "absolute" || cp == "fixed") continue;
            inFlowChildren.push_back(child);
        }

        // Re-layout each child at column width and place into columns
        int currentCol = 0;
        float colY = 0.0f;
        float maxColHeight = 0.0f;

        for (auto* child : inFlowChildren) {
            // Re-layout child at column width
            layoutNode(child, actualColWidth, metrics);

            float childFullH = child->box.margin.top + child->box.border.top +
                               child->box.padding.top + child->box.contentRect.height +
                               child->box.padding.bottom + child->box.border.bottom +
                               child->box.margin.bottom;

            // Check if child would overflow current column
            if (colY > 0 && colY + childFullH > targetColHeight && currentCol < columnCount - 1) {
                maxColHeight = std::max(maxColHeight, colY);
                currentCol++;
                colY = 0.0f;
            }

            float colX = currentCol * (actualColWidth + columnGap);

            child->box.contentRect.x = colX + child->box.margin.left +
                                       child->box.padding.left + child->box.border.left;
            child->box.contentRect.y = colY + child->box.margin.top +
                                       child->box.padding.top + child->box.border.top;

            // Clamp child width to column width
            if (child->box.contentRect.width > actualColWidth) {
                child->box.contentRect.width = actualColWidth;
            }

            colY += childFullH;
        }
        maxColHeight = std::max(maxColHeight, colY);
        node->box.contentRect.height = maxColHeight;
    }
}

} // namespace htmlayout::layout
