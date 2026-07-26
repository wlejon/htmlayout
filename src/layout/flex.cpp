#include "layout/flex.h"
#include "layout/formatting_context.h"
#include "layout/text.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
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
    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(node, kMarginProps, availableWidth, fontSize);
    node->box.padding = resolveEdges(node, kPaddingProps, availableWidth, fontSize);
    node->box.border = resolveBorders(node, availableWidth, fontSize);

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;

    // Container dimensions
    float specW = resolveDim(styleVal(node, Prop::Width), availableWidth, fontSize);
    float containerMain;
    if (specW >= 0) {
        if (styleVal(node, Prop::BoxSizing) == "border-box")
            containerMain = specW - paddingH - borderH;
        else
            containerMain = specW;
        if (containerMain < 0) containerMain = 0;
    } else {
        containerMain = availableWidth - node->box.margin.left - node->box.margin.right - paddingH - borderH;
        if (containerMain < 0) containerMain = 0;
    }

    // Under `box-sizing: border-box` every specified box dimension — width,
    // min-width, max-width, height — names the border box, while the sizes
    // tracked from here on are content sizes. Converting once keeps the two from
    // being silently compared to each other.
    const bool borderBox = styleVal(node, Prop::BoxSizing) == "border-box";
    auto toContent = [&](float v, float edges) {
        return (borderBox && v >= 0) ? std::max(0.0f, v - edges) : v;
    };

    // Apply min/max-width constraints.
    //
    // These have to be de-border-boxed first. `min-width: 30px` on a button with
    // 8px of padding means the *border* box is at least 30px, so the content box
    // is at least 14px. Clamping the content box to 30 instead lays the items out
    // across a main axis wider than the box holding them, and justify-content
    // then centres them in that phantom width — a short label in a padded button
    // ends up flush against the right border, or past it.
    float minW = toContent(resolveDim(styleVal(node, Prop::MinWidth), availableWidth, fontSize),
                           paddingH + borderH);
    float maxW = toContent(resolveDim(styleVal(node, Prop::MaxWidth), availableWidth, fontSize),
                           paddingH + borderH);
    if (minW >= 0 && containerMain < minW) containerMain = minW;
    if (maxW >= 0 && containerMain > maxW) containerMain = maxW;

    // Flex properties
    const std::string& flexDir = styleVal(node, Prop::FlexDirection);
    const std::string& flexWrap = styleVal(node, Prop::FlexWrap);
    const std::string& justifyContent = styleVal(node, Prop::JustifyContent);
    const std::string& alignItems = styleVal(node, Prop::AlignItems);
    const std::string& alignContent = styleVal(node, Prop::AlignContent);

    bool isRow = (flexDir == "row" || flexDir == "row-reverse" || flexDir.empty());
    bool isReverse = (flexDir == "row-reverse" || flexDir == "column-reverse");
    bool isWrap = (flexWrap == "wrap" || flexWrap == "wrap-reverse");

    // Under `direction: rtl` a row flex container's main axis runs right-to-left,
    // so the main-start edge is the right edge and items pack from the right —
    // positionally identical to row-reverse. XOR the two so `row`+rtl reverses
    // and `row-reverse`+rtl cancels back to left-to-right. (rtl affects only the
    // cross axis for column containers, which is left as-is.)
    if (isRow && styleVal(node, Prop::Direction) == "rtl")
        isReverse = !isReverse;

    float mainAvailable = isRow ? containerMain
                                : toContent(resolveDim(styleVal(node, Prop::Height),
                                                       node->availableHeight, fontSize),
                                            paddingV + borderV);
    // Column flex: clamp the definite main size by min/max-height, mirroring the
    // min/max-width clamp applied to containerMain (the row main size) above.
    // Without this a `height:88vh; max-height:660px` column container distributes
    // free space against the uncapped 88vh, over-growing a flexible scroll body
    // past the clamped box — a fixed footer is pushed out of view and the
    // scrollbar draws for the oversized region. A percentage min/max-height
    // against an indefinite containing block is 'none'/'auto' (same guard as the
    // box-height clamp near the end of this function), not 0.
    if (!isRow && mainAvailable >= 0) {
        auto pctIndefiniteH = [&](const std::string& v) {
            return node->availableHeight <= 0.0f && !v.empty() && v.back() == '%';
        };
        const std::string& maxHVal = styleVal(node, Prop::MaxHeight);
        const std::string& minHVal = styleVal(node, Prop::MinHeight);
        float maxH = toContent(pctIndefiniteH(maxHVal) ? -1.0f
                                   : resolveDim(maxHVal, node->availableHeight, fontSize),
                               paddingV + borderV);
        float minH = toContent(pctIndefiniteH(minHVal) ? -1.0f
                                   : resolveDim(minHVal, node->availableHeight, fontSize),
                               paddingV + borderV);
        if (maxH >= 0 && mainAvailable > maxH) mainAvailable = maxH;
        if (minH >= 0 && mainAvailable < minH) mainAvailable = minH;
    }
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

    float gapMain = resolveLength(
        styleVal(node, isRow ? Prop::ColumnGap : Prop::RowGap), mainAvailable, fontSize);
    float gapCross = resolveLength(
        styleVal(node, isRow ? Prop::RowGap : Prop::ColumnGap), mainAvailable, fontSize);

    // Resolve definite cross size for percentage height propagation to children
    float crossSpecH = resolveDim(styleVal(node, Prop::Height), node->availableHeight, fontSize);
    float childAvailableHeight = 0.0f;
    if (isRow && crossSpecH >= 0) {
        if (styleVal(node, Prop::BoxSizing) == "border-box")
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

            const std::string& fontFamily = styleVal(node, Prop::FontFamily);
            const std::string& fontWeight = styleVal(node, Prop::FontWeight);
            // Measure the text-transformed glyphs (matches paint + breakTextIntoRuns).
            std::string shaped = applyTextTransform(std::string(text), styleVal(node, Prop::TextTransform));
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
        if (styleVal(child, Prop::Display) == "none") {
            child->box = LayoutBox{};
            continue;
        }
        const std::string& childPos = styleVal(child, Prop::Position);
        if (childPos == "absolute" || childPos == "fixed") continue;

        FlexItem item;
        item.node = child;
        float childFontSize = resolveLength(styleVal(child, Prop::FontSize), fontSize, fontSize);
        if (childFontSize <= 0) childFontSize = fontSize;

        item.flexGrow = resolveLength(styleVal(child, Prop::FlexGrow), 0, childFontSize);
        item.flexShrink = resolveLength(styleVal(child, Prop::FlexShrink), 0, childFontSize);
        if (item.flexShrink < 0) item.flexShrink = 1.0f;
        item.order = static_cast<int>(resolveLength(styleVal(child, Prop::Order), 0, childFontSize));

        // Re-resolve the item's margins from style every pass (auto → 0, per
        // §9.7 auto margins are treated as 0 while sizing). A previous layout
        // pass may have written *resolved* main-axis auto margins into
        // box.margin (they absorb free space at positioning time); counting
        // those stale values as real margins here would eat into this pass's
        // free space and shrink items that fit.
        child->box.margin = resolveEdges(child, kMarginProps, containerMain, childFontSize);

        // Resolve flex-basis. flex-basis represents the outer (border-box) main
        // size of the item — the rest of flex layout subtracts padding/border to
        // recover content size. When the basis comes from a width/height (or an
        // explicit length on flex-basis) and box-sizing is content-box, the
        // specified value is content size, so we must add padding+border to
        // convert to the outer main size.
        const std::string& basis = styleVal(child, Prop::FlexBasis);
        bool basisFromMainDim = false;
        if (basis == "auto" || basis.empty()) {
            // Use width/height as basis
            float dim = resolveDim(styleVal(child, isRow ? Prop::Width : Prop::Height),
                                   mainAvailable, childFontSize);
            item.flexBasis = dim >= 0 ? dim : -1.0f;
            basisFromMainDim = (dim >= 0);
        } else {
            item.flexBasis = resolveLength(basis, mainAvailable, childFontSize);
            basisFromMainDim = (item.flexBasis >= 0);
        }
        if (basisFromMainDim && styleVal(child, Prop::BoxSizing) != "border-box") {
            float edges = 0;
            if (isRow) {
                edges += resolveLength(styleVal(child, Prop::PaddingLeft), mainAvailable, childFontSize) +
                         resolveLength(styleVal(child, Prop::PaddingRight), mainAvailable, childFontSize);
                if (styleVal(child, Prop::BorderLeftStyle) != "none")
                    edges += resolveLength(styleVal(child, Prop::BorderLeftWidth), mainAvailable, childFontSize);
                if (styleVal(child, Prop::BorderRightStyle) != "none")
                    edges += resolveLength(styleVal(child, Prop::BorderRightWidth), mainAvailable, childFontSize);
            } else {
                edges += resolveLength(styleVal(child, Prop::PaddingTop), mainAvailable, childFontSize) +
                         resolveLength(styleVal(child, Prop::PaddingBottom), mainAvailable, childFontSize);
                if (styleVal(child, Prop::BorderTopStyle) != "none")
                    edges += resolveLength(styleVal(child, Prop::BorderTopWidth), mainAvailable, childFontSize);
                if (styleVal(child, Prop::BorderBottomStyle) != "none")
                    edges += resolveLength(styleVal(child, Prop::BorderBottomWidth), mainAvailable, childFontSize);
            }
            item.flexBasis += edges;
        }

        // Resolve min/max on main axis.
        // CSS Flexbox §4.5: min-width/min-height: auto on a flex item resolves to
        // the item's min-content size on the main axis (when overflow is visible),
        // so unbreakable content (long words) is not shrunk below its min-content.
        const std::string& minMainVal =
            styleVal(child, isRow ? Prop::MinWidth : Prop::MinHeight);
        bool minMainAuto = (minMainVal == "auto" || minMainVal.empty());
        if (isRow) {
            item.minMain = minMainAuto ? -1.0f : resolveDim(minMainVal, mainAvailable, childFontSize);
            item.maxMain = resolveDim(styleVal(child, Prop::MaxWidth), mainAvailable, childFontSize);
        } else {
            item.minMain = minMainAuto ? -1.0f : resolveDim(minMainVal, mainAvailable, childFontSize);
            item.maxMain = resolveDim(styleVal(child, Prop::MaxHeight), mainAvailable, childFontSize);
        }
        if (minMainAuto && isRow) {
            // §4.5: the automatic minimum resolves to 0 when the item is a scroll
            // container on the MAIN axis. For row flex that is the inline axis, so
            // check overflow-x (the `overflow` shorthand expands into it too);
            // reading the shorthand alone misses `overflow-x`/`overflow-y` longhands.
            const std::string& overflow = styleVal(child, Prop::OverflowX);
            if (overflow == "visible" || overflow.empty()) {
                // Auto-min = min-content on the main axis (plus border/padding edges).
                float minContent = computeMinContentWidth(item.node, metrics);
                float ph = resolveLength(styleVal(child, Prop::PaddingLeft), mainAvailable, childFontSize) +
                           resolveLength(styleVal(child, Prop::PaddingRight), mainAvailable, childFontSize);
                Edges bEdges = resolveBorders(item.node, mainAvailable, childFontSize);
                float bh = bEdges.left + bEdges.right;
                item.minMain = minContent + ph + bh;
                // CSS Flexbox §4.5: the automatic minimum size is the smaller of
                // the content size suggestion and the "specified size suggestion"
                // — the item's definite main size *property* (width), if any.
                // flex-basis does NOT provide a specified size suggestion: an
                // item with `flex: 0 0 240px` and wider content is still floored
                // at its min-content size (Chromium behavior).
                const std::string& wProp = styleVal(child, Prop::Width);
                if (!isIntrinsicSizingKeyword(wProp)) {
                    float specMain = resolveDim(wProp, mainAvailable, childFontSize);
                    if (specMain >= 0) {
                        if (styleVal(child, Prop::BoxSizing) != "border-box")
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
            // scroll container are still allowed to shrink.) The main axis here is
            // the block axis, so check overflow-y (the `overflow` shorthand expands
            // into it too); reading the shorthand alone misses the longhand form.
            const std::string& overflow = styleVal(child, Prop::OverflowY);
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
                if (styleVal(item.node, Prop::BoxSizing) == "border-box") {
                    float childFontSize = resolveLength(styleVal(item.node, Prop::FontSize), fontSize, fontSize);
                    if (childFontSize <= 0) childFontSize = fontSize;
                    float edges = 0;
                    if (isRow) {
                        edges += resolveLength(styleVal(item.node, Prop::PaddingLeft), mainAvailable, childFontSize) +
                                 resolveLength(styleVal(item.node, Prop::PaddingRight), mainAvailable, childFontSize);
                        if (styleVal(item.node, Prop::BorderLeftStyle) != "none")
                            edges += resolveLength(styleVal(item.node, Prop::BorderLeftWidth), mainAvailable, childFontSize);
                        if (styleVal(item.node, Prop::BorderRightStyle) != "none")
                            edges += resolveLength(styleVal(item.node, Prop::BorderRightWidth), mainAvailable, childFontSize);
                    } else {
                        edges += resolveLength(styleVal(item.node, Prop::PaddingTop), mainAvailable, childFontSize) +
                                 resolveLength(styleVal(item.node, Prop::PaddingBottom), mainAvailable, childFontSize);
                        if (styleVal(item.node, Prop::BorderTopStyle) != "none")
                            edges += resolveLength(styleVal(item.node, Prop::BorderTopWidth), mainAvailable, childFontSize);
                        if (styleVal(item.node, Prop::BorderBottomStyle) != "none")
                            edges += resolveLength(styleVal(item.node, Prop::BorderBottomWidth), mainAvailable, childFontSize);
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
                float childFontSize = resolveLength(styleVal(item.node, Prop::FontSize), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;
                float ph = resolveLength(styleVal(item.node, Prop::PaddingLeft), mainAvailable, childFontSize) +
                           resolveLength(styleVal(item.node, Prop::PaddingRight), mainAvailable, childFontSize);
                Edges bEdges = resolveBorders(item.node, mainAvailable, childFontSize);
                float bh = bEdges.left + bEdges.right;
                item.hypotheticalMain = intrinsic + ph + bh;
                if (item.hypotheticalMain > mainAvailable)
                    item.hypotheticalMain = mainAvailable;
                // Lay out at this width to compute cross size — but only if the
                // item actually changed. The measure's downstream inputs (the
                // hypothetical main above, margins re-resolved from style for
                // every item each pass) don't come from this layout, so a clean,
                // previously-laid item can skip straight to the final layout,
                // where its cached subtree is checked against the final inputs.
                if (item.node->box.dirty || std::isnan(item.node->cachedAvailWidth))
                    layoutNode(item.node, item.hypotheticalMain, metrics);
            } else {
                // Column: the measure produces the item's laid-out outer height
                // at the container's inner width. Cache that scalar per node so
                // a clean item doesn't re-lay its subtree to re-derive it.
                if (item.node->box.dirty || !(item.node->measuredAtW == containerMain)) {
                    layoutNode(item.node, containerMain, metrics);
                    item.node->measuredAtW = containerMain;
                    item.node->measuredOuterMain =
                        item.node->box.contentRect.height +
                        item.node->box.padding.top + item.node->box.padding.bottom +
                        item.node->box.border.top + item.node->box.border.bottom;
                }
                item.hypotheticalMain = item.node->measuredOuterMain;
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

    // wrap-reverse flips the cross axis: the first line lands at the cross-end
    // edge. Reversing the stacking order here and swapping the flex-start /
    // flex-end interpretation of align-content (and per-item alignment below)
    // implements that flip.
    const bool isWrapReverse = (flexWrap == "wrap-reverse");
    if (isWrapReverse) std::reverse(lines.begin(), lines.end());
    auto flipCrossAlign = [&](const std::string& a) -> std::string {
        if (!isWrapReverse) return a;
        if (a == "flex-start" || a == "start") return "flex-end";
        if (a == "flex-end" || a == "end") return "flex-start";
        // wrap-reverse packs lines at the cross-end edge when alignment
        // defaults to the start side; the default for align-content is
        // handled at its use site (empty/normal stays stretch).
        return a;
    };

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
            float childFontSize = resolveLength(styleVal(item->node, Prop::FontSize), fontSize, fontSize);
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
                // than collapsing to content size. Computed here but written
                // below, after the item has been claimed for this layout pass —
                // see beginLayoutNode.
                float preStretchH = -1.0f;
                {
                    const std::string& itemHVal = styleVal(item->node, Prop::Height);
                    bool itemAutoH = (itemHVal == "auto" || itemHVal.empty());
                    const std::string& selfAlign = styleVal(item->node, Prop::AlignSelf);
                    const std::string& effAlign =
                        (selfAlign == "auto" || selfAlign.empty()) ? alignItems : selfAlign;
                    bool willStretch = itemAutoH &&
                        (effAlign == "stretch" || effAlign == "normal" || effAlign.empty());
                    // Container's resolved content height (cross axis).
                    float specHc = resolveDim(styleVal(node, Prop::Height), node->availableHeight, fontSize);
                    float containerCrossH = -1.0f;
                    if (specHc >= 0) {
                        containerCrossH = (styleVal(node, Prop::BoxSizing) == "border-box")
                            ? specHc - paddingV - borderV : specHc;
                    } else if (node->box.contentRect.height > 0) {
                        containerCrossH = node->box.contentRect.height;
                    }
                    if (containerCrossH > 0) {
                        // Propagate the container's definite cross size as the
                        // item's availableHeight so a *percentage* height on the
                        // item (e.g. a non-stretch <iframe height:100%> aligned
                        // to center) resolves against it instead of collapsing to
                        // 0. This is independent of stretch — any item needs a
                        // definite CB height to resolve percentage heights.
                        item->node->availableHeight = containerCrossH;
                        if (willStretch) {
                            // Auto-height stretch item: also pre-set contentRect
                            // height to the stretch height so the inner layout
                            // (e.g. a CSS Grid with 1fr rows or a nested flex
                            // column) sees a definite height up front rather than
                            // collapsing to content size.
                            float pad = resolveLength(styleVal(item->node, Prop::PaddingTop), mainAvailable, childFontSize) +
                                        resolveLength(styleVal(item->node, Prop::PaddingBottom), mainAvailable, childFontSize);
                            float bor = 0;
                            if (styleVal(item->node, Prop::BorderTopStyle) != "none")
                                bor += resolveLength(styleVal(item->node, Prop::BorderTopWidth), mainAvailable, childFontSize);
                            if (styleVal(item->node, Prop::BorderBottomStyle) != "none")
                                bor += resolveLength(styleVal(item->node, Prop::BorderBottomWidth), mainAvailable, childFontSize);
                            preStretchH = containerCrossH - pad - bor;
                        }
                    }
                }
                // The flex algorithm resolved the used main size (finalMain,
                // border-box). Pre-resolve padding/border and pass the flexed
                // CONTENT width into the inner layout via overrideContentWidth
                // so the item's children are laid out against the flexed size
                // rather than the item's specified style width (which flexing
                // may have grown or shrunk away from).
                {
                    float ipadH = resolveLength(styleVal(item->node, Prop::PaddingLeft), mainAvailable, childFontSize) +
                                  resolveLength(styleVal(item->node, Prop::PaddingRight), mainAvailable, childFontSize);
                    float iborH = 0;
                    if (styleVal(item->node, Prop::BorderLeftStyle) != "none")
                        iborH += resolveLength(styleVal(item->node, Prop::BorderLeftWidth), mainAvailable, childFontSize);
                    if (styleVal(item->node, Prop::BorderRightStyle) != "none")
                        iborH += resolveLength(styleVal(item->node, Prop::BorderRightWidth), mainAvailable, childFontSize);
                    float flexedContentW = contentWidth - ipadH - iborH;
                    if (flexedContentW < 0) flexedContentW = 0;
                    item->node->overrideContentWidth = flexedContentW;
                }
                // Claim the item before writing the stretch height into its box:
                // that height is an *input* to its inner layout, and layoutNode()
                // clears a node's box on its first visit of a pass. An item with a
                // definite flex-basis (`flex: 1`) is never measured, so this final
                // layout IS its first visit. beginLayoutNode returns false when the
                // item's cached subtree is still valid — then nothing is laid out
                // and the writes below simply re-state what the box already holds.
                bool laidNow = beginLayoutNode(item->node, contentWidth);
                if (laidNow) {
                    if (preStretchH > 0) item->node->box.contentRect.height = preStretchH;
                    layoutNode(item->node, contentWidth, metrics);
                }
                item->node->overrideContentWidth = -1.0f;
                item->node->box.contentRect.width = contentWidth -
                    item->node->box.padding.left - item->node->box.padding.right -
                    item->node->box.border.left - item->node->box.border.right;
                if (item->node->box.contentRect.width < 0) item->node->box.contentRect.width = 0;

                // A reused box holds last pass's FINAL height — align stretch
                // included — so deriving the cross size from it would feed the
                // previous line height back in and lines could never shrink.
                // Use the cross size recorded when the item was last laid.
                if (laidNow || item->node->flexNaturalCross < 0) {
                    item->crossSize = item->node->box.contentRect.height +
                        item->node->box.padding.top + item->node->box.padding.bottom +
                        item->node->box.border.top + item->node->box.border.bottom +
                        item->node->box.margin.top + item->node->box.margin.bottom;
                    item->node->flexNaturalCross = item->crossSize;
                } else {
                    item->crossSize = item->node->flexNaturalCross;
                }
            } else {
                // Main = height, cross = width
                // Pass allocated height so nested column-flex children know their constraint.
                item->node->availableHeight = item->finalMain;
                // Pre-resolve padding/border so we can pre-set contentRect.height —
                // the inner layout (flex/block) needs to see the grown height for
                // cross-axis alignment (align-items, vertical centering, etc.)
                // when it's larger than the item's specified height.
                auto& cs = item->node->computedStyle();
                float ifs = resolveLength(styleVal(item->node, Prop::FontSize), fontSize, fontSize);
                if (ifs <= 0) ifs = fontSize;
                float padV = resolveLength(styleVal(item->node, Prop::PaddingTop), mainAvailable, ifs) +
                             resolveLength(styleVal(item->node, Prop::PaddingBottom), mainAvailable, ifs);
                float borV = 0;
                if (styleVal(item->node, Prop::BorderTopStyle) != "none")
                    borV += resolveLength(styleVal(item->node, Prop::BorderTopWidth), mainAvailable, ifs);
                if (styleVal(item->node, Prop::BorderBottomStyle) != "none")
                    borV += resolveLength(styleVal(item->node, Prop::BorderBottomWidth), mainAvailable, ifs);
                float grownContentH = item->finalMain - padV - borV;
                if (grownContentH < 0) grownContentH = 0;
                // Non-stretch cross alignment sizes an auto-width item to its
                // fit-content width (CSS Flexbox §9.4 hypothetical cross
                // size: min(max-content, container width)), not the full
                // container width; the alignment pass below then positions
                // the fitted box (center / flex-start / flex-end).
                float itemAvailW = containerMain;
                {
                    const std::string& selfAlign = styleVal(item->node, Prop::AlignSelf);
                    const std::string& effAlign =
                        (selfAlign == "auto" || selfAlign.empty()) ? alignItems : selfAlign;
                    bool stretches = (effAlign == "stretch" || effAlign == "normal" ||
                                      effAlign.empty());
                    const std::string& wVal = styleVal(item->node, Prop::Width);
                    if (!stretches && (wVal.empty() || wVal == "auto")) {
                        float maxC = computeMaxContentWidth(item->node, metrics);
                        float padH = resolveLength(styleVal(item->node, Prop::PaddingLeft), mainAvailable, ifs) +
                                     resolveLength(styleVal(item->node, Prop::PaddingRight), mainAvailable, ifs);
                        float borH = 0;
                        if (styleVal(item->node, Prop::BorderLeftStyle) != "none")
                            borH += resolveLength(styleVal(item->node, Prop::BorderLeftWidth), mainAvailable, ifs);
                        if (styleVal(item->node, Prop::BorderRightStyle) != "none")
                            borH += resolveLength(styleVal(item->node, Prop::BorderRightWidth), mainAvailable, ifs);
                        float marH = resolveLength(styleVal(item->node, Prop::MarginLeft), mainAvailable, ifs) +
                                     resolveLength(styleVal(item->node, Prop::MarginRight), mainAvailable, ifs);
                        float fit = maxC + padH + borH + marH;
                        if (fit < itemAvailW) itemAvailW = fit;
                    }
                }
                // Claim the item before writing the grown height into its box: that
                // height is an *input* to its inner layout (it is what lets a nested
                // `flex: 1` or `1fr` descendant distribute the space), and layoutNode()
                // clears a node's box on its first visit of a pass. An item with a
                // definite flex-basis (`flex: 1`) is never measured, so this final
                // layout IS its first visit. beginLayoutNode returns false when the
                // item's cached subtree is still valid — then nothing is laid out and
                // the re-apply below simply re-states what the box already holds.
                bool laidNow = beginLayoutNode(item->node, itemAvailW);
                if (laidNow) {
                    item->node->box.contentRect.height = grownContentH;
                    layoutNode(item->node, itemAvailW, metrics);
                }
                // Re-apply (block/flex inner layout may have overwritten) — the
                // flex contract is that the item is finalMain on the main axis.
                item->node->box.contentRect.height = grownContentH;
                if (item->node->box.contentRect.height < 0) item->node->box.contentRect.height = 0;

                // Same as the row branch: a reused box's width may carry last
                // pass's cross stretch, so use the recorded natural cross size.
                if (laidNow || item->node->flexNaturalCross < 0) {
                    item->crossSize = item->node->box.contentRect.width +
                        item->node->box.padding.left + item->node->box.padding.right +
                        item->node->box.border.left + item->node->box.border.right +
                        item->node->box.margin.left + item->node->box.margin.right;
                    item->node->flexNaturalCross = item->crossSize;
                } else {
                    item->crossSize = item->node->flexNaturalCross;
                }
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
        float specH = resolveDim(styleVal(node, Prop::Height), node->availableHeight, fontSize);
        if (specH >= 0) {
            if (styleVal(node, Prop::BoxSizing) == "border-box")
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

        // Under wrap-reverse the start/end interpretation flips (lines were
        // already reversed, so "flex-start" must pack at the far edge).
        // Non-directional values (center/stretch/space-*) pass through.
        std::string effAlignContent = flipCrossAlign(alignContent);

        if (effAlignContent == "center") {
            crossOffset = freeCross / 2.0f;
        } else if (effAlignContent == "flex-end") {
            crossOffset = freeCross;
        } else if (effAlignContent == "space-between" && lines.size() > 1) {
            crossGapAdjusted = gapCross + freeCross / (lines.size() - 1);
        } else if (effAlignContent == "space-around" && !lines.empty()) {
            float lineGap = freeCross / lines.size();
            crossOffset = lineGap / 2.0f;
            crossGapAdjusted = gapCross + lineGap;
        } else if (effAlignContent == "space-evenly" && !lines.empty()) {
            float lineGap = freeCross / (lines.size() + 1);
            crossOffset = lineGap;
            crossGapAdjusted = gapCross + lineGap;
        } else if (effAlignContent == "stretch" || effAlignContent == "normal" || effAlignContent.empty()) {
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
                if (styleVal(item->node, Prop::MarginLeft) == "auto") autoMainMargins++;
                if (styleVal(item->node, Prop::MarginRight) == "auto") autoMainMargins++;
            } else {
                if (styleVal(item->node, Prop::MarginTop) == "auto") autoMainMargins++;
                if (styleVal(item->node, Prop::MarginBottom) == "auto") autoMainMargins++;
            }
        }

        // Distribute free space to auto margins (resolve them from 0 to computed value)
        if (autoMainMargins > 0 && freeMain > 0) {
            float perAutoMargin = freeMain / autoMainMargins;
            for (auto* item : line.items) {
                if (item->node->isTextNode()) continue;
                auto& cs = item->node->computedStyle();
                if (isRow) {
                    if (styleVal(item->node, Prop::MarginLeft) == "auto")
                        item->node->box.margin.left = perAutoMargin;
                    if (styleVal(item->node, Prop::MarginRight) == "auto")
                        item->node->box.margin.right = perAutoMargin;
                } else {
                    if (styleVal(item->node, Prop::MarginTop) == "auto")
                        item->node->box.margin.top = perAutoMargin;
                    if (styleVal(item->node, Prop::MarginBottom) == "auto")
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
                    hasCrossAutoMargin = (styleVal(item->node, Prop::MarginTop) == "auto" ||
                                          styleVal(item->node, Prop::MarginBottom) == "auto");
                } else {
                    hasCrossAutoMargin = (styleVal(item->node, Prop::MarginLeft) == "auto" ||
                                          styleVal(item->node, Prop::MarginRight) == "auto");
                }
            }

            // Cross-axis alignment
            const std::string& selfAlign = styleVal(item->node, Prop::AlignSelf);
            // wrap-reverse flips per-item cross alignment too (flex-start ↔ flex-end).
            const std::string align = flipCrossAlign(
                (selfAlign == "auto" || selfAlign.empty()) ? alignItems : selfAlign);

            float crossPos = crossCursor;
            if (hasCrossAutoMargin) {
                // Cross-axis auto margins absorb free space (override align-items/align-self)
                float freeCross = line.crossSize - item->crossSize;
                if (freeCross < 0) freeCross = 0;
                if (isRow) {
                    bool topAuto = (styleVal(item->node, Prop::MarginTop) == "auto");
                    bool bottomAuto = (styleVal(item->node, Prop::MarginBottom) == "auto");
                    if (topAuto && bottomAuto) {
                        item->node->box.margin.top = freeCross / 2.0f;
                        item->node->box.margin.bottom = freeCross / 2.0f;
                    } else if (topAuto) {
                        item->node->box.margin.top = freeCross;
                    } else {
                        item->node->box.margin.bottom = freeCross;
                    }
                } else {
                    bool leftAuto = (styleVal(item->node, Prop::MarginLeft) == "auto");
                    bool rightAuto = (styleVal(item->node, Prop::MarginRight) == "auto");
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
                float childFontSize = resolveLength(styleVal(item->node, Prop::FontSize), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;
                float itemBaseline = item->node->box.margin.top +
                    item->node->box.border.top + item->node->box.padding.top + childFontSize;

                // Find max baseline in this line for baseline-aligned items
                float maxBaseline = 0;
                for (auto* li : line.items) {
                    auto& lis = li->node->computedStyle();
                    const std::string& liSelf = styleVal(li->node, Prop::AlignSelf);
                    const std::string& liAlign = (liSelf == "auto" || liSelf.empty()) ? alignItems : liSelf;
                    if (liAlign == "baseline") {
                        float lfs = resolveLength(styleVal(li->node, Prop::FontSize), fontSize, fontSize);
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
                    const std::string& h = styleVal(item->node, Prop::Height);
                    if (h == "auto" || h.empty()) {
                        float stretchCross = line.crossSize -
                            item->node->box.margin.top - item->node->box.margin.bottom -
                            item->node->box.padding.top - item->node->box.padding.bottom -
                            item->node->box.border.top - item->node->box.border.bottom;
                        if (stretchCross > 0) item->node->box.contentRect.height = stretchCross;
                    }
                } else {
                    const std::string& w = styleVal(item->node, Prop::Width);
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
            const std::string& childPos = styleVal(item->node, Prop::Position);
            if (childPos == "relative" || childPos == "sticky") {
                float childFontSize = resolveLength(styleVal(item->node, Prop::FontSize), fontSize, fontSize);
                if (childFontSize <= 0) childFontSize = fontSize;
                const std::string& topVal = styleVal(item->node, Prop::Top);
                const std::string& leftVal = styleVal(item->node, Prop::Left);
                const std::string& bottomVal = styleVal(item->node, Prop::Bottom);
                const std::string& rightVal = styleVal(item->node, Prop::Right);

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

    float specH = resolveDim(styleVal(node, Prop::Height), node->availableHeight, fontSize);
    if (specH >= 0) {
        if (styleVal(node, Prop::BoxSizing) == "border-box")
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

    // Apply min/max-height constraints (same as block layout). A percentage
    // min/max-height against an indefinite containing-block height (ref <= 0) is
    // treated as 'none'/'auto' per CSS, not 0 — resolving to 0 would collapse
    // this flex container's box (see the matching guard in block.cpp).
    auto pctAgainstIndefiniteH = [&](const std::string& v) {
        return node->availableHeight <= 0.0f && !v.empty() && v.back() == '%';
    };
    const std::string& minHVal = styleVal(node, Prop::MinHeight);
    const std::string& maxHVal = styleVal(node, Prop::MaxHeight);
    float minH = pctAgainstIndefiniteH(minHVal) ? -1.0f
                 : resolveDim(minHVal, node->availableHeight, fontSize);
    float maxH = pctAgainstIndefiniteH(maxHVal) ? -1.0f
                 : resolveDim(maxHVal, node->availableHeight, fontSize);
    if (minH >= 0.0f && node->box.contentRect.height < minH) node->box.contentRect.height = minH;
    if (maxH >= 0.0f && node->box.contentRect.height > maxH) node->box.contentRect.height = maxH;

}

} // namespace htmlayout::layout
