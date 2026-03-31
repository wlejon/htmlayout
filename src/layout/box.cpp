#include "layout/box.h"
#include "layout/formatting_context.h"
#include <algorithm>

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

// Clip a child's content rect to a parent's padding box (content + padding).
void clipToParentPaddingBox(LayoutBox& childBox, const LayoutBox& parentBox) {
    // Parent's padding box boundaries
    float px = parentBox.contentRect.x - parentBox.padding.left;
    float py = parentBox.contentRect.y - parentBox.padding.top;
    float pw = parentBox.contentRect.width + parentBox.padding.left + parentBox.padding.right;
    float ph = parentBox.contentRect.height + parentBox.padding.top + parentBox.padding.bottom;

    float cx = childBox.contentRect.x;
    float cy = childBox.contentRect.y;
    float cw = childBox.contentRect.width;
    float ch = childBox.contentRect.height;

    // Clip left
    if (cx < px) {
        float diff = px - cx;
        cw -= diff;
        cx = px;
    }
    // Clip top
    if (cy < py) {
        float diff = py - cy;
        ch -= diff;
        cy = py;
    }
    // Clip right
    if (cx + cw > px + pw) {
        cw = px + pw - cx;
    }
    // Clip bottom
    if (cy + ch > py + ph) {
        ch = py + ph - cy;
    }

    // Clamp to non-negative
    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;

    childBox.contentRect.x = cx;
    childBox.contentRect.y = cy;
    childBox.contentRect.width = cw;
    childBox.contentRect.height = ch;
}

void applyOverflowClippingRecursive(LayoutNode* node, bool parentClips, const LayoutBox* clipBox) {
    if (!node) return;

    auto& style = node->computedStyle();
    if (styleVal(style, "display") == "none") return;

    // If parent clips and this node extends outside, clip it
    if (parentClips && clipBox) {
        clipToParentPaddingBox(node->box, *clipBox);
    }

    // Check if this node clips its children
    const std::string& overflow = styleVal(style, "overflow");
    bool thisClips = (overflow == "hidden" || overflow == "scroll" || overflow == "auto");

    for (auto* child : node->children()) {
        applyOverflowClippingRecursive(child, thisClips, thisClips ? &node->box : nullptr);
    }
}

} // anonymous namespace

void applyOverflowClipping(LayoutNode* root) {
    applyOverflowClippingRecursive(root, false, nullptr);
}

LayoutNode* hitTest(LayoutNode* root, float x, float y) {
    return hitTestRecursive(root, x, y);
}

} // namespace htmlayout::layout
