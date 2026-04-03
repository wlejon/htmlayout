#include "layout/flex.h"
#include "layout/formatting_context.h"
#include <algorithm>
#include <cmath>
#include <numeric>

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

struct FlexItem {
    LayoutNode* node;
    float flexGrow;
    float flexShrink;
    float flexBasis;      // resolved basis (px), -1 = auto
    float hypotheticalMain; // size after basis resolution
    float minMain;
    float maxMain;
    float crossSize;
    int order;
    bool frozen = false;
    float finalMain = 0;
};

struct FlexLine {
    std::vector<FlexItem*> items;
    float mainSize = 0;
    float crossSize = 0;
};

} // anonymous namespace

void layoutFlex(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
    node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);

    Edges borderWidth{};
    const char* sideNames[] = {"top", "right", "bottom", "left"};
    float* bw[] = {&borderWidth.top, &borderWidth.right, &borderWidth.bottom, &borderWidth.left};
    for (int i = 0; i < 4; i++) {
        if (styleVal(style, std::string("border-") + sideNames[i] + "-style") != "none")
            *bw[i] = resolveLength(styleVal(style, std::string("border-") + sideNames[i] + "-width"), availableWidth, fontSize);
    }
    node->box.border = borderWidth;

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;

    // Container dimensions
    float specW = resolveDim(styleVal(style, "width"), availableWidth, fontSize);
    float containerMain;
    if (specW >= 0) {
        if (styleVal(style, "box-sizing") == "border-box")
            containerMain = specW - paddingH - borderH;
        else
            containerMain = specW;
        if (containerMain < 0) containerMain = 0;
    } else {
        containerMain = availableWidth - node->box.margin.left - node->box.margin.right - paddingH - borderH;
        if (containerMain < 0) containerMain = 0;
    }

    // Apply min/max-width constraints
    float minW = resolveDim(styleVal(style, "min-width"), availableWidth, fontSize);
    float maxW = resolveDim(styleVal(style, "max-width"), availableWidth, fontSize);
    if (minW >= 0 && containerMain < minW) containerMain = minW;
    if (maxW >= 0 && containerMain > maxW) containerMain = maxW;

    // Flex properties
    const std::string& flexDir = styleVal(style, "flex-direction");
    const std::string& flexWrap = styleVal(style, "flex-wrap");
    const std::string& justifyContent = styleVal(style, "justify-content");
    const std::string& alignItems = styleVal(style, "align-items");
    const std::string& alignContent = styleVal(style, "align-content");

    bool isRow = (flexDir == "row" || flexDir == "row-reverse" || flexDir.empty());
    bool isReverse = (flexDir == "row-reverse" || flexDir == "column-reverse");
    bool isWrap = (flexWrap == "wrap" || flexWrap == "wrap-reverse");

    float mainAvailable = isRow ? containerMain : resolveDim(styleVal(style, "height"), node->availableHeight, fontSize);
    bool columnAutoHeight = (!isRow && mainAvailable < 0);
    if (mainAvailable < 0) mainAvailable = containerMain; // initial fallback for column with auto height

    float gapMain = resolveLength(styleVal(style, isRow ? "column-gap" : "row-gap"), mainAvailable, fontSize);
    float gapCross = resolveLength(styleVal(style, isRow ? "row-gap" : "column-gap"), mainAvailable, fontSize);

    // Resolve definite cross size for percentage height propagation to children
    float crossSpecH = resolveDim(styleVal(style, "height"), node->availableHeight, fontSize);
    float childAvailableHeight = 0.0f;
    if (isRow && crossSpecH >= 0) {
        if (styleVal(style, "box-sizing") == "border-box")
            childAvailableHeight = crossSpecH - paddingV - borderV;
        else
            childAvailableHeight = crossSpecH;
        if (childAvailableHeight < 0) childAvailableHeight = 0;
    } else if (!isRow && mainAvailable > 0) {
        // For column flex, children's available height is the resolved main size
        childAvailableHeight = mainAvailable;
    }

    // Collect flex items, filtering out absolutely/fixed positioned children.
    // Text nodes become anonymous flex items (CSS spec).
    std::vector<FlexItem> items;
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            // Anonymous flex item: measure text and include as a flex item
            std::string text = child->textContent();
            bool allWhitespace = true;
            for (char c : text) {
                if (!std::isspace(static_cast<unsigned char>(c))) { allWhitespace = false; break; }
            }
            if (allWhitespace) continue;

            const std::string& fontFamily = styleVal(style, "font-family");
            const std::string& fontWeight = styleVal(style, "font-weight");
            float textW = metrics.measureWidth(text, fontFamily, fontSize, fontWeight);
            float textH = metrics.lineHeight(fontFamily, fontSize, fontWeight);

            child->box.contentRect.width = textW;
            child->box.contentRect.height = textH;

            FlexItem item;
            item.node = child;
            item.flexGrow = 0;
            item.flexShrink = 1;
            item.flexBasis = isRow ? textW : textH;
            item.minMain = 0;
            item.maxMain = -1;
            item.order = 0;
            items.push_back(item);
            continue;
        }
        child->viewportHeight = node->viewportHeight;
        child->availableHeight = childAvailableHeight;
        auto& cs = child->computedStyle();
        if (styleVal(cs, "display") == "none") {
            child->box = LayoutBox{};
            continue;
        }
        const std::string& childPos = styleVal(cs, "position");
        if (childPos == "absolute" || childPos == "fixed") continue;

        FlexItem item;
        item.node = child;
        float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
        if (childFontSize <= 0) childFontSize = fontSize;

        item.flexGrow = resolveLength(styleVal(cs, "flex-grow"), 0, childFontSize);
        item.flexShrink = resolveLength(styleVal(cs, "flex-shrink"), 0, childFontSize);
        if (item.flexShrink < 0) item.flexShrink = 1.0f;
        item.order = static_cast<int>(resolveLength(styleVal(cs, "order"), 0, childFontSize));

        // Resolve flex-basis
        const std::string& basis = styleVal(cs, "flex-basis");
        if (basis == "auto" || basis.empty()) {
            // Use width/height as basis
            const std::string& dimProp = isRow ? "width" : "height";
            float dim = resolveDim(styleVal(cs, dimProp), mainAvailable, childFontSize);
            item.flexBasis = dim >= 0 ? dim : -1.0f;
        } else {
            item.flexBasis = resolveLength(basis, mainAvailable, childFontSize);
        }

        // Resolve min/max on main axis
        if (isRow) {
            item.minMain = resolveDim(styleVal(cs, "min-width"), mainAvailable, childFontSize);
            item.maxMain = resolveDim(styleVal(cs, "max-width"), mainAvailable, childFontSize);
        } else {
            item.minMain = resolveDim(styleVal(cs, "min-height"), mainAvailable, childFontSize);
            item.maxMain = resolveDim(styleVal(cs, "max-height"), mainAvailable, childFontSize);
        }
        if (item.minMain < 0) item.minMain = 0;

        items.push_back(item);
    }

    // Sort by order
    std::stable_sort(items.begin(), items.end(),
        [](const FlexItem& a, const FlexItem& b) { return a.order < b.order; });

    // Determine hypothetical main sizes
    for (auto& item : items) {
        if (item.flexBasis >= 0) {
            item.hypotheticalMain = item.flexBasis;
        } else {
            // Auto basis: use intrinsic (max-content) size so wrapping works.
            // Without this, items laid out with mainAvailable expand to fill
            // the container and wrapping never triggers.
            if (isRow) {
                float intrinsic = computeMaxContentWidth(item.node, metrics);
                // Add padding/border/margin edges
                auto& cs = item.node->computedStyle();
                float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;
                float ph = resolveLength(styleVal(cs, "padding-left"), mainAvailable, childFontSize) +
                           resolveLength(styleVal(cs, "padding-right"), mainAvailable, childFontSize);
                float bh = 0;
                const char* sides[] = {"left", "right"};
                for (auto* s : sides) {
                    if (styleVal(cs, std::string("border-") + s + "-style") != "none")
                        bh += resolveLength(styleVal(cs, std::string("border-") + s + "-width"), mainAvailable, childFontSize);
                }
                item.hypotheticalMain = intrinsic + ph + bh;
                if (item.hypotheticalMain > mainAvailable)
                    item.hypotheticalMain = mainAvailable;
                // Now lay out at this width to compute cross size
                layoutNode(item.node, item.hypotheticalMain, metrics);
            } else {
                layoutNode(item.node, containerMain, metrics);
                item.hypotheticalMain = item.node->box.contentRect.height +
                                         item.node->box.padding.top + item.node->box.padding.bottom +
                                         item.node->box.border.top + item.node->box.border.bottom;
            }
        }
        // Clamp to min/max
        if (item.maxMain >= 0 && item.hypotheticalMain > item.maxMain)
            item.hypotheticalMain = item.maxMain;
        if (item.hypotheticalMain < item.minMain)
            item.hypotheticalMain = item.minMain;
    }

    // Compute per-item main-axis margins for sizing and positioning
    auto itemMarginMain = [&](const FlexItem& item) -> float {
        if (isRow)
            return item.node->box.margin.left + item.node->box.margin.right;
        else
            return item.node->box.margin.top + item.node->box.margin.bottom;
    };

    // Split into flex lines
    std::vector<FlexLine> lines;
    {
        FlexLine currentLine;
        float lineMain = 0;
        for (size_t i = 0; i < items.size(); i++) {
            float itemOuter = items[i].hypotheticalMain + itemMarginMain(items[i]);
            float itemMain = itemOuter + (currentLine.items.empty() ? 0 : gapMain);
            if (isWrap && !currentLine.items.empty() && lineMain + itemMain > mainAvailable) {
                lines.push_back(std::move(currentLine));
                currentLine = FlexLine{};
                lineMain = 0;
                itemMain = itemOuter;
            }
            currentLine.items.push_back(&items[i]);
            lineMain += itemMain;
            currentLine.mainSize = lineMain;
        }
        if (!currentLine.items.empty()) lines.push_back(std::move(currentLine));
    }

    // For column flex with auto height, ensure mainAvailable is at least the total
    // hypothetical size so items are never shrunk (the container grows to fit).
    if (columnAutoHeight) {
        float totalNeeded = 0;
        float totalGapsAll = (items.size() > 1) ? gapMain * (items.size() - 1) : 0;
        for (auto& item : items) {
            totalNeeded += item.hypotheticalMain + itemMarginMain(item);
        }
        totalNeeded += totalGapsAll;
        if (totalNeeded > mainAvailable) mainAvailable = totalNeeded;
    }

    // Resolve flexible lengths per line
    for (auto& line : lines) {
        float totalHypothetical = 0;
        float totalMargins = 0;
        float totalGaps = (line.items.size() > 1) ? gapMain * (line.items.size() - 1) : 0;
        for (auto* item : line.items) {
            totalHypothetical += item->hypotheticalMain;
            totalMargins += itemMarginMain(*item);
        }

        float freeSpace = mainAvailable - totalHypothetical - totalMargins - totalGaps;

        if (freeSpace > 0) {
            // Distribute via flex-grow
            float totalGrow = 0;
            for (auto* item : line.items) totalGrow += item->flexGrow;
            for (auto* item : line.items) {
                if (totalGrow > 0) {
                    item->finalMain = item->hypotheticalMain + freeSpace * (item->flexGrow / totalGrow);
                } else {
                    item->finalMain = item->hypotheticalMain;
                }
            }
        } else if (freeSpace < 0) {
            // Shrink via flex-shrink
            float totalShrinkScaled = 0;
            for (auto* item : line.items)
                totalShrinkScaled += item->flexShrink * item->hypotheticalMain;
            for (auto* item : line.items) {
                if (totalShrinkScaled > 0) {
                    float ratio = (item->flexShrink * item->hypotheticalMain) / totalShrinkScaled;
                    item->finalMain = item->hypotheticalMain + freeSpace * ratio;
                } else {
                    item->finalMain = item->hypotheticalMain;
                }
            }
        } else {
            for (auto* item : line.items) item->finalMain = item->hypotheticalMain;
        }

        // Clamp to min/max
        for (auto* item : line.items) {
            if (item->maxMain >= 0 && item->finalMain > item->maxMain) item->finalMain = item->maxMain;
            if (item->finalMain < item->minMain) item->finalMain = item->minMain;
            if (item->finalMain < 0) item->finalMain = 0;
        }

        // Layout each item with its final main size
        for (auto* item : line.items) {
            // Text nodes (anonymous flex items) already have their size set
            if (item->node->isTextNode()) {
                if (isRow) {
                    item->node->box.contentRect.width = item->finalMain;
                    item->crossSize = item->node->box.contentRect.height;
                } else {
                    item->node->box.contentRect.height = item->finalMain;
                    item->crossSize = item->node->box.contentRect.width;
                }
                continue;
            }

            auto& cs = item->node->computedStyle();
            float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;

            // Set the item's content size
            float itemPadH = 0, itemPadV = 0, itemBorH = 0, itemBorV = 0;

            // Layout the item to determine cross size
            if (isRow) {
                // Main = width, cross = height
                float contentWidth = item->finalMain;
                layoutNode(item->node, contentWidth, metrics);
                item->node->box.contentRect.width = contentWidth -
                    item->node->box.padding.left - item->node->box.padding.right -
                    item->node->box.border.left - item->node->box.border.right;
                if (item->node->box.contentRect.width < 0) item->node->box.contentRect.width = 0;

                item->crossSize = item->node->box.contentRect.height +
                    item->node->box.padding.top + item->node->box.padding.bottom +
                    item->node->box.border.top + item->node->box.border.bottom +
                    item->node->box.margin.top + item->node->box.margin.bottom;
            } else {
                // Main = height, cross = width
                layoutNode(item->node, containerMain, metrics);
                item->node->box.contentRect.height = item->finalMain -
                    item->node->box.padding.top - item->node->box.padding.bottom -
                    item->node->box.border.top - item->node->box.border.bottom;
                if (item->node->box.contentRect.height < 0) item->node->box.contentRect.height = 0;

                item->crossSize = item->node->box.contentRect.width +
                    item->node->box.padding.left + item->node->box.padding.right +
                    item->node->box.border.left + item->node->box.border.right +
                    item->node->box.margin.left + item->node->box.margin.right;
            }
        }

        // Determine line cross size
        float maxCross = 0;
        for (auto* item : line.items) maxCross = std::max(maxCross, item->crossSize);
        line.crossSize = maxCross;
    }

    // Distribute cross-axis space among lines (align-content)
    float totalLineCross = 0;
    for (auto& line : lines) totalLineCross += line.crossSize;
    float totalLineGaps = (lines.size() > 1) ? gapCross * (lines.size() - 1) : 0;

    // Determine definite cross size
    float crossAvailable = -1;
    if (isRow) {
        float specH = resolveDim(styleVal(style, "height"), node->availableHeight, fontSize);
        if (specH >= 0) {
            if (styleVal(style, "box-sizing") == "border-box")
                crossAvailable = specH - paddingV - borderV;
            else
                crossAvailable = specH;
            if (crossAvailable < 0) crossAvailable = 0;
        }
    } else {
        crossAvailable = containerMain; // for column flex, cross = width
    }

    float crossOffset = 0;
    float crossGapAdjusted = gapCross;
    if (crossAvailable >= 0 && lines.size() > 0) {
        float freeCross = crossAvailable - totalLineCross - totalLineGaps;
        if (freeCross < 0) freeCross = 0;

        if (alignContent == "center") {
            crossOffset = freeCross / 2.0f;
        } else if (alignContent == "flex-end") {
            crossOffset = freeCross;
        } else if (alignContent == "space-between" && lines.size() > 1) {
            crossGapAdjusted = gapCross + freeCross / (lines.size() - 1);
        } else if (alignContent == "space-around" && !lines.empty()) {
            float lineGap = freeCross / lines.size();
            crossOffset = lineGap / 2.0f;
            crossGapAdjusted = gapCross + lineGap;
        } else if (alignContent == "space-evenly" && !lines.empty()) {
            float lineGap = freeCross / (lines.size() + 1);
            crossOffset = lineGap;
            crossGapAdjusted = gapCross + lineGap;
        } else if (alignContent == "stretch" || alignContent.empty()) {
            // Stretch: distribute free space equally to each line's cross size
            if (!lines.empty() && freeCross > 0) {
                float extra = freeCross / lines.size();
                for (auto& line : lines) line.crossSize += extra;
            }
        }
        // else flex-start (default): crossOffset = 0
    }

    // Position items
    float crossCursor = crossOffset;
    for (auto& line : lines) {
        // Compute justify-content offsets
        float totalMain = 0;
        float totalMainMargins = 0;
        float totalGaps = (line.items.size() > 1) ? gapMain * (line.items.size() - 1) : 0;
        for (auto* item : line.items) {
            totalMain += item->finalMain;
            totalMainMargins += itemMarginMain(*item);
        }
        float freeMain = mainAvailable - totalMain - totalMainMargins - totalGaps;
        if (freeMain < 0) freeMain = 0;

        // Auto margins on main axis: count how many auto margins exist on the main axis.
        // If any exist, they absorb all free space (overriding justify-content).
        int autoMainMargins = 0;
        for (auto* item : line.items) {
            if (item->node->isTextNode()) continue;
            auto& cs = item->node->computedStyle();
            if (isRow) {
                if (styleVal(cs, "margin-left") == "auto") autoMainMargins++;
                if (styleVal(cs, "margin-right") == "auto") autoMainMargins++;
            } else {
                if (styleVal(cs, "margin-top") == "auto") autoMainMargins++;
                if (styleVal(cs, "margin-bottom") == "auto") autoMainMargins++;
            }
        }

        // Distribute free space to auto margins (resolve them from 0 to computed value)
        if (autoMainMargins > 0 && freeMain > 0) {
            float perAutoMargin = freeMain / autoMainMargins;
            for (auto* item : line.items) {
                if (item->node->isTextNode()) continue;
                auto& cs = item->node->computedStyle();
                if (isRow) {
                    if (styleVal(cs, "margin-left") == "auto")
                        item->node->box.margin.left = perAutoMargin;
                    if (styleVal(cs, "margin-right") == "auto")
                        item->node->box.margin.right = perAutoMargin;
                } else {
                    if (styleVal(cs, "margin-top") == "auto")
                        item->node->box.margin.top = perAutoMargin;
                    if (styleVal(cs, "margin-bottom") == "auto")
                        item->node->box.margin.bottom = perAutoMargin;
                }
            }
            // Recalculate totalMainMargins with resolved auto margins
            totalMainMargins = 0;
            for (auto* item : line.items) totalMainMargins += itemMarginMain(*item);
            freeMain = 0; // all free space consumed by auto margins
        }

        float mainCursor = 0;
        float gap = gapMain;
        if (autoMainMargins == 0) {
            // Only apply justify-content when there are no auto margins
            if (justifyContent == "center") {
                mainCursor = freeMain / 2.0f;
            } else if (justifyContent == "flex-end") {
                mainCursor = freeMain;
            } else if (justifyContent == "space-between" && line.items.size() > 1) {
                gap = gapMain + freeMain / (line.items.size() - 1);
            } else if (justifyContent == "space-around" && !line.items.empty()) {
                float itemGap = freeMain / line.items.size();
                mainCursor = itemGap / 2.0f;
                gap = gapMain + itemGap;
            } else if (justifyContent == "space-evenly" && !line.items.empty()) {
                float itemGap = freeMain / (line.items.size() + 1);
                mainCursor = itemGap;
                gap = gapMain + itemGap;
            }
        }
        // else flex-start: mainCursor = 0

        if (isReverse) {
            // Reverse the item positions
            mainCursor = mainAvailable;
        }

        for (size_t i = 0; i < line.items.size(); i++) {
            auto* item = line.items[i];
            auto& cs = item->node->computedStyle();

            // Cross-axis auto margins: if auto margins exist on the cross axis,
            // they absorb free space (overriding align-items/align-self).
            bool hasCrossAutoMargin = false;
            if (!item->node->isTextNode()) {
                if (isRow) {
                    hasCrossAutoMargin = (styleVal(cs, "margin-top") == "auto" ||
                                          styleVal(cs, "margin-bottom") == "auto");
                } else {
                    hasCrossAutoMargin = (styleVal(cs, "margin-left") == "auto" ||
                                          styleVal(cs, "margin-right") == "auto");
                }
            }

            // Cross-axis alignment
            const std::string& selfAlign = styleVal(cs, "align-self");
            const std::string& align = (selfAlign == "auto" || selfAlign.empty()) ? alignItems : selfAlign;

            float crossPos = crossCursor;
            if (hasCrossAutoMargin) {
                // Cross-axis auto margins absorb free space (override align-items/align-self)
                float freeCross = line.crossSize - item->crossSize;
                if (freeCross < 0) freeCross = 0;
                if (isRow) {
                    bool topAuto = (styleVal(cs, "margin-top") == "auto");
                    bool bottomAuto = (styleVal(cs, "margin-bottom") == "auto");
                    if (topAuto && bottomAuto) {
                        item->node->box.margin.top = freeCross / 2.0f;
                        item->node->box.margin.bottom = freeCross / 2.0f;
                    } else if (topAuto) {
                        item->node->box.margin.top = freeCross;
                    } else {
                        item->node->box.margin.bottom = freeCross;
                    }
                } else {
                    bool leftAuto = (styleVal(cs, "margin-left") == "auto");
                    bool rightAuto = (styleVal(cs, "margin-right") == "auto");
                    if (leftAuto && rightAuto) {
                        item->node->box.margin.left = freeCross / 2.0f;
                        item->node->box.margin.right = freeCross / 2.0f;
                    } else if (leftAuto) {
                        item->node->box.margin.left = freeCross;
                    } else {
                        item->node->box.margin.right = freeCross;
                    }
                }
                // Recalculate crossSize with resolved margins
                if (isRow) {
                    item->crossSize = item->node->box.contentRect.height +
                        item->node->box.padding.top + item->node->box.padding.bottom +
                        item->node->box.border.top + item->node->box.border.bottom +
                        item->node->box.margin.top + item->node->box.margin.bottom;
                } else {
                    item->crossSize = item->node->box.contentRect.width +
                        item->node->box.padding.left + item->node->box.padding.right +
                        item->node->box.border.left + item->node->box.border.right +
                        item->node->box.margin.left + item->node->box.margin.right;
                }
                crossPos = crossCursor;
            } else if (align == "baseline" && isRow) {
                // Baseline alignment: compute item baseline as distance from
                // outer top edge to the first text baseline (font-size from content top)
                float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;
                float itemBaseline = item->node->box.margin.top +
                    item->node->box.border.top + item->node->box.padding.top + childFontSize;

                // Find max baseline in this line for baseline-aligned items
                float maxBaseline = 0;
                for (auto* li : line.items) {
                    auto& lis = li->node->computedStyle();
                    const std::string& liSelf = styleVal(lis, "align-self");
                    const std::string& liAlign = (liSelf == "auto" || liSelf.empty()) ? alignItems : liSelf;
                    if (liAlign == "baseline") {
                        float lfs = resolveLength(styleVal(lis, "font-size"), fontSize, fontSize);
                        if (lfs <= 0) lfs = fontSize;
                        float lb = li->node->box.margin.top +
                            li->node->box.border.top + li->node->box.padding.top + lfs;
                        maxBaseline = std::max(maxBaseline, lb);
                    }
                }
                crossPos = crossCursor + (maxBaseline - itemBaseline);
            } else if (align == "center") {
                crossPos = crossCursor + (line.crossSize - item->crossSize) / 2.0f;
            } else if (align == "flex-end") {
                crossPos = crossCursor + line.crossSize - item->crossSize;
            } else if (align == "stretch") {
                // Stretch to fill line cross size (only if no explicit cross dimension)
                if (isRow) {
                    const std::string& h = styleVal(cs, "height");
                    if (h == "auto" || h.empty()) {
                        float stretchCross = line.crossSize -
                            item->node->box.margin.top - item->node->box.margin.bottom -
                            item->node->box.padding.top - item->node->box.padding.bottom -
                            item->node->box.border.top - item->node->box.border.bottom;
                        if (stretchCross > 0) item->node->box.contentRect.height = stretchCross;
                    }
                } else {
                    const std::string& w = styleVal(cs, "width");
                    if (w == "auto" || w.empty()) {
                        float stretchCross = line.crossSize -
                            item->node->box.margin.left - item->node->box.margin.right -
                            item->node->box.padding.left - item->node->box.padding.right -
                            item->node->box.border.left - item->node->box.border.right;
                        if (stretchCross > 0) item->node->box.contentRect.width = stretchCross;
                    }
                }
                crossPos = crossCursor;
            }
            // else flex-start (default): crossPos stays at crossCursor

            // Set position (margins are added to contentRect below, advance
            // cursor by outer size = finalMain + margins)
            float marginM = itemMarginMain(*item);
            float outerMain = item->finalMain + marginM;
            float mainPos;
            if (isReverse) {
                mainCursor -= outerMain;
                mainPos = mainCursor;
                if (i + 1 < line.items.size()) mainCursor -= gap;
            } else {
                mainPos = mainCursor;
                mainCursor += outerMain;
                if (i + 1 < line.items.size()) mainCursor += gap;
            }

            if (isRow) {
                item->node->box.contentRect.x = mainPos +
                    item->node->box.margin.left + item->node->box.padding.left + item->node->box.border.left;
                item->node->box.contentRect.y = crossPos +
                    item->node->box.margin.top + item->node->box.padding.top + item->node->box.border.top;
            } else {
                item->node->box.contentRect.y = mainPos +
                    item->node->box.margin.top + item->node->box.padding.top + item->node->box.border.top;
                item->node->box.contentRect.x = crossPos +
                    item->node->box.margin.left + item->node->box.padding.left + item->node->box.border.left;
            }

            // Apply position: relative offset
            const std::string& childPos = styleVal(cs, "position");
            if (childPos == "relative" || childPos == "sticky") {
                float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;
                const std::string& topVal = styleVal(cs, "top");
                const std::string& leftVal = styleVal(cs, "left");
                const std::string& bottomVal = styleVal(cs, "bottom");
                const std::string& rightVal = styleVal(cs, "right");

                if (topVal != "auto" && !topVal.empty()) {
                    item->node->box.contentRect.y += resolveLength(topVal, 0, childFontSize);
                } else if (bottomVal != "auto" && !bottomVal.empty()) {
                    item->node->box.contentRect.y -= resolveLength(bottomVal, 0, childFontSize);
                }
                if (leftVal != "auto" && !leftVal.empty()) {
                    item->node->box.contentRect.x += resolveLength(leftVal, containerMain, childFontSize);
                } else if (rightVal != "auto" && !rightVal.empty()) {
                    item->node->box.contentRect.x -= resolveLength(rightVal, containerMain, childFontSize);
                }
            }
        }

        crossCursor += line.crossSize + crossGapAdjusted;
    }

    // Set container dimensions
    node->box.contentRect.width = containerMain;

    float specH = resolveDim(styleVal(style, "height"), node->availableHeight, fontSize);
    if (specH >= 0) {
        if (styleVal(style, "box-sizing") == "border-box")
            node->box.contentRect.height = specH - paddingV - borderV;
        else
            node->box.contentRect.height = specH;
        if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
    } else {
        node->box.contentRect.height = crossCursor > 0 ? crossCursor - crossGapAdjusted : 0;
    }

    // Store natural height before clamping (for scroll extent calculation)
    float naturalCross = crossCursor > 0 ? crossCursor - crossGapAdjusted : 0;
    node->box.naturalHeight = std::max(naturalCross, node->box.contentRect.height);

    // Apply min/max-height constraints (same as block layout)
    float minH = resolveDim(styleVal(style, "min-height"), node->availableHeight, fontSize);
    float maxH = resolveDim(styleVal(style, "max-height"), node->availableHeight, fontSize);
    if (minH >= 0.0f && node->box.contentRect.height < minH) node->box.contentRect.height = minH;
    if (maxH >= 0.0f && node->box.contentRect.height > maxH) node->box.contentRect.height = maxH;

}

} // namespace htmlayout::layout
