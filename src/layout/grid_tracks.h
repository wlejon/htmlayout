#pragma once
#include "layout/box.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace htmlayout::layout {

float resolveDim(const std::string& value, float available, float fontSize);

// Parse a track size value: could be a length, fr, auto, min-content, max-content, minmax()
struct TrackSize {
    enum Kind { Fixed, Fractional, Auto, MinContent, MaxContent };
    Kind kind = Auto;
    float value = 0;       // for Fixed: resolved px; for Fractional: fr value
    float minValue = 0;    // for minmax: minimum
    float maxValue = -1;   // for minmax: maximum (-1 = auto/intrinsic/fr)
    bool isMinmax = false;
    // The track's min sizing function is auto/min-content: a flexible track's
    // base size then floors at its items' min-content contributions (Grid
    // §11.8 - the "automatic minimum"). Plain `1fr` means minmax(auto, 1fr).
    bool minIsAuto = false;
    // The track's min sizing function in minmax() is max-content.
    bool minIsMaxContent = false;
    // fit-content(L): an Auto track whose growth limit is its max-content
    // contribution capped at L (no less than its base); not stretched.
    float fitLimit = -1;
};

// Named grid lines: maps line name to 1-based line indices.
// Multiple lines can share a name (e.g., from repeat()).
using NamedLines = std::unordered_map<std::string, std::vector<int>>;

struct ParsedTrackList {
    std::vector<TrackSize> tracks;
    NamedLines lineNames;
    bool hasAutoFit = false;  // if auto-fit was used, empty tracks should collapse
    int autoFitBegin = -1;    // track-index range produced by the auto-fit repeat
    int autoFitEnd = -1;
};

// Named grid area: 1-based line numbers
struct GridArea {
    int rowStart = 0, colStart = 0, rowEnd = 0, colEnd = 0;
};

// Parse grid-area value: "row-start / column-start / row-end / column-end"
// or named area. Returns 1-based line numbers.
struct GridPlacement {
    int rowStart = 0;  // 0 = auto
    int colStart = 0;
    int rowEnd = 0;
    int colEnd = 0;
};

TrackSize parseTrackSize(const std::string& token, float available, float fontSize);

ParsedTrackList parseTrackListWithNames(const std::string& value, float available, float fontSize,
                                        float gap = 0.0f);

std::vector<TrackSize> parseTrackList(const std::string& value, float available, float fontSize);

std::vector<float> resolveTrackSizes(
    const std::vector<TrackSize>& tracks,
    float available, float gap,
    const std::vector<float>& contentMin,
    const std::vector<float>& contentMax,
    const std::vector<float>& minContributions = std::vector<float>());

float itemMinContentContribution(LayoutNode* item, float parentFontSize, TextMetrics& metrics);
float itemMaxContentContribution(LayoutNode* item, float parentFontSize, TextMetrics& metrics);

std::unordered_map<std::string, GridArea> parseGridTemplateAreas(const std::string& value);

GridPlacement parseGridPlacement(const LayoutNode* node,
                                 const std::unordered_map<std::string, GridArea>& namedAreas,
                                 const NamedLines& colLines,
                                 const NamedLines& rowLines,
                                 int explicitCols, int explicitRows);

} // namespace htmlayout::layout
