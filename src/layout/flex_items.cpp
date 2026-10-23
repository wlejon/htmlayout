#include "layout/flex_items.h"
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

float resolveFlexDim(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

} // namespace

// CSS Flexbox §9.4 step 11: a stretched item's used cross size is definite,
// and its contents are laid out again against it — so a `flex: 1` or `1fr`
// inside a card stretched to a taller neighbour fills the card. Row flex
// only (the cross size is the height).
void relayoutStretchedRowItem(FlexItem* item, float stretchCross, float vExtra,
                              TextMetrics& metrics) {
    LayoutNode* n = item->node;
    if (item->preStretched) return;
    const float naturalH = item->crossSize - n->box.margin.top - n->box.margin.bottom - vExtra;
    const bool grew = stretchCross > naturalH + 0.01f;
    const bool reused = n->lastLayoutPass != currentLayoutPass();
    if (!grew && !reused) return;
    const float contentW = n->box.contentRect.width;
    const float availW = item->finalMain;
    n->availableHeight = stretchCross + vExtra;
    n->overrideContentWidth = contentW;
    if (beginLayoutNode(n, availW)) {
        n->box.contentRect.height = stretchCross;
        layoutNode(n, availW, metrics);
    }
    n->overrideContentWidth = -1.0f;
    n->box.contentRect.width = contentW;
}

std::string collapsedFlexLineText(std::string_view text, const std::string& whiteSpace) {
    if (whiteSpace == "pre" || whiteSpace == "pre-wrap" || whiteSpace == "break-spaces")
        return std::string(text);
    const bool keepNewlines = whiteSpace == "pre-line";
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (char ch : text) {
        const bool nl = ch == '\n' || ch == '\r';
        if (nl && keepNewlines) {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out += '\n';
            space = false;
            continue;
        }
        if (nl || std::isspace(static_cast<unsigned char>(ch))) {
            space = true;
            continue;
        }
        if (space && !out.empty() && out.back() != '\n') out += ' ';
        space = false;
        out += ch;
    }
    return out;
}

