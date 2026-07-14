#include "layout/grid.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>

namespace htmlayout::layout {

using layout::styleVal;

namespace {

float resolveDim(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

// Parse a track size value: could be a length, fr, auto, min-content, max-content, minmax()
struct TrackSize {
    enum Kind { Fixed, Fractional, Auto, MinContent, MaxContent };
    Kind kind = Auto;
    float value = 0;       // for Fixed: resolved px; for Fractional: fr value
    float minValue = 0;    // for minmax: minimum
    float maxValue = -1;   // for minmax: maximum (-1 = auto)
    bool isMinmax = false;
    // The track's min sizing function is auto/min-content: a flexible track's
    // base size then floors at its items' min-content contributions (Grid
    // §11.8 — the "automatic minimum"). Plain `1fr` means minmax(auto, 1fr).
    bool minIsAuto = false;
};

// Parse a single track size token
TrackSize parseTrackSize(const std::string& token, float available, float fontSize) {
    TrackSize ts;
    if (token == "auto") {
        ts.kind = TrackSize::Auto;
        return ts;
    }
    if (token == "min-content") {
        ts.kind = TrackSize::MinContent;
        return ts;
    }
    if (token == "max-content") {
        ts.kind = TrackSize::MaxContent;
        return ts;
    }
    // Check for fr unit
    if (token.size() > 2 && token.substr(token.size() - 2) == "fr") {
        ts.kind = TrackSize::Fractional;
        ts.minIsAuto = true; // <flex> alone means minmax(auto, <flex>)
        try { ts.value = std::stof(token.substr(0, token.size() - 2)); } catch (...) { ts.value = 1; }
        return ts;
    }
    // Check for minmax(min, max)
    if (token.size() > 7 && token.substr(0, 7) == "minmax(") {
        ts.isMinmax = true;
        std::string inner = token.substr(7);
        if (!inner.empty() && inner.back() == ')') inner.pop_back();
        auto comma = inner.find(',');
        if (comma != std::string::npos) {
            std::string minStr = inner.substr(0, comma);
            std::string maxStr = inner.substr(comma + 1);
            // Trim
            while (!minStr.empty() && minStr.front() == ' ') minStr.erase(0, 1);
            while (!minStr.empty() && minStr.back() == ' ') minStr.pop_back();
            while (!maxStr.empty() && maxStr.front() == ' ') maxStr.erase(0, 1);
            while (!maxStr.empty() && maxStr.back() == ' ') maxStr.pop_back();

            if (maxStr.size() > 2 && maxStr.substr(maxStr.size() - 2) == "fr") {
                ts.kind = TrackSize::Fractional;
                try { ts.value = std::stof(maxStr.substr(0, maxStr.size() - 2)); } catch (...) { ts.value = 1; }
            } else if (maxStr == "auto") {
                ts.kind = TrackSize::Auto;
            } else {
                ts.kind = TrackSize::Fixed;
                ts.value = resolveLength(maxStr, available, fontSize);
            }

            if (minStr != "auto" && minStr != "min-content") {
                ts.minValue = resolveLength(minStr, available, fontSize);
            } else {
                ts.minIsAuto = true;
            }
        }
        return ts;
    }
    // Fixed length
    ts.kind = TrackSize::Fixed;
    ts.value = resolveLength(token, available, fontSize);
    return ts;
}

// Named grid lines: maps line name to 1-based line indices.
// Multiple lines can share a name (e.g., from repeat()).
using NamedLines = std::unordered_map<std::string, std::vector<int>>;

// Tokenize a track list, handling repeat(), minmax(), [name], and simple tokens.
// Splits on whitespace while respecting parentheses and brackets.
std::vector<std::string> tokenizeTrackList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    int parenDepth = 0;
    bool inBracket = false;

    for (size_t i = 0; i < value.size(); i++) {
        char c = value[i];
        if (c == '[' && parenDepth == 0) {
            // Start of named line — collect until ']'
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
            current += c;
            inBracket = true;
            continue;
        }
        if (c == ']' && inBracket) {
            current += c;
            tokens.push_back(current);
            current.clear();
            inBracket = false;
            continue;
        }
        if (inBracket) { current += c; continue; }
        if (c == '(') { parenDepth++; current += c; continue; }
        if (c == ')') { parenDepth--; current += c; continue; }
        if (parenDepth > 0) { current += c; continue; }
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

// Compute the fixed-size sum of a set of track tokens (for auto-fill/auto-fit calculation).
// Returns the total fixed space consumed by one repetition of the pattern.
float computeRepeatPatternSize(const std::vector<std::string>& subTokens, float available, float fontSize) {
    float total = 0;
    for (auto& st : subTokens) {
        if (st.front() == '[') continue; // skip named lines
        auto ts = parseTrackSize(st, available, fontSize);
        if (ts.kind == TrackSize::Fixed) {
            total += ts.value;
        } else if (ts.isMinmax && ts.minValue > 0) {
            total += ts.minValue; // use minimum for auto-fill calculation
        }
        // auto/fr/min-content/max-content contribute 0 for auto-fill count calc
    }
    return total;
}

// Parse a grid-template-columns/rows value into track sizes.
// Supports: fixed lengths, fr, auto, repeat(N|auto-fill|auto-fit, track),
// minmax(), named lines [name].
struct ParsedTrackList {
    std::vector<TrackSize> tracks;
    NamedLines lineNames;
    bool hasAutoFit = false;  // if auto-fit was used, empty tracks should collapse
    int autoFitBegin = -1;    // track-index range produced by the auto-fit repeat
    int autoFitEnd = -1;
};

ParsedTrackList parseTrackListWithNames(const std::string& value, float available, float fontSize,
                                        float gap = 0.0f) {
    ParsedTrackList result;
    if (value.empty() || value == "none") return result;

    auto tokens = tokenizeTrackList(value);

    // Track the current line index (1-based: line 1 is before track 0)
    int lineIndex = 1;

    for (size_t ti = 0; ti < tokens.size(); ti++) {
        auto& token = tokens[ti];

        // Handle named lines: [name1 name2]
        if (!token.empty() && token.front() == '[' && token.back() == ']') {
            std::string names = token.substr(1, token.size() - 2);
            // Split names by whitespace
            std::istringstream iss(names);
            std::string n;
            while (iss >> n) {
                result.lineNames[n].push_back(lineIndex);
            }
            continue;
        }

        // Handle repeat(count|auto-fill|auto-fit, track-size...)
        if (token.size() > 7 && token.substr(0, 7) == "repeat(") {
            std::string inner = token.substr(7);
            if (!inner.empty() && inner.back() == ')') inner.pop_back();
            // Find first comma (not inside parens)
            int pd = 0;
            size_t comma = std::string::npos;
            for (size_t j = 0; j < inner.size(); j++) {
                if (inner[j] == '(') pd++;
                else if (inner[j] == ')') pd--;
                else if (inner[j] == ',' && pd == 0) { comma = j; break; }
            }
            if (comma != std::string::npos) {
                std::string countStr = inner.substr(0, comma);
                // Trim
                while (!countStr.empty() && countStr.front() == ' ') countStr.erase(0, 1);
                while (!countStr.empty() && countStr.back() == ' ') countStr.pop_back();

                std::string trackStr = inner.substr(comma + 1);
                while (!trackStr.empty() && trackStr.front() == ' ') trackStr.erase(0, 1);
                while (!trackStr.empty() && trackStr.back() == ' ') trackStr.pop_back();

                auto subTokens = tokenizeTrackList(trackStr);

                int count = 0;
                bool isAutoFill = (countStr == "auto-fill");
                bool isAutoFit = (countStr == "auto-fit");

                if (isAutoFill || isAutoFit) {
                    if (isAutoFit) result.hasAutoFit = true;
                    // Compute how many repetitions fit in the available space,
                    // gaps included: count·P + (count·k − 1)·gap ≤ available,
                    // where P is the fixed size of one repetition and k the
                    // number of tracks per repetition (Grid §7.2.3.1).
                    float patternSize = computeRepeatPatternSize(subTokens, available, fontSize);
                    int tracksPerRep = 0;
                    for (auto& st : subTokens)
                        if (st.empty() || st.front() != '[') tracksPerRep++;
                    float denom = patternSize + tracksPerRep * gap;
                    if (patternSize > 0 && denom > 0) {
                        count = static_cast<int>(std::floor((available + gap) / denom));
                        if (count < 1) count = 1;
                    } else {
                        count = 1; // fallback: at least one repetition
                    }
                    if (isAutoFit) {
                        result.autoFitBegin = static_cast<int>(result.tracks.size());
                        result.autoFitEnd = result.autoFitBegin + count * tracksPerRep;
                    }
                } else {
                    try { count = std::stoi(countStr); } catch (...) { count = 1; }
                }

                for (int r = 0; r < count; r++) {
                    for (auto& st : subTokens) {
                        if (!st.empty() && st.front() == '[' && st.back() == ']') {
                            // Named line inside repeat
                            std::string names = st.substr(1, st.size() - 2);
                            std::istringstream iss(names);
                            std::string n;
                            while (iss >> n) {
                                result.lineNames[n].push_back(lineIndex);
                            }
                        } else {
                            result.tracks.push_back(parseTrackSize(st, available, fontSize));
                            lineIndex++;
                        }
                    }
                }
            }
            continue;
        }

        result.tracks.push_back(parseTrackSize(token, available, fontSize));
        lineIndex++;
    }

    return result;
}

// Legacy wrapper that returns just the track sizes
std::vector<TrackSize> parseTrackList(const std::string& value, float available, float fontSize) {
    return parseTrackListWithNames(value, available, fontSize).tracks;
}

// Resolve track sizes to actual pixel widths/heights.
// Distributes fr units among remaining space after fixed tracks are resolved.
// minContributions (parallel to tracks; may be empty) carries the items'
// min-content contributions per track: a flexible track whose min sizing
// function is auto cannot end up smaller than that (Grid §11.8/§12.7) —
// even if that overflows the grid container, matching Chromium.
std::vector<float> resolveTrackSizes(const std::vector<TrackSize>& tracks,
                                      float available, float gap,
                                      const std::vector<float>& contentSizes,
                                      const std::vector<float>& minContributions = std::vector<float>()) {
    size_t n = tracks.size();
    std::vector<float> sizes(n, 0);

    float totalGaps = (n > 1) ? gap * (n - 1) : 0;
    float usedSpace = totalGaps;
    float totalFr = 0;

    // First pass: resolve fixed and auto sizes
    for (size_t i = 0; i < n; i++) {
        auto& t = tracks[i];
        switch (t.kind) {
            case TrackSize::Fixed:
                sizes[i] = t.value;
                if (t.isMinmax && sizes[i] < t.minValue) sizes[i] = t.minValue;
                usedSpace += sizes[i];
                break;
            case TrackSize::Fractional:
                totalFr += t.value;
                break;
            case TrackSize::Auto:
            case TrackSize::MinContent:
            case TrackSize::MaxContent:
                // Use content size if available
                sizes[i] = (i < contentSizes.size()) ? contentSizes[i] : 0;
                if (t.isMinmax && sizes[i] < t.minValue) sizes[i] = t.minValue;
                usedSpace += sizes[i];
                break;
        }
    }

    // Second pass: distribute remaining space to fr tracks
    float freeSpace = available - usedSpace;
    if (freeSpace < 0) freeSpace = 0;

    if (totalFr > 0) {
        // Each flexible track has a floor: an explicit minmax() minimum, or —
        // when its min sizing function is auto — its items' min-content
        // contributions. Find the fr unit iteratively (Grid §12.7.1): any
        // track whose fr share falls below its floor is frozen at the floor
        // and removed from the distribution, then the unit is recomputed.
        std::vector<float> floors(n, 0.0f);
        for (size_t i = 0; i < n; i++) {
            if (tracks[i].kind != TrackSize::Fractional) continue;
            if (tracks[i].isMinmax && !tracks[i].minIsAuto)
                floors[i] = std::max(0.0f, tracks[i].minValue);
            else if (i < minContributions.size())
                floors[i] = std::max(0.0f, minContributions[i]);
        }
        std::vector<bool> frozen(n, false);
        float space = freeSpace;
        float activeFr = totalFr;
        bool changed = true;
        while (changed) {
            changed = false;
            float frUnit = (activeFr > 0) ? space / activeFr : 0.0f;
            for (size_t i = 0; i < n; i++) {
                if (tracks[i].kind != TrackSize::Fractional || frozen[i]) continue;
                if (tracks[i].value * frUnit < floors[i]) {
                    sizes[i] = floors[i];
                    frozen[i] = true;
                    space -= floors[i];
                    if (space < 0) space = 0;
                    activeFr -= tracks[i].value;
                    changed = true;
                }
            }
        }
        float frUnit = (activeFr > 0) ? space / activeFr : 0.0f;
        for (size_t i = 0; i < n; i++) {
            if (tracks[i].kind == TrackSize::Fractional && !frozen[i])
                sizes[i] = tracks[i].value * frUnit;
        }
    }

    return sizes;
}

// Outer min-content contribution of a grid item: content min-content width
// plus its own padding, border, and margins. A definite width overrides the
// content measurement; min/max-width clamp it (mirroring the contribution
// rules in computeMinContentWidth's child walk). Items that are scroll
// containers (overflow != visible) have an automatic minimum of zero, so
// only their edges contribute.
float itemMinContentContribution(LayoutNode* item, float parentFontSize, TextMetrics& metrics) {
    auto& cs = item->computedStyle();
    float cfs = resolveLength(styleVal(item, Prop::FontSize), parentFontSize, parentFontSize);
    if (cfs <= 0) cfs = parentFontSize;
    float ph = resolveLength(styleVal(item, Prop::PaddingLeft), 0, cfs) +
               resolveLength(styleVal(item, Prop::PaddingRight), 0, cfs);
    float bh = 0;
    if (styleVal(item, Prop::BorderLeftStyle) != "none")
        bh += resolveLength(styleVal(item, Prop::BorderLeftWidth), 0, cfs);
    if (styleVal(item, Prop::BorderRightStyle) != "none")
        bh += resolveLength(styleVal(item, Prop::BorderRightWidth), 0, cfs);
    float mh = resolveLength(styleVal(item, Prop::MarginLeft), 0, cfs) +
               resolveLength(styleVal(item, Prop::MarginRight), 0, cfs);

    const std::string& ov = styleVal(item, Prop::Overflow);
    const std::string& ovx = styleVal(item, Prop::OverflowX);
    bool scrollContainer = (!ov.empty() && ov != "visible") ||
                           (!ovx.empty() && ovx != "visible");

    const std::string& wVal = styleVal(item, Prop::Width);
    bool definiteW = !wVal.empty() && wVal != "auto" &&
                     wVal.find('%') == std::string::npos &&
                     !isIntrinsicSizingKeyword(wVal);
    float contribution;
    if (definiteW) {
        float w = resolveLength(wVal, 0, cfs);
        contribution = (styleVal(item, Prop::BoxSizing) == "border-box")
            ? w + mh : w + ph + bh + mh;
    } else if (scrollContainer) {
        contribution = ph + bh + mh;
    } else {
        contribution = computeMinContentWidth(item, metrics) + ph + bh + mh;
    }
    const std::string& minWVal = styleVal(item, Prop::MinWidth);
    if (!minWVal.empty() && minWVal != "auto" &&
        minWVal.find('%') == std::string::npos) {
        float v = resolveLength(minWVal, 0, cfs);
        float t = (styleVal(item, Prop::BoxSizing) == "border-box")
            ? v + mh : v + ph + bh + mh;
        if (contribution < t) contribution = t;
    }
    const std::string& maxWVal = styleVal(item, Prop::MaxWidth);
    if (!maxWVal.empty() && maxWVal != "none" &&
        maxWVal.find('%') == std::string::npos) {
        float v = resolveLength(maxWVal, 0, cfs);
        float t = (styleVal(item, Prop::BoxSizing) == "border-box")
            ? v + mh : v + ph + bh + mh;
        if (contribution > t) contribution = t;
    }
    return contribution;
}

// Named grid area: 1-based line numbers
struct GridArea {
    int rowStart = 0, colStart = 0, rowEnd = 0, colEnd = 0;
};

// Parse grid-template-areas into a map of name → GridArea (1-based lines).
// Format: "header header" "sidebar content" "footer footer"
std::unordered_map<std::string, GridArea> parseGridTemplateAreas(const std::string& value) {
    std::unordered_map<std::string, GridArea> areas;
    if (value.empty() || value == "none") return areas;

    // Extract quoted row strings
    std::vector<std::vector<std::string>> grid;
    size_t pos = 0;
    while (pos < value.size()) {
        auto q = value.find('"', pos);
        if (q == std::string::npos) q = value.find('\'', pos);
        if (q == std::string::npos) break;
        char quote = value[q];
        auto end = value.find(quote, q + 1);
        if (end == std::string::npos) break;
        std::string rowStr = value.substr(q + 1, end - q - 1);
        // Tokenize row by whitespace
        std::vector<std::string> rowTokens;
        std::istringstream iss(rowStr);
        std::string tok;
        while (iss >> tok) rowTokens.push_back(tok);
        grid.push_back(std::move(rowTokens));
        pos = end + 1;
    }

    // Build areas from the grid (name → bounding rectangle)
    for (size_t r = 0; r < grid.size(); r++) {
        for (size_t c = 0; c < grid[r].size(); c++) {
            const std::string& name = grid[r][c];
            if (name == ".") continue;
            auto it = areas.find(name);
            if (it == areas.end()) {
                // 1-based line numbers
                areas[name] = {static_cast<int>(r + 1), static_cast<int>(c + 1),
                               static_cast<int>(r + 2), static_cast<int>(c + 2)};
            } else {
                // Expand to encompass this cell
                auto& a = it->second;
                if (static_cast<int>(r + 1) < a.rowStart) a.rowStart = static_cast<int>(r + 1);
                if (static_cast<int>(c + 1) < a.colStart) a.colStart = static_cast<int>(c + 1);
                if (static_cast<int>(r + 2) > a.rowEnd) a.rowEnd = static_cast<int>(r + 2);
                if (static_cast<int>(c + 2) > a.colEnd) a.colEnd = static_cast<int>(c + 2);
            }
        }
    }
    return areas;
}

// Parse grid-area value: "row-start / column-start / row-end / column-end"
// or named area. Returns 1-based line numbers.
struct GridPlacement {
    int rowStart = 0;  // 0 = auto
    int colStart = 0;
    int rowEnd = 0;
    int colEnd = 0;
};

// Special sentinel for named line references (stored in GridPlacement).
// Named lines are resolved later when we have the NamedLines map.
constexpr int GRID_LINE_NAMED = -1000;

struct GridLineRef {
    int value = 0;       // positive = line number, negative = span, 0 = auto
    std::string name;    // non-empty if this is a named line reference
};

// Returns positive for line numbers, negative for span counts (e.g., -2 = span 2), 0 for auto.
// Named line references are stored in the name field of GridLineRef.
GridLineRef parseGridLineRef(const std::string& val) {
    if (val.empty() || val == "auto") return {0, ""};
    // "span" or "span N"
    if (val.size() >= 4 && val.substr(0, 4) == "span") {
        std::string rest = val.substr(4);
        size_t s = rest.find_first_not_of(" \t");
        if (s == std::string::npos || rest.empty()) return {-1, ""}; // bare "span" = span 1
        rest = rest.substr(s);
        int n = 1;
        try { n = std::stoi(rest); } catch (...) {
            // "span name" — span to a named line (not fully supported, treat as span 1)
            n = 1;
        }
        if (n < 1) n = 1;
        return {-n, ""};
    }
    // Try numeric line
    try { return {std::stoi(val), ""}; } catch (...) {}
    // Must be a named line reference
    return {GRID_LINE_NAMED, val};
}

// Legacy wrapper for backward compat in parseGridPlacement
int parseGridLine(const std::string& val) {
    return parseGridLineRef(val).value;
}

// Resolve a named line reference to a 1-based line number using the NamedLines map.
// Returns 0 (auto) if the name is not found.
int resolveNamedLine(const GridLineRef& ref, const NamedLines& lineNames, int occurrence = 1) {
    if (ref.name.empty() || ref.value != GRID_LINE_NAMED) return ref.value;
    auto it = lineNames.find(ref.name);
    if (it == lineNames.end() || it->second.empty()) return 0;
    // Return the nth occurrence (1-based)
    int idx = std::min(occurrence - 1, static_cast<int>(it->second.size()) - 1);
    if (idx < 0) idx = 0;
    return it->second[idx];
}

GridPlacement parseGridPlacement(const LayoutNode* node,
                                 const std::unordered_map<std::string, GridArea>& namedAreas,
                                 const NamedLines& colLines = {},
                                 const NamedLines& rowLines = {}) {
    GridPlacement gp;

    // Check grid-area first (shorthand)
    const std::string& area = styleVal(node, Prop::GridArea);
    if (!area.empty() && area != "auto") {
        // Check if it's a named area reference (single identifier, no slashes)
        if (area.find('/') == std::string::npos) {
            auto it = namedAreas.find(area);
            if (it != namedAreas.end()) {
                gp.rowStart = it->second.rowStart;
                gp.colStart = it->second.colStart;
                gp.rowEnd = it->second.rowEnd;
                gp.colEnd = it->second.colEnd;
                return gp;
            }
        }

        // Parse "row-start / col-start / row-end / col-end"
        std::vector<std::string> parts;
        std::string current;
        for (char c : area) {
            if (c == '/') {
                while (!current.empty() && current.back() == ' ') current.pop_back();
                while (!current.empty() && current.front() == ' ') current.erase(0, 1);
                parts.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        while (!current.empty() && current.back() == ' ') current.pop_back();
        while (!current.empty() && current.front() == ' ') current.erase(0, 1);
        if (!current.empty()) parts.push_back(current);

        if (parts.size() >= 1) gp.rowStart = parseGridLine(parts[0]);
        if (parts.size() >= 2) gp.colStart = parseGridLine(parts[1]);
        if (parts.size() >= 3) gp.rowEnd = parseGridLine(parts[2]);
        if (parts.size() >= 4) gp.colEnd = parseGridLine(parts[3]);
        return gp;
    }

    // Individual properties — resolve named lines
    auto resolveRow = [&](const std::string& val) {
        auto ref = parseGridLineRef(val);
        return resolveNamedLine(ref, rowLines);
    };
    auto resolveCol = [&](const std::string& val) {
        auto ref = parseGridLineRef(val);
        return resolveNamedLine(ref, colLines);
    };
    gp.rowStart = resolveRow(styleVal(node, Prop::GridRowStart));
    gp.colStart = resolveCol(styleVal(node, Prop::GridColumnStart));
    gp.rowEnd = resolveRow(styleVal(node, Prop::GridRowEnd));
    gp.colEnd = resolveCol(styleVal(node, Prop::GridColumnEnd));

    // Handle grid-row / grid-column shorthands
    const std::string& gridRow = styleVal(node, Prop::GridRow);
    if (!gridRow.empty() && gridRow != "auto") {
        auto slash = gridRow.find('/');
        if (slash != std::string::npos) {
            std::string s = gridRow.substr(0, slash);
            std::string e = gridRow.substr(slash + 1);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            while (!e.empty() && e.back() == ' ') e.pop_back();
            while (!e.empty() && e.front() == ' ') e.erase(0, 1);
            gp.rowStart = resolveRow(s);
            gp.rowEnd = resolveRow(e);
        } else {
            gp.rowStart = resolveRow(gridRow);
        }
    }

    const std::string& gridCol = styleVal(node, Prop::GridColumn);
    if (!gridCol.empty() && gridCol != "auto") {
        auto slash = gridCol.find('/');
        if (slash != std::string::npos) {
            std::string s = gridCol.substr(0, slash);
            std::string e = gridCol.substr(slash + 1);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            while (!e.empty() && e.back() == ' ') e.pop_back();
            while (!e.empty() && e.front() == ' ') e.erase(0, 1);
            gp.colStart = resolveCol(s);
            gp.colEnd = resolveCol(e);
        } else {
            gp.colStart = resolveCol(gridCol);
        }
    }

    return gp;
}

} // anonymous namespace

float gridMaxContentWidth(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;
    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;

    // Widest in-flow item's outer max-content contribution — the stand-in
    // for intrinsic (auto/fr/min-content/max-content) tracks. This is an
    // approximation: proper track sizing distributes items per column, but
    // for the common fit-content cases (fixed tracks, or a single intrinsic
    // track) it matches.
    float itemMax = 0.0f;
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) continue;
        auto& cs = child->computedStyle();
        if (styleVal(child, Prop::Display) == "none") continue;
        const std::string& cpos = styleVal(child, Prop::Position);
        if (cpos == "absolute" || cpos == "fixed") continue;
        float cfs = resolveLength(styleVal(child, Prop::FontSize), fontSize, fontSize);
        if (cfs <= 0) cfs = fontSize;
        float ph = resolveLength(styleVal(child, Prop::PaddingLeft), 0, cfs) +
                   resolveLength(styleVal(child, Prop::PaddingRight), 0, cfs);
        float bh = 0;
        if (styleVal(child, Prop::BorderLeftStyle) != "none")
            bh += resolveLength(styleVal(child, Prop::BorderLeftWidth), 0, cfs);
        if (styleVal(child, Prop::BorderRightStyle) != "none")
            bh += resolveLength(styleVal(child, Prop::BorderRightWidth), 0, cfs);
        float mh = resolveLength(styleVal(child, Prop::MarginLeft), 0, cfs) +
                   resolveLength(styleVal(child, Prop::MarginRight), 0, cfs);
        const std::string& wVal = styleVal(child, Prop::Width);
        float contribution;
        if (!wVal.empty() && wVal != "auto" && wVal.find('%') == std::string::npos) {
            float w = resolveLength(wVal, 0, cfs);
            contribution = (styleVal(child, Prop::BoxSizing) == "border-box")
                ? w + mh : w + ph + bh + mh;
        } else {
            contribution = computeMaxContentWidth(child, metrics) + ph + bh + mh;
        }
        itemMax = std::max(itemMax, contribution);
    }

    // Percentages and auto-fill/auto-fit resolve against an indefinite (0)
    // available size under max-content sizing.
    auto tracks = parseTrackList(styleVal(node, Prop::GridTemplateColumns), 0.0f, fontSize);
    if (tracks.empty()) return itemMax;

    float sum = 0.0f;
    for (const auto& t : tracks) {
        if (t.isMinmax)
            sum += (t.maxValue >= 0) ? t.maxValue : itemMax;
        else if (t.kind == TrackSize::Fixed)
            sum += t.value;
        else
            sum += itemMax; // fr / auto / min-content / max-content
    }
    float gap = resolveLength(styleVal(node, Prop::ColumnGap), 0, fontSize);
    if (gap > 0 && tracks.size() > 1) sum += gap * (tracks.size() - 1);
    return sum;
}

void layoutGrid(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(node, kMarginProps, availableWidth, fontSize);
    node->box.padding = resolveEdges(node, kPaddingProps, availableWidth, fontSize);
    node->box.border = resolveBorders(node, availableWidth, fontSize);

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;

    // Container width
    float specW = resolveDim(styleVal(node, Prop::Width), availableWidth, fontSize);
    float containerWidth;
    if (specW >= 0) {
        if (styleVal(node, Prop::BoxSizing) == "border-box")
            containerWidth = specW - paddingH - borderH;
        else
            containerWidth = specW;
        if (containerWidth < 0) containerWidth = 0;
    } else {
        containerWidth = availableWidth - node->box.margin.left - node->box.margin.right - paddingH - borderH;
        if (containerWidth < 0) containerWidth = 0;
    }

    // Parse gap
    float rowGap = resolveLength(styleVal(node, Prop::RowGap), containerWidth, fontSize);
    float colGap = resolveLength(styleVal(node, Prop::ColumnGap), containerWidth, fontSize);
    // gap shorthand handling (already expanded by properties.cpp)

    // Parse grid template (with named lines and auto-fill/auto-fit support)
    auto colParsed = parseTrackListWithNames(styleVal(node, Prop::GridTemplateColumns), containerWidth, fontSize, colGap);
    auto rowParsed = parseTrackListWithNames(styleVal(node, Prop::GridTemplateRows), containerWidth, fontSize, rowGap);
    auto colTracks = std::move(colParsed.tracks);
    auto rowTracks = std::move(rowParsed.tracks);
    auto colLineNames = std::move(colParsed.lineNames);
    auto rowLineNames = std::move(rowParsed.lineNames);
    bool hasAutoFitCols = colParsed.hasAutoFit;
    bool hasAutoFitRows = rowParsed.hasAutoFit;
    int autoFitColBegin = colParsed.autoFitBegin, autoFitColEnd = colParsed.autoFitEnd;
    int autoFitRowBegin = rowParsed.autoFitBegin, autoFitRowEnd = rowParsed.autoFitEnd;

    // Parse implicit track sizing (default to Auto if not specified)
    const std::string& autoColVal = styleVal(node, Prop::GridAutoColumns);
    const std::string& autoRowVal = styleVal(node, Prop::GridAutoRows);
    TrackSize autoColTrack = (autoColVal.empty() || autoColVal == "auto")
        ? TrackSize{TrackSize::Auto, 0, 0, -1, false}
        : parseTrackSize(autoColVal, containerWidth, fontSize);
    TrackSize autoRowTrack = (autoRowVal.empty() || autoRowVal == "auto")
        ? TrackSize{TrackSize::Auto, 0, 0, -1, false}
        : parseTrackSize(autoRowVal, containerWidth, fontSize);

    // Parse named grid areas
    auto namedAreas = parseGridTemplateAreas(styleVal(node, Prop::GridTemplateAreas));

    // Infer track counts from template areas if tracks not explicitly defined
    if (!namedAreas.empty()) {
        size_t areaMaxCol = 0, areaMaxRow = 0;
        for (auto& [name, area] : namedAreas) {
            areaMaxCol = std::max(areaMaxCol, static_cast<size_t>(area.colEnd - 1));
            areaMaxRow = std::max(areaMaxRow, static_cast<size_t>(area.rowEnd - 1));
        }
        while (colTracks.size() < areaMaxCol) {
            colTracks.push_back(autoColTrack);
        }
        while (rowTracks.size() < areaMaxRow) {
            rowTracks.push_back(autoRowTrack);
        }
    }

    // Collect grid items (skip text nodes, display:none, and absolute/fixed positioned)
    struct GridItem {
        LayoutNode* node;
        GridPlacement placement;
        int row, col;     // 0-based resolved position
        int rowSpan, colSpan;
    };
    std::vector<GridItem> items;

    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) continue;
        auto& cs = child->computedStyle();
        if (styleVal(child, Prop::Display) == "none") {
            child->box = LayoutBox{};
            continue;
        }
        const std::string& childPos = styleVal(child, Prop::Position);
        if (childPos == "absolute" || childPos == "fixed") continue;
        GridItem item;
        item.node = child;
        item.placement = parseGridPlacement(child, namedAreas, colLineNames, rowLineNames);
        item.row = -1; item.col = -1;
        item.rowSpan = 1; item.colSpan = 1;
        items.push_back(item);
    }

