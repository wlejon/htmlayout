#include "layout/inline.h"
#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/text.h"
#include <algorithm>
#include <cmath>

namespace htmlayout::layout {

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
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

// Apply text-align to position items within a line
float alignLine(const LineBox& line, float availableWidth, const std::string& textAlign) {
    float extraSpace = availableWidth - line.totalWidth;
    if (extraSpace <= 0) return 0;

    if (textAlign == "center") return extraSpace / 2.0f;
    if (textAlign == "right" || textAlign == "end") return extraSpace;
    return 0; // left/start is default
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

        if (widthVal != "auto" && !widthVal.empty()) {
            const std::string& boxSizing = styleVal(style, "box-sizing");
            if (boxSizing == "border-box") {
                node->box.contentRect.width = specW - paddingH - borderH;
                if (node->box.contentRect.width < 0) node->box.contentRect.width = 0;
            } else {
                node->box.contentRect.width = specW;
            }
            contentAvail = node->box.contentRect.width;
        }

        // Layout children as block inside inline-block
        float cursorY = 0;
        for (auto* child : node->children()) {
            if (child->isTextNode()) continue;
            layoutNode(child, contentAvail, metrics);
            child->box.contentRect.x = child->box.margin.left + child->box.padding.left + child->box.border.left;
            child->box.contentRect.y = cursorY + child->box.padding.top + child->box.border.top;
            cursorY += child->box.margin.top + child->box.border.top + child->box.padding.top +
                       child->box.contentRect.height +
                       child->box.padding.bottom + child->box.border.bottom + child->box.margin.bottom;
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
            // For now, use contentAvail
            node->box.contentRect.width = contentAvail;
        }

        return;
    }

    // Pure inline element: collect all children into line items
    float contentAvail = availableWidth - paddingH - borderH;
    std::vector<LineItem> allItems;

    for (auto* child : node->children()) {
        if (child->isTextNode()) {
            // Break text into runs
            auto runs = breakTextIntoRuns(
                child->textContent(), contentAvail,
                fontFamily, fontSize, fontWeight, whiteSpace, metrics);

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

    // Position items within line boxes
    float cursorY = 0;
    for (auto& line : lineBoxes) {
        float xOffset = alignLine(line, contentAvail, textAlign);
        float cursorX = xOffset;

        for (auto& item : line.items) {
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
            cursorX += item.width;
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
}

} // namespace htmlayout::layout
