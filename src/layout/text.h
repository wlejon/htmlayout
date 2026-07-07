#pragma once
#include "layout/box.h"
#include <string>
#include <vector>

namespace htmlayout::layout {

// A segment of text that fits on one line.
//
// srcStart/srcEnd point into the original (uncollapsed) source string so
// consumers can map runs back to DOM character offsets for caret placement
// and selection geometry. `text` is the post-whitespace-processing string
// actually rendered — its length may differ from (srcEnd - srcStart) when
// whitespace is collapsed.
struct TextRun {
    std::string text;
    float width;
    float height;
    int srcStart = 0;
    int srcEnd   = 0;
    // True when a soft line-break is allowed immediately before/after this
    // run. Populated by the word-boundary splitter: intermediate runs are
    // always breakable on the interior side, and the outer sides inherit
    // whether the original source had collapsible whitespace at its edges.
    // The line-box builder uses these (plus leading/trailing whitespace in
    // .text) to decide where a width-driven wrap may actually land.
    bool canBreakBefore = false;
    bool canBreakAfter  = false;
    // True when a hard line break (a literal newline preserved by
    // white-space: pre/pre-wrap/pre-line) immediately follows this run.
    // The IFC line builder treats this like a <br>: terminate the
    // current line after this run regardless of available width.
    bool forceBreakAfter = false;
};

// Apply CSS text-transform ("uppercase" / "lowercase" / "capitalize"; anything
// else is a no-op) to an ASCII string in place of return. Must be applied
// during layout — not just at paint — so measurement and line-breaking use the
// glyphs that are actually rendered (e.g. "rendering" laid out as "RENDERING").
std::string applyTextTransform(const std::string& text,
                               const std::string& transform);

// Intrinsic widths of a collapsing-whitespace (white-space: normal) text string,
// measured the SAME way block.cpp's word-granularity IFC builder reconstructs a
// line: each word's advance is measured on its own (letter-spacing applied to
// every slot INCLUDING the trailing one, matching breakTextIntoRuns'
// measureWithSpacing), and adjacent words are separated by one synthetic space
// advance (space glyph + letter-spacing + word-spacing, matching block.cpp's
// spaceWidth). Because the whole-string measurement and the per-word sum differ
// by sub-pixel font accumulation, sizing a box to the whole-string width can
// leave its own reconstructed line a hair too wide and force a spurious wrap;
// computing max-content this way keeps the intrinsic-sizing and layout paths in
// lockstep so a box sized to max-content always fits its text on one line.
//
//   outMin — widest single word (words never share a line, so no space advance).
//   outMax — the whole string on one line: Σ word advances + (n-1) space advances.
//
// Only the collapsing/wrapping path uses this; nowrap/pre and break-word/
// break-all are measured elsewhere (they don't build word+space items).
// Keep this in lockstep with layoutBlock's word-mode item construction in
// block.cpp.
void measureWordModeIntrinsics(const std::string& text,
                               const std::string& fontFamily,
                               float fontSize,
                               const std::string& fontWeight,
                               float letterSpacing,
                               float wordSpacing,
                               const std::string& textTransform,
                               TextMetrics& metrics,
                               float& outMin,
                               float& outMax);

// Break text into runs that fit within availableWidth.
// Uses TextMetrics for measurement.
// textTransform: CSS text-transform applied before measuring (so the box and
//   wrap match the painted glyphs). "none" (default) leaves text untouched.
// overflowWrap: "normal" (default) or "break-word" / "anywhere"
// wordBreak: "normal" (default) or "break-all" / "keep-all"
std::vector<TextRun> breakTextIntoRuns(const std::string& text,
                                        float availableWidth,
                                        const std::string& fontFamily,
                                        float fontSize,
                                        const std::string& fontWeight,
                                        const std::string& whiteSpace,
                                        TextMetrics& metrics,
                                        const std::string& overflowWrap = "normal",
                                        const std::string& wordBreak = "normal",
                                        float letterSpacing = 0,
                                        float wordSpacing = 0,
                                        const std::string& textTransform = "none");

} // namespace htmlayout::layout
