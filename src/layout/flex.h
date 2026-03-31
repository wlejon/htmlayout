#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Layout a flex formatting context.
// Implements CSS Flexible Box Layout (flex-direction, wrapping, grow/shrink).
void layoutFlex(LayoutNode* node, float availableWidth, TextMetrics& metrics);

} // namespace htmlayout::layout
