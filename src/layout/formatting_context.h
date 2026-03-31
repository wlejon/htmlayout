#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Determines which layout algorithm to use for a node based on
// its computed display property, then dispatches to the right one.
void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Resolve CSS length values (px, em, %, auto) to pixels
float resolveLength(const std::string& value, float referenceSize, float fontSize);

// Parse edges (margin, padding, border-width) from computed style
Edges resolveEdges(const css::ComputedStyle& style,
                   const std::string& prefix,
                   float referenceWidth,
                   float fontSize);

} // namespace htmlayout::layout
