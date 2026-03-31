#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Layout a block formatting context.
// Lays out children vertically, handles margin collapsing.
void layoutBlock(LayoutNode* node, float availableWidth, TextMetrics& metrics);

} // namespace htmlayout::layout
