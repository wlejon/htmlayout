#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// CSS Grid layout formatting context.
void layoutGrid(LayoutNode* node, float availableWidth, TextMetrics& metrics);

} // namespace htmlayout::layout
