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
    float cursorY = 0.0f;
    float prevMarginBottom = 0.0f;
    bool firstChild = true;

    for (auto* child : node->children()) {
        if (child->isTextNode()) continue; // text nodes handled by inline layout

        auto& childStyle = child->computedStyle();
        const std::string& childDisplay = styleVal(childStyle, "display");
        if (childDisplay == "none") {
            child->box = LayoutBox{};
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
}

} // namespace htmlayout::layout