void collectAndMeasureFlexItems(LayoutNode* node, bool isRow, float fontSize,
                                float containerMain, float mainAvailable,
                                float childAvailableHeight, bool columnAutoHeight,
                                TextMetrics& metrics, std::vector<FlexItem>& items) {
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            std::string_view text = child->textContent();
            bool allWhitespace = true;
            for (char c : text) {
                if (!std::isspace(static_cast<unsigned char>(c))) { allWhitespace = false; break; }
            }
            if (allWhitespace) continue;

            const std::string& fontFamily = styleVal(node, Prop::FontFamily);
            const std::string& fontWeight = styleVal(node, Prop::FontWeight);
            FontFeatureScope features(metrics, fontFeaturesOf(node));
            std::string shaped = applyTextTransform(collapsedFlexLineText(text, styleVal(node, Prop::WhiteSpace)),
                                                    styleVal(node, Prop::TextTransform));
            float textW = metrics.measureWidth(shaped, fontFamily, fontSize, fontWeight);
            float textH = metrics.lineHeight(fontFamily, fontSize, fontWeight);

            child->box.contentRect.width = textW;
            child->box.contentRect.height = textH;

            FlexItem item;
            item.node = child;
            item.flexGrow = 0;
            item.flexShrink = 1;
            item.flexBasis = isRow ? textW : textH;
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
            clearHiddenBox(child);
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

        child->box.margin = resolveEdges(child, kMarginProps, containerMain, childFontSize);

        const std::string& basis = styleVal(child, Prop::FlexBasis);
        bool basisFromMainDim = false;
        if (basis == "auto" || basis.empty()) {
            float dim = resolveFlexDim(styleVal(child, isRow ? Prop::Width : Prop::Height),
                                       mainAvailable, childFontSize);
            item.flexBasis = dim >= 0 ? dim : -1.0f;
            basisFromMainDim = (dim >= 0);
        } else {
            item.flexBasis = resolveLength(basis, mainAvailable, childFontSize);
            basisFromMainDim = (item.flexBasis >= 0);
        }
        float contentBoxEdges = 0;
        if (styleVal(child, Prop::BoxSizing) != "border-box") {
            float& edges = contentBoxEdges;
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
        }
        if (basisFromMainDim) item.flexBasis += contentBoxEdges;

        const std::string& minMainVal =
            styleVal(child, isRow ? Prop::MinWidth : Prop::MinHeight);
        bool minMainAuto = (minMainVal == "auto" || minMainVal.empty());
        if (isRow) {
            item.minMain = minMainAuto ? -1.0f : resolveFlexDim(minMainVal, mainAvailable, childFontSize);
            item.maxMain = resolveFlexDim(styleVal(child, Prop::MaxWidth), mainAvailable, childFontSize);
        } else {
            item.minMain = minMainAuto ? -1.0f : resolveFlexDim(minMainVal, mainAvailable, childFontSize);
            item.maxMain = resolveFlexDim(styleVal(child, Prop::MaxHeight), mainAvailable, childFontSize);
        }
        if (item.minMain >= 0) item.minMain += contentBoxEdges;
        if (item.maxMain >= 0) item.maxMain += contentBoxEdges;
        if (minMainAuto && isRow) {
            const std::string& overflow = styleVal(child, Prop::OverflowX);
            if (overflow == "visible" || overflow.empty()) {
                float minContent = computeMinContentWidth(item.node, metrics);
                float ph = resolveLength(styleVal(child, Prop::PaddingLeft), mainAvailable, childFontSize) +
                           resolveLength(styleVal(child, Prop::PaddingRight), mainAvailable, childFontSize);
                Edges bEdges = resolveBorders(item.node, mainAvailable, childFontSize);
                float bh = bEdges.left + bEdges.right;
                item.minMain = minContent + ph + bh;
                const std::string& wProp = styleVal(child, Prop::Width);
                if (!isIntrinsicSizingKeyword(wProp)) {
                    float specMain = resolveFlexDim(wProp, mainAvailable, childFontSize);
                    if (specMain >= 0) {
                        if (styleVal(child, Prop::BoxSizing) != "border-box")
                            specMain += ph + bh;
                        if (item.minMain > specMain) item.minMain = specMain;
                    }
                }
                if (item.maxMain >= 0 && item.minMain > item.maxMain)
                    item.minMain = item.maxMain;
            }
        } else if (minMainAuto && !isRow) {
            const std::string& overflow = styleVal(child, Prop::OverflowY);
            if (overflow == "visible" || overflow.empty())
                item.colAutoMinPending = true;
        }
        if (item.minMain < 0) item.minMain = 0;

        items.push_back(item);
    }

    std::stable_sort(items.begin(), items.end(),
        [](const FlexItem& a, const FlexItem& b) { return a.order < b.order; });

    auto measureOuterMain = [&](FlexItem& item) -> float {
        if (item.node->box.dirty || !(item.node->measuredAtW == containerMain) ||
            !(item.node->cachedAvailHeight == item.node->availableHeight)) {
            layoutNode(item.node, containerMain, metrics);
            item.node->measuredAtW = containerMain;
            item.node->measuredOuterMain =
                item.node->box.contentRect.height +
                item.node->box.padding.top + item.node->box.padding.bottom +
                item.node->box.border.top + item.node->box.border.bottom;
        }
        return item.node->measuredOuterMain;
    };

    for (auto& item : items) {
        if (item.flexBasis >= 0) {
            item.hypotheticalMain = item.flexBasis;
            if (columnAutoHeight && item.flexGrow > 0 && !item.node->isTextNode()) {
                item.contentMain = measureOuterMain(item);
                if (item.maxMain >= 0 && item.contentMain > item.maxMain)
                    item.contentMain = item.maxMain;
            }
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
            if (isRow) {
                float intrinsic = computeMaxContentWidth(item.node, metrics);
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
                if (item.node->box.dirty || std::isnan(item.node->cachedAvailWidth))
                    layoutNode(item.node, item.hypotheticalMain, metrics);
            } else {
                item.hypotheticalMain = measureOuterMain(item);
                if (item.colAutoMinPending && item.hypotheticalMain > item.minMain) {
                    item.minMain = item.hypotheticalMain;
                    if (item.maxMain >= 0 && item.minMain > item.maxMain)
                        item.minMain = item.maxMain;
                }
            }
        }
        item.baseMain = item.hypotheticalMain;
        if (item.maxMain >= 0 && item.hypotheticalMain > item.maxMain)
            item.hypotheticalMain = item.maxMain;
        if (item.hypotheticalMain < item.minMain)
            item.hypotheticalMain = item.minMain;
    }
}

void resolveFlexibleLineLengths(FlexLine& line, float mainAvailable, float gapMain, bool isRow) {
    float totalHypothetical = 0;
    float totalMargins = 0;
    float totalGaps = (line.items.size() > 1) ? gapMain * (line.items.size() - 1) : 0;
    for (auto* item : line.items) {
        totalHypothetical += item->hypotheticalMain;
        totalMargins += flexItemMarginMain(*item, isRow);
    }

    bool growing = totalHypothetical + totalMargins + totalGaps < mainAvailable;

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
            for (auto* item : line.items)
                if (!item->frozen && item->finalMain > item->minMain - 0.0001f &&
                    item->finalMain <= item->minMain + 0.0001f)
                    item->frozen = true;
        } else if (totalViolation < -0.0001f) {
            for (auto* item : line.items)
                if (!item->frozen && item->maxMain >= 0 &&
                    item->finalMain >= item->maxMain - 0.0001f)
                    item->frozen = true;
        } else {
            break;
        }
    }
}

} // namespace htmlayout::layout
