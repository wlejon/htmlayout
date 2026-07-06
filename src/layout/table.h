#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Table formatting context: lays out children as table rows/cells.
void layoutTable(LayoutNode* node, float availableWidth, TextMetrics& metrics);

// Intrinsic min/max widths of a table's content box per CSS2 §17.5.2.2:
// column min/max sums + border-spacing (+ the collapsed outer half-borders
// in border-collapse mode, which live inside the table's content width).
// Excludes the table's own padding/border/margin. Runs the same column
// algorithm layoutTable() uses, so the two always agree.
void computeTableIntrinsicWidths(LayoutNode* node, TextMetrics& metrics,
                                 float& minWidth, float& maxWidth);

} // namespace htmlayout::layout
