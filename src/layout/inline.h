#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Layout an inline formatting context.
// Wraps inline children into line boxes.
void layoutInline(LayoutNode* node, float availableWidth, TextMetrics& metrics);

} // namespace htmlayout::layout
