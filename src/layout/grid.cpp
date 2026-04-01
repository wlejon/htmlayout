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

// Tokenize a track list, handling repeat(), minmax(), and simple tokens.
// Splits on whitespace while respecting parentheses.
std::vector<std::string> tokenizeTrackList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    int parenDepth = 0;

    for (size_t i = 0; i < value.size(); i++) {
        char c = value[i];
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

// Parse a grid-template-columns/rows value into track sizes.
// Supports: fixed lengths, fr, auto, repeat(N, track), minmax()
std::vector<TrackSize> parseTrackList(const std::string& value, float available, float fontSize) {
    std::vector<TrackSize> tracks;
    if (value.empty() || value == "none") return tracks;

    auto tokens = tokenizeTrackList(value);

    for (auto& token : tokens) {
        // Handle repeat(count, track-size)
        if (token.size() > 7 && token.substr(0, 7) == "repeat(") {
            std::string inner = token.substr(7);
            if (!inner.empty() && inner.back() == ')') inner.pop_back();
            auto comma = inner.find(',');
            if (comma != std::string::npos) {
                int count = 1;
                try { count = std::stoi(inner.substr(0, comma)); } catch (...) {}
                std::string trackStr = inner.substr(comma + 1);
                while (!trackStr.empty() && trackStr.front() == ' ') trackStr.erase(0, 1);
                while (!trackStr.empty() && trackStr.back() == ' ') trackStr.pop_back();

                // Parse the repeated track sizes
                auto subTokens = tokenizeTrackList(trackStr);
                for (int r = 0; r < count; r++) {
                    for (auto& st : subTokens) {
                        tracks.push_back(parseTrackSize(st, available, fontSize));
                    }
                }
            }
            continue;
        }

        tracks.push_back(parseTrackSize(token, available, fontSize));
    }

    return tracks;
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

// Parse grid-area value: "row-start / column-start / row-end / column-end"
// or named area. Returns 1-based line numbers.
struct GridPlacement {
    int rowStart = 0;  // 0 = auto
    int colStart = 0;
    int rowEnd = 0;
    int colEnd = 0;
};

int parseGridLine(const std::string& val) {
    if (val.empty() || val == "auto") return 0;
    if (val == "span") return 0; // simplified: span without number = span 1
    try { return std::stoi(val); } catch (...) { return 0; }
}

GridPlacement parseGridPlacement(const css::ComputedStyle& style) {
    GridPlacement gp;

    // Check grid-area first (shorthand)
    const std::string& area = styleVal(style, "grid-area");
    if (!area.empty() && area != "auto") {
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

    // Individual properties
    gp.rowStart = parseGridLine(styleVal(style, "grid-row-start"));
    gp.colStart = parseGridLine(styleVal(style, "grid-column-start"));
    gp.rowEnd = parseGridLine(styleVal(style, "grid-row-end"));
    gp.colEnd = parseGridLine(styleVal(style, "grid-column-end"));

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
            gp.rowStart = parseGridLine(s);
            gp.rowEnd = parseGridLine(e);
        } else {
            gp.rowStart = parseGridLine(gridRow);
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
            gp.colStart = parseGridLine(s);
            gp.colEnd = parseGridLine(e);
        } else {
            gp.colStart = parseGridLine(gridCol);
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

    // Parse grid template
    auto colTracks = parseTrackList(styleVal(style, "grid-template-columns"), containerWidth, fontSize);
    auto rowTracks = parseTrackList(styleVal(style, "grid-template-rows"), containerWidth, fontSize);

    // Collect grid items (skip text nodes, display:none, and absolute/fixed positioned)
    struct GridItem {
        LayoutNode* node;
        GridPlacement placement;
        int row, col;     // 0-based resolved position
        int rowSpan, colSpan;
    };
    std::vector<GridItem> items;
    std::vector<LayoutNode*> absChildren;

    for (auto* child : node->children()) {
        if (child->isTextNode()) continue;
        auto& cs = child->computedStyle();
        if (styleVal(cs, "display") == "none") {
            child->box = LayoutBox{};
            continue;
        }
        const std::string& childPos = styleVal(cs, "position");
        if (childPos == "absolute" || childPos == "fixed") {
            absChildren.push_back(child);
            continue;
        }
        GridItem item;
        item.node = child;
        item.placement = parseGridPlacement(cs);
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

        if (gp.colEnd > 0 && item.col >= 0) {
            item.colSpan = gp.colEnd - gp.colStart;
            if (item.colSpan < 1) item.colSpan = 1;
        }
        if (gp.rowEnd > 0 && item.row >= 0) {
            item.rowSpan = gp.rowEnd - gp.rowStart;
            if (item.rowSpan < 1) item.rowSpan = 1;
        }
    }

    // Auto-place items that don't have explicit positions
    size_t autoRow = 0, autoCol = 0;
    for (auto& item : items) {
        if (item.col < 0 && item.row < 0) {
            // Find next available cell
            ensureRows(autoRow);
            while (autoRow < occupied.size() && autoCol < numCols && occupied[autoRow][autoCol]) {
                autoCol++;
                if (autoCol >= numCols) { autoCol = 0; autoRow++; ensureRows(autoRow); }
            }
            item.row = static_cast<int>(autoRow);
            item.col = static_cast<int>(autoCol);
            autoCol++;
            if (autoCol >= numCols) { autoCol = 0; autoRow++; }
        } else if (item.col < 0) {
            // Has explicit row, find next column in that row
            ensureRows(item.row);
            size_t c = 0;
            while (c < numCols && occupied[item.row][c]) c++;
            item.col = static_cast<int>(c);
        } else if (item.row < 0) {
            // Has explicit column, find next row for that column
            size_t r = 0;
            ensureRows(r);
            while (r < occupied.size() && occupied[r][item.col]) r++;
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

    // Ensure we have enough track definitions
    while (colTracks.size() < numCols) {
        colTracks.push_back(TrackSize{TrackSize::Auto, 0, 0, -1, false});
    }
    while (rowTracks.size() < numRows) {
        rowTracks.push_back(TrackSize{TrackSize::Auto, 0, 0, -1, false});
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

    // Position absolutely/fixed positioned children against this container
    float cbWidth = node->box.contentRect.width;
    float cbHeight = node->box.contentRect.height;

    for (auto* child : absChildren) {
        auto& childStyle = child->computedStyle();
        float childFontSize = resolveLength(styleVal(childStyle, "font-size"), fontSize, fontSize);
        if (childFontSize <= 0) childFontSize = fontSize;

        float left = resolveDim(styleVal(childStyle, "left"), cbWidth, childFontSize);
        float right = resolveDim(styleVal(childStyle, "right"), cbWidth, childFontSize);
        float specAbsW = resolveDim(styleVal(childStyle, "width"), cbWidth, childFontSize);

        bool shrinkWrap = (specAbsW < 0 && !(left >= 0 && right >= 0));
        if (shrinkWrap) {
            layoutNode(child, 100000.0f, metrics);
            float intrW = std::min(child->box.contentRect.width, cbWidth);
            child->box.contentRect.width = intrW;
            layoutNode(child, intrW + child->box.padding.left + child->box.padding.right +
                       child->box.border.left + child->box.border.right, metrics);
        } else {
            layoutNode(child, cbWidth, metrics);
        }

        float top = resolveDim(styleVal(childStyle, "top"), cbHeight, childFontSize);
        float bottom = resolveDim(styleVal(childStyle, "bottom"), cbHeight, childFontSize);

        if (specAbsW < 0 && left >= 0 && right >= 0) {
            float w = cbWidth - left - right -
                      child->box.margin.left - child->box.margin.right -
                      child->box.padding.left - child->box.padding.right -
                      child->box.border.left - child->box.border.right;
            if (w > 0) child->box.contentRect.width = w;
        }

        float specAbsH = resolveDim(styleVal(childStyle, "height"), cbHeight, childFontSize);
        if (specAbsH < 0 && top >= 0 && bottom >= 0) {
            float h = cbHeight - top - bottom -
                      child->box.margin.top - child->box.margin.bottom -
                      child->box.padding.top - child->box.padding.bottom -
                      child->box.border.top - child->box.border.bottom;
            if (h > 0) child->box.contentRect.height = h;
        }

        float xPos = child->box.margin.left + child->box.padding.left + child->box.border.left;
        float yPos = child->box.margin.top + child->box.padding.top + child->box.border.top;

        if (left >= 0) {
            xPos = left + child->box.margin.left + child->box.padding.left + child->box.border.left;
        } else if (right >= 0) {
            xPos = cbWidth - right - child->box.margin.right -
                   child->box.padding.right - child->box.border.right - child->box.contentRect.width;
        }

        if (top >= 0) {
            yPos = top + child->box.margin.top + child->box.padding.top + child->box.border.top;
        } else if (bottom >= 0) {
            yPos = cbHeight - bottom - child->box.margin.bottom -
                   child->box.padding.bottom - child->box.border.bottom - child->box.contentRect.height;
        }

        child->box.contentRect.x = xPos;
        child->box.contentRect.y = yPos;
    }
}

} // namespace htmlayout::layout
