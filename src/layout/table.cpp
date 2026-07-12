#include "layout/table.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// A collected row: the <tr> node (or nullptr for anonymous rows), the row
// group it lives in (or nullptr), and its cells.
struct TableRow {
    LayoutNode* rowNode = nullptr;
    LayoutNode* groupNode = nullptr;
    std::vector<LayoutNode*> cells;
};

// Row groups, in document order, plus the [firstRow,lastRow] range they cover.
struct RowGroup {
    LayoutNode* node = nullptr;
    size_t firstRow = 0;
    size_t lastRow = 0; // inclusive
};

// Column metadata from <colgroup>/<col>. Each col contributes a column
// (or `span` columns) with a possibly-explicit width.
struct ColInfo {
    LayoutNode* colNode = nullptr;
    LayoutNode* colGroupNode = nullptr;
    float specWidth = -1.0f; // -1 = auto/unset
};

// A grid-placed cell with its span extents.
struct CellInfo {
    LayoutNode* node = nullptr;
    size_t gridRow = 0, gridCol = 0;
    size_t colspan = 1, rowspan = 1;
};

// Everything the column algorithm (CSS2 §17.5.2.2) produces: the cell grid,
// per-column intrinsic min/max widths, and the table's intrinsic content-box
// min/max widths. Built once and shared by layoutTable() and the intrinsic
// width queries so both always agree.
struct TableStructure {
    float fontSize = 16.0f;
    bool collapse = false;
    Edges borderWidth{};   // the table's own computed border widths
    Edges collapseInset{}; // outer half of the collapsed edge borders
    float borderSpacingH = 0;
    float borderSpacingV = 0;

    std::vector<TableRow> rows;
    std::vector<LayoutNode*> captions;
    std::vector<RowGroup> rowGroups;
    std::vector<ColInfo> colInfos;
    std::vector<LayoutNode*> colGroups; // for box assignment
    std::vector<CellInfo> cellInfos;
    // grid[r][c] points at the CellInfo occupying that slot (nullptr = empty).
    std::vector<std::vector<CellInfo*>> grid;
    size_t numRows = 0, numCols = 0;

    std::vector<float> colMin;
    std::vector<float> colMax;
    // Per-column percentage width target (fraction of the table content
    // width) for cells with `width: N%`. Negative means "no percent set".
    std::vector<float> colPctFrac;

    float totalSpacing = 0; // borderSpacingH * (numCols + 1)
    float sumMin = 0, sumMax = 0;
    // Intrinsic content-box widths: column sums + border-spacing + the
    // collapsed outer half-borders. Exclude the table's padding/border.
    float minContent = 0;
    float maxContent = 0;
};

float cellFontSize(LayoutNode* cell, float fallback) {
    auto& cs = cell->computedStyle();
    float cfs = resolveLength(styleVal(cs, "font-size"), fallback, fallback);
    return cfs > 0 ? cfs : fallback;
}

float cellBorderSide(LayoutNode* cell, const char* side, float fallbackFs) {
    auto& cs = cell->computedStyle();
    float cfs = cellFontSize(cell, fallbackFs);
    std::string styleProp = std::string("border-") + side + "-style";
    if (styleVal(cs, styleProp) == "none") return 0;
    std::string widthProp = std::string("border-") + side + "-width";
    return resolveLength(styleVal(cs, widthProp), 0, cfs);
}

float getCellBorderL(LayoutNode* cell, float fs) { return cellBorderSide(cell, "left", fs); }
float getCellBorderR(LayoutNode* cell, float fs) { return cellBorderSide(cell, "right", fs); }
float getCellBorderT(LayoutNode* cell, float fs) { return cellBorderSide(cell, "top", fs); }
float getCellBorderB(LayoutNode* cell, float fs) { return cellBorderSide(cell, "bottom", fs); }

