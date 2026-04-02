#include "layout/grid.h"
#include "layout/formatting_context.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>

namespace htmlayout::layout {

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

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
};

ParsedTrackList parseTrackListWithNames(const std::string& value, float available, float fontSize) {
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
                    // Compute how many repetitions fit in the available space.
                    // Count only the track-size tokens (skip named lines).
                    float patternSize = computeRepeatPatternSize(subTokens, available, fontSize);
                    if (patternSize > 0) {
                        count = static_cast<int>(std::floor(available / patternSize));
                        if (count < 1) count = 1;
                    } else {
                        count = 1; // fallback: at least one repetition
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
std::vector<float> resolveTrackSizes(const std::vector<TrackSize>& tracks,
                                      float available, float gap,
                                      const std::vector<float>& contentSizes) {
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
        float frUnit = freeSpace / totalFr;
        for (size_t i = 0; i < n; i++) {
            if (tracks[i].kind == TrackSize::Fractional) {
                sizes[i] = tracks[i].value * frUnit;
                if (tracks[i].isMinmax && sizes[i] < tracks[i].minValue) {
                    sizes[i] = tracks[i].minValue;
                }
            }
        }
    }

    return sizes;
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

GridPlacement parseGridPlacement(const css::ComputedStyle& style,
                                 const std::unordered_map<std::string, GridArea>& namedAreas,
                                 const NamedLines& colLines = {},
                                 const NamedLines& rowLines = {}) {
    GridPlacement gp;

    // Check grid-area first (shorthand)
    const std::string& area = styleVal(style, "grid-area");
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
    gp.rowStart = resolveRow(styleVal(style, "grid-row-start"));
    gp.colStart = resolveCol(styleVal(style, "grid-column-start"));
    gp.rowEnd = resolveRow(styleVal(style, "grid-row-end"));
    gp.colEnd = resolveCol(styleVal(style, "grid-column-end"));

    // Handle grid-row / grid-column shorthands
    const std::string& gridRow = styleVal(style, "grid-row");
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

    const std::string& gridCol = styleVal(style, "grid-column");
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

void layoutGrid(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
    node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);

    Edges borderWidth{};
    const char* sideNames[] = {"top", "right", "bottom", "left"};
    float* bw[] = {&borderWidth.top, &borderWidth.right, &borderWidth.bottom, &borderWidth.left};
    for (int i = 0; i < 4; i++) {
        if (styleVal(style, std::string("border-") + sideNames[i] + "-style") != "none")
            *bw[i] = resolveLength(styleVal(style, std::string("border-") + sideNames[i] + "-width"), availableWidth, fontSize);
    }
    node->box.border = borderWidth;

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float paddingV = node->box.padding.top + node->box.padding.bottom;
    float borderV = node->box.border.top + node->box.border.bottom;

    // Container width
    float specW = resolveDim(styleVal(style, "width"), availableWidth, fontSize);
    float containerWidth;
    if (specW >= 0) {
        if (styleVal(style, "box-sizing") == "border-box")
            containerWidth = specW - paddingH - borderH;
        else
            containerWidth = specW;
        if (containerWidth < 0) containerWidth = 0;
    } else {
        containerWidth = availableWidth - node->box.margin.left - node->box.margin.right - paddingH - borderH;
        if (containerWidth < 0) containerWidth = 0;
    }

    // Parse gap
    float rowGap = resolveLength(styleVal(style, "row-gap"), containerWidth, fontSize);
    float colGap = resolveLength(styleVal(style, "column-gap"), containerWidth, fontSize);
    // gap shorthand handling (already expanded by properties.cpp)

    // Parse grid template (with named lines and auto-fill/auto-fit support)
    auto colParsed = parseTrackListWithNames(styleVal(style, "grid-template-columns"), containerWidth, fontSize);
    auto rowParsed = parseTrackListWithNames(styleVal(style, "grid-template-rows"), containerWidth, fontSize);
    auto colTracks = std::move(colParsed.tracks);
    auto rowTracks = std::move(rowParsed.tracks);
    auto colLineNames = std::move(colParsed.lineNames);
    auto rowLineNames = std::move(rowParsed.lineNames);
    bool hasAutoFitCols = colParsed.hasAutoFit;
    bool hasAutoFitRows = rowParsed.hasAutoFit;

    // Parse implicit track sizing (default to Auto if not specified)
    const std::string& autoColVal = styleVal(style, "grid-auto-columns");
    const std::string& autoRowVal = styleVal(style, "grid-auto-rows");
    TrackSize autoColTrack = (autoColVal.empty() || autoColVal == "auto")
        ? TrackSize{TrackSize::Auto, 0, 0, -1, false}
        : parseTrackSize(autoColVal, containerWidth, fontSize);
    TrackSize autoRowTrack = (autoRowVal.empty() || autoRowVal == "auto")
        ? TrackSize{TrackSize::Auto, 0, 0, -1, false}
        : parseTrackSize(autoRowVal, containerWidth, fontSize);

    // Parse named grid areas
    auto namedAreas = parseGridTemplateAreas(styleVal(style, "grid-template-areas"));

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
        if (styleVal(cs, "display") == "none") {
            child->box = LayoutBox{};
            continue;
        }
        const std::string& childPos = styleVal(cs, "position");
        if (childPos == "absolute" || childPos == "fixed") continue;
        GridItem item;
        item.node = child;
        item.placement = parseGridPlacement(cs, namedAreas, colLineNames, rowLineNames);
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
    const std::string& autoFlow = styleVal(style, "grid-auto-flow");
    bool columnFlow = (autoFlow == "column");

    // Auto-place items that don't have explicit positions
    size_t autoRow = 0, autoCol = 0;
    for (auto& item : items) {
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

    // Layout items to determine content sizes for auto tracks
    std::vector<float> colContentSizes(numCols, 0);
    std::vector<float> rowContentSizes(numRows, 0);

    for (auto& item : items) {
        float itemAvail = containerWidth / numCols; // rough estimate
        layoutNode(item.node, itemAvail, metrics);
        float itemW = item.node->box.fullWidth() + item.node->box.margin.left + item.node->box.margin.right;
        float itemH = item.node->box.fullHeight() + item.node->box.margin.top + item.node->box.margin.bottom;

        if (item.colSpan == 1 && item.col < static_cast<int>(numCols)) {
            colContentSizes[item.col] = std::max(colContentSizes[item.col], itemW);
        }
        if (item.rowSpan == 1 && item.row < static_cast<int>(numRows)) {
            rowContentSizes[item.row] = std::max(rowContentSizes[item.row], itemH);
        }
    }

    // Resolve track sizes
    auto colSizes = resolveTrackSizes(colTracks, containerWidth, colGap, colContentSizes);
    auto rowSizes = resolveTrackSizes(rowTracks, 0 /* no explicit height reference */, rowGap, rowContentSizes);

    // auto-fit: collapse empty tracks to 0
    if (hasAutoFitCols) {
        for (size_t c = 0; c < numCols; c++) {
            bool hasItem = false;
            for (auto& item : items) {
                if (item.col <= static_cast<int>(c) && static_cast<int>(c) < item.col + item.colSpan) {
                    hasItem = true;
                    break;
                }
            }
            if (!hasItem) colSizes[c] = 0;
        }
    }
    if (hasAutoFitRows) {
        for (size_t r = 0; r < numRows; r++) {
            bool hasItem = false;
            for (auto& item : items) {
                if (item.row <= static_cast<int>(r) && static_cast<int>(r) < item.row + item.rowSpan) {
                    hasItem = true;
                    break;
                }
            }
            if (!hasItem) rowSizes[r] = 0;
        }
    }

    // Re-layout items with resolved column widths
    for (auto& item : items) {
        // Calculate available width from spanned columns
        float itemWidth = 0;
        for (int c = item.col; c < item.col + item.colSpan && c < static_cast<int>(numCols); c++) {
            itemWidth += colSizes[c];
            if (c > item.col) itemWidth += colGap;
        }

        layoutNode(item.node, itemWidth, metrics);

        // Set content width to fill the grid area
        auto& cs = item.node->computedStyle();
        const std::string& w = styleVal(cs, "width");
        if (w == "auto" || w.empty()) {
            float cw = itemWidth -
                item.node->box.padding.left - item.node->box.padding.right -
                item.node->box.border.left - item.node->box.border.right -
                item.node->box.margin.left - item.node->box.margin.right;
            if (cw > 0) item.node->box.contentRect.width = cw;
        }

        // Update row content sizes after relayout
        float itemH = item.node->box.fullHeight() + item.node->box.margin.top + item.node->box.margin.bottom;
        if (item.rowSpan == 1 && item.row < static_cast<int>(numRows)) {
            rowContentSizes[item.row] = std::max(rowContentSizes[item.row], itemH);
        }
    }

    // Re-resolve row sizes with updated content
    rowSizes = resolveTrackSizes(rowTracks, 0, rowGap, rowContentSizes);

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

        // Stretch item to fill grid area (default behavior)
        auto& cs = item.node->computedStyle();
        const std::string& h = styleVal(cs, "height");
        if (h == "auto" || h.empty()) {
            float ch = areaH -
                item.node->box.margin.top - item.node->box.margin.bottom -
                item.node->box.padding.top - item.node->box.padding.bottom -
                item.node->box.border.top - item.node->box.border.bottom;
            if (ch > 0) item.node->box.contentRect.height = ch;
        }

        item.node->box.contentRect.x = areaX +
            item.node->box.margin.left + item.node->box.padding.left + item.node->box.border.left;
        item.node->box.contentRect.y = areaY +
            item.node->box.margin.top + item.node->box.padding.top + item.node->box.border.top;

        // Apply position: relative offset
        const std::string& childPos = styleVal(cs, "position");
        if (childPos == "relative" || childPos == "sticky") {
            float childFontSize = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
            if (childFontSize <= 0) childFontSize = fontSize;
            const std::string& topVal = styleVal(cs, "top");
            const std::string& leftVal = styleVal(cs, "left");
            const std::string& bottomVal = styleVal(cs, "bottom");
            const std::string& rightVal = styleVal(cs, "right");

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

    float specH = resolveDim(styleVal(style, "height"), 0, fontSize);
    if (specH >= 0) {
        if (styleVal(style, "box-sizing") == "border-box")
            node->box.contentRect.height = specH - paddingV - borderV;
        else
            node->box.contentRect.height = specH;
        if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
    } else {
        node->box.contentRect.height = numRows > 0 ? rowPositions[numRows] : 0;
        // Remove trailing gap
        if (numRows > 0) {
            node->box.contentRect.height = rowPositions[numRows] - (numRows > 0 ? rowGap : 0);
            if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
        }
    }

}

} // namespace htmlayout::layout
