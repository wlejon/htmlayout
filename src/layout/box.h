#pragma once
#include "css/cascade.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <initializer_list>
#include <limits>
#include <memory>
#include <cstdint>
#include <utility>

namespace htmlayout::layout {

// A positioned rectangle
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

// Edge values (margin, padding, border)
struct Edges {
    float top = 0, right = 0, bottom = 0, left = 0;
};

// Geometry of one placed text run within a TextNode's layout box. Populated
// by inline layout for each run returned by breakTextIntoRuns. Coordinates
// are in the same space as LayoutBox.contentRect (relative to the containing
// block's border box for block children; absolute after layoutTree converts).
//
// srcStart/srcEnd are byte offsets into the original (uncollapsed) source
// text of the TextNode, so callers can map DOM Range endpoints to runs for
// caret placement and selection rendering. The displayed `text` is the
// post-processing string the renderer draws and may be shorter than
// (srcEnd - srcStart) when whitespace was collapsed.
struct PlacedTextRun {
    int   srcStart  = 0;
    int   srcEnd    = 0;
    float x         = 0;
    float y         = 0;
    float width     = 0;
    float height    = 0;
    std::string text; // rendered substring (for prefix-width lookups)
};

// The output of layout: a positioned box with resolved geometry
struct LayoutBox {
    Rect contentRect;       // content area position and size
    Edges margin;
    Edges padding;
    Edges border;

    // Natural content height before min/max-height clamping (for scroll extent).
    // For overflow:auto/scroll elements, scrollHeight = naturalHeight.
    float naturalHeight = 0;

    // Pure flow height of the content (line-box stack / block-children
    // extent), BEFORE any explicit height / min-max override — unlike
    // naturalHeight, which folds contentRect.height in. Table cells center
    // their content against this (a height:120px cell with one 20px line
    // must report 20 here, not 120). Negative = not computed (non-block
    // layout paths).
    float flowHeight = -1.0f;

    // Whether text content was truncated by overflow (for text-overflow: ellipsis)
    bool textTruncated = false;

    // Needs re-layout. Set by markDirty()/markSubtreeDirty() when the node's
    // style, content or structure changed; cleared by layoutNode() once the
    // node has been laid out. A node whose flag is clear holds geometry that
    // is still valid for the inputs recorded in LayoutNode's cached* fields,
    // and layoutNode() skips it (and its whole subtree) outright.
    //
    // Text nodes are never passed to layoutNode() — their parent's inline
    // layout owns their boxes — so their flag stays set, which is what makes
    // a dirty parent always re-measure its text.
    bool dirty = true;

    // Placed text runs — filled for text nodes during inline layout. Empty
    // for every other kind of node. Cleared on each relayout.
    std::vector<PlacedTextRun> textRuns;

    // Floats established by descendants that this block does NOT contain
    // (it is not a block-formatting-context root, so per CSS2 §9.4.1 the
    // floats belong to the nearest BFC ancestor and keep intruding into
    // following content). Coordinates are margin boxes relative to this
    // box's content origin; the parent adopts them into its own float list.
    // shapeR >= 0 carries a shape-outside: circle() exclusion (center cx/cy,
    // radius r in the same coordinate space).
    struct EscapedFloat {
        float x = 0, y = 0, width = 0, height = 0;
        bool isLeft = true;
        float shapeCx = 0, shapeCy = 0, shapeR = -1.0f;
    };
    std::vector<EscapedFloat> escapedFloats;

    // Distance from the top of contentRect to the box's text baseline, set
    // by inline layout when the box has inline-level content. Inline boxes
    // record their first-line baseline (the parent line box aligns an inline
    // child by its first line); blocks and inline-blocks record their last
    // in-flow line's baseline (CSS2 §10.8.1: the baseline of an inline-block
    // is the baseline of its last line box). Negative means "no baseline" —
    // per spec the parent then synthesizes one from the bottom margin edge.
    float baselineOffset = -1.0f;

    // Content extents of a non-replaced inline element, measured from its
    // first-line baseline (positive above/below). The element's own box is
    // only its font strip (Chromium's fragment geometry — see layoutInline),
    // so taller content (inline-block children, wrapped lines) overflows it;
    // parent line boxes size against these extents instead of the box height.
    // Deliberately EXCLUDES the element's own first-line text: that is
    // covered by the element's leaded (line-height) contribution, which with
    // a negative half-leading is smaller than the glyphs (CSS2 §10.8.1).
    // Negative means "not an inline / not computed" — parents fall back to
    // box-height-based sizing.
    float inlineExtentAbove = -1.0f;
    float inlineExtentBelow = -1.0f;

