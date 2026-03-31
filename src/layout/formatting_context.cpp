#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/inline.h"
#include "layout/flex.h"

namespace htmlayout::layout {

void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    // TODO: Read display property, dispatch to block/inline/flex
}

float resolveLength(const std::string& value, float referenceSize, float fontSize) {
    // TODO: Implement px/em/%/auto resolution
    return 0.0f;
}

Edges resolveEdges(const css::ComputedStyle& style,
                   const std::string& prefix,
                   float referenceWidth,
                   float fontSize) {
    // TODO: Implement
    return {};
}

} // namespace htmlayout::layout
