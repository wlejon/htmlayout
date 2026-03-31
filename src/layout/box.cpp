#include "layout/box.h"

namespace htmlayout::layout {

void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    // TODO: Implement — entry point that dispatches to block/inline/flex
}

LayoutNode* hitTest(LayoutNode* root, float x, float y) {
    // TODO: Implement point-in-box recursive hit test
    return nullptr;
}

} // namespace htmlayout::layout
