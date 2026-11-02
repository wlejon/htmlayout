#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Layout an inline formatting context.
// Wraps inline children into line boxes.
void layoutInline(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Layout an inline-block or replaced inline element. Returns true if handled,
// or false if `node` is a pure non-replaced inline element (with margin,
// padding, border, and baseline/extent fields initialized on `node->box`).
bool tryLayoutInlineBlockOrReplaced(LayoutNode* node, float availableWidth,
                                    float fontSize, TextMetrics& metrics);

} // namespace htmlayout::layout
