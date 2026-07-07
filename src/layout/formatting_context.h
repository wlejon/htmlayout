#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Determines which layout algorithm to use for a node based on
// its computed display property, then dispatches to the right one.
void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Sentinel values for intrinsic sizing keywords. Valid CSS sizing values are
// non-negative, so these specific negative values cannot collide with real lengths.
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

// Set the viewport used to resolve vw/vh/vmin/vmax units. Called once per layout
// pass by layoutTree() so the workhorse length resolver (which has no node/context
// in hand) can size viewport-relative units against the real viewport rather than
// the local percentage reference. A dimension of 0 means "unknown" — viewport units
// then fall back to the percentage reference, preserving pre-viewport behavior.
void setLayoutViewport(float width, float height);

// Set the root element (<html>) font-size in px, used to resolve rem units.
// Called once per layout pass by layoutTree() so the per-site resolver — which
// has no node in hand — can size rem against the document root rather than a
// hardcoded 16px. A non-positive value falls back to 16 (the initial font-size).
void setRootFontSize(float px);

// Set the font-metric context (in px) used to resolve ch and ex units for the
// element currently being laid out: chPx is the advance width of "0", exPx the
// x-height, both in the element's own font at its own size. Layout calls this
// right before resolving that element's lengths. Passing 0 for either restores
// the 0.5em fallback for that unit.
void setLengthFontContext(float chPx, float exPx);

// Resolve CSS length values (px, em, rem, %, vw, vh, vmin, vmax, ch, pt, auto) to pixels.
// Supports calc() expressions.
float resolveLength(const std::string& value, float referenceSize, float fontSize);

// Viewport-aware length resolution: uses separate viewport width/height for vw/vh units.
float resolveLength(const std::string& value, float referenceSize, float fontSize,
                    float viewportWidth, float viewportHeight);

// Resolve line-height value. "normal" resolves to ~1.2 * fontSize
// rather than 0. A unitless number (e.g. "1.5") is a multiplier of fontSize.
float resolveLineHeight(const std::string& value, float fontSize);

// Preferred overload: "normal" resolves to the font's intrinsic line height
// (ascent + descent + leading) via TextMetrics, matching browser behavior.
// Falls back to 1.2 * fontSize if metrics returns <= 0.
float resolveLineHeight(const std::string& value, float fontSize,
                        const std::string& fontFamily,
                        const std::string& fontWeight,
                        TextMetrics& metrics);

// Parse edges (margin, padding, border-width) from computed style
Edges resolveEdges(const css::ComputedStyle& style,
                   const std::string& prefix,
                   float referenceWidth,
                   float fontSize);

// Post-layout pass: position all absolute/fixed elements against their
// correct containing blocks. Called automatically by layoutTree().
void layoutAbsoluteElements(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics);

} // namespace htmlayout::layout