    // Determine grid dimensions
    size_t numCols = colTracks.size();
    size_t numRows = rowTracks.size();

    // If no explicit tracks, create implicit tracks based on item count
    if (numCols == 0) {
        // Auto-place: determine columns from sqrt of item count or explicit placements
        size_t maxCol = 0;
        for (auto& item : items) {
            if (item.placement.colStart > 0)
                maxCol = std::max(maxCol, static_cast<size_t>(item.placement.colStart));
        }
        numCols = std::max(maxCol, items.empty() ? size_t(1) : std::max(size_t(1), items.size()));
    }

    // Resolve item placements
    // First: place explicitly positioned items
    std::vector<std::vector<bool>> occupied; // [row][col]
    auto ensureRows = [&](size_t r) {
        while (occupied.size() <= r) {
            occupied.push_back(std::vector<bool>(numCols, false));
        }
    };

    for (auto& item : items) {
        auto& gp = item.placement;

        // Convert 1-based grid lines to 0-based indices
        if (gp.colStart > 0) item.col = gp.colStart - 1;
        if (gp.rowStart > 0) item.row = gp.rowStart - 1;

        // Resolve span values (negative = span count)
        if (gp.colEnd < 0) {
            // colEnd is a span count
            item.colSpan = -gp.colEnd;
        } else if (gp.colEnd > 0 && item.col >= 0) {
            item.colSpan = gp.colEnd - gp.colStart;
            if (item.colSpan < 1) item.colSpan = 1;
        } else if (gp.colStart < 0) {
            // colStart is a span (e.g., grid-column: span 2)
            item.colSpan = -gp.colStart;
            item.col = -1; // needs auto-placement
        }

        if (gp.rowEnd < 0) {
            item.rowSpan = -gp.rowEnd;
        } else if (gp.rowEnd > 0 && item.row >= 0) {
            item.rowSpan = gp.rowEnd - gp.rowStart;
            if (item.rowSpan < 1) item.rowSpan = 1;
        } else if (gp.rowStart < 0) {
            item.rowSpan = -gp.rowStart;
            item.row = -1;
        }
    }

