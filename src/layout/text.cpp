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
                                        TextMetrics& metrics) {
    std::vector<TextRun> runs;
    if (text.empty() || availableWidth <= 0) return runs;

    float lineH = metrics.lineHeight(fontFamily, fontSize, fontWeight);

    // white-space: pre — preserve all whitespace, no wrapping
    if (whiteSpace == "pre" || whiteSpace == "pre-wrap") {
        // Split by newlines, each line is a run
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            float w = metrics.measureWidth(line, fontFamily, fontSize, fontWeight);
            if (whiteSpace == "pre-wrap" && w > availableWidth && !line.empty()) {
                // pre-wrap: preserve whitespace but wrap at available width
                // For simplicity, wrap at word boundaries
                std::string current;
                float currentW = 0;
                for (size_t i = 0; i < line.size(); i++) {
                    char c = line[i];
                    std::string test = current + c;
                    float testW = metrics.measureWidth(test, fontFamily, fontSize, fontWeight);
                    if (testW > availableWidth && !current.empty()) {
                        runs.push_back({current, currentW, lineH});
                        current = std::string(1, c);
                        currentW = metrics.measureWidth(current, fontFamily, fontSize, fontWeight);
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
        float w = metrics.measureWidth(collapsed, fontFamily, fontSize, fontWeight);
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
    float spaceWidth = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight);

    for (size_t i = 0; i < words.size(); i++) {
        float wordWidth = metrics.measureWidth(words[i], fontFamily, fontSize, fontWeight);

        if (currentLine.empty()) {
            // First word on line — always accept even if it overflows
            currentLine = words[i];
            currentWidth = wordWidth;
        } else {
            float testWidth = currentWidth + spaceWidth + wordWidth;
            if (testWidth <= availableWidth) {
                // Fits on current line
                currentLine += " " + words[i];
                currentWidth = testWidth;
            } else {
                // Wrap: emit current line, start new one
                runs.push_back({currentLine, currentWidth, lineH});
                currentLine = words[i];
                currentWidth = wordWidth;
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
