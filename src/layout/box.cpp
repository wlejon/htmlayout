#include "layout/box.h"
#include "css/transform.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace htmlayout::layout {

using layout::styleVal;

namespace { Rect computeSubtreeHitBounds(LayoutNode* node); }

void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    layoutTree(root, Viewport{viewportWidth, 0.0f}, metrics);
}

// Bumped once per layoutTree() call, so layoutNode() can tell its first visit to
// a node this pass (which clears the box) from the later ones (which must not —
// see LayoutNode::lastLayoutPass). Layout is single-threaded, one tree at a
// time, so a file-scoped counter is enough; it only ever has to be *different*
// from the value a node last recorded.
static uint32_t g_layoutPass = 0;

uint32_t currentLayoutPass() { return g_layoutPass; }

void markSubtreeDirty(LayoutNode* node) {
    if (!node) return;
    node->box.dirty = true;
    for (auto* child : node->children()) markSubtreeDirty(child);
    // ::before / ::after wrappers hang off the node outside children().
    if (auto* p = node->pseudoBefore()) markSubtreeDirty(p);
    if (auto* p = node->pseudoAfter())  markSubtreeDirty(p);
}

void layoutTree(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics) {
    if (!root) return;
    ++g_layoutPass;
    setLayoutViewport(viewport.width, viewport.height);
    // rem resolves against the root element's font-size. The root's computed
    // font-size is already absolute (px/unitless) by the time it reaches layout,
    // so resolve it against the initial 16px base (em/% would compound off 16).
    float rootFontSize = resolveLength(styleVal(root->computedStyle(), "font-size"),
                                       16.0f, 16.0f);
    setRootFontSize(rootFontSize);

    // The viewport and the rem base are readable from any node's lengths but
    // are not part of any node's own inputs, so a subtree with a `50vw` child
    // has no way to notice they moved. Dirty everything when they do.
    if (!(root->cachedViewportW == viewport.width) ||
        !(root->cachedViewportH == viewport.height) ||
        !(root->cachedRootFontSize == rootFontSize)) {
        markSubtreeDirty(root);
        root->cachedViewportW = viewport.width;
        root->cachedViewportH = viewport.height;
        root->cachedRootFontSize = rootFontSize;
    }

    root->availableHeight = viewport.height;
    root->viewportHeight = viewport.height;
    layoutNode(root, viewport.width, metrics);
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;

    // Pass 2: position all absolute/fixed elements against their correct containing blocks
    layoutAbsoluteElements(root, viewport, metrics);

    // Pass 3: cache per-node subtree hit-bounds so hit testing can prune whole
    // branches instead of walking every element on each mouse move.
    computeSubtreeHitBounds(root);
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

// Post-order pass computing box.hitBounds for every node: the union of the
// node's border box and all descendant boxes, folding in each descendant's own
// transform and clipping. Expressed in the SAME space as box.contentRect
// (relative to the parent's content origin), and returns that rect so the
// parent can lift and union it. The offset math mirrors hitTestRecursive
// exactly, so the cached bounds and the walk that consults them can never
// disagree.
Rect computeSubtreeHitBounds(LayoutNode* node) {
    if (!node) return {0, 0, 0, 0};
    const auto& style = node->computedStyle();
    if (styleVal(style, "display") == "none") {
        node->box.hitBounds = {0, 0, 0, 0};
        return {0, 0, 0, 0};
    }

    // This node's own border box, in parent-content space (same as contentRect).
    float bx = node->box.contentRect.x - node->box.padding.left - node->box.border.left;
    float by = node->box.contentRect.y - node->box.padding.top - node->box.border.top;
    float bw = node->box.fullWidth();
    float bh = node->box.fullHeight();
    float minX = bx, minY = by, maxX = bx + bw, maxY = by + bh;

    // Children live in this node's content space, shifted by its scroll offset —
    // the exact mapping hitTestRecursive uses (childOffset = absX - scroll).
    float childOffX = node->box.contentRect.x - node->scrollLeftPx();
    float childOffY = node->box.contentRect.y - node->scrollTopPx();
    bool clips = clipsHitTesting(style);
    for (auto* child : node->children()) {
        Rect cb = computeSubtreeHitBounds(child);   // in our content space
        // A clipping node bounds descendants to its own border box, so their
        // extent never enlarges ours (they still get their own hitBounds above,
        // for pruning once the point is known to be inside us).
        if (clips) continue;
        if (cb.width <= 0 && cb.height <= 0) continue;   // display:none / empty
        float cx = cb.x + childOffX, cy = cb.y + childOffY;
        minX = std::min(minX, cx);            minY = std::min(minY, cy);
        maxX = std::max(maxX, cx + cb.width); maxY = std::max(maxY, cy + cb.height);
    }

    Rect bounds{minX, minY, maxX - minX, maxY - minY};

    // Fold in this node's own transform — its whole box + subtree move together,
    // mirroring hitTestRecursive's forward transform about transform-origin.
    const std::string& transform = styleVal(style, "transform");
    if (!transform.empty() && transform != "none") {
        css::Matrix2D mat = css::parseTransform(transform, bw, bh);
        if (!mat.isIdentity()) {
            float ox, oy;
            css::parseTransformOrigin(styleVal(style, "transform-origin"), bw, bh, ox, oy);
            css::Matrix2D toOrigin{1, 0, 0, 1, bx + ox, by + oy};
            css::Matrix2D fromOrigin{1, 0, 0, 1, -(bx + ox), -(by + oy)};
            css::Matrix2D full = toOrigin * mat * fromOrigin;
            float cxs[4] = {bounds.x, bounds.x + bounds.width, bounds.x, bounds.x + bounds.width};
            float cys[4] = {bounds.y, bounds.y, bounds.y + bounds.height, bounds.y + bounds.height};
            float tMinX = 1e30f, tMinY = 1e30f, tMaxX = -1e30f, tMaxY = -1e30f;
            for (int i = 0; i < 4; ++i) {
                float tx = full.a * cxs[i] + full.c * cys[i] + full.e;
                float ty = full.b * cxs[i] + full.d * cys[i] + full.f;
                tMinX = std::min(tMinX, tx); tMinY = std::min(tMinY, ty);
                tMaxX = std::max(tMaxX, tx); tMaxY = std::max(tMaxY, ty);
            }
            bounds = {tMinX, tMinY, tMaxX - tMinX, tMaxY - tMinY};
        }
    }

    node->box.hitBounds = bounds;
    return bounds;
}

// Hit test with offset accumulation: positions are relative to parent content area,
// so we track the accumulated offset from the root.
LayoutNode* hitTestRecursive(LayoutNode* node, float x, float y,
                              float offsetX, float offsetY) {
    if (!node) return nullptr;

    auto& style = node->computedStyle();

    // Skip display:none entirely (no layout, no descendants to test).
    if (styleVal(style, "display") == "none") return nullptr;
    // visibility:hidden still occupies space but is not hit-testable
    if (styleVal(style, "visibility") == "hidden") return nullptr;
    // Flow-collapsed content (closed <details> body) has real geometry but
    // is never rendered or interactive — see -x-flow-collapse in block.cpp.
    if (styleVal(style, "-x-flow-collapse") == "collapse") return nullptr;
    // pointer-events:none makes *this node* non-hittable, but descendants
    // with pointer-events:auto must still be reachable. Track it and skip
    // returning `node` below — children are still traversed normally.
    bool pointerEventsNone = (styleVal(style, "pointer-events") == "none");

    // Subtree prune: box.hitBounds (cached at layout time) is the union of this
    // node and every descendant, in the same space as the incoming point + the
    // accumulated offset. If the point is outside it, nothing here can be hit —
    // skip the whole branch without the per-node alloc/sort/recursion below.
    // width < 0 is the "not computed" sentinel (e.g. hitTest without a layout
    // pass); pruning is skipped then and the full walk runs as before.
    const Rect& hb = node->box.hitBounds;
    if (hb.width >= 0.0f &&
        (x < hb.x + offsetX || x >= hb.x + offsetX + hb.width ||
         y < hb.y + offsetY || y >= hb.y + offsetY + hb.height))
        return nullptr;

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

    // Children's positions are relative to this node's content area, offset
    // by this element's scroll position (scrollLeft/scrollTop shift the
    // visible content in the opposite direction).
    float childOffsetX = absX - node->scrollLeftPx();
    float childOffsetY = absY - node->scrollTopPx();

    // Fast path: when no child is positioned and every child's z-index is
    // auto/0 (the overwhelmingly common case), paint order == source order, so
    // hit order is simply reverse source order — no per-hit vector alloc + sort
    // needed. Only fall back to the full stacking sort when some child is
    // positioned or z-ordered.
    bool needsSort = false;
    for (auto* child : children) {
        if (!child) continue;
        const auto& cs = child->computedStyle();
        const std::string& p = styleVal(cs, "position");
        if (p == "absolute" || p == "relative" || p == "fixed" || p == "sticky") {
            needsSort = true; break;
        }
        if (getZIndex(cs) != 0) { needsSort = true; break; }
    }

    if (!needsSort) {
        for (size_t i = children.size(); i-- > 0; ) {
            LayoutNode* hit = hitTestRecursive(children[i], testX, testY,
                                                childOffsetX, childOffsetY);
            if (hit) return hit;
        }
        // No child hit — fall through to the self-test below.
    } else {

    // Sort children by CSS stacking order for hit testing (topmost first).
    // Per CSS: positioned elements paint above non-positioned; within the same
    // category, higher z-index paints above lower; equal z-index uses source order
    // (later paints on top). Hit testing reverses paint order so the topmost
    // (last-painted) element is tested first.
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

    for (auto& zc : zChildren) {
        LayoutNode* hit = hitTestRecursive(zc.node, testX, testY,
                                            childOffsetX, childOffsetY);
        if (hit) return hit;
    }

    } // end stacking-sort path

    // No child hit — this node is the deepest match only if the point is
    // actually inside its bounds (overflow:visible descendants may have
    // extended us past the border box, but the node itself is not hittable
    // there). pointer-events:none keeps this node out of the result even
    // when the point is inside.
    if (insideBounds && !pointerEventsNone) return node;
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
    // Walk all the way to the root. Stopping at the first already-dirty
    // ancestor would assume every dirty node eventually reaches layoutNode()
    // and has its flag cleared — which is false for the structural nodes some
    // formatting contexts position by hand (table rows and row groups get
    // their boxes written directly by layoutTable, never via layoutNode). Such
    // a node stays dirty forever and would swallow the walk, leaving the
    // container above it clean and its whole subtree skipped as unchanged.
    for (LayoutNode* p = node->parent(); p; p = p->parent())
        p->box.dirty = true;
}

void layoutTreeIncremental(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    layoutTree(root, viewportWidth, metrics);
}

// Layout-affecting properties: if any of these change, relayout is needed.
// Properties not in this set only need repaint.
static const std::vector<std::string_view>& layoutProperties() {
    static const std::vector<std::string_view> props = {
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
    if (auto* before = node->pseudoBefore()) {
        result.push_back(before);
    }
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
    if (auto* after = node->pseudoAfter()) {
        result.push_back(after);
    }
    return result;
}

bool needsRelayout(std::initializer_list<std::string_view> changedProperties) {
    auto& lp = layoutProperties();
    for (auto prop : changedProperties) {
        for (auto lProp : lp) {
            if (prop == lProp) return true;
        }
    }
    return false;
}

} // namespace htmlayout::layout