    // Check auto-flow direction
    const std::string& autoFlow = styleVal(node, Prop::GridAutoFlow);
    bool columnFlow = (autoFlow == "column");

    // Pre-pass: mark cells occupied for items with fully explicit placement
    // BEFORE auto-placing the rest. Per spec, auto-placed items skip cells
    // already taken by explicitly-placed items regardless of source order.
    for (auto& item : items) {
        if (item.col >= 0 && item.row >= 0) {
            for (int r = item.row; r < item.row + item.rowSpan; r++) {
                ensureRows(r);
                for (int c = item.col; c < item.col + item.colSpan && c < static_cast<int>(numCols); c++) {
                    if (c >= 0 && r >= 0) occupied[r][c] = true;
                }
            }
        }
    }

    // Auto-place items that don't have explicit positions
    size_t autoRow = 0, autoCol = 0;
    for (auto& item : items) {
        if (item.col >= 0 && item.row >= 0) {
            // Already marked above — skip the marking that would re-do it.
            continue;
        }
        if (item.col < 0 && item.row < 0) {
            // Find next available cell that fits the item's span
            ensureRows(autoRow);
            if (columnFlow) {
                // Column-major: advance row first, then column
                // Use numRows from explicit tracks as the wrapping point
                size_t wrapRows = numRows > 0 ? numRows : items.size();
                auto fits = [&](size_t r, size_t c) {
                    for (int dr = 0; dr < item.rowSpan; dr++) {
                        ensureRows(r + dr);
                        for (int dc = 0; dc < item.colSpan; dc++) {
                            if (c + dc >= numCols || occupied[r + dr][c + dc]) return false;
                        }
                    }
                    return true;
                };
                while (!fits(autoRow, autoCol)) {
                    autoRow++;
                    if (autoRow + item.rowSpan > wrapRows) { autoRow = 0; autoCol++; }
                    ensureRows(autoRow);
                }
                item.row = static_cast<int>(autoRow);
                item.col = static_cast<int>(autoCol);
                autoRow += item.rowSpan;
                if (autoRow >= wrapRows) { autoRow = 0; autoCol++; }
            } else {
                // Row-major: advance column first, then row
                auto fits = [&](size_t r, size_t c) {
                    for (int dr = 0; dr < item.rowSpan; dr++) {
                        ensureRows(r + dr);
                        for (int dc = 0; dc < item.colSpan; dc++) {
                            if (c + dc >= numCols || occupied[r + dr][c + dc]) return false;
                        }
                    }
                    return true;
                };
                while (!fits(autoRow, autoCol)) {
                    autoCol++;
                    if (autoCol + item.colSpan > numCols) { autoCol = 0; autoRow++; ensureRows(autoRow); }
                }
                item.row = static_cast<int>(autoRow);
                item.col = static_cast<int>(autoCol);
                autoCol += item.colSpan;
                if (autoCol >= numCols) { autoCol = 0; autoRow++; }
            }
        } else if (item.col < 0) {
            // Has explicit row, find next column in that row
            ensureRows(item.row);
            size_t c = 0;
            while (c + item.colSpan > numCols || (c < numCols && occupied[item.row][c])) c++;
            item.col = static_cast<int>(c);
        } else if (item.row < 0) {
            // Has explicit column, find next row for that column
            size_t r = 0;
            ensureRows(r);
            while (occupied.size() > r && occupied[r][item.col]) { r++; ensureRows(r); }
            item.row = static_cast<int>(r);
        }

        // Mark cells as occupied
        for (int r = item.row; r < item.row + item.rowSpan; r++) {
            ensureRows(r);
            for (int c = item.col; c < item.col + item.colSpan && c < static_cast<int>(numCols); c++) {
                occupied[r][c] = true;
            }
        }
    }

