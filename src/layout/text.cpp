#include "layout/text.h"
#include <sstream>
#include <cctype>

namespace htmlayout::layout {

namespace {

// A word extracted from the source text along with its byte position in the
// original string. Needed so caller can round-trip DOM char offsets.
struct SourceWord {
    std::string text;
    int srcStart;
    int srcEnd; // exclusive
};

// Scan a source string into words (runs of non-whitespace) with their source
// byte ranges. Leading/trailing whitespace is discarded; only word positions
// survive. Used by the collapsing whitespace paths ("normal", "nowrap").
// Byte length of the UTF-8 sequence starting at `i`, clamped to the bytes
// remaining. Stray continuation / invalid lead bytes count as length 1 so
// callers always make progress. Character-level wrapping must consume whole
// sequences — slicing mid-sequence hands invalid UTF-8 to the measurer, and
// Skia's text APIs fatally abort on it.
size_t utf8SeqLen(const std::string& s, size_t i) {
    unsigned char b = static_cast<unsigned char>(s[i]);
    size_t n = 1;
    if      ((b & 0xE0) == 0xC0) n = 2;
    else if ((b & 0xF0) == 0xE0) n = 3;
    else if ((b & 0xF8) == 0xF0) n = 4;
    if (i + n > s.size()) n = s.size() - i;
    return n;
}

// Number of codepoints in a UTF-8 string — the letter-spacing slot count.
size_t utf8CodepointCount(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); i += utf8SeqLen(s, i)) ++count;
    return count;
}

std::vector<SourceWord> scanWords(const std::string& text) {
    std::vector<SourceWord> words;
    std::string current;
    int currentStart = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                words.push_back({std::move(current), currentStart, static_cast<int>(i)});
                current.clear();
                currentStart = -1;
            }
        } else {
            if (currentStart < 0) currentStart = static_cast<int>(i);
            current += c;
        }
    }
    if (!current.empty()) {
        words.push_back({std::move(current), currentStart, static_cast<int>(text.size())});
    }
    return words;
}

} // namespace

std::string applyTextTransform(const std::string& text,
                               const std::string& transform) {
    if (transform == "uppercase") {
        std::string r = text;
        for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return r;
    }
    if (transform == "lowercase") {
        std::string r = text;
        for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    }
    if (transform == "capitalize") {
        std::string r = text;
        bool nextCap = true;
        for (auto& c : r) {
            if (std::isspace(static_cast<unsigned char>(c))) { nextCap = true; }
            else if (nextCap) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); nextCap = false; }
        }
        return r;
    }
    return text;
}

void measureWordModeIntrinsics(const std::string& srcText,
                               const std::string& fontFamily,
                               float fontSize,
                               const std::string& fontWeight,
                               float letterSpacing,
                               float wordSpacing,
                               const std::string& textTransform,
                               TextMetrics& metrics,
                               float& outMin,
                               float& outMax) {
    outMin = 0.0f;
    outMax = 0.0f;
    // text-transform first so measurements use the painted glyphs, exactly as
    // breakTextIntoRuns does before scanning words.
    const std::string text = applyTextTransform(srcText, textTransform);
    std::vector<SourceWord> words = scanWords(text);
    if (words.empty()) return;

    // Per-word advance — mirrors breakTextIntoRuns' measureWithSpacing (n slots,
    // trailing included).
    auto measureWord = [&](const std::string& s) -> float {
        float w = metrics.measureWidth(s, fontFamily, fontSize, fontWeight);
        if (letterSpacing != 0 && !s.empty())
            w += letterSpacing * static_cast<float>(utf8CodepointCount(s));
        return w;
    };
    // Inter-word space advance — mirrors block.cpp's
    // spaceWidth = measureWidth(" ") + letter-spacing + word-spacing.
    float spaceWidth = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight)
                       + letterSpacing + wordSpacing;

    for (const auto& word : words) {
        float w = measureWord(word.text);
        outMin = std::max(outMin, w);
        outMax += w;
    }
    outMax += static_cast<float>(words.size() - 1) * spaceWidth;
}

