#include "layout/box.h"
#include "layout/formatting_context.h"

namespace htmlayout::layout {

void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    if (!root) return;
    layoutNode(root, viewportWidth, metrics);
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;
}

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

bool pointInBox(const LayoutBox& box, float x, float y) {
    // Border box = content + padding + border
    float bx = box.contentRect.x - box.padding.left - box.border.left;
    float by = box.contentRect.y - box.padding.top - box.border.top;
    float bw = box.contentRect.width + box.padding.left + box.padding.right + box.border.left + box.border.right;
    float bh = box.contentRect.height + box.padding.top + box.padding.bottom + box.border.top + box.border.bottom;
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

LayoutNode* hitTestRecursive(LayoutNode* node, float x, float y) {
    if (!node) return nullptr;

    auto& style = node->computedStyle();

    // Skip display:none and pointer-events:none
    if (styleVal(style, "display") == "none") return nullptr;
    if (styleVal(style, "pointer-events") == "none") return nullptr;

    // Check if point is within this node's border box
    if (!pointInBox(node->box, x, y)) return nullptr;

    // Overflow clipping: if overflow is hidden/scroll/auto, clip to border box
    const std::string& overflow = styleVal(style, "overflow");
    if (overflow == "hidden" || overflow == "scroll" || overflow == "auto") {
        // Point is already confirmed inside; children outside are clipped.
        // Children that extend outside are not hit-testable.
    }

    // Check children in reverse order (later siblings are painted on top)
    auto children = node->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        LayoutNode* hit = hitTestRecursive(*it, x, y);
        if (hit) return hit;
    }

    // No child hit — this node is the deepest match
    return node;
}

} // anonymous namespace

LayoutNode* hitTest(LayoutNode* root, float x, float y) {
    return hitTestRecursive(root, x, y);
}

} // namespace htmlayout::layout