    // Determine actual number of rows needed
    for (auto& item : items) {
        numRows = std::max(numRows, static_cast<size_t>(item.row + item.rowSpan));
    }

    // Ensure we have enough track definitions — use grid-auto-columns/rows for implicit tracks
    while (colTracks.size() < numCols) {
        colTracks.push_back(autoColTrack);
    }
    while (rowTracks.size() < numRows) {
        rowTracks.push_back(autoRowTrack);
    }

    // auto-fit: collapse empty repeated tracks (Grid §7.2.3.2). A collapsed
    // track is fixed at 0 and its gutters collapse — equivalent, for sizing
    // and positioning, to removing the track entirely, which also lets the
    // remaining flexible tracks absorb the freed space like Chromium does.
    auto collapseEmptyTracks = [&](std::vector<TrackSize>& tracks, size_t& count,
                                   int fitBegin, int fitEnd, bool isCol) {
        int limit = static_cast<int>(std::min(tracks.size(), count));
        if (fitBegin < 0 || fitBegin >= limit) return;
        fitEnd = std::min(fitEnd, limit);
        std::vector<bool> hasItem(limit, false);
        for (auto& item : items) {
            int start = isCol ? item.col : item.row;
            int span = isCol ? item.colSpan : item.rowSpan;
            for (int c = std::max(start, 0); c < start + span && c < limit; c++)
                hasItem[c] = true;
        }
        std::vector<int> shift(limit, 0);
        std::vector<TrackSize> kept;
        kept.reserve(tracks.size());
        int removed = 0;
        for (int i = 0; i < limit; i++) {
            shift[i] = removed;
            if (i >= fitBegin && i < fitEnd && !hasItem[i]) { removed++; continue; }
            kept.push_back(tracks[i]);
        }
        if (removed == 0) return;
        for (size_t i = limit; i < tracks.size(); i++) kept.push_back(tracks[i]);
        for (auto& item : items) {
            int& start = isCol ? item.col : item.row;
            if (start >= 0 && start < limit) start -= shift[start];
        }
        tracks = std::move(kept);
        count -= removed;
    };
    if (hasAutoFitCols)
        collapseEmptyTracks(colTracks, numCols, autoFitColBegin, autoFitColEnd, true);
    if (hasAutoFitRows)
        collapseEmptyTracks(rowTracks, numRows, autoFitRowBegin, autoFitRowEnd, false);

