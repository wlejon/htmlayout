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

    // Resolve definite height early so children can use percentage heights.
    // For auto-height parents, children get availableHeight = 0 (per CSS spec,
    // percentage heights only resolve against definite containing-block heights).
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;
    float earlyHeight = resolveDimension(styleVal(style, "height"), node->availableHeight, fontSize);
    float earlyChildAvailableHeight = 0.0f;
    if (earlyHeight >= 0.0f) {
        const std::string& boxSizing = styleVal(style, "box-sizing");
        if (boxSizing == "border-box")
            earlyChildAvailableHeight = earlyHeight - paddingV - borderV;
        else
            earlyChildAvailableHeight = earlyHeight;
        if (earlyChildAvailableHeight < 0.0f) earlyChildAvailableHeight = 0.0f;
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
        float lineHeight = resolveLineHeight(styleVal(style, "line-height"), fontSize);

        struct IFCItem {
            float width = 0, height = 0;
            LayoutNode* node = nullptr;
            bool isElement = false;
        };
        std::vector<IFCItem> items;

        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) {
                float ls = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
                float ws = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
                auto runs = breakTextIntoRuns(child->textContent(), childAvailable,
                    fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                    "normal", "normal", ls, ws);
                for (auto& run : runs) {
                    if (run.text.empty() && run.width == 0) continue;
                    items.push_back({run.width, std::max(run.height, lineHeight), child, false});
                }
            } else {
                auto& cs = child->computedStyle();
                const std::string& d = styleVal(cs, "display");
                if (d == "none") { child->box = LayoutBox{}; continue; }
                const std::string& cp = styleVal(cs, "position");
                if (cp == "absolute" || cp == "fixed") continue;
                layoutNode(child, childAvailable, metrics);
                items.push_back({
                    child->box.fullWidth() + child->box.margin.left + child->box.margin.right,
                    child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom,
                    child, true});
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

        // Build line boxes first, then position with alignment
        struct LineBounds { size_t start; size_t end; float totalWidth; float maxHeight; };
        std::vector<LineBounds> lines;
        {
            size_t lineStart = 0;
            float cursorX = 0, lineMaxH = 0;
            for (size_t i = 0; i < items.size(); i++) {
                if (cursorX > 0 && cursorX + items[i].width > childAvailable) {
                    lines.push_back({lineStart, i, cursorX, lineMaxH});
                    lineStart = i;
                    cursorX = 0;
                    lineMaxH = 0;
                }
                cursorX += items[i].width;
                lineMaxH = std::max(lineMaxH, items[i].height);
            }
            if (lineStart < items.size()) {
                lines.push_back({lineStart, items.size(), cursorX, lineMaxH});
            }
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

            for (size_t i = line.start; i < line.end; i++) {
                auto& item = items[i];
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
                cursorX += item.width + gap;
            }
            cursorY += line.maxHeight;
        }
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

    // Resolve text-align for anonymous inline boxes in BFC
    const std::string& bfcTextAlign = styleVal(style, "text-align");
    const std::string& bfcDirection = styleVal(style, "direction");
    std::string bfcResolvedAlign = bfcTextAlign;
    if (bfcResolvedAlign == "start" || bfcResolvedAlign.empty()) {
        bfcResolvedAlign = (bfcDirection == "rtl") ? "right" : "left";
    } else if (bfcResolvedAlign == "end") {
        bfcResolvedAlign = (bfcDirection == "rtl") ? "left" : "right";
    }

    // Helper: flush accumulated inline children as an anonymous line box
    std::vector<LayoutNode*> pendingInline;
    auto flushInlineRun = [&]() {
        if (pendingInline.empty()) return;

        // First pass: measure all items and build line structure
        struct AnonItem {
            LayoutNode* node = nullptr;
            float width = 0, height = 0;
            bool isText = false;
        };
        std::vector<AnonItem> anonItems;

        for (auto* inl : pendingInline) {
            if (inl->isTextNode()) {
                float tw = 0, th = 0;
                float ls2 = resolveLength(styleVal(style, "letter-spacing"), 0, fontSize);
                float ws2 = resolveLength(styleVal(style, "word-spacing"), 0, fontSize);
                auto runs = breakTextIntoRuns(inl->textContent(), childAvailable,
                    styleVal(style, "font-family"), fontSize, styleVal(style, "font-weight"),
                    styleVal(style, "white-space"), metrics,
                    "normal", "normal", ls2, ws2);
                for (auto& run : runs) {
                    tw += run.width;
                    th = std::max(th, run.height);
                }
                float lh = resolveLineHeight(styleVal(style, "line-height"), fontSize);
                th = std::max(th, lh);
                anonItems.push_back({inl, tw, th, true});
            } else {
                layoutNode(inl, childAvailable, metrics);
                float cw = inl->box.fullWidth() + inl->box.margin.left + inl->box.margin.right;
                float ch = inl->box.fullHeight() + inl->box.margin.top + inl->box.margin.bottom;
                anonItems.push_back({inl, cw, ch, false});
            }
        }

        // Build lines
        struct AnonLine { size_t start; size_t end; float totalWidth; float maxHeight; };
        std::vector<AnonLine> anonLines;
        {
            size_t ls = 0;
            float cx = 0, mh = 0;
            for (size_t i = 0; i < anonItems.size(); i++) {
                if (cx > 0 && cx + anonItems[i].width > childAvailable) {
                    anonLines.push_back({ls, i, cx, mh});
                    ls = i; cx = 0; mh = 0;
                }
                cx += anonItems[i].width;
                mh = std::max(mh, anonItems[i].height);
            }
            if (ls < anonItems.size()) anonLines.push_back({ls, anonItems.size(), cx, mh});
        }

        // Position with text-align (skip zero-width lines from whitespace)
        for (auto& line : anonLines) {
            if (line.totalWidth <= 0) continue;
            float extra = childAvailable - line.totalWidth;
            float xOff = 0;
            if (extra > 0) {
                if (bfcResolvedAlign == "center") xOff = extra / 2.0f;
                else if (bfcResolvedAlign == "right" || bfcResolvedAlign == "end") xOff = extra;
            }
            float cx = xOff;
            for (size_t i = line.start; i < line.end; i++) {
                auto& ai = anonItems[i];
                if (ai.isText) {
                    ai.node->box.contentRect.x = cx;
                    ai.node->box.contentRect.y = cursorY;
                    ai.node->box.contentRect.width = ai.width;
                    ai.node->box.contentRect.height = ai.height;
                } else {
                    ai.node->box.contentRect.x = cx + ai.node->box.margin.left +
                        ai.node->box.padding.left + ai.node->box.border.left;
                    ai.node->box.contentRect.y = cursorY + ai.node->box.margin.top +
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
    } else {
        // height: auto — shrink to fit content
        node->box.contentRect.height = cursorY;
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
