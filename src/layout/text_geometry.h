#pragma once
#include "layout/box.h"
#include <vector>

namespace htmlayout::layout {

// Hit-test result: the text-node LayoutNode under a point plus the byte
// offset within its source string closest to the hit.
struct TextHit {
    LayoutNode* node = nullptr;
    int srcOffset = 0;  // byte offset into node->textContent()
};

// Find the text node at (x, y) and the closest source character offset. If
// the point isn't over any text node, returns {nullptr, 0}.
//
// Coordinate space: (x, y) are absolute layout coordinates (same space used
// by hitTest()). The query walks the tree accumulating offsets in the same
// way hitTest does.
//
// `scope` restricts the answer to text inside one subtree while still walking
// from `root`, which is what keeps the coordinates absolute. Passing the
// subtree as `root` instead would look equivalent and is not: the walk starts
// its offset accumulation at (0, 0), so every run would come back positioned
// relative to the subtree and get compared against an absolute query point.
TextHit hitTestText(LayoutNode* root, float x, float y, TextMetrics& metrics,
                    const LayoutNode* scope = nullptr);

// Caret geometry for a cursor at `srcOffset` within `textNode`'s source text.
// Output (x, y) are absolute layout coordinates; height is the caret's
// vertical extent (matches the line-box height at the caret). Returns false
// if textNode has no placed runs (e.g. empty text).
bool getCaretRect(LayoutNode* root, LayoutNode* textNode, int srcOffset,
                  TextMetrics& metrics, float& x, float& y, float& height);

// Selection geometry: per-line rectangles covering the source range
// [startNode:startOff, endNode:endOff] in tree order. Returns absolute
// coordinates. Empty when the range is collapsed or doesn't intersect any
// placed text runs.
std::vector<Rect> getSelectionRects(LayoutNode* root,
                                    LayoutNode* startNode, int startOff,
                                    LayoutNode* endNode, int endOff,
                                    TextMetrics& metrics);

} // namespace htmlayout::layout