    // Subtree hit-test bounds: the union of this node's border box and all
    // descendant boxes (each descendant's own transform + clipping folded in),
    // in the SAME space as contentRect (relative to the parent's content
    // origin). hitTest() rejects an entire subtree in O(1) when the point lies
    // outside this rect — the browser "visual-overflow rect" trick that turns
    // hit testing from O(element count) into O(tree depth). Computed by the
    // post-order pass at the end of layoutTree(). width < 0 is the sentinel for
    // "not computed" — hitTest then skips pruning and walks the whole tree.
    Rect hitBounds{0, 0, -1, -1};

    // Full box dimensions including padding + border
    float fullWidth() const { return contentRect.width + padding.left + padding.right + border.left + border.right; }
    float fullHeight() const { return contentRect.height + padding.top + padding.bottom + border.top + border.bottom; }

    // Outer box including margin
    Rect marginBox() const {
        return {
            contentRect.x - padding.left - border.left - margin.left,
            contentRect.y - padding.top - border.top - margin.top,
            fullWidth() + margin.left + margin.right,
            fullHeight() + margin.top + margin.bottom
        };
    }
};

// Abstract interface for a node in the layout tree.
// Consumers implement this to bridge their DOM.
struct LayoutNode {
    virtual ~LayoutNode() = default;

    // Identity
    virtual std::string_view tagName() const = 0;
    virtual bool isTextNode() const = 0;
    virtual std::string_view textContent() const = 0;

    // Tree
    virtual LayoutNode* parent() const = 0;
    virtual std::span<LayoutNode* const> children() const = 0;

    // Computed style (from the CSS cascade)
    virtual const css::ComputedStyle& computedStyle() const = 0;

    // HTML attribute lookup for layout-affecting presentational attributes
    // (colspan, rowspan, width, height, align on table cells, etc.).
    // Default returns empty; consumers override to bridge to their DOM.
    virtual std::string_view attribute(std::string_view name) const { (void)name; return {}; }

    // Replaced element intrinsic size (e.g. <input>, <textarea>, <select>).
    // Returns true if this node has an intrinsic size, false otherwise.
    virtual bool intrinsicSize(float& w, float& h, float maxWidth) const { return false; }

    // True when intrinsicSize() reports a *fixed intrinsic aspect ratio* — i.e.
    // replaced media (<img>, <canvas>, <video>, <svg>) whose width and height
    // scale together. Form controls (<input>, <textarea>, <select>) report an
    // intrinsic size but NOT a locked ratio, so they return false. Layout uses
    // this to preserve the ratio when one axis is constrained away from its
    // intrinsic value (CSS2 §10.4) — e.g. max-width shrinking a canvas's width
    // must shrink its height to match. Default false.
    virtual bool hasIntrinsicRatio() const { return false; }

    // Scroll offsets in px. Used by hit testing to map the test point into
    // the child coordinate space when an element scrolls its content. Default
    // is 0 (no scrolling). Consumers that expose scrolling containers should
    // override these.
    virtual float scrollLeftPx() const { return 0.0f; }
    virtual float scrollTopPx()  const { return 0.0f; }

    // Generated content boxes for ::before / ::after. Consumers return a
    // synthesized LayoutNode for each pseudo-element whose `content` resolves
    // to a non-empty string, otherwise null. The pseudo is treated as an
    // anonymous inline child prepended (before) or appended (after) to the
    // element's child sequence by getLayoutChildren().
    virtual LayoutNode* pseudoBefore() const { return nullptr; }
    virtual LayoutNode* pseudoAfter()  const { return nullptr; }

    // Output: layout writes the positioned box here
    LayoutBox box;

    // Available height from containing block (for percentage height resolution).
    // Set by the parent before layout; 0 means percentage heights resolve to auto.
    float availableHeight = 0;

    // Flexed used content width, set (>= 0) by a flex container before laying
    // out a row-direction item whose main size was grown/shrunk away from its
    // style width. Block layout uses it in place of the resolved style width
    // so the item's CHILDREN see the flexed size, not the specified one.
    // -1 means "no override". The flex container resets it after layout.
    float overrideContentWidth = -1.0f;

    // Viewport height, propagated from root to all descendants.
    // Used as fallback for absolute elements whose containing block has no definite height.
    float viewportHeight = 0;

