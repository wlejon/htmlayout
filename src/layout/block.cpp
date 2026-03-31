#include "layout/block.h"
#include "layout/formatting_context.h"
#include <algorithm>
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
    if (specifiedWidth >= 0.0f) {
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

    // Available width for children
    float childAvailable = contentWidth;

    // Layout children vertically (block formatting context)
    // Absolutely/fixed positioned children are laid out but don't affect flow.
    float cursorY = 0.0f;
    float prevMarginBottom = 0.0f;
    bool firstChild = true;

    // Collect absolutely positioned children to process after in-flow layout
    std::vector<LayoutNode*> absChildren;

    for (auto* child : node->children()) {
        if (child->isTextNode()) continue; // text nodes handled by inline layout

        auto& childStyle = child->computedStyle();
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

        // Recursively layout the child
        layoutNode(child, childAvailable, metrics);

        float childMarginTop = child->box.margin.top;
        float childMarginBottom = child->box.margin.bottom;

        // Margin collapsing: adjacent vertical margins collapse to the larger value
        float collapsedMargin;
        if (firstChild) {
            collapsedMargin = childMarginTop;
            firstChild = false;
        } else {
            collapsedMargin = std::max(prevMarginBottom, childMarginTop);
        }

        cursorY += collapsedMargin;

        // Position the child's content rect
        child->box.contentRect.x = child->box.margin.left + child->box.padding.left + child->box.border.left;
        child->box.contentRect.y = cursorY + child->box.padding.top + child->box.border.top;

        // Apply position: relative offset after normal positioning
        if (childPos == "relative") {
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

    // Add the last child's bottom margin
    if (!firstChild) {
        cursorY += prevMarginBottom;
    }

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

        // Layout the child to determine its intrinsic size
        layoutNode(child, cbWidth, metrics);

        // Resolve offsets: top/right/bottom/left
        float top = resolveDimension(styleVal(childStyle, "top"), cbHeight, childFontSize);
        float right = resolveDimension(styleVal(childStyle, "right"), cbWidth, childFontSize);
        float bottom = resolveDimension(styleVal(childStyle, "bottom"), cbHeight, childFontSize);
        float left = resolveDimension(styleVal(childStyle, "left"), cbWidth, childFontSize);

        // Resolve width for absolute: if left and right are both set and width is auto
        float specW = resolveDimension(styleVal(childStyle, "width"), cbWidth, childFontSize);
        if (specW < 0 && left >= 0 && right >= 0) {
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
}

} // namespace htmlayout::layout
