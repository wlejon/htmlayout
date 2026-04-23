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
};

// Break text into runs that fit within availableWidth.
// Uses TextMetrics for measurement.
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
                                        float wordSpacing = 0);

} // namespace htmlayout::layout