// Compute the four shared half-borders for a cell at its grid position.
// Looks at actual neighbors in the grid (not column-wise max) so e.g.
// a thin cell next to a thick cell only shares the gridline with that
// specific neighbor, not the max across all rows. At the table's outer
// edge the cell shares the gridline with the table border.
void cellSharedHalfBorders(const TableStructure& ts, LayoutNode* cell,
                           size_t gridRow, size_t gridCol,
                           size_t colspan, size_t rowspan,
                           float& hL, float& hR, float& hT, float& hB) {
    size_t rcol = gridCol + colspan - 1;
    size_t rrow = gridRow + rowspan - 1;
    float fs = ts.fontSize;
    float ownL = getCellBorderL(cell, fs);
    float ownR = getCellBorderR(cell, fs);
    float ownT = getCellBorderT(cell, fs);
    float ownB = getCellBorderB(cell, fs);

    // Left edge: scan over rowspan rows on the left side.
    float leftMax = ownL;
    if (gridCol == 0) {
        leftMax = std::max(leftMax, ts.borderWidth.left);
    } else {
        for (size_t dr = 0; dr < rowspan && gridRow + dr < ts.numRows; dr++) {
            CellInfo* nb = ts.grid[gridRow + dr][gridCol - 1];
            if (nb && nb->node) leftMax = std::max(leftMax, getCellBorderR(nb->node, fs));
        }
    }
    // Right edge.
    float rightMax = ownR;
    if (rcol >= ts.numCols - 1) {
        rightMax = std::max(rightMax, ts.borderWidth.right);
    } else {
        for (size_t dr = 0; dr < rowspan && gridRow + dr < ts.numRows; dr++) {
            CellInfo* nb = ts.grid[gridRow + dr][rcol + 1];
            if (nb && nb->node) rightMax = std::max(rightMax, getCellBorderL(nb->node, fs));
        }
    }
    // Top edge.
    float topMax = ownT;
    if (gridRow == 0) {
        topMax = std::max(topMax, ts.borderWidth.top);
    } else {
        for (size_t dc = 0; dc < colspan && gridCol + dc < ts.numCols; dc++) {
            CellInfo* nb = ts.grid[gridRow - 1][gridCol + dc];
            if (nb && nb->node) topMax = std::max(topMax, getCellBorderB(nb->node, fs));
        }
    }
    // Bottom edge.
    float bottomMax = ownB;
    if (rrow >= ts.numRows - 1) {
        bottomMax = std::max(bottomMax, ts.borderWidth.bottom);
    } else {
        for (size_t dc = 0; dc < colspan && gridCol + dc < ts.numCols; dc++) {
            CellInfo* nb = ts.grid[rrow + 1][gridCol + dc];
            if (nb && nb->node) bottomMax = std::max(bottomMax, getCellBorderT(nb->node, fs));
        }
    }
    hL = leftMax   * 0.5f;
    hR = rightMax  * 0.5f;
    hT = topMax    * 0.5f;
    hB = bottomMax * 0.5f;
}

// Horizontal padding + border a cell adds around its content when it
// contributes to column intrinsic widths. In collapse mode each gridline's
// border is shared: the cell contributes half of the collapsed (max) border
// it actually shares with its neighbors — matching how the cell's border box
// is later sized — instead of its full own border.
float cellPadBorderH(const TableStructure& ts, const CellInfo& ci) {
    auto& cs = ci.node->computedStyle();
    float cfs = cellFontSize(ci.node, ts.fontSize);
    float pl = resolveLength(styleVal(cs, "padding-left"), 0, cfs);
    float pr = resolveLength(styleVal(cs, "padding-right"), 0, cfs);
    float bl, br;
    if (ts.collapse) {
        float hL, hR, hT, hB;
        cellSharedHalfBorders(ts, ci.node, ci.gridRow, ci.gridCol,
                              ci.colspan, ci.rowspan, hL, hR, hT, hB);
        bl = hL;
        br = hR;
    } else {
        bl = getCellBorderL(ci.node, ts.fontSize);
        br = getCellBorderR(ci.node, ts.fontSize);
    }
    return pl + pr + bl + br;
}

