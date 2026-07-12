#pragma once
#include "css/cascade.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <initializer_list>
#include <memory>
#include <cstdint>

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

    // Whether text content was truncated by overflow (for text-overflow: ellipsis)
    bool textTruncated = false;

    // Dirty flag for incremental relayout
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
};

// Text measurement callback — consumers provide this (e.g. via Skia)
struct TextMetrics {
    virtual ~TextMetrics() = default;
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

// Mark a subtree as dirty for incremental relayout.
// Marks the given node and all its ancestors as needing re-layout.
void markDirty(LayoutNode* node);

// Incremental layout: only re-layout dirty subtrees.
// Falls back to full layout if root is dirty.
void layoutTreeIncremental(LayoutNode* root, float viewportWidth, TextMetrics& metrics);

// Style invalidation: given a set of changed property names,
// determine if a node needs re-layout or just re-paint.
// Returns true if any layout-affecting property changed.
bool needsRelayout(std::initializer_list<std::string_view> changedProperties);

} // namespace htmlayout::layout
