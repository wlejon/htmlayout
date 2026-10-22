#include "layout/grid_tracks.h"
#include "layout/grid.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace htmlayout::layout {

using layout::styleVal;

float resolveDim(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

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
    // fit-content(L): minmax(auto, max-content) with the max clamped to L.
    if (token.size() > 12 && token.substr(0, 12) == "fit-content(") {
        std::string inner = token.substr(12);
        if (!inner.empty() && inner.back() == ')') inner.pop_back();
        while (!inner.empty() && inner.front() == ' ') inner.erase(0, 1);
        while (!inner.empty() && inner.back() == ' ') inner.pop_back();
        ts.kind = TrackSize::Auto;
        ts.minIsAuto = true;
        ts.fitLimit = std::max(0.0f, resolveLength(inner, available, fontSize));
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
            } else if (maxStr == "max-content") {
                ts.kind = TrackSize::MaxContent;
            } else if (maxStr == "min-content") {
                ts.kind = TrackSize::MinContent;
            } else {
                ts.kind = TrackSize::Fixed;
                ts.value = resolveLength(maxStr, available, fontSize);
                ts.maxValue = ts.value;
            }

            if (minStr == "auto" || minStr == "min-content") {
                ts.minIsAuto = true;
            } else if (minStr == "max-content") {
                ts.minIsMaxContent = true;
            } else {
                ts.minValue = resolveLength(minStr, available, fontSize);
                if (ts.maxValue >= 0 && ts.maxValue < ts.minValue) {
                    ts.maxValue = ts.minValue;
                }
            }
        }
        return ts;
    }
    // Fixed length
    ts.kind = TrackSize::Fixed;
    ts.value = resolveLength(token, available, fontSize);
    return ts;
}

namespace {

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
    }
    return total;
}

} // anonymous namespace