    // ---- Incremental layout ----
    //
    // The inputs of the pass that produced `box`. Everything layout learns
    // about a node from outside it arrives through these three values (plus
    // the two document-globals below), so a clean node re-entered with the
    // same inputs would recompute exactly the box it already has — layoutNode()
    // returns immediately and its entire subtree is skipped. The node's own
    // position is written by its parent *after* layoutNode() returns, so a
    // reused box needs no repositioning of its own; contentRect.x/y being
    // stale on entry is expected.
    //
    // NaN never compares equal, so a node that has never been laid out (or was
    // reset) always runs.
    float cachedAvailWidth    = std::numeric_limits<float>::quiet_NaN();
    float cachedAvailHeight   = std::numeric_limits<float>::quiet_NaN();
    float cachedOverrideWidth = std::numeric_limits<float>::quiet_NaN();

    // The pass in which layoutNode() last cleared this box. Layout is allowed
    // to call layoutNode() on the same node more than once per pass — a flex or
    // grid container measures an item, resolves its tracks, writes the used size
    // straight into the item's box, and lays it out again to distribute that
    // size — so the box is cleared on the *first* call of each pass and left
    // alone on the rest, which is what lets those writes survive as inputs.
    uint32_t lastLayoutPass = 0;

    // The box position box.hitBounds was last derived against. A pass that skips
    // this subtree (nothing in it laid out) compares it to the current position
    // to see how far the parent has moved the box, and translates the cached
    // bounds by that much instead of re-deriving them. See computeSubtreeHitBounds.
    float hitBoundsOriginX = 0.0f;
    float hitBoundsOriginY = 0.0f;

    // Cached intrinsic (content-based) widths — the result of
    // computeMin/MaxContentWidth over this subtree. Those walks are recursive
    // over every descendant, so without a cache one dirty flex/grid/shrink-to-fit
    // ancestor re-measures its entire subtree per pass. Intrinsic widths depend
    // only on subtree content and style, and any change to either arrives via
    // markDirty()/markSubtreeDirty() on a node inside the subtree — whose
    // walk-to-root passes through here — so those are the invalidation points.
    // -1 = not computed (intrinsic widths are never negative).
    float cachedMinContentW = -1.0f;
    float cachedMaxContentW = -1.0f;

    // Flex measure cache. A flex container computes an auto-basis item's
    // hypothetical size by laying it out once before the real layout — for a
    // column container that measure is the item's laid-out height at the
    // container's inner width. Cache that scalar (keyed by the width it was
    // measured at) so a clean item doesn't re-lay its whole subtree just to
    // re-derive it. Invalidated by the markDirty()/markSubtreeDirty() walks,
    // like the intrinsic widths above.
    float measuredAtW = std::numeric_limits<float>::quiet_NaN();
    float measuredOuterMain = -1.0f;

    // The flex item cross size this node reported when it was last actually
    // laid out by its flex container (outer: content + padding + border +
    // margin). The box itself can't provide it later: align stretch writes
    // the stretched size back into the box, so re-deriving the cross size
    // from a reused box would feed the previous line height back into the
    // next line calculation and lines could never shrink. Recomputed on
    // every real (non-reused) item layout.
    float flexNaturalCross = -1.0f;

    // Grid measure cache. Before it can size auto tracks a grid container lays
    // every item out once at a rough guess at the column width, and reads the
    // laid-out outer size back as the item's track contribution. That guess is
    // essentially never the column width the tracks then resolve to, so the
    // measure could never hit the reuse cache — every item re-laid its whole
    // subtree here, every pass, before the real layout re-laid it again. The
    // measure is a pure function of (subtree, style, width), so cache its two
    // outputs keyed by the width they were taken at. Invalidated by the
    // markDirty()/markSubtreeDirty() walks, like the flex cache above.
    float gridMeasuredAtW = std::numeric_limits<float>::quiet_NaN();
    float gridMeasuredOuterW = -1.0f;
    float gridMeasuredOuterH = -1.0f;

    // What this node contributed to its grid container's row sizing, and the
    // content height it had, when it was last actually laid out at its resolved
    // column width. Same reason flexNaturalCross exists: align-stretch writes
    // the stretched height straight into the box, so a reused box reports last
    // pass's stretched height as this pass's contribution and rows could only
    // ever ratchet upward. Recomputed on every real (non-reused) item layout.
    // NaN = never recorded, which a box reused on its first pass as a grid item
    // can be (a container restyled to display:grid dirties itself, not its
    // children) — negative margins make -1 an unsafe sentinel here.
    float gridNaturalOuterH = std::numeric_limits<float>::quiet_NaN();
    float gridNaturalContentH = std::numeric_limits<float>::quiet_NaN();

