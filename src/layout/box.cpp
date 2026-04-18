#include "layout/box.h"
#include "css/transform.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace htmlayout::layout {

using layout::styleVal;

void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    layoutTree(root, Viewport{viewportWidth, 0.0f}, metrics);
}

// Reset every box in the tree to defaults so layoutTree can be called
// repeatedly on the same (persistent) tree without carrying stale padding/
// border/margin/contentRect state from the previous run. A few code paths —
// notably layoutAbsoluteChild's shrink-wrap calculation — read these fields
// to compute available widths, which silently drifts layout if not cleared.
static void resetBoxes(LayoutNode* node) {
    if (!node) return;
    node->box = LayoutBox{};
    for (auto* child : node->children()) resetBoxes(child);
}

void layoutTree(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics) {
    if (!root) return;
    resetBoxes(root);
    root->availableHeight = viewport.height;
    root->viewportHeight = viewport.height;
    layoutNode(root, viewport.width, metrics);
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;

    // Pass 2: position all absolute/fixed elements against their correct containing blocks
    layoutAbsoluteElements(root, viewport, metrics);
}

namespace {

bool pointInBox(const LayoutBox& box, float x, float y) {
    // Border box = content + padding + border
    float bx = box.contentRect.x - box.padding.left - box.border.left;
    float by = box.contentRect.y - box.padding.top - box.border.top;
    float bw = box.contentRect.width + box.padding.left + box.padding.right + box.border.left + box.border.right;
    float bh = box.contentRect.height + box.padding.top + box.padding.bottom + box.border.top + box.border.bottom;
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// Get z-index as integer (auto treated as 0 for ordering purposes)
int getZIndex(const css::ComputedStyle& style) {
    const std::string& z = styleVal(style, "z-index");
    if (z.empty() || z == "auto") return 0;
    try { return std::stoi(z); } catch (...) { return 0; }
}

// Check if an element creates a stacking context
bool createsStackingContext(const css::ComputedStyle& style) {
    const std::string& pos = styleVal(style, "position");
    const std::string& z = styleVal(style, "z-index");
    // position:fixed and position:sticky always create a stacking context
    if (pos == "fixed" || pos == "sticky") return true;
    // Other positioned elements with z-index other than auto
    if ((pos == "absolute" || pos == "relative") &&
        !z.empty() && z != "auto") {
        return true;
    }
    // opacity < 1 creates a stacking context
    const std::string& op = styleVal(style, "opacity");
    if (!op.empty() && op != "1") {
        try {
            float opVal = std::stof(op);
            if (opVal < 1.0f) return true;
        } catch (...) {}
    }
    // transform other than none
    const std::string& tr = styleVal(style, "transform");
    if (!tr.empty() && tr != "none") return true;
    // filter other than none
    const std::string& ft = styleVal(style, "filter");
    if (!ft.empty() && ft != "none") return true;
    // isolation: isolate
    if (styleVal(style, "isolation") == "isolate") return true;
    return false;
}

// Does this element clip descendants' hit testing to its border box?
// Matches the overflow-clipping rules used at paint time.
bool clipsHitTesting(const css::ComputedStyle& style) {
    auto check = [](const std::string& v) {
        return !v.empty() && v != "visible";
    };
    if (check(styleVal(style, "overflow"))) return true;
    if (check(styleVal(style, "overflow-x"))) return true;
    if (check(styleVal(style, "overflow-y"))) return true;
    return false;
}

// Hit test with offset accumulation: positions are relative to parent content area,
// so we track the accumulated offset from the root.
LayoutNode* hitTestRecursive(LayoutNode* node, float x, float y,
                              float offsetX, float offsetY) {
    if (!node) return nullptr;

    auto& style = node->computedStyle();

    // Skip display:none and pointer-events:none
    if (styleVal(style, "display") == "none") return nullptr;
    if (styleVal(style, "pointer-events") == "none") return nullptr;
    // visibility:hidden still occupies space but is not hit-testable
    if (styleVal(style, "visibility") == "hidden") return nullptr;

    // Compute this node's absolute content position
    float absX = node->box.contentRect.x + offsetX;
    float absY = node->box.contentRect.y + offsetY;

    // Border box bounds
    float bx = absX - node->box.padding.left - node->box.border.left;
    float by = absY - node->box.padding.top - node->box.border.top;
    float bw = node->box.fullWidth();
    float bh = node->box.fullHeight();

    // Apply CSS transform: map the test point through the inverse transform
    // around transform-origin. Must be computed after bw/bh are known so
    // percentage translates/origins resolve against the element's border box.
    float testX = x, testY = y;
    const std::string& transform = styleVal(style, "transform");
    if (!transform.empty() && transform != "none") {
        css::Matrix2D mat = css::parseTransform(transform, bw, bh);
        if (!mat.isIdentity()) {
            float ox, oy;
            css::parseTransformOrigin(styleVal(style, "transform-origin"),
                                       bw, bh, ox, oy);
            // Build full transform about the origin: T(origin) * M * T(-origin)
            css::Matrix2D toOrigin{1,0,0,1, bx+ox, by+oy};
            css::Matrix2D fromOrigin{1,0,0,1, -(bx+ox), -(by+oy)};
            css::Matrix2D full = toOrigin * mat * fromOrigin;
            css::Matrix2D inv;
            if (full.invert(inv)) {
                testX = inv.a * x + inv.c * y + inv.e;
                testY = inv.b * x + inv.d * y + inv.f;
            }
        }
    }

    bool insideBounds =
        (testX >= bx && testX < bx + bw && testY >= by && testY < by + bh);

    // If this element clips descendants and the point is outside its border
    // box, reject entirely — neither it nor its children can be hit. With
    // overflow:visible, we still descend in case positioned children extend
    // past our bounds.
    if (!insideBounds && clipsHitTesting(style))
        return nullptr;

    // Sort children by CSS stacking order for hit testing (topmost first).
    // Per CSS: positioned elements paint above non-positioned; within the same
    // category, higher z-index paints above lower; equal z-index uses source order
    // (later paints on top). Hit testing reverses paint order so the topmost
    // (last-painted) element is tested first.
    auto children = node->children();

    struct ZChild { int z; bool positioned; size_t srcIdx; LayoutNode* node; };
    std::vector<ZChild> zChildren;
    zChildren.reserve(children.size());
    for (size_t i = 0; i < children.size(); ++i) {
        auto* child = children[i];
        int z = 0;
        bool pos = false;
        if (child) {
            z = getZIndex(child->computedStyle());
            const std::string& p = styleVal(child->computedStyle(), "position");
            pos = (p == "absolute" || p == "relative" || p == "fixed" || p == "sticky");
        }
        zChildren.push_back({z, pos, i, child});
    }

    std::stable_sort(zChildren.begin(), zChildren.end(),
        [](const ZChild& a, const ZChild& b) {
            if (a.z != b.z) return a.z > b.z;           // higher z-index first
            if (a.positioned != b.positioned) return a.positioned; // positioned above non-positioned
            return a.srcIdx > b.srcIdx;                  // later source order first
        });

    // Children's positions are relative to this node's content area, offset
    // by this element's scroll position (scrollLeft/scrollTop shift the
    // visible content in the opposite direction).
    float childOffsetX = absX - node->scrollLeftPx();
    float childOffsetY = absY - node->scrollTopPx();
    for (auto& zc : zChildren) {
        LayoutNode* hit = hitTestRecursive(zc.node, testX, testY,
                                            childOffsetX, childOffsetY);
        if (hit) return hit;
    }

    // No child hit — this node is the deepest match only if the point is
    // actually inside its bounds (overflow:visible descendants may have
    // extended us past the border box, but the node itself is not hittable
    // there).
    if (insideBounds) return node;
    return nullptr;
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
    bool thisClips = (overflow == "hidden" || overflow == "scroll" || overflow == "auto" || overflow == "clip");
    // CSS Containment L2: contain: paint clips children to padding box
    if (!thisClips) {
        const std::string& contain = styleVal(style, "contain");
        if (!contain.empty() && contain != "none") {
            thisClips = (contain == "strict" || contain == "content" ||
                         contain.find("paint") != std::string::npos);
        }
    }

    for (auto* child : node->children()) {
        applyOverflowClippingRecursive(child, thisClips, thisClips ? &node->box : nullptr);
    }
}

} // anonymous namespace

void applyOverflowClipping(LayoutNode* root) {
    applyOverflowClippingRecursive(root, false, nullptr);
}

LayoutNode* hitTest(LayoutNode* root, float x, float y) {
    return hitTestRecursive(root, x, y, 0.0f, 0.0f);
}

void markDirty(LayoutNode* node) {
    if (!node) return;
    node->box.dirty = true;
    // Walk up to root, marking ancestors dirty
    LayoutNode* p = node->parent();
    while (p) {
        if (p->box.dirty) break; // already dirty up the chain
        p->box.dirty = true;
        p = p->parent();
    }
}

namespace {

void layoutNodeIncremental(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;
    if (!node->box.dirty) return; // skip clean subtrees

    layoutNode(node, availableWidth, metrics);
    node->box.dirty = false;

    // Children are laid out by layoutNode, mark them clean
    for (auto* child : node->children()) {
        child->box.dirty = false;
    }
}

} // anonymous namespace

void layoutTreeIncremental(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    if (!root) return;

    if (root->box.dirty) {
        // Root is dirty: full relayout
        layoutTree(root, viewportWidth, metrics);
        // Mark entire tree clean
        std::vector<LayoutNode*> stack = {root};
        while (!stack.empty()) {
            auto* n = stack.back();
            stack.pop_back();
            n->box.dirty = false;
            for (auto* c : getLayoutChildren(n)) stack.push_back(c);
        }
    } else {
        // Walk tree, re-layout only dirty subtrees
        std::vector<std::pair<LayoutNode*, float>> stack;
        stack.push_back({root, viewportWidth});
        while (!stack.empty()) {
            auto [node, avail] = stack.back();
            stack.pop_back();
            if (node->box.dirty) {
                layoutNode(node, avail, metrics);
                // Mark subtree clean
                std::vector<LayoutNode*> sub = {node};
                while (!sub.empty()) {
                    auto* n = sub.back();
                    sub.pop_back();
                    n->box.dirty = false;
                    for (auto* c : getLayoutChildren(n)) sub.push_back(c);
                }
            } else {
                for (auto* c : node->children()) {
                    stack.push_back({c, node->box.contentRect.width});
                }
            }
        }
    }
}

// Layout-affecting properties: if any of these change, relayout is needed.
// Properties not in this set only need repaint.
static const std::vector<std::string>& layoutProperties() {
    static const std::vector<std::string> props = {
        "display", "position", "float", "clear",
        "width", "height", "min-width", "min-height", "max-width", "max-height",
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
        "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
        "box-sizing", "overflow",
        "flex-direction", "flex-wrap", "justify-content", "align-items", "align-content",
        "align-self", "flex-grow", "flex-shrink", "flex-basis", "order",
        "gap", "row-gap", "column-gap",
        "grid-template-columns", "grid-template-rows", "grid-area",
        "grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end",
        "font-size", "font-family", "font-weight", "line-height",
        "white-space", "text-align", "vertical-align",
        "top", "right", "bottom", "left",
        "column-count", "column-width",
        "table-layout", "border-collapse", "border-spacing",
        "writing-mode", "direction",
        "word-break", "overflow-wrap", "text-overflow",
    };
    return props;
}

std::vector<LayoutNode*> getLayoutChildren(LayoutNode* node) {
    std::vector<LayoutNode*> result;
    for (auto* child : node->children()) {
        if (!child->isTextNode()) {
            auto& cs = child->computedStyle();
            if (styleVal(cs, "display") == "contents") {
                // Flatten: promote this node's children into the parent's sequence
                auto grandchildren = getLayoutChildren(child);
                result.insert(result.end(), grandchildren.begin(), grandchildren.end());
                continue;
            }
        }
        result.push_back(child);
    }
    return result;
}

bool needsRelayout(const std::vector<std::string>& changedProperties) {
    auto& lp = layoutProperties();
    for (auto& prop : changedProperties) {
        for (auto& lProp : lp) {
            if (prop == lProp) return true;
        }
    }
    return false;
}

} // namespace htmlayout::layout
