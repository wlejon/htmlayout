#include <chrono>
#include "layout/box.h"
#include "css/transform.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include "layout/measure_cache.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace htmlayout::layout {

using layout::styleVal;

// Out of line because LayoutNode::styleCache is a unique_ptr to NodeStyleCache,
// which box.h only forward-declares. Here the type is complete.


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

// Same single-threaded, one-tree-at-a-time reasoning as g_layoutPass.
static LayoutStats g_layoutStats;

const LayoutStats& lastLayoutStats() { return g_layoutStats; }
LayoutStats& layoutStatsMut() { return g_layoutStats; }

#ifdef HTMLAYOUT_STYLE_PROFILE
static std::unordered_map<std::string, uint64_t, css::SvHash, css::SvEq> g_styleHist;
static std::unordered_map<std::string, uint64_t, css::SvHash, css::SvEq> g_styleSiteHist;

void recordStyleLookup(std::string_view prop, const char* file, unsigned line) {
    auto it = g_styleHist.find(prop);
    if (it == g_styleHist.end()) g_styleHist.emplace(std::string(prop), 1);
    else it->second++;

    std::string_view f(file);
    if (auto slash = f.find_last_of("/\\"); slash != std::string_view::npos)
        f.remove_prefix(slash + 1);
    std::string key = std::string(f) + ":" + std::to_string(line) + " " + std::string(prop);
    auto sit = g_styleSiteHist.find(key);
    if (sit == g_styleSiteHist.end()) g_styleSiteHist.emplace(std::move(key), 1);
    else sit->second++;
}
void resetStyleLookupHistogram() { g_styleHist.clear(); g_styleSiteHist.clear(); }

static std::vector<std::pair<std::string, uint64_t>>
sorted(const std::unordered_map<std::string, uint64_t, css::SvHash, css::SvEq>& m) {
    std::vector<std::pair<std::string, uint64_t>> out(m.begin(), m.end());
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}
std::vector<std::pair<std::string, uint64_t>> styleLookupHistogram() { return sorted(g_styleHist); }
std::vector<std::pair<std::string, uint64_t>> styleLookupSiteHistogram() { return sorted(g_styleSiteHist); }
#endif

void markSubtreeDirty(LayoutNode* node) {
    if (!node) return;
    node->box.dirty = true;
    node->cachedMinContentW = -1.0f;
    node->cachedMaxContentW = -1.0f;
    node->measuredAtW = std::numeric_limits<float>::quiet_NaN();
    node->gridMeasuredAtW = std::numeric_limits<float>::quiet_NaN();
    node->subtreeHasPositioned = -1;
    for (auto* child : node->children()) markSubtreeDirty(child);
    // ::before / ::after wrappers hang off the node outside children().
    if (auto* p = node->pseudoBefore()) markSubtreeDirty(p);
    if (auto* p = node->pseudoAfter())  markSubtreeDirty(p);
}