ParsedTrackList parseTrackListWithNames(const std::string& value, float available, float fontSize,
                                        float gap) {
    ParsedTrackList result;
    if (value.empty() || value == "none") return result;

    auto tokens = tokenizeTrackList(value);
    int lineIndex = 1;

    for (size_t ti = 0; ti < tokens.size(); ti++) {
        auto& token = tokens[ti];

        // Handle named lines: [name1 name2]
        if (!token.empty() && token.front() == '[' && token.back() == ']') {
            std::string names = token.substr(1, token.size() - 2);
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
            int pd = 0;
            size_t comma = std::string::npos;
            for (size_t j = 0; j < inner.size(); j++) {
                if (inner[j] == '(') pd++;
                else if (inner[j] == ')') pd--;
                else if (inner[j] == ',' && pd == 0) { comma = j; break; }
            }
            if (comma != std::string::npos) {
                std::string countStr = inner.substr(0, comma);
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
                    float patternSize = computeRepeatPatternSize(subTokens, available, fontSize);
                    int tracksPerRep = 0;
                    for (auto& st : subTokens)
                        if (st.empty() || st.front() != '[') tracksPerRep++;
                    float denom = patternSize + tracksPerRep * gap;
                    if (patternSize > 0 && denom > 0) {
                        count = static_cast<int>(std::floor((available + gap) / denom));
                        if (count < 1) count = 1;
                    } else {
                        count = 1;
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

std::vector<TrackSize> parseTrackList(const std::string& value, float available, float fontSize) {
    return parseTrackListWithNames(value, available, fontSize).tracks;
}

std::vector<float> resolveTrackSizes(const std::vector<TrackSize>& tracks,
                                      float available, float gap,
                                      const std::vector<float>& contentMin,
                                      const std::vector<float>& contentMax,
                                      const std::vector<float>& minContributions) {
    size_t n = tracks.size();
    std::vector<float> sizes(n, 0);
    std::vector<float> growth(n, 0);

    float totalGaps = (n > 1) ? gap * (n - 1) : 0;
    float usedSpace = totalGaps;
    float totalFr = 0;
    float frFloors = 0;

    // Base sizes and growth limits.
    for (size_t i = 0; i < n; i++) {
        auto& t = tracks[i];
        const float cmin = (i < contentMin.size()) ? contentMin[i] : 0.0f;
        const float cmax = std::max(cmin, (i < contentMax.size()) ? contentMax[i] : 0.0f);
        const float minFloor = t.minIsMaxContent ? cmax : (t.minIsAuto ? cmin : t.minValue);
        switch (t.kind) {
            case TrackSize::Fixed:
                sizes[i] = t.value;
                if (t.isMinmax && sizes[i] < minFloor) sizes[i] = minFloor;
                growth[i] = sizes[i];
                break;
            case TrackSize::Fractional:
                totalFr += t.value;
                if (t.isMinmax && t.minIsMaxContent) frFloors += std::max(0.0f, cmax);
                else if (t.isMinmax && !t.minIsAuto) frFloors += std::max(0.0f, t.minValue);
                else if (i < minContributions.size()) frFloors += std::max(0.0f, minContributions[i]);
                break;
            case TrackSize::Auto:
                sizes[i] = t.isMinmax ? minFloor : cmin;
                growth[i] = t.fitLimit >= 0 ? std::min(cmax, t.fitLimit) : cmax;
                break;
            case TrackSize::MinContent:
                sizes[i] = t.isMinmax ? minFloor : cmin;
                growth[i] = cmin;
                break;
            case TrackSize::MaxContent:
                sizes[i] = t.isMinmax ? minFloor : cmax;
                growth[i] = cmax;
                break;
        }
        if (t.kind != TrackSize::Fractional) {
            if (growth[i] < sizes[i]) growth[i] = sizes[i];
            usedSpace += sizes[i];
        }
    }

    // Maximize tracks (§12.6): the free space grows the intrinsic tracks'
    // base sizes toward their growth limits, equally, freezing each at its
    // limit. With no definite size to fill (an indefinite row axis), every
    // track is its growth limit, as under a max-content constraint.
    {
        std::vector<bool> open(n, false);
        size_t openCount = 0;
        for (size_t i = 0; i < n; i++) {
            if (tracks[i].kind == TrackSize::Fractional || growth[i] <= sizes[i]) continue;
            open[i] = true;
            openCount++;
        }
        if (openCount > 0 && available <= 0) {
            for (size_t i = 0; i < n; i++) {
                if (!open[i]) continue;
                usedSpace += growth[i] - sizes[i];
                sizes[i] = growth[i];
            }
        } else {
            float free = available - usedSpace - frFloors;
            while (openCount > 0 && free > 0.001f) {
                const float share = free / static_cast<float>(openCount);
                float spent = 0;
                for (size_t i = 0; i < n; i++) {
                    if (!open[i]) continue;
                    const float add = std::min(share, growth[i] - sizes[i]);
                    sizes[i] += add;
                    spent += add;
                    if (sizes[i] >= growth[i] - 0.001f) {
                        open[i] = false;
                        openCount--;
                    }
                }
                usedSpace += spent;
                free -= spent;
                if (spent <= 0) break;
            }
        }
    }

    // Second pass: distribute remaining space to fr tracks
    float freeSpace = available - usedSpace;
    if (freeSpace < 0) freeSpace = 0;

    if (totalFr > 0) {
        std::vector<float> floors(n, 0.0f);
        for (size_t i = 0; i < n; i++) {
            if (tracks[i].kind != TrackSize::Fractional) continue;
            const float cmin = (i < contentMin.size()) ? contentMin[i] : 0.0f;
            const float cmax = std::max(cmin, (i < contentMax.size()) ? contentMax[i] : 0.0f);
            if (tracks[i].isMinmax && tracks[i].minIsMaxContent)
                floors[i] = std::max(0.0f, cmax);
            else if (tracks[i].isMinmax && !tracks[i].minIsAuto)
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

namespace {

float itemContentContribution(LayoutNode* item, float parentFontSize, TextMetrics& metrics, bool max) {
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
    } else if (max) {
        contribution = computeMaxContentWidth(item, metrics) + ph + bh + mh;
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

constexpr int GRID_LINE_NAMED = -1000;

struct GridLineRef {
    int value = 0;       // positive = line number, negative = span, 0 = auto
    std::string name;    // non-empty if this is a named line reference
};

GridLineRef parseGridLineRef(const std::string& val, int explicitTracks) {
    if (val.empty() || val == "auto") return {0, ""};
    if (val.size() >= 4 && val.substr(0, 4) == "span") {
        std::string rest = val.substr(4);
        size_t s = rest.find_first_not_of(" \t");
        if (s == std::string::npos || rest.empty()) return {-1, ""};
        rest = rest.substr(s);
        int n = 1;
        try { n = std::stoi(rest); } catch (...) {
            n = 1;
        }
        if (n < 1) n = 1;
        return {-n, ""};
    }
    try {
        int n = std::stoi(val);
        if (n < 0) {
            n = explicitTracks + 2 + n;
            if (n < 1) n = 1;
        }
        return {n, ""};
    } catch (...) {}
    return {GRID_LINE_NAMED, val};
}

int parseGridLine(const std::string& val, int explicitTracks) {
    return parseGridLineRef(val, explicitTracks).value;
}

int resolveNamedLine(const GridLineRef& ref, const NamedLines& lineNames, int occurrence = 1) {
    if (ref.name.empty() || ref.value != GRID_LINE_NAMED) return ref.value;
    auto it = lineNames.find(ref.name);
    if (it == lineNames.end() || it->second.empty()) return 0;
    int idx = std::min(occurrence - 1, static_cast<int>(it->second.size()) - 1);
    if (idx < 0) idx = 0;
    return it->second[idx];
}

} // anonymous namespace

float itemMinContentContribution(LayoutNode* item, float parentFontSize, TextMetrics& metrics) {
    return itemContentContribution(item, parentFontSize, metrics, false);
}

float itemMaxContentContribution(LayoutNode* item, float parentFontSize, TextMetrics& metrics) {
    return itemContentContribution(item, parentFontSize, metrics, true);
}

std::unordered_map<std::string, GridArea> parseGridTemplateAreas(const std::string& value) {
    std::unordered_map<std::string, GridArea> areas;
    if (value.empty() || value == "none") return areas;

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
        std::vector<std::string> rowTokens;
        std::istringstream iss(rowStr);
        std::string tok;
        while (iss >> tok) rowTokens.push_back(tok);
        grid.push_back(std::move(rowTokens));
        pos = end + 1;
    }

    for (size_t r = 0; r < grid.size(); r++) {
        for (size_t c = 0; c < grid[r].size(); c++) {
            const std::string& name = grid[r][c];
            if (name == ".") continue;
            auto it = areas.find(name);
            if (it == areas.end()) {
                areas[name] = {static_cast<int>(r + 1), static_cast<int>(c + 1),
                               static_cast<int>(r + 2), static_cast<int>(c + 2)};
            } else {
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

GridPlacement parseGridPlacement(const LayoutNode* node,
                                 const std::unordered_map<std::string, GridArea>& namedAreas,
                                 const NamedLines& colLines,
                                 const NamedLines& rowLines,
                                 int explicitCols, int explicitRows) {
    GridPlacement gp;

    const std::string& area = styleVal(node, Prop::GridArea);
    if (!area.empty() && area != "auto") {
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

        if (parts.size() >= 1) gp.rowStart = parseGridLine(parts[0], explicitRows);
        if (parts.size() >= 2) gp.colStart = parseGridLine(parts[1], explicitCols);
        if (parts.size() >= 3) gp.rowEnd = parseGridLine(parts[2], explicitRows);
        if (parts.size() >= 4) gp.colEnd = parseGridLine(parts[3], explicitCols);
        return gp;
    }

    auto resolveRow = [&](const std::string& val) {
        auto ref = parseGridLineRef(val, explicitRows);
        return resolveNamedLine(ref, rowLines);
    };
    auto resolveCol = [&](const std::string& val) {
        auto ref = parseGridLineRef(val, explicitCols);
        return resolveNamedLine(ref, colLines);
    };
    gp.rowStart = resolveRow(styleVal(node, Prop::GridRowStart));
    gp.colStart = resolveCol(styleVal(node, Prop::GridColumnStart));
    gp.rowEnd = resolveRow(styleVal(node, Prop::GridRowEnd));
    gp.colEnd = resolveCol(styleVal(node, Prop::GridColumnEnd));

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

float gridMaxContentWidth(LayoutNode* node, TextMetrics& metrics) {
    if (!node) return 0.0f;
    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0) fontSize = 16.0f;

    float itemMax = 0.0f;
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) continue;
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

    auto tracks = parseTrackList(styleVal(node, Prop::GridTemplateColumns), 0.0f, fontSize);
    if (tracks.empty()) return itemMax;

    float sum = 0.0f;
    for (const auto& t : tracks) {
        if (t.isMinmax) {
            float maxContribution = (t.maxValue >= 0) ? t.maxValue : itemMax;
            float minContribution = t.minIsMaxContent ? itemMax : (t.minIsAuto ? 0.0f : t.minValue);
            sum += std::max(maxContribution, minContribution);
        } else if (t.kind == TrackSize::Fixed) {
            sum += t.value;
        } else {
            sum += itemMax;
        }
    }
    float gap = resolveLength(styleVal(node, Prop::ColumnGap), 0, fontSize);
    if (gap > 0 && tracks.size() > 1) sum += gap * (tracks.size() - 1);
    return sum;
}

} // namespace htmlayout::layout