    // Whether this subtree contains any absolute/fixed element (the node
    // itself included). Lets layoutAbsoluteElements() skip whole clean
    // branches instead of reading position/display on every node each pass.
    // Positioned elements themselves are still re-laid every pass — their
    // containing block can move without any signal reaching them — so this
    // only prunes branches with none. Invalidated (-1) by the same
    // markDirty()/markSubtreeDirty() walks that keep the caches above honest;
    // a position/display change is a layout-affecting style change, so it
    // always arrives through one of them.
    int8_t subtreeHasPositioned = -1;

    // Root node only: the document-global inputs that any node's lengths may
    // resolve against — the viewport (vw/vh/vmin/vmax) and the root font-size
    // (rem). A subtree that is otherwise untouched still has to re-layout when
    // either changes, and has no local signal to notice it, so layoutTree()
    // compares these on the root and dirties the whole tree when they move.
    float cachedViewportW    = std::numeric_limits<float>::quiet_NaN();
    float cachedViewportH    = std::numeric_limits<float>::quiet_NaN();
    float cachedRootFontSize = std::numeric_limits<float>::quiet_NaN();
};

// Text measurement callback — consumers provide this (e.g. via Skia)
struct TextMetrics {
    virtual ~TextMetrics() = default;

    // Instrumentation: bumped by the consumer's implementation on every
    // measurement it serves. Layout never reads or resets it; the consumer does,
    // around a pass it wants to attribute. Shaping text is the most expensive
    // thing layout asks anyone to do, so this is the number that explains a pass
    // whose cost the node counts alone do not.
    uint64_t measureCalls = 0;
    virtual float measureWidth(std::string_view text,
                                std::string_view fontFamily,
                                float fontSize,
                                std::string_view fontWeight) = 0;
    virtual float lineHeight(std::string_view fontFamily,
                              float fontSize,
                              std::string_view fontWeight) = 0;
    // Distance from the top of the font's natural line box (ascent+descent
    // (+leading)) to the alphabetic baseline. Used for CSS2 §10.8 baseline
    // alignment of line-box contents. The default approximates the ascent as
    // 80% of the natural line height — close to the ascent share of common
    // UI fonts — so consumers that only implement the two pure virtuals keep
    // working; override to supply exact metrics from the font backend.
    virtual float ascent(std::string_view fontFamily,
                         float fontSize,
                         std::string_view fontWeight) {
        return 0.8f * lineHeight(fontFamily, fontSize, fontWeight);
    }
    // x-height of the font (height of a lowercase 'x' above the baseline).
    // vertical-align: middle centers a box on baseline + xHeight/2 (CSS2
    // §10.8). The default approximates 1ex = 0.5em — the CSS fallback
    // ratio — so consumers that only implement the pure virtuals keep
    // working; override to supply the real metric from the font backend.
    virtual float xHeight(std::string_view fontFamily,
                          float fontSize,
                          std::string_view fontWeight) {
        (void)fontFamily; (void)fontWeight;
        return 0.5f * fontSize;
    }
    // Natural height of a text run's rect: round(ascent) + round(descent),
    // WITHOUT the font's line gap. Chromium reports inline/text-run rects
    // at this height (17 for 16px Arial) while line-height:normal — what
    // lineHeight() returns — additionally includes round(lineGap) (18).
    // The default returns lineHeight() so existing TextMetrics
    // implementations keep their current behavior; override to supply the
    // exact metric from the font backend.
    virtual float naturalHeight(std::string_view fontFamily,
                                float fontSize,
                                std::string_view fontWeight) {
        return lineHeight(fontFamily, fontSize, fontWeight);
    }
};

// Viewport dimensions for layout
struct Viewport {
    float width = 0;
    float height = 0;
};

// Perform layout on a tree, computing LayoutBox for every node.
// viewportWidth is the available width for the root element.
//
// Layout is incremental: only nodes marked dirty (see markDirty /
// markSubtreeDirty), nodes whose available space changed, and their ancestors
// are recomputed — every other subtree keeps the geometry it already has. A
// caller that marks nothing therefore gets no work done, so a consumer must
// mark what it changed, or markSubtreeDirty(root) for an unconditional pass.
void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics);

// Layout with explicit viewport dimensions for proper vw/vh resolution.
void layoutTree(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics);

// Apply overflow clipping to a laid-out tree.
// Children of nodes with overflow:hidden/scroll/auto are clipped to the parent's
// content+padding box. Call after layoutTree().
void applyOverflowClipping(LayoutNode* root);

// Hit test: find the deepest LayoutNode at a given point.
// Returns null if the point is outside the root's box.
LayoutNode* hitTest(LayoutNode* root, float x, float y);

// Get children for layout, flattening any 'display: contents' nodes into the parent's sequence.
std::vector<LayoutNode*> getLayoutChildren(LayoutNode* node);