// Collect rows/columns/captions, place cells into the span-aware grid, and
// compute per-column intrinsic min/max widths (CSS2 §17.5.2.2).
// availableWidth is only used to resolve percentage <col> widths and is 0
// when called for pure intrinsic sizing (percentages then contribute
// nothing, per CSS Sizing).
TableStructure buildTableStructure(LayoutNode* node, float availableWidth,
                                   TextMetrics& metrics) {
    TableStructure ts;
    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    ts.fontSize = fontSize;

    const char* sides[] = {"top", "right", "bottom", "left"};
    float* bw[] = {&ts.borderWidth.top, &ts.borderWidth.right,
                   &ts.borderWidth.bottom, &ts.borderWidth.left};
    for (int i = 0; i < 4; i++) {
        std::string styleProp = std::string("border-") + sides[i] + "-style";
        std::string widthProp = std::string("border-") + sides[i] + "-width";
        if (styleVal(style, styleProp) != "none") {
            *bw[i] = resolveLength(styleVal(style, widthProp), availableWidth, fontSize);
        }
    }
    ts.collapse = (styleVal(style, "border-collapse") == "collapse");
    if (ts.collapse) {
        ts.collapseInset.top    = ts.borderWidth.top    * 0.5f;
        ts.collapseInset.right  = ts.borderWidth.right  * 0.5f;
        ts.collapseInset.bottom = ts.borderWidth.bottom * 0.5f;
        ts.collapseInset.left   = ts.borderWidth.left   * 0.5f;
    }

    // Border spacing — accepts one or two lengths: "H" or "H V". Default V = H.
    {
        const std::string& bsVal = styleVal(style, "border-spacing");
        if (!bsVal.empty()) {
            // Split on whitespace.
            size_t i = 0;
            while (i < bsVal.size() && std::isspace(static_cast<unsigned char>(bsVal[i]))) i++;
            size_t aStart = i;
            while (i < bsVal.size() && !std::isspace(static_cast<unsigned char>(bsVal[i]))) i++;
            std::string a = bsVal.substr(aStart, i - aStart);
            while (i < bsVal.size() && std::isspace(static_cast<unsigned char>(bsVal[i]))) i++;
            std::string b = bsVal.substr(i);
            ts.borderSpacingH = resolveLength(a, availableWidth, fontSize);
            ts.borderSpacingV = b.empty() ? ts.borderSpacingH
                                          : resolveLength(b, availableWidth, fontSize);
        }
    }
    if (ts.collapse) { ts.borderSpacingH = 0; ts.borderSpacingV = 0; }

    auto& rows = ts.rows;
    auto& colInfos = ts.colInfos;

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
                if (added) ts.rowGroups.push_back(rg);
                else { child->box = LayoutBox{}; } // empty row group
            } else if (d == "table-column-group") {
                ts.colGroups.push_back(child);
                // colgroup width applies to each contained col without an
                // explicit width, OR if no <col> children exist, expands span
                auto& gcs = child->computedStyle();
                std::string gWidthVal = styleVal(gcs, "width");
                std::string gSpan(child->attribute("span"));
                if (gSpan.empty()) gSpan = styleVal(gcs, "span");
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
                        std::string colSpanAttr(gc->attribute("span"));
                        if (colSpanAttr.empty()) colSpanAttr = styleVal(gccs, "span");
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
                std::string colSpanAttr(child->attribute("span"));
                if (colSpanAttr.empty()) colSpanAttr = styleVal(cs, "span");
                if (!colSpanAttr.empty()) span = std::max(1, std::atoi(colSpanAttr.c_str()));
                for (int s = 0; s < span; s++) colInfos.push_back(ci);
            } else if (isTableCaption(d)) {
                ts.captions.push_back(child);
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

    // Read colspan/rowspan from the HTML attribute when the consumer bridges
    // it, or from the computed style otherwise (the cascade surfaces the
    // colspan/rowspan attributes as computed keys). Returns >= 1.
    auto readSpan = [&](LayoutNode* cell, const char* name) -> int {
        std::string a(cell->attribute(name));
        int v = 0;
        if (!a.empty()) v = std::atoi(a.c_str());
        if (v <= 0) {
            auto& cs = cell->computedStyle();
            v = static_cast<int>(resolveLength(styleVal(cs, name), 0, fontSize));
        }
        return v < 1 ? 1 : v;
    };

    size_t numRows = rows.size();
    size_t numCols = 0;

    // Pre-scan to estimate column count including colspans
    for (auto& row : rows) {
        size_t cols = 0;
        for (auto* cell : row.cells) {
            cols += static_cast<size_t>(readSpan(cell, "colspan"));
        }
        numCols = std::max(numCols, cols);
    }

    if (numCols == 0) {
        ts.numRows = numRows;
        ts.numCols = 0;
        return ts;
    }

    // Reserve cellInfos so pointers stored in grid stay valid as we push.
    {
        size_t totalCells = 0;
        for (auto& row : rows) totalCells += row.cells.size();
        ts.cellInfos.reserve(totalCells + 8);
    }
    // Build grid — may grow if rowspans push cells into new columns
    auto& grid = ts.grid;
    grid.assign(numRows, std::vector<CellInfo*>(numCols, nullptr));

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

            ts.cellInfos.push_back({cell, r, gridCol, static_cast<size_t>(cspan), static_cast<size_t>(rspan)});
            CellInfo* ci = &ts.cellInfos.back();

            for (size_t dr = 0; dr < static_cast<size_t>(rspan); dr++) {
                for (size_t dc = 0; dc < static_cast<size_t>(cspan); dc++) {
                    grid[r + dr][gridCol + dc] = ci;
                }
            }
            gridCol += cspan;
        }
    }

    ts.numRows = numRows;
    ts.numCols = numCols;

    // In border-collapse mode the table's outer edge gets a half-share of
    // max(tableBorder, max boundary-cell border) — the outer half of the
    // collapsed edge border, which the table reserves as a cell inset.
    if (ts.collapse) {
        std::vector<float> colMaxLeft(numCols, 0.0f);
        std::vector<float> colMaxRight(numCols, 0.0f);
        std::vector<float> rowMaxTop(numRows, 0.0f);
        std::vector<float> rowMaxBottom(numRows, 0.0f);
        for (auto& ci : ts.cellInfos) {
            float bl = getCellBorderL(ci.node, fontSize);
            float br = getCellBorderR(ci.node, fontSize);
            float bt = getCellBorderT(ci.node, fontSize);
            float bb = getCellBorderB(ci.node, fontSize);
            colMaxLeft[ci.gridCol]  = std::max(colMaxLeft[ci.gridCol],  bl);
            size_t rcol = ci.gridCol + ci.colspan - 1;
            if (rcol < numCols) colMaxRight[rcol] = std::max(colMaxRight[rcol], br);
            if (ci.gridRow < numRows)
                rowMaxTop[ci.gridRow] = std::max(rowMaxTop[ci.gridRow], bt);
            size_t rrow = ci.gridRow + ci.rowspan - 1;
            if (rrow < numRows) rowMaxBottom[rrow] = std::max(rowMaxBottom[rrow], bb);
        }
        float topMax    = numRows > 0 ? rowMaxTop[0]              : 0;
        float bottomMax = numRows > 0 ? rowMaxBottom[numRows - 1] : 0;
        float leftMax   = numCols > 0 ? colMaxLeft[0]             : 0;
        float rightMax  = numCols > 0 ? colMaxRight[numCols - 1]  : 0;
        ts.collapseInset.top    = std::max(ts.borderWidth.top,    topMax)    * 0.5f;
        ts.collapseInset.bottom = std::max(ts.borderWidth.bottom, bottomMax) * 0.5f;
        ts.collapseInset.left   = std::max(ts.borderWidth.left,   leftMax)   * 0.5f;
        ts.collapseInset.right  = std::max(ts.borderWidth.right,  rightMax)  * 0.5f;
    }

    // Intrinsic column min/max widths from cells (CSS2 §17.5.2.2).
    // For each non-spanning cell, contribute its own min/max content width
    // (plus padding+border) to its column.
    ts.colMin.assign(numCols, 0.0f);
    ts.colMax.assign(numCols, 0.0f);
    ts.colPctFrac.assign(numCols, -1.0f);
    ts.totalSpacing = ts.borderSpacingH * (numCols + 1);

    for (auto& ci : ts.cellInfos) {
        float pbh = cellPadBorderH(ts, ci);
        // Honor explicit cell width if specified
        auto& cs = ci.node->computedStyle();
        const std::string& cwVal = styleVal(cs, "width");
        float cellMin = computeMinContentWidth(ci.node, metrics) + pbh;
        float cellMax = computeMaxContentWidth(ci.node, metrics) + pbh;
        bool isPercentWidth = (!cwVal.empty() && cwVal.back() == '%');
        if (!cwVal.empty() && cwVal != "auto" && !isPercentWidth) {
            float specCellW = resolveLength(cwVal, 0, fontSize);
            if (styleVal(cs, "box-sizing") != "border-box") specCellW += pbh;
            // Treat explicit (length) width as a strong preferred for max-content,
            // and as a floor for min-content.
            cellMax = std::max(cellMax, specCellW);
            cellMin = std::max(cellMin, specCellW);
        }
        if (ci.colspan == 1) {
            ts.colMin[ci.gridCol] = std::max(ts.colMin[ci.gridCol], cellMin);
            ts.colMax[ci.gridCol] = std::max(ts.colMax[ci.gridCol], cellMax);
            if (isPercentWidth) {
                // Parse "N%" -> fraction
                float pct = 0.0f;
                try { pct = std::stof(cwVal.substr(0, cwVal.size() - 1)); } catch (...) {}
                float frac = std::max(0.0f, pct / 100.0f);
                if (ts.colPctFrac[ci.gridCol] < frac) ts.colPctFrac[ci.gridCol] = frac;
            }
        }
    }

    // Distribute spanning cell intrinsic widths over spanned columns.
    // Per CSS2 §17.5.2.2 a spanning cell increases the spanned columns only
    // by the amount its min/max exceeds their current sum (plus the
    // spanned-over border spacing); the excess is split across the columns.
    for (auto& ci : ts.cellInfos) {
        if (ci.colspan <= 1) continue;
        float pbh = cellPadBorderH(ts, ci);
        float cellMin = computeMinContentWidth(ci.node, metrics) + pbh;
        float cellMax = computeMaxContentWidth(ci.node, metrics) + pbh;
        float spanSpacing = ts.borderSpacingH * (ci.colspan - 1);

        float minSum = 0, maxSum = 0;
        for (size_t c = 0; c < ci.colspan; c++) {
            minSum += ts.colMin[ci.gridCol + c];
            maxSum += ts.colMax[ci.gridCol + c];
        }
        float minDeficit = cellMin - spanSpacing - minSum;
        if (minDeficit > 0) {
            float per = minDeficit / ci.colspan;
            for (size_t c = 0; c < ci.colspan; c++) ts.colMin[ci.gridCol + c] += per;
        }
        float maxDeficit = cellMax - spanSpacing - maxSum;
        if (maxDeficit > 0) {
            float per = maxDeficit / ci.colspan;
            for (size_t c = 0; c < ci.colspan; c++) ts.colMax[ci.gridCol + c] += per;
        }
    }

    // Apply explicit <col>/<colgroup> widths: when set, they pin both min and max.
    for (size_t c = 0; c < numCols && c < colInfos.size(); c++) {
        if (colInfos[c].specWidth > 0) {
            float w = colInfos[c].specWidth;
            // Treat col width as the column's preferred width — but never below
            // the column's intrinsic min-content (avoid clipping inline text).
            float floor = ts.colMin[c];
            float pinned = std::max(w, floor);
            ts.colMin[c] = pinned;
            ts.colMax[c] = pinned;
        }
    }

    for (size_t c = 0; c < numCols; c++) {
        ts.sumMin += ts.colMin[c];
        ts.sumMax += ts.colMax[c];
    }
    float insetExtra = ts.collapseInset.left + ts.collapseInset.right;
    ts.minContent = ts.sumMin + ts.totalSpacing + insetExtra;
    ts.maxContent = ts.sumMax + ts.totalSpacing + insetExtra;

    return ts;
}

} // anonymous namespace

