#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Determines which layout algorithm to use for a node based on
// its computed display property, then dispatches to the right one.
void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Sentinel values for intrinsic sizing keywords
constexpr float SIZING_MIN_CONTENT = -10.0f;
constexpr float SIZING_MAX_CONTENT = -20.0f;
constexpr float SIZING_FIT_CONTENT = -30.0f;

// Check if a CSS value is an intrinsic sizing keyword
bool isIntrinsicSizingKeyword(const std::string& value);

// Compute min-content width: the narrowest an element can be without overflow.
// Each word goes on its own line.
float computeMinContentWidth(LayoutNode* node, TextMetrics& metrics);

// Compute max-content width: the width if the content never wraps.
float computeMaxContentWidth(LayoutNode* node, TextMetrics& metrics);

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

// Post-layout pass: position all absolute/fixed elements against their
// correct containing blocks. Called automatically by layoutTree().
void layoutAbsoluteElements(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics);

} // namespace htmlayout::layout
