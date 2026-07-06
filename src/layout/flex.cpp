#include "layout/flex.h"
#include "layout/formatting_context.h"
#include "layout/text.h"
#include "layout/style_util.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace htmlayout::layout {

using layout::styleVal;

namespace {

float resolveDim(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

struct FlexItem {
    LayoutNode* node;
    float flexGrow;
    float flexShrink;
    float flexBasis;      // resolved basis (px), -1 = auto
    float baseMain = 0;     // flex base size (outer, before min/max clamp)
    float hypotheticalMain; // base size clamped by min/max
    float minMain;
    float maxMain;
    float crossSize;
    int order;
    bool frozen = false;
    float finalMain = 0;
    // Column flex only: min-main is auto (min-height:auto) with visible overflow,
    // so its content-based minimum can only be resolved once the item is laid out
    // (its block-axis content height depends on the definite cross size). Deferred
    // to the hypothetical-size loop where that layout happens.
    bool colAutoMinPending = false;
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
    // Column flex with no explicit height but a definite available height from
    // the parent (e.g. parent flex distributed space to us): use it as the
    // main-axis constraint so children are properly sized.
    if (!isRow && mainAvailable < 0 && node->box.contentRect.height > 0) {
        // An outer pass (e.g. position:absolute with top+bottom pinned, or an
        // enclosing flex that stretched/grew this item) already resolved a
        // definite content height for this container — use it as the main-axis
        // constraint. Note: the *containing block's* availableHeight is NOT a
        // substitute — an auto-height column flex container that was not
        // stretched is content-sized (fit-content) per CSS2 §10.6.7, and its
        // children must never be flex-shrunk to the parent's height.
        mainAvailable = node->box.contentRect.height;
    }
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
            std::string_view text = child->textContent();
            bool allWhitespace = true;
            for (char c : text) {
                if (!std::isspace(static_cast<unsigned char>(c))) { allWhitespace = false; break; }
            }
            if (allWhitespace) continue;

            const std::string& fontFamily = styleVal(style, "font-family");
            const std::string& fontWeight = styleVal(style, "font-weight");
            // Measure the text-transformed glyphs (matches paint + breakTextIntoRuns).
            std::string shaped = applyTextTransform(std::string(text), styleVal(style, "text-transform"));
            float textW = metrics.measureWidth(shaped, fontFamily, fontSize, fontWeight);
            float textH = metrics.lineHeight(fontFamily, fontSize, fontWeight);

            child->box.contentRect.width = textW;
            child->box.contentRect.height = textH;

            FlexItem item;
            item.node = child;
            item.flexGrow = 0;
            item.flexShrink = 1;
            item.flexBasis = isRow ? textW : textH;
            // Automatic minimum size (§4.5) applies to anonymous items too:
            // in a row container the text may not be shrunk below its
            // min-content width (the widest unbreakable word).
            item.minMain = 0;
            if (isRow) {
                std::string word;
                float widestWord = 0.0f;
                for (size_t ci = 0; ci <= shaped.size(); ci++) {
                    char c = (ci < shaped.size()) ? shaped[ci] : ' ';
                    if (std::isspace(static_cast<unsigned char>(c))) {
                        if (!word.empty()) {
                            widestWord = std::max(widestWord,
                                metrics.measureWidth(word, fontFamily, fontSize, fontWeight));
                            word.clear();
                        }
                    } else {
                        word += c;
                    }
                }
                item.minMain = widestWord;
            }
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

        // Re-resolve the item's margins from style every pass (auto → 0, per
        // §9.7 auto margins are treated as 0 while sizing). A previous layout
        // pass may have written *resolved* main-axis auto margins into
        // box.margin (they absorb free space at positioning time); counting
        // those stale values as real margins here would eat into this pass's
        // free space and shrink items that fit.
        child->box.margin = resolveEdges(cs, "margin", containerMain, childFontSize);

        // Resolve flex-basis. flex-basis represents the outer (border-box) main
        // size of the item — the rest of flex layout subtracts padding/border to
        // recover content size. When the basis comes from a width/height (or an
        // explicit length on flex-basis) and box-sizing is content-box, the
        // specified value is content size, so we must add padding+border to
        // convert to the outer main size.
        const std::string& basis = styleVal(cs, "flex-basis");
        bool basisFromMainDim = false;
        if (basis == "auto" || basis.empty()) {
            // Use width/height as basis
            const std::string& dimProp = isRow ? "width" : "height";
            float dim = resolveDim(styleVal(cs, dimProp), mainAvailable, childFontSize);
            item.flexBasis = dim >= 0 ? dim : -1.0f;
            basisFromMainDim = (dim >= 0);
        } else {
            item.flexBasis = resolveLength(basis, mainAvailable, childFontSize);
            basisFromMainDim = (item.flexBasis >= 0);
        }
        if (basisFromMainDim && styleVal(cs, "box-sizing") != "border-box") {
            float edges = 0;
            if (isRow) {
                edges += resolveLength(styleVal(cs, "padding-left"), mainAvailable, childFontSize) +
                         resolveLength(styleVal(cs, "padding-right"), mainAvailable, childFontSize);
                if (styleVal(cs, "border-left-style") != "none")
                    edges += resolveLength(styleVal(cs, "border-left-width"), mainAvailable, childFontSize);
                if (styleVal(cs, "border-right-style") != "none")
                    edges += resolveLength(styleVal(cs, "border-right-width"), mainAvailable, childFontSize);
            } else {
                edges += resolveLength(styleVal(cs, "padding-top"), mainAvailable, childFontSize) +
                         resolveLength(styleVal(cs, "padding-bottom"), mainAvailable, childFontSize);
                if (styleVal(cs, "border-top-style") != "none")
                    edges += resolveLength(styleVal(cs, "border-top-width"), mainAvailable, childFontSize);
                if (styleVal(cs, "border-bottom-style") != "none")
                    edges += resolveLength(styleVal(cs, "border-bottom-width"), mainAvailable, childFontSize);
            }
            item.flexBasis += edges;
        }

        // Resolve min/max on main axis.
        // CSS Flexbox §4.5: min-width/min-height: auto on a flex item resolves to
        // the item's min-content size on the main axis (when overflow is visible),
        // so unbreakable content (long words) is not shrunk below its min-content.
        const std::string& minMainProp = isRow ? "min-width" : "min-height";
        const std::string& minMainVal = styleVal(cs, minMainProp);
        bool minMainAuto = (minMainVal == "auto" || minMainVal.empty());
        if (isRow) {
            item.minMain = minMainAuto ? -1.0f : resolveDim(minMainVal, mainAvailable, childFontSize);
            item.maxMain = resolveDim(styleVal(cs, "max-width"), mainAvailable, childFontSize);
        } else {
            item.minMain = minMainAuto ? -1.0f : resolveDim(minMainVal, mainAvailable, childFontSize);
            item.maxMain = resolveDim(styleVal(cs, "max-height"), mainAvailable, childFontSize);
        }
        if (minMainAuto && isRow) {
            const std::string& overflow = styleVal(cs, "overflow");
            if (overflow == "visible" || overflow.empty()) {
                // Auto-min = min-content on the main axis (plus border/padding edges).
                float minContent = computeMinContentWidth(item.node, metrics);
                float ph = resolveLength(styleVal(cs, "padding-left"), mainAvailable, childFontSize) +
                           resolveLength(styleVal(cs, "padding-right"), mainAvailable, childFontSize);
                float bh = 0;
                const char* sides[] = {"left", "right"};
                for (auto* s : sides) {
                    if (styleVal(cs, std::string("border-") + s + "-style") != "none")
                        bh += resolveLength(styleVal(cs, std::string("border-") + s + "-width"), mainAvailable, childFontSize);
                }
                item.minMain = minContent + ph + bh;
                // CSS Flexbox §4.5: the automatic minimum size is the smaller of
                // the content size suggestion and the "specified size suggestion"
                // — the item's definite main size *property* (width), if any.
                // flex-basis does NOT provide a specified size suggestion: an
                // item with `flex: 0 0 240px` and wider content is still floored
                // at its min-content size (Chromium behavior).
                const std::string& wProp = styleVal(cs, "width");
                if (!isIntrinsicSizingKeyword(wProp)) {
                    float specMain = resolveDim(wProp, mainAvailable, childFontSize);
                    if (specMain >= 0) {
                        if (styleVal(cs, "box-sizing") != "border-box")
                            specMain += ph + bh;
                        if (item.minMain > specMain) item.minMain = specMain;
                    }
                }
                // In all cases, the automatic minimum is clamped by the max
                // main size property.
                if (item.maxMain >= 0 && item.minMain > item.maxMain)
                    item.minMain = item.maxMain;
            }
        } else if (minMainAuto && !isRow) {
            // Column flex: the automatic minimum on the main (block) axis is the
            // item's content-based min-content height. Unlike the inline axis it
            // depends on the item's definite cross size, so it can't be measured
            // until the item is laid out — defer to the hypothetical-size loop.
            // (Overflow != visible resolves auto-min to 0, so items with their own
            // scroll container are still allowed to shrink.)
            const std::string& overflow = styleVal(cs, "overflow");
            if (overflow == "visible" || overflow.empty())
                item.colAutoMinPending = true;
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
            // For border-box items, a flex-basis of 0 must still reserve space for
            // padding+border so flex-grow distributes the content area proportionally.
            if (item.hypotheticalMain == 0 && !item.node->isTextNode()) {
                auto& cs = item.node->computedStyle();
                if (styleVal(cs, "box-sizing") == "border-box") {
                    float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
                    if (childFontSize <= 0) childFontSize = fontSize;
                    float edges = 0;
                    if (isRow) {
                        edges += resolveLength(styleVal(cs, "padding-left"), mainAvailable, childFontSize) +
                                 resolveLength(styleVal(cs, "padding-right"), mainAvailable, childFontSize);
                        if (styleVal(cs, "border-left-style") != "none")
                            edges += resolveLength(styleVal(cs, "border-left-width"), mainAvailable, childFontSize);
                        if (styleVal(cs, "border-right-style") != "none")
                            edges += resolveLength(styleVal(cs, "border-right-width"), mainAvailable, childFontSize);
                    } else {
                        edges += resolveLength(styleVal(cs, "padding-top"), mainAvailable, childFontSize) +
                                 resolveLength(styleVal(cs, "padding-bottom"), mainAvailable, childFontSize);
                        if (styleVal(cs, "border-top-style") != "none")
                            edges += resolveLength(styleVal(cs, "border-top-width"), mainAvailable, childFontSize);
                        if (styleVal(cs, "border-bottom-style") != "none")
                            edges += resolveLength(styleVal(cs, "border-bottom-width"), mainAvailable, childFontSize);
                    }
                    item.hypotheticalMain = edges;
                }
            }
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
                // Resolve the deferred column auto-min: with the cross size definite
                // (containerMain), the item's laid-out outer height is its block-axis
                // content-min, so it must not be shrunk below it in a height-limited
                // container (content overflows and scrolls instead of collapsing).
                if (item.colAutoMinPending && item.hypotheticalMain > item.minMain) {
                    item.minMain = item.hypotheticalMain;
                    // §4.5: the automatic minimum is clamped by the max main
                    // size property (same rule as the row axis above).
                    if (item.maxMain >= 0 && item.minMain > item.maxMain)
                        item.minMain = item.maxMain;
                }
            }
        }
        // The flex base size is the unclamped size; the hypothetical main
        // size is the base clamped by min/max. §9.7 distributes free space
        // over base sizes, so keep both.
        item.baseMain = item.hypotheticalMain;
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

        // Resolve flexible lengths (CSS Flexbox §9.7). The used flex factor is
        // chosen by comparing the sum of the *hypothetical* outer sizes to the
        // container, but free space is then distributed over the *flex base
        // sizes* of the unfrozen items, clamping violations and redistributing
        // iteratively. Distributing from base sizes (not min/max-clamped
        // hypotheticals) matches the spec and Chromium: an item's automatic
        // minimum floors its final size but must not inflate its share of the
        // free space.
        bool growing = totalHypothetical + totalMargins + totalGaps < mainAvailable;

        // Freeze inflexible items at their hypothetical size: zero flex
        // factor, or a min/max clamp that already binds against the flex
        // direction (base > hypothetical when growing, base < when shrinking).
        for (auto* item : line.items) {
            item->finalMain = item->hypotheticalMain;
            float factor = growing ? item->flexGrow : item->flexShrink;
            item->frozen = (factor == 0) ||
                (growing && item->baseMain > item->hypotheticalMain) ||
                (!growing && item->baseMain < item->hypotheticalMain);
        }

        for (size_t iter = 0; iter <= line.items.size(); iter++) {
            float freeSpace = mainAvailable - totalMargins - totalGaps;
            float totalFactor = 0, totalScaled = 0;
            bool anyUnfrozen = false;
            for (auto* item : line.items) {
                if (item->frozen) {
                    freeSpace -= item->finalMain;
                } else {
                    freeSpace -= item->baseMain;
                    totalFactor += growing ? item->flexGrow : item->flexShrink;
                    totalScaled += item->flexShrink * item->baseMain;
                    anyUnfrozen = true;
                }
            }
            if (!anyUnfrozen) break;

            // §9.7.4.b: flex factors summing below 1 consume only that
            // fraction of the free space.
            float used = freeSpace;
            if (totalFactor > 0 && totalFactor < 1) used *= totalFactor;

            float totalViolation = 0;
            for (auto* item : line.items) {
                if (item->frozen) continue;
                float target = item->baseMain;
                if (growing) {
                    if (used > 0 && totalFactor > 0)
                        target += used * (item->flexGrow / std::max(totalFactor, 1.0f));
                } else {
                    // Shrink proportional to the scaled flex shrink factor
                    // (shrink × base) so larger items give up more.
                    if (used < 0 && totalScaled > 0)
                        target += used * ((item->flexShrink * item->baseMain) / totalScaled);
                }
                float clamped = target;
                if (item->maxMain >= 0 && clamped > item->maxMain) clamped = item->maxMain;
                if (clamped < item->minMain) clamped = item->minMain;
                if (clamped < 0) clamped = 0;
                item->finalMain = clamped;
                totalViolation += clamped - target;
            }

            if (totalViolation > 0.0001f) {
                // Min violations win: freeze the items clamped upward.
                for (auto* item : line.items)
                    if (!item->frozen && item->finalMain > item->minMain - 0.0001f &&
                        item->finalMain <= item->minMain + 0.0001f)
                        item->frozen = true;
            } else if (totalViolation < -0.0001f) {
                // Max violations win: freeze the items clamped downward.
                for (auto* item : line.items)
                    if (!item->frozen && item->maxMain >= 0 &&
                        item->finalMain >= item->maxMain - 0.0001f)
                        item->frozen = true;
            } else {
                break;  // all targets fit their min/max — done
            }
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
                // If this item has no explicit height and the container has a
                // definite cross size, pre-set contentRect.height to the stretch
                // height so the inner layout (e.g. a CSS Grid with 1fr rows or
                // a nested flex column) sees a definite height up front rather
                // than collapsing to content size.
                {
                    const std::string& itemHVal = styleVal(cs, "height");
                    bool itemAutoH = (itemHVal == "auto" || itemHVal.empty());
                    const std::string& selfAlign = styleVal(cs, "align-self");
                    const std::string& effAlign =
                        (selfAlign == "auto" || selfAlign.empty()) ? alignItems : selfAlign;
                    bool willStretch = itemAutoH &&
                        (effAlign == "stretch" || effAlign == "normal" || effAlign.empty());
                    if (willStretch) {
                        // Container's resolved content height (cross axis).
                        float specHc = resolveDim(styleVal(style, "height"), node->availableHeight, fontSize);
                        float containerCrossH = -1.0f;
                        if (specHc >= 0) {
                            containerCrossH = (styleVal(style, "box-sizing") == "border-box")
                                ? specHc - paddingV - borderV : specHc;
                        } else if (node->box.contentRect.height > 0) {
                            containerCrossH = node->box.contentRect.height;
                        }
                        if (containerCrossH > 0) {
                            float pad = resolveLength(styleVal(cs, "padding-top"), mainAvailable, childFontSize) +
                                        resolveLength(styleVal(cs, "padding-bottom"), mainAvailable, childFontSize);
                            float bor = 0;
                            if (styleVal(cs, "border-top-style") != "none")
                                bor += resolveLength(styleVal(cs, "border-top-width"), mainAvailable, childFontSize);
                            if (styleVal(cs, "border-bottom-style") != "none")
                                bor += resolveLength(styleVal(cs, "border-bottom-width"), mainAvailable, childFontSize);
                            float stretchH = containerCrossH - pad - bor;
                            if (stretchH > 0) {
                                item->node->availableHeight = containerCrossH;
                                item->node->box.contentRect.height = stretchH;
                            }
                        }
                    }
                }
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
                // Pass allocated height so nested column-flex children know their constraint.
                item->node->availableHeight = item->finalMain;
                // Pre-resolve padding/border so we can pre-set contentRect.height —
                // the inner layout (flex/block) needs to see the grown height for
                // cross-axis alignment (align-items, vertical centering, etc.)
                // when it's larger than the item's specified height.
                auto& cs = item->node->computedStyle();
                float ifs = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
                if (ifs <= 0) ifs = fontSize;
                float padV = resolveLength(styleVal(cs, "padding-top"), mainAvailable, ifs) +
                             resolveLength(styleVal(cs, "padding-bottom"), mainAvailable, ifs);
                float borV = 0;
                if (styleVal(cs, "border-top-style") != "none")
                    borV += resolveLength(styleVal(cs, "border-top-width"), mainAvailable, ifs);
                if (styleVal(cs, "border-bottom-style") != "none")
                    borV += resolveLength(styleVal(cs, "border-bottom-width"), mainAvailable, ifs);
                float grownContentH = item->finalMain - padV - borV;
                if (grownContentH < 0) grownContentH = 0;
                item->node->box.contentRect.height = grownContentH;
                // Non-stretch cross alignment sizes an auto-width item to its
                // fit-content width (CSS Flexbox §9.4 hypothetical cross
                // size: min(max-content, container width)), not the full
                // container width; the alignment pass below then positions
                // the fitted box (center / flex-start / flex-end).
                float itemAvailW = containerMain;
                {
                    const std::string& selfAlign = styleVal(cs, "align-self");
                    const std::string& effAlign =
                        (selfAlign == "auto" || selfAlign.empty()) ? alignItems : selfAlign;
                    bool stretches = (effAlign == "stretch" || effAlign == "normal" ||
                                      effAlign.empty());
                    const std::string& wVal = styleVal(cs, "width");
                    if (!stretches && (wVal.empty() || wVal == "auto")) {
                        float maxC = computeMaxContentWidth(item->node, metrics);
                        float padH = resolveLength(styleVal(cs, "padding-left"), mainAvailable, ifs) +
                                     resolveLength(styleVal(cs, "padding-right"), mainAvailable, ifs);
                        float borH = 0;
                        if (styleVal(cs, "border-left-style") != "none")
                            borH += resolveLength(styleVal(cs, "border-left-width"), mainAvailable, ifs);
                        if (styleVal(cs, "border-right-style") != "none")
                            borH += resolveLength(styleVal(cs, "border-right-width"), mainAvailable, ifs);
                        float marH = resolveLength(styleVal(cs, "margin-left"), mainAvailable, ifs) +
                                     resolveLength(styleVal(cs, "margin-right"), mainAvailable, ifs);
                        float fit = maxC + padH + borH + marH;
                        if (fit < itemAvailW) itemAvailW = fit;
                    }
                }
                layoutNode(item->node, itemAvailW, metrics);
                // Re-apply (block/flex inner layout may have overwritten) — the
                // flex contract is that the item is finalMain on the main axis.
                item->node->box.contentRect.height = grownContentH;
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
            // If an outer pass grew this container's content height beyond its
            // specified height (flex item with flex-grow or stretched abs box),
            // honor the grown height for cross-axis alignment so children center
            // within the actual occupied area.
            if (node->box.contentRect.height > crossAvailable)
                crossAvailable = node->box.contentRect.height;
        } else if (node->box.contentRect.height > 0) {
            // Style height is auto, but the container was already sized by an
            // outer pass (e.g. position:absolute with inset:0, or stretched by
            // an enclosing flex). Use that resolved height so align-items can
            // place items in the cross axis instead of collapsing to 0.
            crossAvailable = node->box.contentRect.height;
        }
    } else {
        crossAvailable = containerMain; // for column flex, cross = width
    }

