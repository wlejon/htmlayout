#pragma once
#include "layout/box.h"
#include <string>
#include <vector>

namespace htmlayout::layout {

// A segment of text that fits on one line
struct TextRun {
    std::string text;
    float width;
    float height;
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
                                        const std::string& wordBreak = "normal");

} // namespace htmlayout::layout