    // Layout items to determine content sizes for auto tracks
    std::vector<float> colContentSizes(numCols, 0);
    std::vector<float> rowContentSizes(numRows, 0);

    for (auto& item : items) {
        float itemAvail = containerWidth / numCols; // rough estimate
        // The measure exists only to produce the two contributions read back
        // below, and it is a pure function of (subtree, style, itemAvail) — the
        // track sizes it feeds are computed after this loop, so nothing it sees
        // depends on them. Cache the two scalars per item, keyed by the width
        // they were taken at, so a clean item doesn't re-lay its whole subtree
        // here just to re-derive them. This is the measure that could never hit
        // the reuse cache on its own: itemAvail is a guess, and the real layout
        // below runs at the resolved column width, so the two never match.
        if (item.node->box.dirty || !(item.node->gridMeasuredAtW == itemAvail)) {
            layoutNode(item.node, itemAvail, metrics);
            item.node->gridMeasuredAtW = itemAvail;
            item.node->gridMeasuredOuterW = item.node->box.fullWidth() +
                item.node->box.margin.left + item.node->box.margin.right;
            item.node->gridMeasuredOuterH = item.node->box.fullHeight() +
                item.node->box.margin.top + item.node->box.margin.bottom;
        }
        float itemW = item.node->gridMeasuredOuterW;
        float itemH = item.node->gridMeasuredOuterH;

        if (item.colSpan == 1 && item.col < static_cast<int>(numCols)) {
            colContentSizes[item.col] = std::max(colContentSizes[item.col], itemW);
        }
        if (item.rowSpan == 1 && item.row < static_cast<int>(numRows)) {
            rowContentSizes[item.row] = std::max(rowContentSizes[item.row], itemH);
        }
    }