    // CSS Flexbox §9.4: if the flex container is single-line and has a
    // definite cross size, the cross size of the (single) flex line is the
    // container's inner cross size. align-items must then center within that
    // full size rather than within the largest item's outer cross size — even
    // if that size is smaller than the largest item (items can overflow).
    if (lines.size() == 1 && crossAvailable >= 0) {
        lines[0].crossSize = crossAvailable;
        totalLineCross = crossAvailable;
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
        } else if (alignContent == "stretch" || alignContent == "normal" || alignContent.empty()) {
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
    float maxMainExtent = 0;  // track max main-axis extent across all lines
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
            } else if (align == "stretch" || align == "normal") {
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

        if (mainCursor > maxMainExtent) maxMainExtent = mainCursor;
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
        // For row flex, height = cross extent. For column flex, height = main extent.
        if (isRow) {
            node->box.contentRect.height = crossCursor > 0 ? crossCursor - crossGapAdjusted : 0;
        } else {
            node->box.contentRect.height = maxMainExtent;
        }
    }

    // Store natural height before clamping (for scroll extent calculation).
    // For row flex, natural height = cross extent. For column flex, natural height = main extent.
    float naturalH;
    if (isRow) {
        naturalH = crossCursor > 0 ? crossCursor - crossGapAdjusted : 0;
    } else {
        naturalH = maxMainExtent;
    }
    node->box.naturalHeight = std::max(naturalH, node->box.contentRect.height);

    // Apply min/max-height constraints (same as block layout)
    float minH = resolveDim(styleVal(style, "min-height"), node->availableHeight, fontSize);
    float maxH = resolveDim(styleVal(style, "max-height"), node->availableHeight, fontSize);
    if (minH >= 0.0f && node->box.contentRect.height < minH) node->box.contentRect.height = minH;
    if (maxH >= 0.0f && node->box.contentRect.height > maxH) node->box.contentRect.height = maxH;

}

} // namespace htmlayout::layout