// Mark a node as needing re-layout, and every ancestor up to the root with it
// (a changed child can resize its parent, which can resize *its* parent, so the
// whole chain has to be recomputed for the change to reach the page).
void markDirty(LayoutNode* node);

// Mark a node and everything below it as needing re-layout — the unconditional
// pass. Consumers use this when a change is not attributable to individual
// nodes: a fresh layout tree, a new stylesheet, a resize of something the
// layout tree can't see.
void markSubtreeDirty(LayoutNode* node);

// Monotonic counter, bumped once per layoutTree() call. See LayoutNode::lastLayoutPass.
uint32_t currentLayoutPass();

// What the last layoutTree() call actually did. Reset at the top of every pass,
// so after one it describes exactly that pass.
//
// This is the signal for "is the incremental layout actually incremental".
// Wall-clock alone can't tell a slow pass from a big one: a change to a single
// element that comes back with `laidOut` in the thousands did not reuse the
// tree, and no amount of making layout faster will fix that — something
// upstream invalidated more than it had to.
struct LayoutStats {
    uint32_t laidOut = 0;   // ran a formatting context (first visit this pass)
    uint32_t reused  = 0;   // handed back its cached subtree untouched
    // Every layoutNodeInner() call, including the re-entrant ones: a flex or grid
    // container lays an item out to measure it, then again to push the resolved
    // size through. `visits` far above `laidOut` means the pass is dominated by
    // that re-measurement, not by the nodes that actually changed.
    uint32_t visits  = 0;
    // The three passes layoutTree() runs, separately: only the first is
    // incremental, so a pass whose cost doesn't move with `laidOut` is being
    // spent in one of the other two.
    double treeMs = 0;        // in-flow layout (incremental)
    double absoluteMs = 0;    // positioning absolute/fixed boxes
    double hitBoundsMs = 0;   // caching per-node subtree hit bounds

    // Every styleVal() map lookup during the pass. Layout re-derives all of a
    // node's style from string keys on every visit, so this scales with
    // visits × properties-consulted — the constant factor of the pass.
    uint64_t styleLookups = 0;

    // measureWidth() calls layout made, and how many of those the per-pass
    // MeasureCache had to forward to the consumer's shaper. Shaping is the most
    // expensive thing layout asks anyone to do, so the gap between these two is
    // the work the cache is absorbing; `textShaped` is what the consumer pays.
    uint64_t textMeasures = 0;
    uint64_t textShaped   = 0;

    // resolveLength() calls: every one re-parses a value out of its text, since
    // the style map stores what the stylesheet said rather than what it meant.
    // Sits next to styleLookups because the two together are the price of style
    // being strings — one to find the value, one to turn it back into a number.
    uint64_t lengthResolves = 0;

    // Why beginLayoutNode() declined to reuse, by first failing condition.
    // `reused` low with laidOut high is only half a diagnosis — these say
    // whether the invalidation was real dirt or a changed layout input
    // (available width/height, flex override) cascading down a clean tree.
    uint32_t reuseFailDirty    = 0;
    uint32_t reuseFailAvailW   = 0;
    uint32_t reuseFailAvailH   = 0;
    uint32_t reuseFailOverride = 0;
};
const LayoutStats& lastLayoutStats();

// Internal: the counters the layout pass writes through.
LayoutStats& layoutStatsMut();

#ifdef HTMLAYOUT_STYLE_PROFILE
// Which properties layout actually reads, and how often. `styleLookups` says the
// pass spends 10ms finding values in a map; this says *which* values, so the set
// that gets a typed field is chosen from the profile rather than from counting
// call sites in the source (the two disagree badly — a property read once inside
// a per-child loop outweighs one read at twenty different sites).
//
// Off by default: recording costs more than the lookup it measures.
void recordStyleLookup(std::string_view prop, const char* file, unsigned line);
// Both cuts of the same data: which property, and which line asked for it. They
// disagree, and the disagreement is the finding — a property read once per child
// inside a loop shows up nowhere in the source and everywhere in the profile.
std::vector<std::pair<std::string, uint64_t>> styleLookupHistogram();     // by property
std::vector<std::pair<std::string, uint64_t>> styleLookupSiteHistogram(); // by call site
void resetStyleLookupHistogram();
#endif

// Deprecated: layoutTree() is itself incremental now. Kept as an alias.
void layoutTreeIncremental(LayoutNode* root, float viewportWidth, TextMetrics& metrics);

// Style invalidation: given a set of changed property names,
// determine if a node needs re-layout or just re-paint.
// Returns true if any layout-affecting property changed.
bool needsRelayout(std::initializer_list<std::string_view> changedProperties);

} // namespace htmlayout::layout