    // Min-content contributions of items in flexible columns: a 1fr track
    // cannot shrink below its items' min-content contributions (Grid §11.8),
    // even when that overflows the container — matching Chromium.
    std::vector<float> colMinSizes(numCols, 0);
    for (auto& item : items) {
        if (item.colSpan != 1 || item.col < 0 || item.col >= static_cast<int>(numCols))
            continue;
        const auto& t = colTracks[item.col];
        if (t.kind != TrackSize::Fractional || (t.isMinmax && !t.minIsAuto))
            continue;
        colMinSizes[item.col] = std::max(colMinSizes[item.col],
            itemMinContentContribution(item.node, fontSize, metrics));
    }

    // Resolve track sizes
    auto colSizes = resolveTrackSizes(colTracks, containerWidth, colGap, colContentSizes, colMinSizes);
    // Determine the container's resolved content height for distributing 1fr
    // rows.  Prefer (in order): an explicit CSS height, a content height
    // pre-set by an outer pass (e.g. the parent flex container distributed
    // space), or the parent's available height.  Without this, 1fr rows
    // collapse to their content size since freeSpace = 0 - usedSpace = 0.
    float rowAvailable = 0.0f;
    {
        float specH = resolveDim(styleVal(node, Prop::Height), node->availableHeight, fontSize);
        if (specH >= 0) {
            if (styleVal(node, Prop::BoxSizing) == "border-box")
                rowAvailable = specH - paddingV - borderV;
            else
                rowAvailable = specH;
        } else if (node->box.contentRect.height > 0) {
            rowAvailable = node->box.contentRect.height;
        }
        if (rowAvailable < 0) rowAvailable = 0;
    }
    // Rows: the items' min-content block contribution is their laid-out
    // height, so rowContentSizes doubles as the flexible-row floor.
    auto rowSizes = resolveTrackSizes(rowTracks, rowAvailable, rowGap, rowContentSizes, rowContentSizes);