void layoutTree(LayoutNode* root, const Viewport& viewport, TextMetrics& consumerMetrics) {
    if (!root) return;
    ++g_layoutPass;
    g_layoutStats = {};

    // Everything below measures text through a memo that lives exactly as long as
    // this pass. Layout asks the same question ~130 times over (min-content sizing,
    // max-content sizing and line breaking each walk the same words; a third of the
    // asks are the width of one space), and shaping is the most expensive thing it
    // asks the consumer to do. See MeasureCache.
    MeasureCache cache(consumerMetrics);
    TextMetrics& metrics = cache;

    // Style is memoized per node for the length of this pass, on the same
    // reasoning: layout visits a node ~2.75 times and asks it for the same
    // properties each time, and nothing can rewrite a style while a synchronous
    // pass is running. Outside the pass the memo is not trusted at all — see
    // style_cache.h — so the window has to close even if a formatting context
    // throws its way out of here.
    struct StylePassScope {
        StylePassScope() { beginStyleCachePass(); }
        ~StylePassScope() { endStyleCachePass(); }
    } stylePass;

    setLayoutViewport(viewport.width, viewport.height);
    // rem resolves against the root element's font-size. The root's computed
    // font-size is already absolute (px/unitless) by the time it reaches layout,
    // so resolve it against the initial 16px base (em/% would compound off 16).
    float rootFontSize = resolveLength(styleVal(root, Prop::FontSize),
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
    using clk = std::chrono::steady_clock;
    const auto ms = [](clk::time_point a, clk::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto t0 = clk::now();
    layoutNode(root, viewport.width, metrics);
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;
    auto t1 = clk::now();

    // Pass 2: position all absolute/fixed elements against their correct containing blocks
    layoutAbsoluteElements(root, viewport, metrics);
    auto t2 = clk::now();

    // Pass 3: cache per-node subtree hit-bounds so hit testing can prune whole
    // branches instead of walking every element on each mouse move.
    computeSubtreeHitBounds(root);
    auto t3 = clk::now();

    g_layoutStats.treeMs = ms(t0, t1);
    g_layoutStats.absoluteMs = ms(t1, t2);
    g_layoutStats.hitBoundsMs = ms(t2, t3);
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
int getZIndex(const LayoutNode* node) {
    const std::string& z = styleVal(node, Prop::ZIndex);
    if (z.empty() || z == "auto") return 0;
    try { return std::stoi(z); } catch (...) { return 0; }
}

// Check if an element creates a stacking context
bool createsStackingContext(const LayoutNode* node) {
    const std::string& pos = styleVal(node, Prop::Position);
    const std::string& z = styleVal(node, Prop::ZIndex);
    // position:fixed and position:sticky always create a stacking context
    if (pos == "fixed" || pos == "sticky") return true;
    // Other positioned elements with z-index other than auto
    if ((pos == "absolute" || pos == "relative") &&
        !z.empty() && z != "auto") {
        return true;
    }
    // opacity < 1 creates a stacking context
    const std::string& op = styleVal(node, Prop::Opacity);
    if (!op.empty() && op != "1") {
        try {
            float opVal = std::stof(op);
            if (opVal < 1.0f) return true;
        } catch (...) {}
    }
    // transform other than none
    const std::string& tr = styleVal(node, Prop::Transform);
    if (!tr.empty() && tr != "none") return true;
    // filter other than none
    const std::string& ft = styleVal(node, Prop::Filter);
    if (!ft.empty() && ft != "none") return true;
    // isolation: isolate
    if (styleVal(node, Prop::Isolation) == "isolate") return true;
    return false;
}

// Does this element's own `overflow` ask for clipping, ignoring who the clip
// ultimately belongs to?
bool overflowStyleClips(const LayoutNode* node) {
    auto check = [](const std::string& v) {
        return !v.empty() && v != "visible";
    };
    if (check(styleVal(node, Prop::Overflow))) return true;
    if (check(styleVal(node, Prop::OverflowX))) return true;
    if (check(styleVal(node, Prop::OverflowY))) return true;
    return false;
}

// CSS 2.1 §11.1.1: the root element's `overflow` propagates to the viewport,
// and when the root computes to `visible` the <body> donates in its place. The
// donor is left behaving as `visible` itself — the viewport does that clipping,
// not the element.
//
// Hit testing has to honour that, because paint does. A page whose body is
// `overflow: hidden` with absolutely positioned children — the whole three.js
// editor, and every other app laid out that way — gives body a zero-height box,
// so treating body as a clipper rejects the entire document at the first step:
// the UI paints perfectly and swallows every click.
bool overflowBelongsToViewport(const LayoutNode* node) {
    const LayoutNode* parent = node->parent();
    if (!parent) return true;                   // the root element itself
    if (parent->parent()) return false;         // deeper than <body>

    std::string_view tag = node->tagName();
    if (tag.size() != 4) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (std::tolower(static_cast<unsigned char>(tag[i])) != "body"[i]) return false;
    }

    // The root donates first; <body> only gets to when the root is visible.
    return !overflowStyleClips(parent);
}

// Does this element clip descendants' hit testing to its border box?
// Matches the overflow-clipping rules used at paint time.
bool clipsHitTesting(const LayoutNode* node) {
    if (!overflowStyleClips(node)) return false;
    return !overflowBelongsToViewport(node);
}

// Does this element take over as the containing block for fixed-position
// descendants? CSS Transforms §3 and CSS Containment: a transform, a filter, a
// perspective, or paint containment all take the job away from the viewport.
// Without one of these, a fixed box is positioned against the viewport and no
// ancestor's `overflow` can clip it.
bool establishesFixedContainingBlock(const LayoutNode* node) {
    auto set = [node](Prop p) {
        const std::string& v = styleVal(node, p);
        return !v.empty() && v != "none";
    };
    if (set(Prop::Transform) || set(Prop::Filter)) return true;
    const std::string& c = styleVal(node, Prop::Contain);
    return c.find("paint") != std::string::npos ||
           c.find("layout") != std::string::npos ||
           c.find("strict") != std::string::npos ||
           c.find("content") != std::string::npos;
}

bool isPositionedNode(const LayoutNode* node) {
    const std::string& p = styleVal(node, Prop::Position);
    return p == "relative" || p == "absolute" || p == "fixed" || p == "sticky";
}

// Union b into a, treating width < 0 as "empty".
void unionInto(Rect& a, const Rect& b) {
    if (b.width < 0.0f) return;
    if (a.width < 0.0f) { a = b; return; }
    float minX = std::min(a.x, b.x), minY = std::min(a.y, b.y);
    float maxX = std::max(a.x + a.width, b.x + b.width);
    float maxY = std::max(a.y + a.height, b.y + b.height);
    a = {minX, minY, maxX - minX, maxY - minY};
}

Rect shifted(const Rect& r, float dx, float dy) {
    if (r.width < 0.0f) return r;
    return {r.x + dx, r.y + dy, r.width, r.height};
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

    // Nothing in this subtree ran a formatting context this pass, so its shape is
    // exactly the shape it already has. The parent may still have *moved* it —
    // a reused node's position is written by its parent after layoutNode()
    // returns — and hitBounds lives in the parent's content space, so it shifts
    // with the box. But everything below is expressed relative to this box and
    // travels with it, so none of it has to be revisited.
    //
    // Without this the pass is O(document) on every layout, which quietly undoes
    // the incremental layout above it: a one-element change would still walk and
    // re-derive bounds for every node in the document.
    if (node->lastLayoutPass != currentLayoutPass() && node->box.hitBounds.width >= 0) {
        float dx = node->box.contentRect.x - node->hitBoundsOriginX;
        float dy = node->box.contentRect.y - node->hitBoundsOriginY;
        if (dx != 0.0f || dy != 0.0f) {
            node->box.hitBounds.x += dx;
            node->box.hitBounds.y += dy;
            node->escapeAbsBounds = shifted(node->escapeAbsBounds, dx, dy);
            node->escapeFixedBounds = shifted(node->escapeFixedBounds, dx, dy);
            node->hitBoundsOriginX = node->box.contentRect.x;
            node->hitBoundsOriginY = node->box.contentRect.y;
        }
        return node->box.hitBounds;
    }

    const auto& style = node->computedStyle();
    node->escapeAbsBounds = {0.0f, 0.0f, -1.0f, -1.0f};
    node->escapeFixedBounds = {0.0f, 0.0f, -1.0f, -1.0f};
    if (styleVal(node, Prop::Display) == "none") {
        node->box.hitBounds = {0, 0, 0, 0};
        node->hitBoundsOriginX = node->box.contentRect.x;
        node->hitBoundsOriginY = node->box.contentRect.y;
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
    bool clips = clipsHitTesting(node);
    // Which escaping descendants stop here: this node is the containing block
    // an absolutely positioned box was looking for if it is positioned at all
    // (or carries a transform), and the one a fixed box was looking for only
    // with a transform-like property.
    const bool absorbsFixed = establishesFixedContainingBlock(node);
    const bool absorbsAbs = absorbsFixed || isPositionedNode(node);

    Rect escAbs{0.0f, 0.0f, -1.0f, -1.0f};      // still travelling up, in our space
    Rect escFixed{0.0f, 0.0f, -1.0f, -1.0f};

    for (auto* child : node->children()) {
        Rect cb = computeSubtreeHitBounds(child);   // in our content space
        if (!child) continue;

        // What the child hands up, plus the child's own box when the child is
        // itself the out-of-flow box doing the escaping.
        Rect childAbs = child->escapeAbsBounds;
        Rect childFixed = child->escapeFixedBounds;
        const std::string& cpos = styleVal(child, Prop::Position);
        if (cpos == "absolute")   unionInto(childAbs, cb);
        else if (cpos == "fixed") unionInto(childFixed, cb);

        // Absorbed escapers have found their containing block: from here they
        // are ordinary content of this node, and this node's clip applies.
        Rect absorbed{0.0f, 0.0f, -1.0f, -1.0f};
        if (absorbsAbs)   { unionInto(absorbed, childAbs);   childAbs = {0, 0, -1, -1}; }
        if (absorbsFixed) { unionInto(absorbed, childFixed); childFixed = {0, 0, -1, -1}; }

        auto lift = [&](const Rect& r) { return shifted(r, childOffX, childOffY); };
        auto grow = [&](const Rect& r) {
            if (r.width < 0.0f) return;
            minX = std::min(minX, r.x);           minY = std::min(minY, r.y);
            maxX = std::max(maxX, r.x + r.width); maxY = std::max(maxY, r.y + r.height);
        };

        // Whatever this node clips never enlarges it (descendants still carry
        // their own hitBounds for pruning once the point is known to be inside).
        if (!clips) {
            if (cb.width > 0 || cb.height > 0) grow(lift(cb));
            grow(lift(absorbed));
        }
        // What escapes does enlarge it, clip or no clip — that is the whole
        // point, and the prune above consults exactly this rect.
        grow(lift(childAbs));
        grow(lift(childFixed));
        unionInto(escAbs, lift(childAbs));
        unionInto(escFixed, lift(childFixed));
    }

    node->escapeAbsBounds = escAbs;
    node->escapeFixedBounds = escFixed;

    Rect bounds{minX, minY, maxX - minX, maxY - minY};

    // Fold in this node's own transform — its whole box + subtree move together,
    // mirroring hitTestRecursive's forward transform about transform-origin.
    const std::string& transform = styleVal(node, Prop::Transform);
    if (!transform.empty() && transform != "none") {
        css::Matrix2D mat = css::parseTransform(transform, bw, bh);
        if (!mat.isIdentity()) {
            float ox, oy;
            css::parseTransformOrigin(styleVal(node, Prop::TransformOrigin), bw, bh, ox, oy);
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
    // The box position these bounds were derived against, so a later pass that
    // skips this subtree can tell how far its parent has since moved it.
    node->hitBoundsOriginX = node->box.contentRect.x;
    node->hitBoundsOriginY = node->box.contentRect.y;
    return bounds;
}

// Hit test with offset accumulation: positions are relative to parent content area,
// so we track the accumulated offset from the root.
// State carried down past a clipping ancestor the point fell outside of. While
// `active`, only boxes that escape that clip may be hit; `sawPositioned` and
// `sawFixedCB` record whether anything on the way down has since become the
// containing block the escapers were looking for, which would put them back
// inside the clip after all.
struct ClipEscape {
    bool active = false;
    bool sawPositioned = false;
    bool sawFixedCB = false;
};

LayoutNode* hitTestRecursive(LayoutNode* node, float x, float y,
                              float offsetX, float offsetY,
                              ClipEscape escape = {}) {
    if (!node) return nullptr;

    // Descending past a clip the point is outside of: this box is reachable
    // only if it is the out-of-flow box that escaped it.
    if (escape.active) {
        const std::string& pos = styleVal(node, Prop::Position);
        if ((pos == "fixed" && !escape.sawFixedCB) ||
            (pos == "absolute" && !escape.sawPositioned))
            escape = {};                       // escaped — hittable from here down
        else {
            escape.sawPositioned |= isPositionedNode(node);
            escape.sawFixedCB |= establishesFixedContainingBlock(node);
        }
    }

    auto& style = node->computedStyle();

    // Skip display:none entirely (no layout, no descendants to test).
    if (styleVal(node, Prop::Display) == "none") return nullptr;
    // visibility:hidden still occupies space but is not hit-testable
    if (styleVal(node, Prop::Visibility) == "hidden") return nullptr;
    // Flow-collapsed content (closed <details> body) has real geometry but
    // is never rendered or interactive — see -x-flow-collapse in block.cpp.
    if (styleVal(node, Prop::XFlowCollapse) == "collapse") return nullptr;
    // pointer-events:none makes *this node* non-hittable, but descendants
    // with pointer-events:auto must still be reachable. Track it and skip
    // returning `node` below — children are still traversed normally.
    bool pointerEventsNone = (styleVal(node, Prop::PointerEvents) == "none");

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
    const std::string& transform = styleVal(node, Prop::Transform);
    if (!transform.empty() && transform != "none") {
        css::Matrix2D mat = css::parseTransform(transform, bw, bh);
        if (!mat.isIdentity()) {
            float ox, oy;
            css::parseTransformOrigin(styleVal(node, Prop::TransformOrigin),
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
    // box, its own content is out of reach. Out-of-flow descendants whose
    // containing block is above this node are NOT clipped by it, though — a
    // `position: fixed` submenu hanging outside its `overflow: auto` panel is
    // the everyday case — so descend for those and nothing else. With
    // overflow:visible, we still descend in case positioned children extend
    // past our bounds.
    if (!insideBounds && clipsHitTesting(node)) {
        const bool couldEscape = node->escapeAbsBounds.width >= 0.0f ||
                                 node->escapeFixedBounds.width >= 0.0f;
        if (!couldEscape) return nullptr;
        escape.active = true;
        escape.sawPositioned = isPositionedNode(node);
        escape.sawFixedCB = establishesFixedContainingBlock(node);
    }

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
        const std::string& p = styleVal(child, Prop::Position);
        if (p == "absolute" || p == "relative" || p == "fixed" || p == "sticky") {
            needsSort = true; break;
        }
        if (getZIndex(child) != 0) { needsSort = true; break; }
    }

    if (!needsSort) {
        for (size_t i = children.size(); i-- > 0; ) {
            LayoutNode* hit = hitTestRecursive(children[i], testX, testY,
                                                childOffsetX, childOffsetY, escape);
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
            z = getZIndex(child);
            const std::string& p = styleVal(child, Prop::Position);
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
                                            childOffsetX, childOffsetY, escape);
        if (hit) return hit;
    }

    } // end stacking-sort path

    // No child hit — this node is the deepest match only if the point is
    // actually inside its bounds (overflow:visible descendants may have
    // extended us past the border box, but the node itself is not hittable
    // there). pointer-events:none keeps this node out of the result even
    // when the point is inside.
    if (insideBounds && !pointerEventsNone && !escape.active) return node;
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
    if (styleVal(node, Prop::Display) == "none") return;

    // If parent clips and this node extends outside, clip it
    if (parentClips && clipBox) {
        clipToParentPaddingBox(node->box, *clipBox);
    }

    // Check if this node clips its children
    const std::string& overflow = styleVal(node, Prop::Overflow);
    bool thisClips = (overflow == "hidden" || overflow == "scroll" || overflow == "auto" || overflow == "clip");
    // CSS Containment L2: contain: paint clips children to padding box
    if (!thisClips) {
        const std::string& contain = styleVal(node, Prop::Contain);
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
    node->cachedMinContentW = -1.0f;
    node->cachedMaxContentW = -1.0f;
    node->measuredAtW = std::numeric_limits<float>::quiet_NaN();
    node->gridMeasuredAtW = std::numeric_limits<float>::quiet_NaN();
    node->subtreeHasPositioned = -1;
    // Walk all the way to the root. Stopping at the first already-dirty
    // ancestor would assume every dirty node eventually reaches layoutNode()
    // and has its flag cleared — which is false for the structural nodes some
    // formatting contexts position by hand (table rows and row groups get
    // their boxes written directly by layoutTable, never via layoutNode). Such
    // a node stays dirty forever and would swallow the walk, leaving the
    // container above it clean and its whole subtree skipped as unchanged.
    // Intrinsic widths are content-based, so the same walk is what keeps the
    // ancestors' cachedMin/MaxContentW honest.
    for (LayoutNode* p = node->parent(); p; p = p->parent()) {
        p->box.dirty = true;
        p->cachedMinContentW = -1.0f;
        p->cachedMaxContentW = -1.0f;
        p->measuredAtW = std::numeric_limits<float>::quiet_NaN();
        p->gridMeasuredAtW = std::numeric_limits<float>::quiet_NaN();
        p->subtreeHasPositioned = -1;
    }
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
            if (styleVal(child, Prop::Display) == "contents") {
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
