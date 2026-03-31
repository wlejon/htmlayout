#include "layout/box.h"
#include "layout/formatting_context.h"

namespace htmlayout::layout {

void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    if (!root) return;

    // Layout root as a block element with the viewport as available width
    layoutNode(root, viewportWidth, metrics);

    // Position root at (0, 0) plus its own margin/padding/border
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;
}

LayoutNode* hitTest(LayoutNode* root, float x, float y) {
    // TODO: Implement point-in-box recursive hit test (Phase 8)
    return nullptr;
}

} // namespace htmlayout::layout