    // Container-level justify-items / align-items defaults to "stretch".
    // Per-item justify-self / align-self can override.
    const std::string& containerJustifyItems = styleVal(node, Prop::JustifyItems);
    const std::string& containerAlignItems = styleVal(node, Prop::AlignItems);
    auto resolveAlign = [](const std::string& self, const std::string& items) {
        if (!self.empty() && self != "auto" && self != "normal") return self;
        if (!items.empty() && items != "normal") return items;
        return std::string("stretch");
    };

    // Re-layout items with resolved column widths
    for (auto& item : items) {
        // Calculate available width from spanned columns
        float itemWidth = 0;
        for (int c = item.col; c < item.col + item.colSpan && c < static_cast<int>(numCols); c++) {
            itemWidth += colSizes[c];
            if (c > item.col) itemWidth += colGap;
        }

        // Claim the item so we know whether the box below is one this pass
        // computed or one handed back from the last. beginLayoutNode returns
        // false when the cached subtree is still valid for this width — then
        // the box already holds the geometry this loop would have produced.
        bool laidNow = beginLayoutNode(item.node, itemWidth);
        if (laidNow) layoutNode(item.node, itemWidth, metrics);

        // Set content width to fill the grid area only if justify-self resolves to stretch.
        // On a reused box this re-states the width it already holds: the inputs
        // (itemWidth and the item's own padding/border/margin) are unchanged.
        auto& cs = item.node->computedStyle();
        const std::string& w = styleVal(item.node, Prop::Width);
        std::string justifySelf = resolveAlign(styleVal(item.node, Prop::JustifySelf), containerJustifyItems);
        if ((w == "auto" || w.empty()) && justifySelf == "stretch") {
            float cw = itemWidth -
                item.node->box.padding.left - item.node->box.padding.right -
                item.node->box.border.left - item.node->box.border.right -
                item.node->box.margin.left - item.node->box.margin.right;
            if (cw > 0) item.node->box.contentRect.width = cw;
        }

        // Update row content sizes after relayout. A reused box holds last
        // pass's align-stretched height, so reading it back here would feed the
        // row's previous size in as this pass's content contribution and the row
        // could never shrink. Use the height recorded when the item was last
        // really laid out at this width.
        if (laidNow || std::isnan(item.node->gridNaturalOuterH)) {
            item.node->gridNaturalOuterH = item.node->box.fullHeight() +
                item.node->box.margin.top + item.node->box.margin.bottom;
            item.node->gridNaturalContentH = item.node->box.contentRect.height;
        }
        float itemH = item.node->gridNaturalOuterH;
        if (item.rowSpan == 1 && item.row < static_cast<int>(numRows)) {
            rowContentSizes[item.row] = std::max(rowContentSizes[item.row], itemH);
        }
    }

    // Re-resolve row sizes with updated content (use the same row available
    // as the first pass so 1fr tracks distribute the container's resolved
    // height, not just intrinsic content size).
    rowSizes = resolveTrackSizes(rowTracks, rowAvailable, rowGap, rowContentSizes, rowContentSizes);

    // CSS Grid stretch step: when the container has a definite resolved size
    // and there is leftover free space (no fr tracks consumed it), distribute
    // it equally across all Auto tracks. Default align-content is "normal"
    // which behaves as "stretch" for auto tracks. This matches Chromium for
    // cases like a 200px grid with 1 implicit auto row — the row stretches
    // to 200px instead of collapsing to content height.
    auto stretchAuto = [](std::vector<float>& sizes,
                          const std::vector<TrackSize>& tracks,
                          float available, float gap) {
        size_t n = tracks.size();
        if (n == 0 || available <= 0) return;
        float used = (n > 1 ? gap * (n - 1) : 0);
        size_t autoCount = 0;
        bool hasFr = false;
        for (size_t i = 0; i < n; i++) {
            used += sizes[i];
            if (tracks[i].kind == TrackSize::Fractional) hasFr = true;
            else if (tracks[i].kind == TrackSize::Auto) autoCount++;
        }
        if (hasFr) return; // fr tracks already absorbed free space
        if (autoCount == 0) return;
        float free = available - used;
        if (free <= 0) return;
        float share = free / static_cast<float>(autoCount);
        for (size_t i = 0; i < n; i++) {
            if (tracks[i].kind == TrackSize::Auto) sizes[i] += share;
        }
    };
    stretchAuto(rowSizes, rowTracks, rowAvailable, rowGap);

    // Compute track positions (cumulative offsets)
    std::vector<float> colPositions(numCols + 1, 0);
    for (size_t c = 0; c < numCols; c++) {
        colPositions[c + 1] = colPositions[c] + colSizes[c] + (c + 1 < numCols ? colGap : 0);
    }

    std::vector<float> rowPositions(numRows + 1, 0);
    for (size_t r = 0; r < numRows; r++) {
        rowPositions[r + 1] = rowPositions[r] + rowSizes[r] + (r + 1 < numRows ? rowGap : 0);
    }

