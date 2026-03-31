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
std::vector<TextRun> breakTextIntoRuns(const std::string& text,
                                        float availableWidth,
                                        const std::string& fontFamily,
                                        float fontSize,
                                        const std::string& fontWeight,
                                        const std::string& whiteSpace,
                                        TextMetrics& metrics);

} // namespace htmlayout::layout
