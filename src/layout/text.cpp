#include "layout/text.h"
#include <sstream>
#include <cctype>

namespace htmlayout::layout {

std::vector<TextRun> breakTextIntoRuns(const std::string& text,
                                        float availableWidth,
                                        const std::string& fontFamily,
                                        float fontSize,
                                        const std::string& fontWeight,
                                        const std::string& whiteSpace,
                                        TextMetrics& metrics,
                                        const std::string& overflowWrap,
                                        const std::string& wordBreak,
                                        float letterSpacing,
                                        float wordSpacing) {
    std::vector<TextRun> runs;
    if (text.empty() || availableWidth <= 0) return runs;

    float lineH = metrics.lineHeight(fontFamily, fontSize, fontWeight);

    // Helper: measure text width with letter-spacing applied
    auto measureWithSpacing = [&](const std::string& s) -> float {
        float w = metrics.measureWidth(s, fontFamily, fontSize, fontWeight);
        if (letterSpacing != 0 && !s.empty()) {
            w += letterSpacing * static_cast<float>(s.size());
        }
        return w;
    };

    // Space width including word-spacing
    auto measureSpace = [&]() -> float {
        float w = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight);
        if (letterSpacing != 0) w += letterSpacing;
        w += wordSpacing;
        return w;
    };

    // white-space: pre-line — collapse spaces but preserve newlines, wrap at width
    if (whiteSpace == "pre-line") {
        // Split by newlines first
        std::istringstream stream(text);
        std::string rawLine;
        while (std::getline(stream, rawLine)) {
            // Collapse whitespace within each line
            std::vector<std::string> words;
            {
                std::string word;
                for (char c : rawLine) {
                    if (c == ' ' || c == '\t') {
                        if (!word.empty()) {
                            words.push_back(word);
                            word.clear();
                        }
                    } else {
                        word += c;
                    }
                }
                if (!word.empty()) words.push_back(word);
            }

            if (words.empty()) {
                runs.push_back({"", 0, lineH});
                continue;
            }

            // Greedy line packing within this source line
            std::string currentLine;
            float currentWidth = 0;
            float spaceWidth = measureSpace();
            for (size_t i = 0; i < words.size(); i++) {
                float wordWidth = measureWithSpacing(words[i]);
                if (currentLine.empty()) {
                    currentLine = words[i];
                    currentWidth = wordWidth;
                } else {
                    float testWidth = currentWidth + spaceWidth + wordWidth;
                    if (testWidth <= availableWidth) {
                        currentLine += " " + words[i];
                        currentWidth = testWidth;
                    } else {
                        runs.push_back({currentLine, currentWidth, lineH});
                        currentLine = words[i];
                        currentWidth = wordWidth;
                    }
                }
            }
            if (!currentLine.empty()) {
                runs.push_back({currentLine, currentWidth, lineH});
            }
        }
        return runs;
    }

    // white-space: pre — preserve all whitespace, no wrapping
    if (whiteSpace == "pre" || whiteSpace == "pre-wrap") {
        // Split by newlines, each line is a run
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            float w = measureWithSpacing(line);
            if (whiteSpace == "pre-wrap" && w > availableWidth && !line.empty()) {
                // pre-wrap: preserve whitespace but wrap at available width
                // For simplicity, wrap at word boundaries
                std::string current;
                float currentW = 0;
                for (size_t i = 0; i < line.size(); i++) {
                    char c = line[i];
                    std::string test = current + c;
                    float testW = measureWithSpacing(test);
                    if (testW > availableWidth && !current.empty()) {
                        runs.push_back({current, currentW, lineH});
                        current = std::string(1, c);
                        currentW = measureWithSpacing(current);
                    } else {
                        current = test;
                        currentW = testW;
                    }
                }
                if (!current.empty()) {
                    runs.push_back({current, currentW, lineH});
                }
            } else {
                runs.push_back({line, w, lineH});
            }
        }
        return runs;
    }

    // white-space: nowrap — collapse whitespace, no wrapping
    if (whiteSpace == "nowrap") {
        // Collapse whitespace and emit as single run
        std::string collapsed;
        bool lastWasSpace = false;
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!lastWasSpace && !collapsed.empty()) {
                    collapsed += ' ';
                    lastWasSpace = true;
                }
            } else {
                collapsed += c;
                lastWasSpace = false;
            }
        }
        // Trim trailing space
        if (!collapsed.empty() && collapsed.back() == ' ')
            collapsed.pop_back();
        float w = measureWithSpacing(collapsed);
        if (!collapsed.empty()) {
            runs.push_back({collapsed, w, lineH});
        }
        return runs;
    }

    // white-space: normal (default) — collapse whitespace, wrap at word boundaries
    // 1. Collapse whitespace and split into words
    std::vector<std::string> words;
    {
        std::string word;
        bool inSpace = true;
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
                inSpace = true;
            } else {
                word += c;
                inSpace = false;
            }
        }
        if (!word.empty()) words.push_back(word);
    }

    if (words.empty()) return runs;

    // 2. Greedy line packing
    std::string currentLine;
    float currentWidth = 0;
    float spaceWidth = measureSpace();

    bool canBreakWord = (overflowWrap == "break-word" || overflowWrap == "anywhere" ||
                         wordBreak == "break-all");

    for (size_t i = 0; i < words.size(); i++) {
        float wordWidth = measureWithSpacing(words[i]);

        if (currentLine.empty()) {
            // First word on line
            if (canBreakWord && wordWidth > availableWidth) {
                // Break the word character by character
                std::string partial;
                float partialW = 0;
                for (size_t ci = 0; ci < words[i].size(); ci++) {
                    std::string test = partial + words[i][ci];
                    float testW = measureWithSpacing(test);
                    if (testW > availableWidth && !partial.empty()) {
                        runs.push_back({partial, partialW, lineH});
                        partial = std::string(1, words[i][ci]);
                        partialW = measureWithSpacing(partial);
                    } else {
                        partial = test;
                        partialW = testW;
                    }
                }
                currentLine = partial;
                currentWidth = partialW;
            } else {
                currentLine = words[i];
                currentWidth = wordWidth;
            }
        } else {
            float testWidth = currentWidth + spaceWidth + wordWidth;
            if (testWidth <= availableWidth) {
                currentLine += " " + words[i];
                currentWidth = testWidth;
            } else {
                // Wrap: emit current line, start new one
                runs.push_back({currentLine, currentWidth, lineH});

                if (canBreakWord && wordWidth > availableWidth) {
                    // Break the word character by character
                    std::string partial;
                    float partialW = 0;
                    for (size_t ci = 0; ci < words[i].size(); ci++) {
                        std::string test = partial + words[i][ci];
                        float testW = measureWithSpacing(test);
                        if (testW > availableWidth && !partial.empty()) {
                            runs.push_back({partial, partialW, lineH});
                            partial = std::string(1, words[i][ci]);
                            partialW = measureWithSpacing(partial);
                        } else {
                            partial = test;
                            partialW = testW;
                        }
                    }
                    currentLine = partial;
                    currentWidth = partialW;
                } else {
                    currentLine = words[i];
                    currentWidth = wordWidth;
                }
            }
        }
    }

    // Emit final line
    if (!currentLine.empty()) {
        runs.push_back({currentLine, currentWidth, lineH});
    }

    return runs;
}

} // namespace htmlayout::layout
