#include "layout/table.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

namespace {

// Determine if a display value is a table-internal display type.
bool isTableRow(const std::string& display) {
    return display == "table-row";
}

bool isTableCell(const std::string& display) {
    return display == "table-cell";
}

bool isTableRowGroup(const std::string& display) {
    return display == "table-row-group" || display == "table-header-group" ||
           display == "table-footer-group";
}

bool isTableCaption(const std::string& display) {
    return display == "table-caption";
}

float resolveDim(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return -1.0f;
    return resolveLength(value, available, fontSize);
}

} // anonymous namespace

void layoutTable(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
    node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);

    Edges borderWidth{};
    const char* sides[] = {"top", "right", "bottom", "left"};
    float* bw[] = {&borderWidth.top, &borderWidth.right, &borderWidth.bottom, &borderWidth.left};
    for (int i = 0; i < 4; i++) {
        std::string styleProp = std::string("border-") + sides[i] + "-style";
        std::string widthProp = std::string("border-") + sides[i] + "-width";
        if (styleVal(style, styleProp) != "none") {
            *bw[i] = resolveLength(styleVal(style, widthProp), availableWidth, fontSize);
        }
    }
    // In border-collapse mode the collapsed border is rendered ON the cell
    // edges and overlaps them — the table's own outer box reports zero border
    // (Chromium getBoundingClientRect on a collapsed table includes only the
    // outer half of the collapsed border inside its content width, not as a
    // separate border edge). We retain a half-border value as a cell inset so
    // cells still sit at the table's painted edge with room for the merged
    // border line.
    bool collapse = (styleVal(style, "border-collapse") == "collapse");
    Edges effectiveBorder = borderWidth;
    Edges collapseInset{};
    if (collapse) {
        collapseInset.top    = borderWidth.top    * 0.5f;
        collapseInset.right  = borderWidth.right  * 0.5f;
        collapseInset.bottom = borderWidth.bottom * 0.5f;
        collapseInset.left   = borderWidth.left   * 0.5f;
        effectiveBorder = {};
    }
    node->box.border = effectiveBorder;

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float marginH = node->box.margin.left + node->box.margin.right;

    // Resolve table width. Note: per CSS 2.1 §17.5.2, width:auto on tables means
    // shrink-to-fit, NOT fill-the-container. We compute a preferred (max-content)
    // width from intrinsic column sizes after collecting cells; for now, set a
    // tentative full-width sentinel that's clamped later.
    float specW = resolveLength(styleVal(style, "width"), availableWidth, fontSize);
    const std::string& widthVal = styleVal(style, "width");
    float availContent = availableWidth - marginH - paddingH - borderH;
    if (availContent < 0) availContent = 0;
    bool widthAuto = (widthVal == "auto" || widthVal.empty());
    float tableContentWidth;
    if (!widthAuto) {
        if (styleVal(style, "box-sizing") == "border-box") {
            tableContentWidth = specW - paddingH - borderH;
        } else {
            tableContentWidth = specW;
        }
        if (tableContentWidth < 0) tableContentWidth = 0;
    } else {
        // Tentative — will be replaced by shrink-to-fit min(available, max-content)
        // once we've computed intrinsic column widths.
        tableContentWidth = availContent;
    }

    // Border spacing (already resolved 'collapse' above)
    float borderSpacing = resolveLength(styleVal(style, "border-spacing"), availableWidth, fontSize);
    if (collapse) borderSpacing = 0;

    // Collect rows: iterate children, handling direct cells, row groups, and rows.
    // Each row is a vector of LayoutNode* cells.
    struct TableRow {
        LayoutNode* rowNode = nullptr; // the <tr> node, or nullptr for anonymous rows
        LayoutNode* groupNode = nullptr; // the <thead>/<tbody>/<tfoot>, or nullptr if none
        std::vector<LayoutNode*> cells;
    };
    std::vector<TableRow> rows;
    std::vector<LayoutNode*> captions;
    // Row groups, in document order, plus the [firstRow,lastRow] range they cover.
    struct RowGroup {
        LayoutNode* node = nullptr;
        size_t firstRow = 0;
        size_t lastRow = 0; // inclusive
    };
    std::vector<RowGroup> rowGroups;

    // Column metadata from <colgroup>/<col>. Each col contributes a column
    // (or `span` columns) with a possibly-explicit width.
    struct ColInfo {
        LayoutNode* colNode = nullptr;
        LayoutNode* colGroupNode = nullptr;
        float specWidth = -1.0f; // -1 = auto/unset
    };
    std::vector<ColInfo> colInfos;
    std::vector<LayoutNode*> colGroups; // for box assignment

    auto collectRows = [&](LayoutNode* parent) {
        for (auto* child : getLayoutChildren(parent)) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            const std::string& d = styleVal(cs, "display");
            if (d == "none") { child->box = LayoutBox{}; continue; }

            // Absolutely/fixed positioned children are out of flow
            const std::string& childPos = styleVal(cs, "position");
            if (childPos == "absolute" || childPos == "fixed") continue;

            if (isTableRow(d)) {
                TableRow row;
                row.rowNode = child;
                for (auto* cell : child->children()) {
                    if (cell->isTextNode()) continue;
                    auto& cellStyle = cell->computedStyle();
                    const std::string& cd = styleVal(cellStyle, "display");
                    if (cd == "none") { cell->box = LayoutBox{}; continue; }
                    row.cells.push_back(cell);
                }
                rows.push_back(std::move(row));
            } else if (isTableRowGroup(d)) {
                // Recurse into row groups (thead, tbody, tfoot)
                RowGroup rg;
                rg.node = child;
                rg.firstRow = rows.size();
                rg.lastRow = rows.size(); // updated below
                bool added = false;
                for (auto* groupChild : child->children()) {
                    if (groupChild->isTextNode()) continue;
                    auto& gcs = groupChild->computedStyle();
                    const std::string& gd = styleVal(gcs, "display");
                    if (gd == "none") { groupChild->box = LayoutBox{}; continue; }
                    if (isTableRow(gd)) {
                        TableRow row;
                        row.rowNode = groupChild;
                        row.groupNode = child;
                        for (auto* cell : getLayoutChildren(groupChild)) {
                            if (cell->isTextNode()) continue;
                            auto& cellStyle = cell->computedStyle();
                            if (styleVal(cellStyle, "display") == "none") {
                                cell->box = LayoutBox{};
                                continue;
                            }
                            row.cells.push_back(cell);
                        }
                        rg.lastRow = rows.size();
                        rows.push_back(std::move(row));
                        added = true;
                    }
                }
                if (added) rowGroups.push_back(rg);
                else { child->box = LayoutBox{}; } // empty row group
            } else if (d == "table-column-group") {
                colGroups.push_back(child);
                // colgroup width applies to each contained col without an
                // explicit width, OR if no <col> children exist, expands span
                auto& gcs = child->computedStyle();
                std::string gWidthVal = styleVal(gcs, "width");
                std::string gSpan = child->attribute("span");
                bool hasColChildren = false;
                for (auto* gc : child->children()) {
                    if (gc->isTextNode()) continue;
                    auto& gccs = gc->computedStyle();
                    if (styleVal(gccs, "display") == "table-column") {
                        hasColChildren = true;
                        ColInfo ci;
                        ci.colNode = gc;
                        ci.colGroupNode = child;
                        std::string wVal = styleVal(gccs, "width");
                        float w = -1.0f;
                        if (!wVal.empty() && wVal != "auto") {
                            w = resolveLength(wVal, availableWidth, fontSize);
                        } else if (!gWidthVal.empty() && gWidthVal != "auto") {
                            w = resolveLength(gWidthVal, availableWidth, fontSize);
                        }
                        ci.specWidth = w;
                        int span = 1;
                        std::string colSpanAttr = gc->attribute("span");
                        if (!colSpanAttr.empty()) span = std::max(1, std::atoi(colSpanAttr.c_str()));
                        for (int s = 0; s < span; s++) colInfos.push_back(ci);
                    }
                }
                if (!hasColChildren) {
                    // colgroup with no <col> children: span N columns at colgroup width
                    int span = 1;
                    if (!gSpan.empty()) span = std::max(1, std::atoi(gSpan.c_str()));
                    float w = -1.0f;
                    if (!gWidthVal.empty() && gWidthVal != "auto") {
                        w = resolveLength(gWidthVal, availableWidth, fontSize);
                    }
                    ColInfo ci;
                    ci.colGroupNode = child;
                    ci.specWidth = w;
                    for (int s = 0; s < span; s++) colInfos.push_back(ci);
                }
            } else if (d == "table-column") {
                // Stray <col> without a <colgroup>
                ColInfo ci;
                ci.colNode = child;
                std::string wVal = styleVal(cs, "width");
                if (!wVal.empty() && wVal != "auto") {
                    ci.specWidth = resolveLength(wVal, availableWidth, fontSize);
                }
                int span = 1;
                std::string colSpanAttr = child->attribute("span");
                if (!colSpanAttr.empty()) span = std::max(1, std::atoi(colSpanAttr.c_str()));
                for (int s = 0; s < span; s++) colInfos.push_back(ci);
            } else if (isTableCaption(d)) {
                captions.push_back(child);
            } else if (isTableCell(d)) {
                // Direct cell without a row — create anonymous row
                if (rows.empty() || !rows.back().cells.empty()) {
                    rows.push_back(TableRow{});
                }
                rows.back().cells.push_back(child);
            } else {
                // Non-table child — treat as anonymous cell in anonymous row
                rows.push_back(TableRow{});
                rows.back().cells.push_back(child);
            }
        }
    };

    collectRows(node);

    // Build a grid-based cell placement that handles colspan and rowspan.
    // colspan/rowspan are read from computed style (consumer sets them).
    struct CellInfo {
        LayoutNode* node = nullptr;
        size_t gridRow = 0, gridCol = 0;
        size_t colspan = 1, rowspan = 1;
    };
    std::vector<CellInfo> cellInfos;

    // First pass: place cells into a 2D grid, respecting spans.
    // grid[r][c] points to the CellInfo that occupies that slot (nullptr = empty).
    size_t numRows = rows.size();
    size_t numCols = 0;

    // Read colspan/rowspan from either the HTML attribute (preferred) or the
    // computed style (test-only fallback). Returns >= 1.
    auto readSpan = [&](LayoutNode* cell, const char* name) -> int {
        std::string a = cell->attribute(name);
        int v = 0;
        if (!a.empty()) v = std::atoi(a.c_str());
        if (v <= 0) {
            auto& cs = cell->computedStyle();
            v = static_cast<int>(resolveLength(styleVal(cs, name), 0, fontSize));
        }
        return v < 1 ? 1 : v;
    };

    // Pre-scan to estimate column count including colspans
    for (auto& row : rows) {
        size_t cols = 0;
        for (auto* cell : row.cells) {
            cols += static_cast<size_t>(readSpan(cell, "colspan"));
        }
        numCols = std::max(numCols, cols);
    }

    if (numCols == 0) {
        node->box.contentRect.width = tableContentWidth;
        // Respect explicit height even for empty tables
        float specH = resolveLength(styleVal(style, "height"), 0, fontSize);
        const std::string& heightVal = styleVal(style, "height");
        if (heightVal != "auto" && !heightVal.empty()) {
            if (styleVal(style, "box-sizing") == "border-box") {
                float paddingV = node->box.padding.top + node->box.padding.bottom;
                float borderV = node->box.border.top + node->box.border.bottom;
                node->box.contentRect.height = std::max(0.0f, specH - paddingV - borderV);
            } else {
                node->box.contentRect.height = specH;
            }
        } else {
            node->box.contentRect.height = 0;
        }
        return;
    }

    if (numCols > 0) {

    // Build grid — may grow if rowspans push cells into new columns
    std::vector<std::vector<CellInfo*>> grid(numRows, std::vector<CellInfo*>(numCols, nullptr));

    auto ensureGridCols = [&](size_t needed) {
        if (needed > numCols) {
            numCols = needed;
            for (auto& gridRow : grid) gridRow.resize(numCols, nullptr);
        }
    };

    auto ensureGridRows = [&](size_t needed) {
        while (grid.size() < needed) {
            grid.emplace_back(numCols, nullptr);
        }
        if (needed > numRows) numRows = needed;
    };

    for (size_t r = 0; r < rows.size(); r++) {
        size_t gridCol = 0;
        for (auto* cell : rows[r].cells) {
            // Skip slots already occupied by spanning cells from previous rows
            while (gridCol < numCols && grid[r][gridCol] != nullptr) gridCol++;
            if (gridCol >= numCols) ensureGridCols(gridCol + 1);

            int cspan = readSpan(cell, "colspan");
            int rspan = readSpan(cell, "rowspan");

            ensureGridCols(gridCol + cspan);
            ensureGridRows(r + rspan);

            cellInfos.push_back({cell, r, gridCol, static_cast<size_t>(cspan), static_cast<size_t>(rspan)});
            CellInfo* ci = &cellInfos.back();

            for (size_t dr = 0; dr < static_cast<size_t>(rspan); dr++) {
                for (size_t dc = 0; dc < static_cast<size_t>(cspan); dc++) {
                    grid[r + dr][gridCol + dc] = ci;
                }
            }
            gridCol += cspan;
        }
    }

    // Phase 1: intrinsic column min/max widths from cells.
    // For each non-spanning cell, contribute its own min/max content width
    // (plus padding+border) to its column. For spanning cells, distribute
    // any deficit equally across the spanned columns.
    std::vector<float> colMin(numCols, 0.0f);
    std::vector<float> colMax(numCols, 0.0f);
    // Per-column percentage width target (fraction of tableContentWidth) for
    // cells with `width: N%`. A negative value means "no percent set".
    std::vector<float> colPctFrac(numCols, -1.0f);
    float totalSpacing = borderSpacing * (numCols + 1);

    auto cellEdges = [&](LayoutNode* cell, float& padBorderH) {
        auto& cs = cell->computedStyle();
        float cfs = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
        if (cfs <= 0) cfs = fontSize;
        float pl = resolveLength(styleVal(cs, "padding-left"), 0, cfs);
        float pr = resolveLength(styleVal(cs, "padding-right"), 0, cfs);
        float bl = (styleVal(cs, "border-left-style") != "none")
                   ? resolveLength(styleVal(cs, "border-left-width"), 0, cfs) : 0;
        float br = (styleVal(cs, "border-right-style") != "none")
                   ? resolveLength(styleVal(cs, "border-right-width"), 0, cfs) : 0;
        padBorderH = pl + pr + bl + br;
    };

    for (auto& ci : cellInfos) {
        float pbh = 0;
        cellEdges(ci.node, pbh);
        // Honor explicit cell width if specified
        auto& cs = ci.node->computedStyle();
        const std::string& cwVal = styleVal(cs, "width");
        float cellMin = computeMinContentWidth(ci.node, metrics) + pbh;
        float cellMax = computeMaxContentWidth(ci.node, metrics) + pbh;
        bool isPercentWidth = (!cwVal.empty() && cwVal.back() == '%');
        if (!cwVal.empty() && cwVal != "auto" && !isPercentWidth) {
            float specCellW = resolveLength(cwVal, tableContentWidth, fontSize);
            if (styleVal(cs, "box-sizing") != "border-box") specCellW += pbh;
            // Treat explicit (length) width as a strong preferred for max-content,
            // and as a floor for min-content.
            cellMax = std::max(cellMax, specCellW);
            cellMin = std::max(cellMin, specCellW);
        }
        if (ci.colspan == 1) {
            colMin[ci.gridCol] = std::max(colMin[ci.gridCol], cellMin);
            colMax[ci.gridCol] = std::max(colMax[ci.gridCol], cellMax);
            if (isPercentWidth) {
                // Parse "N%" -> fraction
                float pct = 0.0f;
                try { pct = std::stof(cwVal.substr(0, cwVal.size() - 1)); } catch (...) {}
                float frac = std::max(0.0f, pct / 100.0f);
                if (colPctFrac[ci.gridCol] < frac) colPctFrac[ci.gridCol] = frac;
            }
        }
    }

    // Distribute spanning cell intrinsic widths over spanned columns.
    // A spanning cell's min/max must be at least the sum of the spanned
    // columns' min/max plus inter-column spacing. If short, distribute
    // the deficit equally.
    for (auto& ci : cellInfos) {
        if (ci.colspan <= 1) continue;
        float pbh = 0;
        cellEdges(ci.node, pbh);
        float cellMin = computeMinContentWidth(ci.node, metrics) + pbh;
        float cellMax = computeMaxContentWidth(ci.node, metrics) + pbh;
        float spanSpacing = borderSpacing * (ci.colspan - 1);

        float minSum = 0, maxSum = 0;
        for (size_t c = 0; c < ci.colspan; c++) {
            minSum += colMin[ci.gridCol + c];
            maxSum += colMax[ci.gridCol + c];
        }
        float minDeficit = cellMin - spanSpacing - minSum;
        if (minDeficit > 0) {
            float per = minDeficit / ci.colspan;
            for (size_t c = 0; c < ci.colspan; c++) colMin[ci.gridCol + c] += per;
        }
        float maxDeficit = cellMax - spanSpacing - maxSum;
        if (maxDeficit > 0) {
            float per = maxDeficit / ci.colspan;
            for (size_t c = 0; c < ci.colspan; c++) colMax[ci.gridCol + c] += per;
        }
    }

    // Apply explicit <col>/<colgroup> widths: when set, they pin both min and max.
    for (size_t c = 0; c < numCols && c < colInfos.size(); c++) {
        if (colInfos[c].specWidth > 0) {
            float w = colInfos[c].specWidth;
            // Treat col width as the column's preferred width — but never below
            // the column's intrinsic min-content (avoid clipping inline text).
            float floor = colMin[c];
            float pinned = std::max(w, floor);
            colMin[c] = pinned;
            colMax[c] = pinned;
        }
    }

    // Compute table preferred (max-content) and minimum widths.
    float sumMin = 0, sumMax = 0;
    for (size_t c = 0; c < numCols; c++) { sumMin += colMin[c]; sumMax += colMax[c]; }
    float prefTable = sumMax + totalSpacing;
    float minTable  = sumMin + totalSpacing;

    // If width is auto, shrink-to-fit: min(available, max-content), floored at min-content.
    if (widthAuto) {
        tableContentWidth = std::min(availContent, prefTable);
        tableContentWidth = std::max(tableContentWidth, minTable);
    } else {
        // Honor explicit width. We may grow above it only if min-content
        // requires it AND there are no percent columns to absorb the slack.
        // With percent columns under an explicit table width, the percent
        // targets apply inside tableContentWidth — even if a cell's
        // min-content is below its percent share, the share wins.
        bool hasPct = false;
        for (size_t c = 0; c < numCols; c++) if (colPctFrac[c] >= 0) { hasPct = true; break; }
        if (!hasPct) {
            tableContentWidth = std::max(tableContentWidth, minTable);
        } else {
            // Allow tableContentWidth below sumMin only if percent cols absorb
            // enough; floor by the sum of (max(colMin, pctTarget) for pct cols)
            // plus colMin for auto cols, plus spacing.
            float floor = totalSpacing;
            for (size_t c = 0; c < numCols; c++) {
                if (colPctFrac[c] >= 0) {
                    float target = colPctFrac[c] * tableContentWidth;
                    floor += std::max(colMin[c], target);
                } else {
                    floor += colMin[c];
                }
            }
            tableContentWidth = std::max(tableContentWidth, floor);
        }
    }

    // Distribute the table content width across columns.
    // Percent columns claim their N% of tableContentWidth (clamped to colMin).
    // Auto columns share the remaining space, starting at colMin and growing
    // toward colMax weighted by slack.
    std::vector<float> colWidths = colMin;
    float available = tableContentWidth - totalSpacing;

    // First, satisfy percent columns.
    bool anyPct = false;
    for (size_t c = 0; c < numCols; c++) if (colPctFrac[c] >= 0) { anyPct = true; break; }
    float pctConsumed = 0.0f;
    float autoMinSum = 0.0f;
    float autoSlackSum = 0.0f;
    float autoMaxSum = 0.0f;
    if (anyPct) {
        for (size_t c = 0; c < numCols; c++) {
            if (colPctFrac[c] >= 0) {
                float target = colPctFrac[c] * tableContentWidth;
                colWidths[c] = std::max(colMin[c], target);
                pctConsumed += colWidths[c];
            } else {
                autoMinSum += colMin[c];
                autoSlackSum += (colMax[c] - colMin[c]);
                autoMaxSum += colMax[c];
            }
        }
    }
    float used = anyPct ? (pctConsumed + autoMinSum) : sumMin;
    if (available > used) {
        float extra = available - used;
        // Distribute extra to non-percent columns first (by slack, then by max).
        if (anyPct && autoSlackSum > 0) {
            float take = std::min(extra, autoSlackSum);
            for (size_t c = 0; c < numCols; c++) {
                if (colPctFrac[c] >= 0) continue;
                float slack = colMax[c] - colMin[c];
                if (slack > 0) colWidths[c] += take * (slack / autoSlackSum);
            }
            extra -= take;
        } else if (!anyPct) {
            float slackTotal = 0;
            for (size_t c = 0; c < numCols; c++) slackTotal += (colMax[c] - colMin[c]);
            if (slackTotal > 0) {
                float take = std::min(extra, slackTotal);
                for (size_t c = 0; c < numCols; c++) {
                    float slack = colMax[c] - colMin[c];
                    colWidths[c] += take * (slack / slackTotal);
                }
                extra -= take;
            }
        }
        if (extra > 0) {
            // Remaining: spread across auto columns by max-content (or
            // across all columns if no auto columns / no maxes).
            if (anyPct) {
                if (autoMaxSum > 0) {
                    for (size_t c = 0; c < numCols; c++) {
                        if (colPctFrac[c] >= 0) continue;
                        colWidths[c] += extra * (colMax[c] / autoMaxSum);
                    }
                } else {
                    // No non-percent cols (or all zero-max): give to percent
                    // columns proportionally to their pct fractions.
                    float fracSum = 0;
                    for (size_t c = 0; c < numCols; c++) if (colPctFrac[c] > 0) fracSum += colPctFrac[c];
                    if (fracSum > 0) {
                        for (size_t c = 0; c < numCols; c++) {
                            if (colPctFrac[c] > 0) colWidths[c] += extra * (colPctFrac[c] / fracSum);
                        }
                    } else {
                        float per = extra / numCols;
                        for (size_t c = 0; c < numCols; c++) colWidths[c] += per;
                    }
                }
            } else {
                if (sumMax > 0) {
                    for (size_t c = 0; c < numCols; c++) {
                        colWidths[c] += extra * (colMax[c] / sumMax);
                    }
                } else {
                    float per = extra / numCols;
                    for (size_t c = 0; c < numCols; c++) colWidths[c] += per;
                }
            }
        }
    } else if (available < used && used > 0) {
        // Shouldn't normally happen because we floored tableContentWidth above,
        // but guard anyway: scale down proportionally to current widths.
        float scale = available / used;
        for (size_t c = 0; c < numCols; c++) colWidths[c] *= scale;
    }

    // Phase 2: Layout cells with final widths and determine row heights
    std::vector<float> rowHeights(numRows, 0.0f);

    for (auto& ci : cellInfos) {
        float cw = borderSpacing * (ci.colspan - 1);
        for (size_t c = 0; c < ci.colspan; c++) cw += colWidths[ci.gridCol + c];
        layoutNode(ci.node, cw, metrics);

        // Per CSS, percentage padding/margin on a table cell resolves against
        // the table's containing block (its content width), not the cell's
        // own width. layoutNode used 'cw' as the basis, so re-resolve any
        // percentage-bearing edges using tableContentWidth and re-layout the
        // cell's content if any edge changed.
        {
            auto& cs = ci.node->computedStyle();
            float cfs = resolveLength(styleVal(cs, "font-size"), fontSize, fontSize);
            if (cfs <= 0) cfs = fontSize;
            Edges newPad = resolveEdges(cs, "padding", tableContentWidth, cfs);
            Edges newMar = resolveEdges(cs, "margin",  tableContentWidth, cfs);
            bool padChanged =
                newPad.left  != ci.node->box.padding.left  ||
                newPad.right != ci.node->box.padding.right ||
                newPad.top   != ci.node->box.padding.top   ||
                newPad.bottom!= ci.node->box.padding.bottom;
            bool marChanged =
                newMar.left  != ci.node->box.margin.left  ||
                newMar.right != ci.node->box.margin.right ||
                newMar.top   != ci.node->box.margin.top   ||
                newMar.bottom!= ci.node->box.margin.bottom;
            if (padChanged || marChanged) {
                ci.node->box.padding = newPad;
                ci.node->box.margin  = newMar;
            }
        }

        // In border-collapse mode, the cell's border is shared with its
        // neighbor (or the table's outer border on the boundary). The reported
        // cell border-box height is content + padding + half(border) on each
        // side, where the boundary side takes max(cellHalfBorder, tableHalfBorder).
        // Override the cell's border edges accordingly so fullHeight() reports
        // the Chromium-compatible value.
        if (collapse) {
            Edges cellHalf{
                ci.node->box.border.top    * 0.5f,
                ci.node->box.border.right  * 0.5f,
                ci.node->box.border.bottom * 0.5f,
                ci.node->box.border.left   * 0.5f,
            };
            Edges tableHalf{
                borderWidth.top    * 0.5f,
                borderWidth.right  * 0.5f,
                borderWidth.bottom * 0.5f,
                borderWidth.left   * 0.5f,
            };
            Edges outerBorder = cellHalf;
            // Top edge is on table boundary if first row.
            if (ci.gridRow == 0)
                outerBorder.top = std::max(cellHalf.top, tableHalf.top);
            // Bottom edge is on table boundary if last spanned row.
            if (ci.gridRow + ci.rowspan >= numRows)
                outerBorder.bottom = std::max(cellHalf.bottom, tableHalf.bottom);
            // Left edge is on table boundary if first column.
            if (ci.gridCol == 0)
                outerBorder.left = std::max(cellHalf.left, tableHalf.left);
            // Right edge is on table boundary if last spanned column.
            if (ci.gridCol + ci.colspan >= numCols)
                outerBorder.right = std::max(cellHalf.right, tableHalf.right);
            ci.node->box.border = outerBorder;
        }

        // The cell's rendered width is always its allocated column-track span.
        // The CSS 'width' on a cell is an input to column-width allocation,
        // not a final render width — the final cell fills its assigned tracks.
        ci.node->box.contentRect.width = cw -
            ci.node->box.padding.left - ci.node->box.padding.right -
            ci.node->box.border.left - ci.node->box.border.right -
            ci.node->box.margin.left - ci.node->box.margin.right;
        if (ci.node->box.contentRect.width < 0) ci.node->box.contentRect.width = 0;

        float cellFullH = ci.node->box.fullHeight() + ci.node->box.margin.top + ci.node->box.margin.bottom;
        if (ci.rowspan == 1) {
            rowHeights[ci.gridRow] = std::max(rowHeights[ci.gridRow], cellFullH);
        }
    }

    // Distribute spanning row heights
    for (auto& ci : cellInfos) {
        if (ci.rowspan <= 1) continue;
        float cellFullH = ci.node->box.fullHeight() + ci.node->box.margin.top + ci.node->box.margin.bottom;
        float spannedHeight = borderSpacing * (ci.rowspan - 1);
        for (size_t r = 0; r < ci.rowspan; r++) spannedHeight += rowHeights[ci.gridRow + r];
        if (cellFullH > spannedHeight) {
            float extra = (cellFullH - spannedHeight) / ci.rowspan;
            for (size_t r = 0; r < ci.rowspan; r++) rowHeights[ci.gridRow + r] += extra;
        }
    }

    // Phase 3: Position cells
    // Compute row Y positions
    float cursorY = 0;

    // Split captions into top/bottom based on caption-side property
    std::vector<LayoutNode*> topCaptions, bottomCaptions;
    for (auto* cap : captions) {
        auto& cs = cap->computedStyle();
        if (styleVal(cs, "caption-side") == "bottom") {
            bottomCaptions.push_back(cap);
        } else {
            topCaptions.push_back(cap);
        }
    }

    // Layout top captions (sit above the collapsed half-border inset so they
    // align with the table's outer edge, matching Chromium).
    for (auto* cap : topCaptions) {
        layoutNode(cap, tableContentWidth, metrics);
        cap->box.contentRect.x = cap->box.margin.left + cap->box.padding.left + cap->box.border.left;
        cap->box.contentRect.y = cursorY + cap->box.margin.top + cap->box.padding.top + cap->box.border.top;
        cursorY += cap->box.fullHeight() + cap->box.margin.top + cap->box.margin.bottom;
    }

    // Inset rows by the collapsed top half-border so cells leave room for the
    // painted collapse line on the table's outer edge.
    cursorY += collapseInset.top;

    std::vector<float> rowYPositions(numRows); // in table-content coords
    for (size_t r = 0; r < numRows; r++) {
        cursorY += borderSpacing;
        rowYPositions[r] = cursorY;
        cursorY += rowHeights[r];
    }
    cursorY += borderSpacing; // bottom spacing
    cursorY += collapseInset.bottom;

    // Position row groups: span from first row's top to last row's bottom.
    // The row group's contentRect is in table-content coords (its parent box).
    // Chromium hugs the row group horizontally to the cell columns (excludes the
    // outer border-spacing on left/right), so do the same. In collapse mode the
    // collapsed half-border lives inside the table's contentRect and offsets
    // cells/row-groups inward.
    float groupInsetX = borderSpacing + collapseInset.left;
    float groupInsetW = std::max(0.0f,
        tableContentWidth - 2.0f * borderSpacing - collapseInset.left - collapseInset.right);
    for (auto& rg : rowGroups) {
        if (!rg.node) continue;
        rg.node->box.margin = {};
        rg.node->box.padding = {};
        rg.node->box.border = {};
        float top = rowYPositions[rg.firstRow];
        float bottom = rowYPositions[rg.lastRow] + rowHeights[rg.lastRow];
        rg.node->box.contentRect.x = groupInsetX;
        rg.node->box.contentRect.y = top;
        rg.node->box.contentRect.width = groupInsetW;
        rg.node->box.contentRect.height = std::max(0.0f, bottom - top);
    }

    // Now set each row's box. Rows that live inside a row group are positioned
    // relative to the row group (their DOM parent); rows that are direct table
    // children stay in table-content coords.
    for (size_t r = 0; r < numRows; r++) {
        if (r >= rows.size() || !rows[r].rowNode) continue;
        auto* rn = rows[r].rowNode;
        rn->box.margin = {};
        rn->box.padding = {};
        rn->box.border = {};
        float ry = rowYPositions[r];
        float rx = groupInsetX;
        if (rows[r].groupNode) {
            // subtract the group's y so we end up with row-relative-to-group
            ry -= rows[r].groupNode->box.contentRect.y;
            rx = 0; // row's parent is the group, which is already inset
        }
        rn->box.contentRect.x = rx;
        rn->box.contentRect.y = ry;
        rn->box.contentRect.width = groupInsetW;
        rn->box.contentRect.height = rowHeights[r];
    }

    // Compute column X positions. In collapse mode shift the first column
    // inward by the half-border so cells line up against the painted edge.
    std::vector<float> colXPositions(numCols);
    {
        float cx = borderSpacing + collapseInset.left;
        for (size_t c = 0; c < numCols; c++) {
            colXPositions[c] = cx;
            cx += colWidths[c] + borderSpacing;
        }
    }

    // Position <col> and <colgroup> boxes to span the actual cell columns.
    // Chromium reports each <col>'s rect as the column's [x..x+colWidth] band
    // covering the table body's vertical extent; <colgroup> spans the union
    // of its <col>s. Match that for parity.
    {
        float bodyTop = 0;
        float bodyBottom = 0;
        if (numRows > 0) {
            bodyTop = rowYPositions[0];
            bodyBottom = rowYPositions[numRows - 1] + rowHeights[numRows - 1];
        }
        float bodyH = std::max(0.0f, bodyBottom - bodyTop);
        // Track which column we're at as we walk colInfos.
        size_t colIdx = 0;
        // Group by colGroupNode, tracking each group's [firstCol..lastCol].
        struct GroupSpan { LayoutNode* node = nullptr; size_t first = 0; size_t last = 0; };
        std::vector<GroupSpan> groupSpans;
        for (size_t i = 0; i < colInfos.size() && colIdx < numCols; i++) {
            auto& ci = colInfos[i];
            if (ci.colNode) {
                ci.colNode->box.margin = {};
                ci.colNode->box.padding = {};
                ci.colNode->box.border = {};
                ci.colNode->box.contentRect.x = colXPositions[colIdx];
                ci.colNode->box.contentRect.y = bodyTop;
                ci.colNode->box.contentRect.width = colWidths[colIdx];
                ci.colNode->box.contentRect.height = bodyH;
            }
            if (ci.colGroupNode) {
                if (groupSpans.empty() || groupSpans.back().node != ci.colGroupNode) {
                    groupSpans.push_back({ci.colGroupNode, colIdx, colIdx});
                } else {
                    groupSpans.back().last = colIdx;
                }
            }
            colIdx++;
        }
        for (auto& gs : groupSpans) {
            if (!gs.node) continue;
            float gx = colXPositions[gs.first];
            float gw = (colXPositions[gs.last] + colWidths[gs.last]) - gx;
            gs.node->box.margin = {};
            gs.node->box.padding = {};
            gs.node->box.border = {};
            gs.node->box.contentRect.x = gx;
            gs.node->box.contentRect.y = bodyTop;
            gs.node->box.contentRect.width = gw;
            gs.node->box.contentRect.height = bodyH;
        }
        // Any <colgroup>s with no entries (empty or all eaten by cell columns
        // beyond colInfos): zero them out.
        for (auto* cg : colGroups) {
            bool found = false;
            for (auto& gs : groupSpans) if (gs.node == cg) { found = true; break; }
            if (!found) cg->box = LayoutBox{};
        }
    }

    // Position each cell
    for (auto& ci : cellInfos) {
        auto* cell = ci.node;
        float cellX = colXPositions[ci.gridCol];

        // Total spanned height for stretching
        float totalH = borderSpacing * (ci.rowspan - 1);
        for (size_t r = 0; r < ci.rowspan; r++) totalH += rowHeights[ci.gridRow + r];

        // Position cells RELATIVE to their parent row (not to the table).
        float cellRelX = cellX;
        float cellRelY = 0.0f;
        bool hasRowNode = (ci.gridRow < rows.size() && rows[ci.gridRow].rowNode != nullptr);
        if (hasRowNode) {
            // Row's content origin is at x=(borderSpacing + collapseInset.left)
            // within the table (or x=0 within the row group, which itself is
            // inset by that amount). Cell coords are relative to the row.
            cellRelX -= (borderSpacing + collapseInset.left);
        } else {
            cellRelY = rowYPositions[ci.gridRow];
        }

        cell->box.contentRect.x = cellRelX + cell->box.margin.left +
            cell->box.padding.left + cell->box.border.left;
        cell->box.contentRect.y = cellRelY + cell->box.margin.top +
            cell->box.padding.top + cell->box.border.top;

        // Stretch cell height to total spanned row height and apply vertical-align.
        // The cell stays anchored at its row position; only its inner content
        // (children) is shifted to satisfy vertical-align.
        float targetH = totalH - cell->box.margin.top - cell->box.margin.bottom -
            cell->box.padding.top - cell->box.padding.bottom -
            cell->box.border.top - cell->box.border.bottom;
        float cellContentH = std::max(cell->box.contentRect.height, targetH);

        // Intrinsic (flowed) child height inside the cell's content box.
        // Children's contentRect.y is in cell-content coords.
        float intrinsicH = 0.0f;
        for (auto* child : getLayoutChildren(cell)) {
            if (!child) continue;
            float ch = child->box.contentRect.y + child->box.contentRect.height
                       + child->box.padding.bottom + child->box.border.bottom
                       + child->box.margin.bottom;
            if (ch > intrinsicH) intrinsicH = ch;
        }
        float available = cellContentH - intrinsicH;
        if (available > 0) {
            auto& cs = cell->computedStyle();
            const std::string& valign = styleVal(cs, "vertical-align");
            float shiftY = 0.0f;
            if (valign == "middle") shiftY = available / 2.0f;
            else if (valign == "bottom") shiftY = available;
            // else top (default): no shift
            if (shiftY > 0.0f) {
                // Shift everything inside the cell down: child boxes and any
                // placed text runs on TextNode descendants. Coordinates of
                // descendant block boxes are relative to their parent so
                // shifting the immediate child propagates them; text runs are
                // stored on the TextNode itself in the cell's content coord
                // space and must be shifted explicitly.
                std::vector<LayoutNode*> walkStack;
                for (auto* child : getLayoutChildren(cell)) {
                    if (!child) continue;
                    child->box.contentRect.y += shiftY;
                    if (child->isTextNode()) {
                        for (auto& tr : child->box.textRuns) tr.y += shiftY;
                    }
                    walkStack.push_back(child);
                }
                while (!walkStack.empty()) {
                    auto* n = walkStack.back();
                    walkStack.pop_back();
                    for (auto* gc : n->children()) {
                        if (!gc) continue;
                        if (gc->isTextNode()) {
                            for (auto& tr : gc->box.textRuns) tr.y += shiftY;
                        }
                        walkStack.push_back(gc);
                    }
                }
            }
        }
        cell->box.contentRect.height = cellContentH;
    }

    // Layout bottom captions
    for (auto* cap : bottomCaptions) {
        layoutNode(cap, tableContentWidth, metrics);
        cap->box.contentRect.x = cap->box.margin.left + cap->box.padding.left + cap->box.border.left;
        cap->box.contentRect.y = cursorY + cap->box.margin.top + cap->box.padding.top + cap->box.border.top;
        cursorY += cap->box.fullHeight() + cap->box.margin.top + cap->box.margin.bottom;
    }

    // Set table dimensions
    node->box.contentRect.width = tableContentWidth;

    // Handle margin: auto for horizontal centering (like block boxes do).
    // The table behaves as a block container in this respect.
    {
        const std::string& marginLeftVal  = styleVal(style, "margin-left");
        const std::string& marginRightVal = styleVal(style, "margin-right");
        if (marginLeftVal == "auto" || marginRightVal == "auto") {
            float fullW = tableContentWidth + paddingH + borderH;
            float remaining = availableWidth - fullW;
            if (remaining < 0) remaining = 0;
            if (marginLeftVal == "auto" && marginRightVal == "auto") {
                node->box.margin.left  = remaining / 2.0f;
                node->box.margin.right = remaining / 2.0f;
            } else if (marginLeftVal == "auto") {
                node->box.margin.left  = remaining - node->box.margin.right;
                if (node->box.margin.left < 0) node->box.margin.left = 0;
            } else {
                node->box.margin.right = remaining - node->box.margin.left;
                if (node->box.margin.right < 0) node->box.margin.right = 0;
            }
        }
    }

    float specH = resolveLength(styleVal(style, "height"), 0, fontSize);
    const std::string& heightVal = styleVal(style, "height");
    if (heightVal != "auto" && !heightVal.empty()) {
        if (styleVal(style, "box-sizing") == "border-box") {
            float paddingV = node->box.padding.top + node->box.padding.bottom;
            float borderV = node->box.border.top + node->box.border.bottom;
            node->box.contentRect.height = specH - paddingV - borderV;
        } else {
            node->box.contentRect.height = specH;
        }
        if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
    } else {
        node->box.contentRect.height = cursorY;
    }

    } else {
        // No table content — just set dimensions
        node->box.contentRect.width = tableContentWidth;
        float specH = resolveLength(styleVal(style, "height"), 0, fontSize);
        const std::string& heightVal2 = styleVal(style, "height");
        if (heightVal2 != "auto" && !heightVal2.empty()) {
            if (styleVal(style, "box-sizing") == "border-box") {
                float paddingV = node->box.padding.top + node->box.padding.bottom;
                float borderV = node->box.border.top + node->box.border.bottom;
                node->box.contentRect.height = std::max(0.0f, specH - paddingV - borderV);
            } else {
                node->box.contentRect.height = specH;
            }
        } else {
            node->box.contentRect.height = 0;
        }
    }

}

} // namespace htmlayout::layout
