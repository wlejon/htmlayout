#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Check if a node has multi-column properties set
bool isMulticolContainer(const LayoutNode* node);

// If column-count or column-width is set on `node`, redistribute its in-flow
// children into columns after initial block layout and return true.
bool layoutMulticolIfNeeded(LayoutNode* node, float contentWidth, float fontSize,
                            float specifiedHeight, TextMetrics& metrics);

} // namespace htmlayout::layout