    // Position items in their grid areas
    for (auto& item : items) {
        size_t c = static_cast<size_t>(item.col);
        size_t r = static_cast<size_t>(item.row);
        size_t cEnd = std::min(c + item.colSpan, numCols);
        size_t rEnd = std::min(r + item.rowSpan, numRows);

        float areaX = colPositions[c];
        float areaY = rowPositions[r];
        float areaW = colPositions[cEnd] - colPositions[c] - (cEnd > c + 1 ? 0 : 0);
        if (cEnd < numCols) areaW -= 0; // already accounted for
        areaW = colPositions[cEnd] - colPositions[c];
        if (cEnd < numCols && item.colSpan > 0) areaW -= colGap; // remove trailing gap
        // Actually: position at colPositions[c], width spans to colPositions[cEnd] - colGap (if not last)
        areaW = 0;
        for (int cc = item.col; cc < item.col + item.colSpan && cc < static_cast<int>(numCols); cc++) {
            areaW += colSizes[cc];
            if (cc > item.col) areaW += colGap;
        }

        float areaH = 0;
        for (int rr = item.row; rr < item.row + item.rowSpan && rr < static_cast<int>(numRows); rr++) {
            areaH += rowSizes[rr];
            if (rr > item.row) areaH += rowGap;
        }

        // Stretch item to fill grid area (default behavior) — only when align-self resolves to stretch
        auto& cs = item.node->computedStyle();
        const std::string& h = styleVal(item.node, Prop::Height);
        std::string alignSelf = resolveAlign(styleVal(item.node, Prop::AlignSelf), containerAlignItems);
        std::string justifySelfPos = resolveAlign(styleVal(item.node, Prop::JustifySelf), containerJustifyItems);
        if ((h == "auto" || h.empty()) && alignSelf == "stretch") {
            float ch = areaH -
                item.node->box.margin.top - item.node->box.margin.bottom -
                item.node->box.padding.top - item.node->box.padding.bottom -
                item.node->box.border.top - item.node->box.border.bottom;
            if (ch > 0) {
                // Judge "did the area stretch me past my content?" against the
                // content height from the item's last real layout, not against
                // the box — on a reused item the box already holds the stretched
                // height this same step wrote last pass, which would read as
                // "didn't grow" and skip the re-layout that produced it.
                float naturalH = item.node->gridNaturalContentH;
                if (std::isnan(naturalH)) naturalH = item.node->box.contentRect.height;
                bool grew = (ch > naturalH + 0.01f);
                item.node->box.contentRect.height = ch;
                // The earlier layoutNode pass ran without a definite height,
                // so any flex/grid layout inside the item collapsed to content
                // size.  Re-layout now that we have the stretched height so
                // 1fr / flex:1 descendants can distribute the new space.
                if (grew) {
                    float itemWidth = item.node->box.contentRect.width +
                        item.node->box.padding.left + item.node->box.padding.right +
                        item.node->box.border.left + item.node->box.border.right;
                    item.node->availableHeight = ch +
                        item.node->box.padding.top + item.node->box.padding.bottom +
                        item.node->box.border.top + item.node->box.border.bottom;
                    layoutNode(item.node, itemWidth, metrics);
                    // layoutNode may have overwritten contentRect.height with
                    // content size; restore the stretched value.
                    item.node->box.contentRect.height = ch;
                    if (item.node->box.contentRect.width < 0) item.node->box.contentRect.width = 0;
                }
            }
        }

        // Compute alignment offsets within the grid area for non-stretch items.
        float alignOffsetX = 0;
        float alignOffsetY = 0;
        {
            float itemOuterW = item.node->box.contentRect.width +
                item.node->box.padding.left + item.node->box.padding.right +
                item.node->box.border.left + item.node->box.border.right +
                item.node->box.margin.left + item.node->box.margin.right;
            float itemOuterH = item.node->box.contentRect.height +
                item.node->box.padding.top + item.node->box.padding.bottom +
                item.node->box.border.top + item.node->box.border.bottom +
                item.node->box.margin.top + item.node->box.margin.bottom;
            float freeX = areaW - itemOuterW;
            float freeY = areaH - itemOuterH;
            if (justifySelfPos == "center") alignOffsetX = freeX * 0.5f;
            else if (justifySelfPos == "end" || justifySelfPos == "flex-end" || justifySelfPos == "right")
                alignOffsetX = freeX;
            // start / stretch / flex-start / left → 0
            if (alignSelf == "center") alignOffsetY = freeY * 0.5f;
            else if (alignSelf == "end" || alignSelf == "flex-end")
                alignOffsetY = freeY;
        }

        item.node->box.contentRect.x = areaX + alignOffsetX +
            item.node->box.margin.left + item.node->box.padding.left + item.node->box.border.left;
        item.node->box.contentRect.y = areaY + alignOffsetY +
            item.node->box.margin.top + item.node->box.padding.top + item.node->box.border.top;

        // Apply position: relative offset
        const std::string& childPos = styleVal(item.node, Prop::Position);
        if (childPos == "relative" || childPos == "sticky") {
            float childFontSize = resolveLength(styleVal(item.node, Prop::FontSize), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            const std::string& topVal = styleVal(item.node, Prop::Top);
            const std::string& leftVal = styleVal(item.node, Prop::Left);
            const std::string& bottomVal = styleVal(item.node, Prop::Bottom);
            const std::string& rightVal = styleVal(item.node, Prop::Right);

            if (topVal != "auto" && !topVal.empty()) {
                item.node->box.contentRect.y += resolveLength(topVal, 0, childFontSize);
            } else if (bottomVal != "auto" && !bottomVal.empty()) {
                item.node->box.contentRect.y -= resolveLength(bottomVal, 0, childFontSize);
            }
            if (leftVal != "auto" && !leftVal.empty()) {
                item.node->box.contentRect.x += resolveLength(leftVal, containerWidth, childFontSize);
            } else if (rightVal != "auto" && !rightVal.empty()) {
                item.node->box.contentRect.x -= resolveLength(rightVal, containerWidth, childFontSize);
            }
        }

    }

    // Set container dimensions
    node->box.contentRect.width = containerWidth;

    // Natural (unconstrained) content height = sum of tracks plus inter-track gaps.
    // rowPositions[numRows] already excludes any trailing gap (the loop only adds
    // a gap when r+1 < numRows). Computed first so it survives any explicit height
    // or parent-imposed clamp — consumers use this for scroll extent / overflow
    // detection.
    float naturalH = 0;
    if (numRows > 0) {
        naturalH = rowPositions[numRows];
        if (naturalH < 0) naturalH = 0;
    }

    float specH = resolveDim(styleVal(node, Prop::Height), 0, fontSize);
    if (specH >= 0) {
        if (styleVal(node, Prop::BoxSizing) == "border-box")
            node->box.contentRect.height = specH - paddingV - borderV;
        else
            node->box.contentRect.height = specH;
        if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
    } else {
        node->box.contentRect.height = naturalH;
    }

    node->box.naturalHeight = std::max(naturalH, node->box.contentRect.height);
}

} // namespace htmlayout::layout
