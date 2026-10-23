#include "layout/grid.h"
#include "layout/grid_tracks.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>

namespace htmlayout::layout {

using layout::styleVal;

void layoutGrid(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

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
        // An auto-width inline-grid is an atomic inline and shrinks to fit.
        if (styleVal(node, Prop::Display) == "inline-grid" &&
            node->overrideContentWidth < 0.0f) {
            float minC = computeMinContentWidth(node, metrics);
            float maxC = computeMaxContentWidth(node, metrics);
            containerWidth = std::min(maxC, std::max(minC, containerWidth));
        }
    }
    // min/max-width, which name the border box under box-sizing: border-box.
    const bool borderBoxSizing = styleVal(node, Prop::BoxSizing) == "border-box";
    {
        auto toContent = [&](float v) {
            return (borderBoxSizing && v >= 0) ? std::max(0.0f, v - paddingH - borderH) : v;
        };
        const float minW = toContent(resolveDim(styleVal(node, Prop::MinWidth), availableWidth, fontSize));
        const float maxW = toContent(resolveDim(styleVal(node, Prop::MaxWidth), availableWidth, fontSize));
        if (maxW >= 0 && containerWidth > maxW) containerWidth = maxW;
        if (minW >= 0 && containerWidth < minW) containerWidth = minW;
    }
    // A parent flex algorithm resolved this box's used width already; a
    // percentage `width` must not be taken again against that result.
    if (node->overrideContentWidth >= 0.0f)
        containerWidth = node->overrideContentWidth;

