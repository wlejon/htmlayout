#pragma once
#include "layout/box.h"
#include <string>
#include <string_view>
#include <vector>

namespace htmlayout::layout {

struct FlexItem {
    LayoutNode* node = nullptr;
    float flexGrow = 0;
    float flexShrink = 1;
    float flexBasis = -1;      // resolved basis (px), -1 = auto
    float baseMain = 0;        // flex base size (outer, before min/max clamp)
    float hypotheticalMain = 0; // base size clamped by min/max
    float minMain = 0;
    float maxMain = -1;
    float crossSize = 0;
    int order = 0;
    bool frozen = false;
    float finalMain = 0;
    // Column flex only: min-main is auto (min-height:auto) with visible overflow,
    // so its content-based minimum can only be resolved once the item is laid out
    // (its block-axis content height depends on the definite cross size). Deferred
    // to the hypothetical-size loop where that layout happens.
    bool colAutoMinPending = false;
    // Column flex with an indefinite main size only: the item's laid-out outer
    // height, which is its max-content contribution to the container's own
    // intrinsic height (§9.9.1). -1 = not measured (the two are the same).
    float contentMain = -1.0f;
    // Row flex only: the item was laid out with its stretched height preset
    // (a single line in a container of definite height), so its contents
    // already fill the line and the stretch needs no second layout.
    bool preStretched = false;
};

struct FlexLine {
    std::vector<FlexItem*> items;
    float mainSize = 0;
    float crossSize = 0;
};

inline float flexItemMarginMain(const FlexItem& item, bool isRow) {
    if (isRow)
        return item.node->box.margin.left + item.node->box.margin.right;
    return item.node->box.margin.top + item.node->box.margin.bottom;
}

void relayoutStretchedRowItem(FlexItem* item, float stretchCross, float vExtra,
                              TextMetrics& metrics);

std::string collapsedFlexLineText(std::string_view text, const std::string& whiteSpace);

void collectAndMeasureFlexItems(LayoutNode* node, bool isRow, float fontSize,
                                float containerMain, float mainAvailable,
                                float childAvailableHeight, bool columnAutoHeight,
                                TextMetrics& metrics, std::vector<FlexItem>& items);

void resolveFlexibleLineLengths(FlexLine& line, float mainAvailable, float gapMain, bool isRow);

} // namespace htmlayout::layout
