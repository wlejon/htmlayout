#pragma once
// Pieces of block layout shared between block.cpp (the block formatting
// context) and block_inline.cpp (a block container's inline formatting
// context). Internal to the layout library.

#include "layout/box.h"
#include <functional>
#include <utility>
#include <vector>

namespace htmlayout::layout {

// CSS2 §10.8 strut for an inline formatting context: a zero-width inline box
// with the box's own font and line-height. The font's natural box (ascent +
// descent) is distributed in the line-height by half-leading; Blink floors
// the ascent-side half (CalculateLeadingSpace) and the descent side takes the
// remainder, so an odd leading puts the extra half-pixel below the baseline.
// font-size:0 produces an empty strut — the font backend is not consulted (it
// may clamp degenerate sizes). The same numbers are an inline element's own
// contribution to the line boxes it sits on (its leaded box).
struct StrutMetrics {
    float above = 0;      // line-top to baseline when only the strut is present
    float below = 0;      // baseline to line-bottom
    float ascent = 0;     // font natural ascent (no leading)
    float descent = 0;    // font natural descent (no leading)
    float xHeight = 0;    // for vertical-align: middle
    float lineHeight = 0; // used line-height
};

StrutMetrics computeStrut(const LayoutNode* node, float fontSize,
                          TextMetrics& metrics);

// Inline space an inside list marker (list-style-position: inside) occupies
// at the start of the list item's first line.
float insideMarkerInlineSize(LayoutNode* node, float fontSize, TextMetrics& metrics);

// The inline formatting context of a block container whose in-flow children
// are all inline-level: breaks them into line boxes `childAvailable` wide,
// places every item, records the block's line boxes and baseline, and
// advances `cursorY` past the last line. See block_inline.cpp.
void layoutBlockInlineContent(LayoutNode* node, float childAvailable, float fontSize,
                              TextMetrics& metrics, float& cursorY);

// How a block formatting context hands an anonymous inline run (the inline
// children between two block-level ones) to the same line builder.
struct InlineRunEnv {
    // The run's children, in order; null means all of the block's children.
    const std::vector<LayoutNode*>* children = nullptr;
    // The float-free band [left, right) at a line top `y` for a line about
    // `h` tall, in the block's content coordinates. Null: [0, childAvailable).
    std::function<std::pair<float, float>(float y, float h)> band;
    // Places a float met inside the run with its margin-box top at `y`.
    std::function<void(LayoutNode* flt, float y)> placeFloat;
    // Called once, before the first line, when the run produces a line box
    // (a line box resolves the pending collapsed margin above it).
    std::function<void()> beforeLines;
    // The run holds the block's first formatted line: text-indent and an
    // inside list marker apply to it.
    bool firstFormattedLine = true;
    // An anonymous run: lines holding nothing are not placed, and the block's
    // baseline is left to its formatting context.
    bool anonymous = false;
};

void layoutInlineRun(LayoutNode* node, float childAvailable, float fontSize,
                     TextMetrics& metrics, float& cursorY, const InlineRunEnv& env);

} // namespace htmlayout::layout