void computeTableIntrinsicWidths(LayoutNode* node, TextMetrics& metrics,
                                 float& minWidth, float& maxWidth) {
    minWidth = 0;
    maxWidth = 0;
    if (!node) return;
    TableStructure ts = buildTableStructure(node, 0.0f, metrics);
    minWidth = ts.minContent;
    maxWidth = ts.maxContent;
}

void layoutTable(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(style, "font-size"), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(style, "margin", availableWidth, fontSize);
    node->box.padding = resolveEdges(style, "padding", availableWidth, fontSize);

    // Collect rows/columns and compute intrinsic column widths.
    TableStructure ts = buildTableStructure(node, availableWidth, metrics);

    // In border-collapse mode the collapsed border is rendered ON the cell
    // edges and overlaps them — the table's own outer box reports zero border
    // (Chromium getBoundingClientRect on a collapsed table includes only the
    // outer half of the collapsed border inside its content width, not as a
    // separate border edge). We retain a half-border value as a cell inset so
    // cells still sit at the table's painted edge with room for the merged
    // border line.
    bool collapse = ts.collapse;
    Edges& collapseInset = ts.collapseInset;
    node->box.border = collapse ? Edges{} : ts.borderWidth;

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float marginH = node->box.margin.left + node->box.margin.right;

    // Resolve table width. Note: per CSS 2.1 §17.5.2, width:auto on tables means
    // shrink-to-fit, NOT fill-the-container. We compute a preferred (max-content)
    // width from intrinsic column sizes; for now, set a tentative full-width
    // sentinel that's clamped later.
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

    // Many call sites use a single 'borderSpacing'. Use H for the horizontal
    // axis (column track gaps and table contentRect width math) and V at the
    // cursorY/spanning-height steps.
    float borderSpacing = ts.borderSpacingH;
    float borderSpacingV = ts.borderSpacingV;

    auto& rows = ts.rows;
    auto& rowGroups = ts.rowGroups;
    auto& colInfos = ts.colInfos;
    auto& colGroups = ts.colGroups;
    auto& captions = ts.captions;
    auto& cellInfos = ts.cellInfos;
    size_t numRows = ts.numRows;
    size_t numCols = ts.numCols;

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

    auto& colMin = ts.colMin;
    auto& colMax = ts.colMax;
    auto& colPctFrac = ts.colPctFrac;
    float totalSpacing = ts.totalSpacing;
    float sumMin = ts.sumMin;
    float sumMax = ts.sumMax;
    float insetExtra = collapseInset.left + collapseInset.right;
    float prefTable = ts.maxContent;
    float minTable  = ts.minContent;

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
            // plus colMin for auto cols, plus spacing and collapse insets.
            float floor = totalSpacing + insetExtra;
            float pctBasisFloor = tableContentWidth - insetExtra;
            if (pctBasisFloor < 0) pctBasisFloor = 0;
            for (size_t c = 0; c < numCols; c++) {
                if (colPctFrac[c] >= 0) {
                    float target = colPctFrac[c] * pctBasisFloor;
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
    // In collapse mode, cells live inside [collapseInset.left, width-collapseInset.right]
    // — cells share the table's outer half-borders, so reserve that space.
    float available = tableContentWidth - totalSpacing
                      - collapseInset.left - collapseInset.right;

    // First, satisfy percent columns.
    bool anyPct = false;
    for (size_t c = 0; c < numCols; c++) if (colPctFrac[c] >= 0) { anyPct = true; break; }
    float pctConsumed = 0.0f;
    float autoMinSum = 0.0f;
    float autoSlackSum = 0.0f;
    float autoMaxSum = 0.0f;
    // Percent targets resolve against the cell-track area (not the table's
    // outer border-box), which excludes the collapse-mode outer half-borders.
    float pctBasis = tableContentWidth - collapseInset.left - collapseInset.right;
    if (pctBasis < 0) pctBasis = 0;
    if (anyPct) {
        for (size_t c = 0; c < numCols; c++) {
            if (colPctFrac[c] >= 0) {
                float target = colPctFrac[c] * pctBasis;
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
        // In border-collapse mode a cell's border box carries HALF of each
        // collapsed gridline border (the other half belongs to its neighbour) —
        // exactly the padding+border the column algorithm reserved for it via
        // cellPadBorderH. layoutBlock, however, resolves the cell's FULL
        // computed border when it flows the content (the collapsed half-border
        // is written to box.border only afterwards, below). Uncorrected, that
        // extra half-border shrinks the flowed content area below the reserved
        // max-content width, so a column sized exactly to max-content wraps the
        // cell's last word. Widen the width handed to layoutNode by
        // (full - half) so the content area it computes equals the collapsed
        // content box the column was sized for; the cell's final rendered width
        // is still pinned to its track span at the box.contentRect.width
        // assignment below.
        float layoutW = cw;
        if (collapse) {
            float hL, hR, hT, hB;
            cellSharedHalfBorders(ts, ci.node, ci.gridRow, ci.gridCol,
                                  ci.colspan, ci.rowspan, hL, hR, hT, hB);
            float fullB = getCellBorderL(ci.node, ts.fontSize) +
                          getCellBorderR(ci.node, ts.fontSize);
            layoutW = cw + (fullB - (hL + hR));
        }
        layoutNode(ci.node, layoutW, metrics);

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
        // actual neighbors in the grid. Each cell gets half(max(adjacent
        // borders meeting on that gridline)).
        if (collapse) {
            Edges b{};
            cellSharedHalfBorders(ts, ci.node, ci.gridRow, ci.gridCol,
                                  ci.colspan, ci.rowspan,
                                  b.left, b.right, b.top, b.bottom);
            ci.node->box.border = b;
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
        float spannedHeight = borderSpacingV * (ci.rowspan - 1);
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
        cursorY += borderSpacingV;
        rowYPositions[r] = cursorY;
        cursorY += rowHeights[r];
    }
    cursorY += borderSpacingV; // bottom spacing
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
        // Under `direction: rtl` the column order reverses: grid column 0 sits
        // at the right edge and the last column at the left. Walk the physical
        // slots left-to-right (same widths and spacing) but assign them to grid
        // columns from last to first.
        const bool rtl = (styleVal(node->computedStyle(), "direction") == "rtl");
        float cx = borderSpacing + collapseInset.left;
        for (size_t s = 0; s < numCols; s++) {
            size_t c = rtl ? (numCols - 1 - s) : s;
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
            // col positioned relative to its colgroup (or table if no group);
            // store table-content x temporarily, fix-up below once we know
            // each colgroup's origin.
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
        // Now make each col's x relative to its parent colgroup (it lives
        // inside the colgroup, not the table directly).
        for (auto& ci : colInfos) {
            if (!ci.colNode || !ci.colGroupNode) continue;
            ci.colNode->box.contentRect.x -= ci.colGroupNode->box.contentRect.x;
            ci.colNode->box.contentRect.y -= ci.colGroupNode->box.contentRect.y;
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
        float totalH = borderSpacingV * (ci.rowspan - 1);
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

        // Intrinsic (flowed) content height inside the cell's content box:
        // start from the cell's flow height (naturalHeight — the line-box
        // stack), NOT just the union of child boxes. The union undershoots
        // for inline content whose line boxes are taller than the boxes
        // themselves (a 14px inline-block on a 20px line flows 20px tall;
        // Chromium centers the line stack, and with text the union was off
        // by the half-leading). The child-box union below stays as a floor
        // for content overflowing the flow height.
        float intrinsicH = cell->box.naturalHeight;
        for (auto* child : getLayoutChildren(cell)) {
            if (!child) continue;
            float ch = child->box.contentRect.y + child->box.contentRect.height
                       + child->box.padding.bottom + child->box.border.bottom
                       + child->box.margin.bottom;
            if (ch > intrinsicH) intrinsicH = ch;
        }
        float availableH = cellContentH - intrinsicH;
        if (availableH > 0) {
            auto& cs = cell->computedStyle();
            const std::string& valign = styleVal(cs, "vertical-align");
            float shiftY = 0.0f;
            if (valign == "middle") shiftY = availableH / 2.0f;
            else if (valign == "bottom") shiftY = availableH;
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
}

} // namespace htmlayout::layout