    // Parse gap
    float rowGap = resolveLength(styleVal(node, Prop::RowGap), containerWidth, fontSize);
    float colGap = resolveLength(styleVal(node, Prop::ColumnGap), containerWidth, fontSize);

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
        if (styleVal(child, Prop::Display) == "none") {
            clearHiddenBox(child);
            continue;
        }
        const std::string& childPos = styleVal(child, Prop::Position);
        if (childPos == "absolute" || childPos == "fixed") continue;
        GridItem item;
        item.node = child;
        item.placement = parseGridPlacement(child, namedAreas, colLineNames, rowLineNames,
                                            static_cast<int>(colTracks.size()),
                                            static_cast<int>(rowTracks.size()));
        item.row = -1; item.col = -1;
        item.rowSpan = 1; item.colSpan = 1;
        items.push_back(item);
    }

    // Determine grid dimensions
    size_t numCols = colTracks.size();
    size_t numRows = rowTracks.size();

    // No explicit column tracks: the explicit grid is zero columns wide and the
    // implicit grid supplies the rest (CSS Grid section 8). Under the default
    // `grid-auto-flow: row` that means ONE column - items stack downwards into
    // implicit rows.
    if (numCols == 0) {
        const bool columnFlowEarly = (styleVal(node, Prop::GridAutoFlow) == "column");
        size_t maxCol = 1;
        for (auto& item : items) {
            const auto& gp = item.placement;
            if (gp.colStart > 0)
                maxCol = std::max(maxCol, static_cast<size_t>(gp.colStart));
            if (gp.colEnd > 1)
                maxCol = std::max(maxCol, static_cast<size_t>(gp.colEnd - 1));
            if (gp.colStart > 0 && gp.colEnd < 0)
                maxCol = std::max(maxCol, static_cast<size_t>(gp.colStart - gp.colEnd - 1));
            else if (gp.colStart == 0 && gp.colEnd < 0)
                maxCol = std::max(maxCol, static_cast<size_t>(-gp.colEnd));
            if (gp.colStart < 0)
                maxCol = std::max(maxCol, static_cast<size_t>(-gp.colStart));
        }
        numCols = columnFlowEarly
            ? std::max(maxCol, items.empty() ? size_t(1) : items.size())
            : maxCol;
    }

    // Resolve item placements
    std::vector<std::vector<bool>> occupied; // [row][col]
    auto ensureRows = [&](size_t r) {
        while (occupied.size() <= r) {
            occupied.push_back(std::vector<bool>(numCols, false));
        }
    };

    for (auto& item : items) {
        auto& gp = item.placement;

        if (gp.colStart > 0) item.col = gp.colStart - 1;
        if (gp.rowStart > 0) item.row = gp.rowStart - 1;

        if (gp.colEnd < 0) {
            item.colSpan = -gp.colEnd;
        } else if (gp.colEnd > 0 && item.col >= 0) {
            item.colSpan = gp.colEnd - gp.colStart;
            if (item.colSpan < 1) item.colSpan = 1;
        } else if (gp.colStart < 0) {
            item.colSpan = -gp.colStart;
            item.col = -1;
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
    // BEFORE auto-placing the rest.
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
            continue;
        }
        if (item.col < 0 && item.row < 0) {
            ensureRows(autoRow);
            if (columnFlow) {
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
            ensureRows(item.row);
            size_t c = 0;
            while (c + item.colSpan > numCols || (c < numCols && occupied[item.row][c])) c++;
            item.col = static_cast<int>(c);
        } else if (item.row < 0) {
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

    // Ensure we have enough track definitions - use grid-auto-columns/rows for implicit tracks
    while (colTracks.size() < numCols) {
        colTracks.push_back(autoColTrack);
    }
    while (rowTracks.size() < numRows) {
        rowTracks.push_back(autoRowTrack);
    }

    // auto-fit: collapse empty repeated tracks (Grid §7.2.3.2).
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
        item.node->viewportHeight = node->viewportHeight;
        float itemAvail = containerWidth / numCols;
        if (item.node->box.dirty || !(item.node->gridMeasuredAtW == itemAvail)) {
            item.node->availableHeight = 0.0f;
            layoutNode(item.node, itemAvail, metrics);
            item.node->gridMeasuredAtW = itemAvail;
            item.node->gridMeasuredOuterW = item.node->box.fullWidth() +
                item.node->box.margin.left + item.node->box.margin.right;
            item.node->gridMeasuredOuterH = item.node->box.fullHeight() +
                item.node->box.margin.top + item.node->box.margin.bottom;
        }
        float itemH = item.node->gridMeasuredOuterH;

        if (item.rowSpan == 1 && item.row < static_cast<int>(numRows)) {
            rowContentSizes[item.row] = std::max(rowContentSizes[item.row], itemH);
        }
    }

    // Min-content contributions of items in flexible columns: a 1fr track
    // cannot shrink below its items' min-content contributions (Grid §11.8).
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

    // The items' min-content and max-content contributions to the intrinsic
    // columns (or minmax columns with intrinsic min) they sit in (span 1).
    std::vector<float> colContentMax(numCols, 0);
    for (auto& item : items) {
        if (item.colSpan != 1 || item.col < 0 || item.col >= static_cast<int>(numCols)) continue;
        const auto& t = colTracks[item.col];
        bool needsIntrinsic = (t.kind != TrackSize::Fixed && t.kind != TrackSize::Fractional) ||
                              (t.isMinmax && (t.minIsAuto || t.minIsMaxContent));
        if (!needsIntrinsic) continue;
        colContentSizes[item.col] = std::max(colContentSizes[item.col],
            itemMinContentContribution(item.node, fontSize, metrics));
        if (t.kind != TrackSize::MinContent || t.minIsMaxContent)
            colContentMax[item.col] = std::max(colContentMax[item.col],
                itemMaxContentContribution(item.node, fontSize, metrics));
    }

    // Resolve track sizes
    auto colSizes = resolveTrackSizes(colTracks, containerWidth, colGap, colContentSizes, colContentMax,
                                      colMinSizes);

    // CSS Grid stretch step (§12.8)
    auto stretchAuto = [](std::vector<float>& sizes,
                          const std::vector<TrackSize>& tracks,
                          float available, float gap) {
        size_t n = tracks.size();
        if (n == 0 || available <= 0) return;
        float used = (n > 1 ? gap * (n - 1) : 0);
        size_t autoCount = 0;
        bool hasFr = false;
        auto stretches = [](const TrackSize& t) { return t.kind == TrackSize::Auto && t.fitLimit < 0; };
        for (size_t i = 0; i < n; i++) {
            used += sizes[i];
            if (tracks[i].kind == TrackSize::Fractional) hasFr = true;
            else if (stretches(tracks[i])) autoCount++;
        }
        if (hasFr) return;
        if (autoCount == 0) return;
        float free = available - used;
        if (free <= 0) return;
        float share = free / static_cast<float>(autoCount);
        for (size_t i = 0; i < n; i++) {
            if (stretches(tracks[i])) sizes[i] += share;
        }
    };
    {
        const std::string& jc = styleVal(node, Prop::JustifyContent);
        if (jc.empty() || jc == "normal" || jc == "stretch")
            stretchAuto(colSizes, colTracks, containerWidth, colGap);
    }

    // Resolve container height: a percentage height against an indefinite
    // containing block (node->availableHeight <= 0) computes to auto (-1.0f),
    // not 0px (CSS Sizing §5.2).
    auto resolveGridHeight = [&](const std::string& v) -> float {
        if (node->availableHeight <= 0.0f && !v.empty() && v.back() == '%') return -1.0f;
        return resolveDim(v, node->availableHeight, fontSize);
    };

    float rowAvailable = 0.0f;
    {
        float specH = resolveGridHeight(styleVal(node, Prop::Height));
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
    auto rowSizes = resolveTrackSizes(rowTracks, rowAvailable, rowGap, rowContentSizes, rowContentSizes,
                                      rowContentSizes);

    const std::string& containerJustifyItems = styleVal(node, Prop::JustifyItems);
    const std::string& containerAlignItems = styleVal(node, Prop::AlignItems);
    auto resolveAlign = [](const std::string& self, const std::string& items) {
        if (!self.empty() && self != "auto" && self != "normal") return self;
        if (!items.empty() && items != "normal") return items;
        return std::string("stretch");
    };

    // Re-layout items with resolved column widths
    for (auto& item : items) {
        float itemWidth = 0;
        for (int c = item.col; c < item.col + item.colSpan && c < static_cast<int>(numCols); c++) {
            itemWidth += colSizes[c];
            if (c > item.col) itemWidth += colGap;
        }

        const std::string& w = styleVal(item.node, Prop::Width);
        const std::string& h = styleVal(item.node, Prop::Height);
        std::string justifySelf = resolveAlign(styleVal(item.node, Prop::JustifySelf), containerJustifyItems);
        std::string alignSelf = resolveAlign(styleVal(item.node, Prop::AlignSelf), containerAlignItems);
        bool stretchesH = (h == "auto" || h.empty()) && alignSelf == "stretch";
        if (!stretchesH || item.node->box.dirty || !(item.node->cachedAvailWidth == itemWidth)) {
            item.node->availableHeight = 0.0f;
        }

        const bool alreadyVisited = (item.node->lastLayoutPass == currentLayoutPass());
        bool laidNow = beginLayoutNode(item.node, itemWidth);
        if (laidNow) {
            if (!alreadyVisited) {
                layoutNode(item.node, itemWidth, metrics);
            } else if (!(item.node->cachedAvailWidth == itemWidth) ||
                       !(item.node->cachedAvailHeight == item.node->availableHeight)) {
                item.node->box.contentRect.width = 0.0f;
                item.node->box.contentRect.height = 0.0f;
                layoutNode(item.node, itemWidth, metrics);
            }
        }

        if (laidNow || std::isnan(item.node->gridNaturalContentW)) {
            item.node->gridNaturalContentW = item.node->box.contentRect.width;
        }
        if (w == "auto" || w.empty()) {
            if (justifySelf == "stretch") {
                float cw = itemWidth -
                    item.node->box.padding.left - item.node->box.padding.right -
                    item.node->box.border.left - item.node->box.border.right -
                    item.node->box.margin.left - item.node->box.margin.right;
                if (cw > 0) item.node->box.contentRect.width = cw;
            } else if (!laidNow && !std::isnan(item.node->gridNaturalContentW)) {
                item.node->box.contentRect.width = item.node->gridNaturalContentW;
            }
        }

        if (laidNow || std::isnan(item.node->gridNaturalOuterH)) {
            item.node->gridNaturalOuterH = item.node->box.fullHeight() +
                item.node->box.margin.top + item.node->box.margin.bottom;
            item.node->gridNaturalContentH = item.node->box.contentRect.height;
        } else if (!stretchesH && (h == "auto" || h.empty()) && !std::isnan(item.node->gridNaturalContentH)) {
            item.node->box.contentRect.height = item.node->gridNaturalContentH;
        }
        float itemH = item.node->gridNaturalOuterH;
        if (item.rowSpan == 1 && item.row < static_cast<int>(numRows)) {
            rowContentSizes[item.row] = std::max(rowContentSizes[item.row], itemH);
        }
    }

    rowSizes = resolveTrackSizes(rowTracks, rowAvailable, rowGap, rowContentSizes, rowContentSizes,
                                 rowContentSizes);

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

        float areaX = colPositions[c];
        float areaY = rowPositions[r];
        float areaW = 0;
        for (int cc = item.col; cc < item.col + item.colSpan && cc < static_cast<int>(numCols); cc++) {
            areaW += colSizes[cc];
            if (cc > item.col) areaW += colGap;
        }

        float areaH = 0;
        for (int rr = item.row; rr < item.row + item.rowSpan && rr < static_cast<int>(numRows); rr++) {
            areaH += rowSizes[rr];
            if (rr > item.row) areaH += rowGap;
        }

        const std::string& h = styleVal(item.node, Prop::Height);
        std::string alignSelf = resolveAlign(styleVal(item.node, Prop::AlignSelf), containerAlignItems);
        std::string justifySelfPos = resolveAlign(styleVal(item.node, Prop::JustifySelf), containerJustifyItems);
        if ((h == "auto" || h.empty()) && alignSelf == "stretch") {
            float ch = areaH -
                item.node->box.margin.top - item.node->box.margin.bottom -
                item.node->box.padding.top - item.node->box.padding.bottom -
                item.node->box.border.top - item.node->box.border.bottom;
            if (ch > 0) {
                float naturalH = item.node->gridNaturalContentH;
                if (std::isnan(naturalH)) naturalH = item.node->box.contentRect.height;
                bool grew = (ch > naturalH + 0.01f);
                const bool reused = item.node->lastLayoutPass != currentLayoutPass();
                item.node->box.contentRect.height = ch;
                if (grew || reused) {
                    float itemWidth = item.node->box.contentRect.width +
                        item.node->box.padding.left + item.node->box.padding.right +
                        item.node->box.border.left + item.node->box.border.right;
                    item.node->availableHeight = ch +
                        item.node->box.padding.top + item.node->box.padding.bottom +
                        item.node->box.border.top + item.node->box.border.bottom;
                    if (beginLayoutNode(item.node, itemWidth)) {
                        item.node->box.contentRect.height = ch;
                        layoutNode(item.node, itemWidth, metrics);
                    }
                    item.node->box.contentRect.height = ch;
                    if (item.node->box.contentRect.width < 0) item.node->box.contentRect.width = 0;
                }
            }
        }

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

    float naturalH = 0;
    if (numRows > 0) {
        naturalH = rowPositions[numRows];
        if (naturalH < 0) naturalH = 0;
    }

    float specH = resolveGridHeight(styleVal(node, Prop::Height));
    if (specH >= 0) {
        if (styleVal(node, Prop::BoxSizing) == "border-box")
            node->box.contentRect.height = specH - paddingV - borderV;
        else
            node->box.contentRect.height = specH;
        if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
    } else {
        node->box.contentRect.height = naturalH;
    }
    // min/max-height (border box under border-box sizing). A percentage
    // against an indefinite containing block is 'none'/'auto', not 0.
    {
        auto clampVal = [&](const std::string& v) {
            if (node->availableHeight <= 0.0f && !v.empty() && v.back() == '%') return -1.0f;
            float r = resolveDim(v, node->availableHeight, fontSize);
            return (borderBoxSizing && r >= 0) ? std::max(0.0f, r - paddingV - borderV) : r;
        };
        const float minH = clampVal(styleVal(node, Prop::MinHeight));
        const float maxH = clampVal(styleVal(node, Prop::MaxHeight));
        if (maxH >= 0 && node->box.contentRect.height > maxH) node->box.contentRect.height = maxH;
        if (minH >= 0 && node->box.contentRect.height < minH) node->box.contentRect.height = minH;
    }

    node->box.naturalHeight = std::max(naturalH, node->box.contentRect.height);
}

} // namespace htmlayout::layout
