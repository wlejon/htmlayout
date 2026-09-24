#pragma once
#include "layout/box.h"
#include <string>

namespace htmlayout::layout {

// line-clamp (CSS Overflow 4) and the legacy -webkit-line-clamp, for block
// containers. The standard clamp is read from the shorthand's longhands:
// `max-lines` <integer> with `continue: collapse` (or discard /
// -webkit-legacy), and `block-ellipsis` for the ellipsis. Block layout calls beginLineClamp() before it lays out the
// children and applyLineClamp() once they are placed, before the auto height
// is resolved.
//
// What a clamp does to the laid-out tree:
//  - the container's line boxes are counted in order through its in-flow
//    block descendants that share its formatting context (not into
//    inline-blocks, floats, flex/grid/table boxes or other BFC roots);
//  - the container's auto height ends at the bottom of the Nth line;
//  - text runs past the Nth line are removed from their text nodes (a text
//    node left with nothing keeps one empty zero-size run, so a consumer that
//    falls back to drawing textContent() when textRuns is empty draws nothing
//    either), and element boxes past it are flagged LayoutBox::clampHidden —
//    kept laid out, skipped by hitTest(), and to be skipped by the painter;
//  - the ellipsis (U+2026, or the <string> from block-ellipsis) goes at the
//    inline end of the Nth line — its right edge in an ltr block, its left
//    in an rtl one — with the line truncated from that edge in visual order
//    until it fits: text runs are cut short from the side facing the edge,
//    atomic inlines kept or flagged clampHidden whole. It is drawn by a text
//    run: appended to the cut run when that runs in the line's direction,
//    otherwise (after an atomic inline, or beside an opposite-direction run)
//    a run of its own holding just the ellipsis, added to a text node on the
//    line or, failing that, to the last text node kept before it. With no
//    text node kept at all (lines of images or inline-blocks), the container
//    holds it in its own LayoutBox::textRuns, in its content coordinates and
//    its font, marked srcStart == srcEnd == kContainerEllipsisSrc — as a
//    pseudo-element box holds its generated text. A line with nothing on it
//    gets the ellipsis at its start edge. The container's
//    LayoutBox::textTruncated is set.
// Nothing happens when the content has N lines or fewer.
// srcStart/srcEnd of an ellipsis run held by the clamp container itself.
inline constexpr int kContainerEllipsisSrc = -1;

struct LineClampSpec {
    int maxLines = 0;            // 0: no clamp
    bool ellipsis = true;        // block-ellipsis: auto | <string>
    std::string ellipsisText = "\xE2\x80\xA6";
};

// Whether the node clamps now or did on its last layout (so block layout,
// which owns clamping, must lay it out).
bool lineClampApplies(LayoutNode* node);

// Resolves the node's clamp, resets last pass's clamp state and, when the node
// clamps now or did last time, marks its descendants for relayout (clamping
// edits their boxes, so reused boxes would carry a stale clamp). Returns true
// when applyLineClamp() must run after the children are laid out.
bool beginLineClamp(LayoutNode* node, LineClampSpec& spec);

// Applies `spec` to the laid-out children of `node` (see above) and returns the
// content height to use in place of `contentHeight`.
float applyLineClamp(LayoutNode* node, const LineClampSpec& spec,
                     float contentHeight, TextMetrics& metrics);

// text-overflow (CSS Overflow 3) on a block container whose inline overflow
// is not visible: each of its own line boxes whose content runs past the
// line's end edge is truncated there with the ellipsis (`ellipsis` is U+2026,
// a <string> is itself; `clip` does nothing), exactly as the line-clamp
// ellipsis truncates the last kept line (see above), and
// LayoutBox::textTruncated is set.
//
// Whether the node ellipsizes now or did on its last layout (so block layout
// must lay it out, as for lineClampApplies).
bool textOverflowApplies(LayoutNode* node);
// Undoes last pass's truncation (marking descendants for relayout) and
// returns true when applyTextOverflow() must run after the children are laid
// out.
bool beginTextOverflow(LayoutNode* node, std::string& ellipsisText);
void applyTextOverflow(LayoutNode* node, const std::string& ellipsisText,
                       TextMetrics& metrics);

} // namespace htmlayout::layout
