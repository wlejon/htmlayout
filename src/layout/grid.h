#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// CSS Grid layout formatting context.
void layoutGrid(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Max-content inline size of a grid container's content box: fixed column
// tracks at their resolved size, intrinsic/flexible tracks at the widest
// in-flow item's max-content contribution, plus column gaps. Used by the
// intrinsic sizing machinery (computeMaxContentWidth) so a grid sized under
// fit-content — e.g. a non-stretched flex item — gets its track-defined
// width instead of the block fallback of "widest child".
float gridMaxContentWidth(LayoutNode* node, TextMetrics& metrics);

} // namespace htmlayout::layout
