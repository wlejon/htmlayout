#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Determines which layout algorithm to use for a node based on
// its computed display property, then dispatches to the right one.
void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Resolve CSS length values (px, em, rem, %, vw, vh, vmin, vmax, ch, pt, auto) to pixels.
// Supports calc() expressions.
float resolveLength(const std::string& value, float referenceSize, float fontSize);

// Viewport-aware length resolution: uses separate viewport width/height for vw/vh units.
float resolveLength(const std::string& value, float referenceSize, float fontSize,
                    float viewportWidth, float viewportHeight);

// Resolve line-height value. "normal" resolves to ~1.2 * fontSize
// rather than 0. A unitless number (e.g. "1.5") is a multiplier of fontSize.
float resolveLineHeight(const std::string& value, float fontSize);

// Parse edges (margin, padding, border-width) from computed style
Edges resolveEdges(const css::ComputedStyle& style,
                   const std::string& prefix,
                   float referenceWidth,
                   float fontSize);

} // namespace htmlayout::layout
