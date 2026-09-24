#pragma once
// The line items of an inline formatting context (block_inline.cpp): what a
// block container's inline content is broken into before it is cut into line
// boxes, and the collector that builds them. Internal to the layout library.

#include "layout/block_internal.h"
#include "layout/box.h"
#include "layout/text.h"
#include <cstddef>
#include <string>
#include <vector>

namespace htmlayout::layout::ifc {

struct IFCItem {
    float width = 0, height = 0;
    LayoutNode* node = nullptr;
    bool isElement = false;
    bool forceBreak = false;
    // A float met inside an anonymous inline run: not line content, placed
    // by the block formatting context while the lines are being built.
    bool isFloat = false;
    // CSS2 §10.8 vertical geometry. Every inline-level item spans
    // [baseline - above, baseline + below] in the line box after baseline
    // alignment; the line box height is the union of these extents (plus the
    // block's strut). `baseline` is the distance from the item's positioned
    // top (run top for text, margin-box top for atomic inlines, content top
    // for non-replaced inline elements — see baselineFromContent) down to its
    // baseline.
    float above = 0;
    float below = 0;
    float baseline = 0;
    // True when `baseline` is measured from the child's contentRect top
    // (non-replaced inline elements laid out whole — their padding/border sit
    // outside the line-height geometry); false when measured from the
    // margin-box top (inline-blocks and replaced elements).
    bool baselineFromContent = false;
    // vertical-align: 0 = baseline, 1 = top, 2 = middle, 3 = bottom.
    // Non-baseline items don't move the line's baseline; they only grow the
    // line box when taller than it.
    int valign = 0;
    // Baseline shift (positive raises): vertical-align sub/super/<length> of
    // the item itself plus that of every inline element it sits in.
    float shift = 0;
    // For text runs: the post-processing display string and the source byte
    // range, so the placed run can be recorded on the text node.
    std::string text;
    int srcStart = 0;
    int srcEnd = 0;
    // Soft-wrap opportunities on each side. Text runs inherit these from the
    // word-boundary splitter; atomic inlines always offer them.
    bool canBreakBefore = false;
    bool canBreakAfter  = false;
    // The element whose content box this item's geometry ends up in: the
    // block for its own children, an inline element for what sits inside it.
    LayoutNode* owner = nullptr;
    // +1 opens inline element `node`, -1 closes it (width = the margin +
    // border + padding of its inline-start / inline-end side: the left and
    // right sides for a left-to-right element, the other way round for a
    // right-to-left one); 0 for content.
    int edge = 0;
    // The element this marker belongs to is right-to-left: its start edge is
    // on the right, so its open marker sits after its content, visually.
    bool rtlEdge = false;
    // Line baseline the item was placed against, block content coordinates.
    float placedBaseline = 0;
    float placedX = 0;
    bool placed = false;
    // A collapsed space in the item's own font, for trimming white space at
    // the line edges.
    float spaceW = 0;
    // Collapsible white space at the very end of the content: takes no room
    // and draws nothing.
    bool dropped = false;
    // Text whose white space is preserved (white-space: pre / pre-wrap):
    // never trimmed at a line edge.
    bool preserved = false;
    // Preserved white space at the end of the run hangs past the line edge:
    // this much of `width` does not count when testing whether it fits.
    float hangW = 0;
    // Where a text run may be cut mid-word: 0 nowhere, 1 only when it does
    // not fit on a line of its own (overflow-wrap: break-word / anywhere),
    // 2 anywhere (word-break: break-all).
    int breakMode = 0;
    // The run's font, for measuring the pieces of a cut.
    const std::string* family = nullptr;
    const std::string* weight = nullptr;
    float fontSize = 0, ls = 0, ws = 0;
};

inline bool isSpaceItem(const IFCItem& it) {
    return !it.isElement && !it.forceBreak && !it.isFloat && it.edge == 0 &&
           !it.dropped && !it.preserved && it.text == " ";
}

// The font, white-space handling and line contribution of the text directly
// inside one inline box — the block itself, or a flattened inline element.
struct InlineCtx {
    LayoutNode* box = nullptr;
    float fontSize = 16.0f;
    const std::string* family = nullptr;
    const std::string* weight = nullptr;
    const std::string* whiteSpace = nullptr;
    const std::string* transform = nullptr;
    float ls = 0, ws = 0;
    float spaceWidth = 0;   // a collapsed space: glyph + letter- + word-spacing
    int breakMode = 0;      // see IFCItem::breakMode
    StrutMetrics strut;     // the box's leaded extents about its baseline
    float shift = 0;        // baseline shift accumulated from vertical-align
};

InlineCtx makeCtx(LayoutNode* box, float fontSize, TextMetrics& metrics);

// A flattened inline element: its bracket in the item sequence and the
// geometry its fragments resolve to.
struct InlineBox {
    LayoutNode* node = nullptr;
    LayoutNode* owner = nullptr;   // its parent box (block or inline element)
    size_t open = 0, close = 0;    // item indices of its markers
    float ascent = 0, descent = 0; // its font's natural box about the baseline
    float shift = 0;
    bool rtl = false;
    // Content rect in the block's content coordinates.
    float x = 0, y = 0, w = 0, h = 0;
};

struct PendingStatic { LayoutNode* node; size_t itemIndex; LayoutNode* owner; };

// Collects the line items of the block's inline content, descending into
// flattenable inline elements.
struct ItemCollector {
    LayoutNode* block;
    float childAvailable;
    TextMetrics& metrics;
    std::vector<IFCItem>& items;
    std::vector<InlineBox>& boxes;
    std::vector<PendingStatic>& pendingStatic;
    // Floats among the block's own children become float items (anonymous
    // inline runs of a block formatting context); otherwise they cannot occur.
    bool acceptFloats = false;

    void children(LayoutNode* parent, const std::vector<LayoutNode*>& list,
                  const InlineCtx& c);

private:
    bool hasContent() const;
    bool contentEndsInSpace() const;
    void text(LayoutNode* child, const InlineCtx& c);
    void emitRun(LayoutNode* child, const InlineCtx& c, const TextRun& run,
                 size_t& emitted, int& prevSrcEnd, bool words, bool collapsing,
                 int srcBase);
    void collapsingText(LayoutNode* child, const InlineCtx& c,
                        const std::string& piece, int srcBase);
    void collapsedSpace(LayoutNode* child, const InlineCtx& c);
    void forcedBreak(LayoutNode* child, const InlineCtx& c, LayoutNode* owner);
    void atomic(LayoutNode* child, const std::string& d, const InlineCtx& c);
    void inlineBox(LayoutNode* el, const InlineCtx& parent);
};

// Cut text item `i` so that its first piece is at most `maxWidth` wide, the
// rest becoming item i + 1 with a break opportunity between the two; the
// indices recorded in `boxes` and `pendingStatic` past i move up by one. The
// first piece always keeps at least one character that is not white space
// (a cut at the start of a line has to make progress), so it can come out
// wider than `maxWidth`. Returns false when the item has nothing to cut.
// With `mustFit`, an item whose shortest first piece is already wider than
// `maxWidth` is left alone instead.
bool splitTextItem(std::vector<IFCItem>& items, size_t i, float maxWidth,
                   bool mustFit, std::vector<InlineBox>& boxes,
                   std::vector<PendingStatic>& pendingStatic,
                   TextMetrics& metrics);

} // namespace htmlayout::layout::ifc
