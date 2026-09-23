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
//  - the last text run of the Nth line is trimmed so the ellipsis fits in the
//    line and then carries it (U+2026, or the <string> from block-ellipsis),
//    and the container's LayoutBox::textTruncated is set.
// Nothing happens when the content has N lines or fewer.
struct LineClampSpec {
    int maxLines = 0;            // 0: no clamp
    bool ellipsis = true;        // block-ellipsis: auto | <string>
    std::string ellipsisText = "\xE2\x80\xA6";
};

// Resolves the node's clamp, resets last pass's clamp state and, when the node
// clamps now or did last time, marks its descendants for relayout (clamping
// edits their boxes, so reused boxes would carry a stale clamp). Returns true
// when applyLineClamp() must run after the children are laid out.
bool beginLineClamp(LayoutNode* node, LineClampSpec& spec);

// Applies `spec` to the laid-out children of `node` (see above) and returns the
// content height to use in place of `contentHeight`.
float applyLineClamp(LayoutNode* node, const LineClampSpec& spec,
                     float contentHeight, TextMetrics& metrics);

} // namespace htmlayout::layout
