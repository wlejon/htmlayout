#include "layout/text.h"

namespace htmlayout::layout {

std::vector<TextRun> breakTextIntoRuns(const std::string& text,
                                        float availableWidth,
                                        const std::string& fontFamily,
                                        float fontSize,
                                        const std::string& fontWeight,
                                        const std::string& whiteSpace,
                                        TextMetrics& metrics) {
    // TODO: Implement text breaking / word wrapping
    return {};
}

} // namespace htmlayout::layout
