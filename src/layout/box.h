#pragma once
#include "css/cascade.h"
#include <string>
#include <vector>
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
    virtual std::string tagName() const = 0;
    virtual bool isTextNode() const = 0;
    virtual std::string textContent() const = 0;

    // Tree
    virtual LayoutNode* parent() const = 0;
    virtual std::vector<LayoutNode*> children() const = 0;

    // Computed style (from the CSS cascade)
    virtual const css::ComputedStyle& computedStyle() const = 0;

    // Replaced element intrinsic size (e.g. <input>, <textarea>, <select>).
    // Returns true if this node has an intrinsic size, false otherwise.
    virtual bool intrinsicSize(float& w, float& h, float maxWidth) const { return false; }

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

    // Viewport height, propagated from root to all descendants.
    // Used as fallback for absolute elements whose containing block has no definite height.
    float viewportHeight = 0;
};

// Text measurement callback — consumers provide this (e.g. via Skia)
struct TextMetrics {
    virtual ~TextMetrics() = default;
    virtual float measureWidth(const std::string& text,
                                const std::string& fontFamily,
                                float fontSize,
                                const std::string& fontWeight) = 0;
    virtual float lineHeight(const std::string& fontFamily,
                              float fontSize,
                              const std::string& fontWeight) = 0;
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
bool needsRelayout(const std::vector<std::string>& changedProperties);

} // namespace htmlayout::layout
