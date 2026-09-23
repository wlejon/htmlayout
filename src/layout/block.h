#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Layout a block formatting context.
// Lays out children vertically, handles margin collapsing.
void layoutBlock(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Does this box establish a new block formatting context (CSS2 §9.4.1)?
bool nodeEstablishesBFC(LayoutNode* node);

} // namespace htmlayout::layout