std::vector<TextRun> breakTextIntoRuns(const std::string& srcText,
                                        float availableWidth,
                                        const std::string& fontFamily,
                                        float fontSize,
                                        const std::string& fontWeight,
                                        const std::string& whiteSpace,
                                        TextMetrics& metrics,
                                        const std::string& overflowWrap,
                                        const std::string& wordBreak,
                                        float letterSpacing,
                                        float wordSpacing,
                                        const std::string& textTransform) {
    std::vector<TextRun> runs;
    if (srcText.empty() || availableWidth <= 0) return runs;

    // Apply text-transform up front so every measurement and wrap decision
    // below uses the painted glyphs. ASCII transforms preserve byte length, so
    // the srcStart/srcEnd offsets still index the original DOM text.
    const std::string text = applyTextTransform(srcText, textTransform);

    // Runs carry the font's natural rect height (ascent+descent, no line
    // gap) — the height Chromium reports for text/inline rects. Line boxes
    // still advance by line-height via the strut in block.cpp.
    float lineH = metrics.naturalHeight(fontFamily, fontSize, fontWeight);

    // Helper: measure text width with letter-spacing applied between glyphs.
    // CSS adds letter-spacing as advance after every character INCLUDING the
    // last — Chromium's measured box carries the trailing slot too, so apply
    // n spacings to match its geometry.
    auto measureWithSpacing = [&](const std::string& s) -> float {
        float w = metrics.measureWidth(s, fontFamily, fontSize, fontWeight);
        if (letterSpacing != 0 && !s.empty()) {
            w += letterSpacing * static_cast<float>(utf8CodepointCount(s));
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
        // Track source offsets while splitting by newlines.
        size_t cursor = 0;
        while (cursor <= text.size()) {
            size_t nl = text.find('\n', cursor);
            size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
            std::string rawLine = text.substr(cursor, lineEnd - cursor);
            int lineBase = static_cast<int>(cursor);

            // Collapse whitespace within each line, tracking source offsets
            auto words = scanWords(rawLine);
            // Offset words' src by lineBase so they point into the original text.
            for (auto& w : words) { w.srcStart += lineBase; w.srcEnd += lineBase; }

            if (words.empty()) {
                TextRun r{"", 0, lineH, lineBase, static_cast<int>(lineEnd)};
                if (nl != std::string::npos) r.forceBreakAfter = true;
                runs.push_back(std::move(r));
                cursor = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
                continue;
            }

            // Greedy line packing within this source line
            std::string currentLine;
            float currentWidth = 0;
            int runSrcStart = words.front().srcStart;
            int runSrcEnd   = words.front().srcEnd;
            float spaceWidth = measureSpace();
            for (size_t i = 0; i < words.size(); i++) {
                float wordWidth = measureWithSpacing(words[i].text);
                if (currentLine.empty()) {
                    currentLine = words[i].text;
                    currentWidth = wordWidth;
                    runSrcStart = words[i].srcStart;
                    runSrcEnd   = words[i].srcEnd;
                } else {
                    float testWidth = currentWidth + spaceWidth + wordWidth;
                    if (testWidth <= availableWidth) {
                        currentLine += " " + words[i].text;
                        currentWidth = testWidth;
                        runSrcEnd = words[i].srcEnd;
                    } else {
                        runs.push_back({currentLine, currentWidth, lineH, runSrcStart, runSrcEnd});
                        currentLine = words[i].text;
                        currentWidth = wordWidth;
                        runSrcStart = words[i].srcStart;
                        runSrcEnd   = words[i].srcEnd;
                    }
                }
            }
            if (!currentLine.empty()) {
                runs.push_back({currentLine, currentWidth, lineH, runSrcStart, runSrcEnd});
            }
            // Mark the last run produced for this source line as ending in
            // a hard break so the IFC builder starts the next source line
            // on its own line box.
            if (nl != std::string::npos && !runs.empty()) {
                runs.back().forceBreakAfter = true;
            }

            cursor = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        }
        return runs;
    }

    // white-space: pre / pre-wrap — preserve all whitespace.
    if (whiteSpace == "pre" || whiteSpace == "pre-wrap") {
        size_t cursor = 0;
        while (cursor <= text.size()) {
            size_t nl = text.find('\n', cursor);
            size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
            std::string line = text.substr(cursor, lineEnd - cursor);
            int lineBase = static_cast<int>(cursor);

            float w = measureWithSpacing(line);
            if (whiteSpace == "pre-wrap" && w > availableWidth && !line.empty()) {
                std::string current;
                float currentW = 0;
                int segStart = lineBase;
                for (size_t i = 0; i < line.size();) {
                    size_t n = utf8SeqLen(line, i);
                    std::string test = current + line.substr(i, n);
                    float testW = measureWithSpacing(test);
                    if (testW > availableWidth && !current.empty()) {
                        runs.push_back({current, currentW, lineH,
                                        segStart, static_cast<int>(lineBase + i)});
                        current = line.substr(i, n);
                        currentW = measureWithSpacing(current);
                        segStart = static_cast<int>(lineBase + i);
                    } else {
                        current = std::move(test);
                        currentW = testW;
                    }
                    i += n;
                }
                if (!current.empty()) {
                    runs.push_back({current, currentW, lineH,
                                    segStart, static_cast<int>(lineEnd)});
                }
            } else {
                runs.push_back({line, w, lineH, lineBase, static_cast<int>(lineEnd)});
            }

            // Mark the last run produced for this source line as ending in
            // a hard break (preserved literal newline).
            if (nl != std::string::npos && !runs.empty()) {
                runs.back().forceBreakAfter = true;
            }

            cursor = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        }
        return runs;
    }

    // white-space: nowrap — collapse whitespace, no wrapping, single run.
    if (whiteSpace == "nowrap") {
        auto words = scanWords(text);
        if (words.empty()) return runs;
        std::string collapsed = words.front().text;
        for (size_t i = 1; i < words.size(); ++i) {
            collapsed += ' ';
            collapsed += words[i].text;
        }
        float w = measureWithSpacing(collapsed);
        runs.push_back({collapsed, w, lineH,
                        words.front().srcStart, words.back().srcEnd});
        return runs;
    }

    // white-space: normal — collapse whitespace, wrap at word boundaries.
    auto words = scanWords(text);
    if (words.empty()) return runs;

    // Greedy line packing
    std::string currentLine;
    float currentWidth = 0;
    int runSrcStart = words.front().srcStart;
    int runSrcEnd   = words.front().srcEnd;
    float spaceWidth = measureSpace();

    bool canBreakWord = (overflowWrap == "break-word" || overflowWrap == "anywhere" ||
                         wordBreak == "break-all");

    // Character-level break: called when a single word doesn't fit on the line.
    // Emits one run per character slice, tracking source offsets by adjusting
    // `base` as we advance through the word's source range.
    auto breakWordByChar = [&](const SourceWord& w) {
        std::string partial;
        float partialW = 0;
        int partialStart = w.srcStart;
        for (size_t ci = 0; ci < w.text.size();) {
            size_t n = utf8SeqLen(w.text, ci);
            std::string test = partial + w.text.substr(ci, n);
            float testW = measureWithSpacing(test);
            if (testW > availableWidth && !partial.empty()) {
                runs.push_back({partial, partialW, lineH,
                                partialStart,
                                static_cast<int>(w.srcStart + ci)});
                partial = w.text.substr(ci, n);
                partialW = measureWithSpacing(partial);
                partialStart = static_cast<int>(w.srcStart + ci);
            } else {
                partial = std::move(test);
                partialW = testW;
            }
            ci += n;
        }
        currentLine = partial;
        currentWidth = partialW;
        runSrcStart = partialStart;
        runSrcEnd   = w.srcEnd;
    };

    for (size_t i = 0; i < words.size(); i++) {
        float wordWidth = measureWithSpacing(words[i].text);

        if (currentLine.empty()) {
            if (canBreakWord && wordWidth > availableWidth) {
                breakWordByChar(words[i]);
            } else {
                currentLine = words[i].text;
                currentWidth = wordWidth;
                runSrcStart = words[i].srcStart;
                runSrcEnd   = words[i].srcEnd;
            }
        } else {
            float testWidth = currentWidth + spaceWidth + wordWidth;
            if (testWidth <= availableWidth) {
                currentLine += " " + words[i].text;
                currentWidth = testWidth;
                runSrcEnd = words[i].srcEnd;
            } else {
                runs.push_back({currentLine, currentWidth, lineH,
                                runSrcStart, runSrcEnd});

                if (canBreakWord && wordWidth > availableWidth) {
                    breakWordByChar(words[i]);
                } else {
                    currentLine = words[i].text;
                    currentWidth = wordWidth;
                    runSrcStart = words[i].srcStart;
                    runSrcEnd   = words[i].srcEnd;
                }
            }
        }
    }

    if (!currentLine.empty()) {
        runs.push_back({currentLine, currentWidth, lineH, runSrcStart, runSrcEnd});
    }

    // Preserve leading/trailing whitespace from the source as a single space
    // attached to the first/last run. Needed so inline boundaries ("foo "
    // + <em>bar</em>) don't collapse into "foobar" at draw time, and so the
    // line breaker can treat inter-item whitespace as a valid break point.
    if (!runs.empty()) {
        bool hasLeading  = std::isspace(static_cast<unsigned char>(text.front()));
        bool hasTrailing = std::isspace(static_cast<unsigned char>(text.back()));
        float spaceW = measureSpace();
        if (hasLeading) {
            auto& first = runs.front();
            first.text.insert(first.text.begin(), ' ');
            first.width += spaceW;
            first.srcStart = 0;
        }
        if (hasTrailing) {
            auto& last = runs.back();
            last.text.push_back(' ');
            last.width += spaceW;
            last.srcEnd = static_cast<int>(text.size());
        }
        // Tag break opportunities. Every run boundary inside `runs` was a
        // word-space in the source (the packer only splits at whitespace),
        // so interior edges are always breakable. The outer edges inherit
        // whether the source itself had leading/trailing whitespace.
        for (size_t i = 0; i < runs.size(); ++i) {
            runs[i].canBreakBefore = (i > 0) || hasLeading;
            runs[i].canBreakAfter  = (i + 1 < runs.size()) || hasTrailing;
        }
    }

    return runs;
}

} // namespace htmlayout::layout
