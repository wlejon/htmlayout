#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Table formatting context: lays out children as table rows/cells.
void layoutTable(LayoutNode* node, float availableWidth, TextMetrics& metrics);

} // namespace htmlayout::layout
