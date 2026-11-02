#include "layout/table_structure.h"
#include "layout/table.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cctype>
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

float cellBorderSide(LayoutNode* cell, Prop styleProp, Prop widthProp, float fallbackFs) {
    float cfs = cellFontSize(cell, fallbackFs);
    if (styleVal(cell, styleProp) == "none") return 0;
    return resolveLength(styleVal(cell, widthProp), 0, cfs);
}

// A width, in px, from a <col> / <colgroup> / first-row cell `width` under
// the fixed table layout: lengths as given, percentages of the table's
// content width. Negative for auto.
float fixedWidthOf(LayoutNode* n, float tableWidth, float fontSize) {
    if (!n) return -1.0f;
    const std::string& w = styleVal(n, Prop::Width);
    if (w.empty() || w == "auto") return -1.0f;
    return resolveLength(w, tableWidth, fontSize);
}

} // anonymous namespace

float cellFontSize(LayoutNode* cell, float fallback) {
    auto& cs = cell->computedStyle();
    float cfs = resolveLength(styleVal(cell, Prop::FontSize), fallback, fallback);
    return cfs > 0 ? cfs : fallback;
}

float getCellBorderL(LayoutNode* c, float fs) {
    return cellBorderSide(c, Prop::BorderLeftStyle, Prop::BorderLeftWidth, fs);
}
float getCellBorderR(LayoutNode* c, float fs) {
    return cellBorderSide(c, Prop::BorderRightStyle, Prop::BorderRightWidth, fs);
}
float getCellBorderT(LayoutNode* c, float fs) {
    return cellBorderSide(c, Prop::BorderTopStyle, Prop::BorderTopWidth, fs);
}
float getCellBorderB(LayoutNode* c, float fs) {
    return cellBorderSide(c, Prop::BorderBottomStyle, Prop::BorderBottomWidth, fs);
}

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
    float pl = resolveLength(styleVal(ci.node, Prop::PaddingLeft), 0, cfs);
    float pr = resolveLength(styleVal(ci.node, Prop::PaddingRight), 0, cfs);
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
    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;
    ts.fontSize = fontSize;

    ts.borderWidth = resolveBorders(node, availableWidth, fontSize);
    ts.collapse = (styleVal(node, Prop::BorderCollapse) == "collapse");
    if (ts.collapse) {
        ts.collapseInset.top    = ts.borderWidth.top    * 0.5f;
        ts.collapseInset.right  = ts.borderWidth.right  * 0.5f;
        ts.collapseInset.bottom = ts.borderWidth.bottom * 0.5f;
        ts.collapseInset.left   = ts.borderWidth.left   * 0.5f;
    }

    // Border spacing — accepts one or two lengths: "H" or "H V". Default V = H.
    {
        const std::string& bsVal = styleVal(node, Prop::BorderSpacing);
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
        std::vector<TableRow> headerRows;
        std::vector<TableRow> bodyRows;
        std::vector<TableRow> footerRows;
        std::vector<LayoutNode*> groupNodes;

        for (auto* child : getLayoutChildren(parent)) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            const std::string& d = styleVal(child, Prop::Display);
            if (d == "none") { clearHiddenBox(child); continue; }

            // Absolutely/fixed positioned children are out of flow
            const std::string& childPos = styleVal(child, Prop::Position);
            if (childPos == "absolute" || childPos == "fixed") continue;

            if (isTableRow(d)) {
                TableRow row;
                row.rowNode = child;
                for (auto* cell : getLayoutChildren(child)) {
                    if (cell->isTextNode()) continue;
                    auto& cellStyle = cell->computedStyle();
                    const std::string& cd = styleVal(cell, Prop::Display);
                    if (cd == "none") { clearHiddenBox(cell); continue; }
                    row.cells.push_back(cell);
                }
                bodyRows.push_back(std::move(row));
            } else if (isTableRowGroup(d)) {
                // Recurse into row groups (thead, tbody, tfoot)
                auto& targetRows = (d == "table-header-group") ? headerRows
                                 : (d == "table-footer-group") ? footerRows
                                                               : bodyRows;
                bool added = false;
                for (auto* groupChild : getLayoutChildren(child)) {
                    if (groupChild->isTextNode()) continue;
                    auto& gcs = groupChild->computedStyle();
                    const std::string& gd = styleVal(groupChild, Prop::Display);
                    if (gd == "none") { clearHiddenBox(groupChild); continue; }
                    if (isTableRow(gd)) {
                        TableRow row;
                        row.rowNode = groupChild;
                        row.groupNode = child;
                        for (auto* cell : getLayoutChildren(groupChild)) {
                            if (cell->isTextNode()) continue;
                            auto& cellStyle = cell->computedStyle();
                            if (styleVal(cell, Prop::Display) == "none") {
                                clearHiddenBox(cell);
                                continue;
                            }
                            row.cells.push_back(cell);
                        }
                        targetRows.push_back(std::move(row));
                        added = true;
                    }
                }
                if (added) groupNodes.push_back(child);
                else { child->box = LayoutBox{}; } // empty row group
            } else if (d == "table-column-group") {
                ts.colGroups.push_back(child);
                // colgroup width applies to each contained col without an
                // explicit width, OR if no <col> children exist, expands span
                auto& gcs = child->computedStyle();
                std::string gWidthVal = styleVal(child, Prop::Width);
                std::string gSpan(child->attribute("span"));
                if (gSpan.empty()) gSpan = styleVal(child, Prop::Span);
                bool hasColChildren = false;
                for (auto* gc : getLayoutChildren(child)) {
                    if (gc->isTextNode()) continue;
                    auto& gccs = gc->computedStyle();
                    const std::string& gcd = styleVal(gc, Prop::Display);
                    if (gcd == "none") { clearHiddenBox(gc); continue; }
                    if (gcd == "table-column") {
                        hasColChildren = true;
                        ColInfo ci;
                        ci.colNode = gc;
                        ci.colGroupNode = child;
                        std::string wVal = styleVal(gc, Prop::Width);
                        float w = -1.0f;
                        if (!wVal.empty() && wVal != "auto") {
                            w = resolveLength(wVal, availableWidth, fontSize);
                        } else if (!gWidthVal.empty() && gWidthVal != "auto") {
                            w = resolveLength(gWidthVal, availableWidth, fontSize);
                        }
                        ci.specWidth = w;
                        int span = 1;
                        std::string colSpanAttr(gc->attribute("span"));
                        if (colSpanAttr.empty()) colSpanAttr = styleVal(gc, Prop::Span);
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
                std::string wVal = styleVal(child, Prop::Width);
                if (!wVal.empty() && wVal != "auto") {
                    ci.specWidth = resolveLength(wVal, availableWidth, fontSize);
                }
                int span = 1;
                std::string colSpanAttr(child->attribute("span"));
                if (colSpanAttr.empty()) colSpanAttr = styleVal(child, Prop::Span);
                if (!colSpanAttr.empty()) span = std::max(1, std::atoi(colSpanAttr.c_str()));
                for (int s = 0; s < span; s++) colInfos.push_back(ci);
            } else if (isTableCaption(d)) {
                ts.captions.push_back(child);
            } else if (isTableCell(d)) {
                // Direct cell without a row — create anonymous row
                if (bodyRows.empty() || !bodyRows.back().cells.empty()) {
                    bodyRows.push_back(TableRow{});
                }
                bodyRows.back().cells.push_back(child);
            } else {
                // Non-table child — treat as anonymous cell in anonymous row
                bodyRows.push_back(TableRow{});
                bodyRows.back().cells.push_back(child);
            }
        }

        rows.reserve(headerRows.size() + bodyRows.size() + footerRows.size());
        for (auto& r : headerRows) rows.push_back(std::move(r));
        for (auto& r : bodyRows)   rows.push_back(std::move(r));
        for (auto& r : footerRows) rows.push_back(std::move(r));

        for (auto* gn : groupNodes) {
            RowGroup rg;
            rg.node = gn;
            bool found = false;
            for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].groupNode == gn) {
                    if (!found) {
                        rg.firstRow = i;
                        found = true;
                    }
                    rg.lastRow = i;
                }
            }
            if (found) ts.rowGroups.push_back(rg);
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
        const std::string& cwVal = styleVal(ci.node, Prop::Width);
        float cellMin = computeMinContentWidth(ci.node, metrics) + pbh;
        float cellMax = computeMaxContentWidth(ci.node, metrics) + pbh;
        bool isPercentWidth = (!cwVal.empty() && cwVal.back() == '%');
        if (!cwVal.empty() && cwVal != "auto" && !isPercentWidth) {
            float specCellW = resolveLength(cwVal, 0, fontSize);
            if (styleVal(ci.node, Prop::BoxSizing) != "border-box") specCellW += pbh;
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

// The fixed table layout (CSS2 §17.5.2.1): column widths come from the
// <col> elements and the first row's cells alone, never from cell content,
// and the columns left over share what remains of the table width equally.
// `tableContentWidth` is widened when the specified columns need more.
std::vector<float> fixedLayoutColumnWidths(const TableStructure& ts,
                                           float& tableContentWidth) {
    const size_t numCols = ts.numCols;
    const float insets = ts.collapseInset.left + ts.collapseInset.right;
    std::vector<float> w(numCols, -1.0f);

    // 1. A column element with a width sets the column's width.
    for (size_t c = 0; c < numCols && c < ts.colInfos.size(); c++) {
        const ColInfo& ci = ts.colInfos[c];
        float v = fixedWidthOf(ci.colNode, tableContentWidth, ts.fontSize);
        if (v < 0) v = fixedWidthOf(ci.colGroupNode, tableContentWidth, ts.fontSize);
        if (v >= 0) w[c] = v;
    }
    // 2. Otherwise a first-row cell with a width sets it (split evenly over
    //    the columns a spanning cell covers).
    for (const CellInfo& ci : ts.cellInfos) {
        if (ci.gridRow != 0) continue;
        float cfs = cellFontSize(ci.node, ts.fontSize);
        float v = fixedWidthOf(ci.node, tableContentWidth, cfs);
        if (v < 0) continue;
        if (styleVal(ci.node, Prop::BoxSizing) != "border-box")
            v += cellPadBorderH(ts, ci);
        v -= ts.borderSpacingH * static_cast<float>(ci.colspan - 1);
        float per = std::max(0.0f, v) / static_cast<float>(ci.colspan);
        for (size_t c = ci.gridCol; c < ci.gridCol + ci.colspan && c < numCols; c++)
            if (w[c] < 0) w[c] = per;
    }
    // 3. The remaining columns divide the rest of the table width equally.
    float available = tableContentWidth - ts.totalSpacing - insets;
    float fixedSum = 0.0f;
    size_t autoCols = 0;
    for (float v : w) {
        if (v >= 0) fixedSum += v; else ++autoCols;
    }
    float rest = available - fixedSum;
    if (autoCols > 0) {
        float each = std::max(0.0f, rest) / static_cast<float>(autoCols);
        for (float& v : w) if (v < 0) v = each;
    } else if (rest > 0 && fixedSum > 0) {
        // Every column is specified and they fall short of the table width:
        // the extra is spread over them in proportion.
        for (float& v : w) v += rest * (v / fixedSum);
    } else if (rest > 0) {
        for (float& v : w) v = available / static_cast<float>(numCols);
    }
    // The table is at least as wide as its specified columns.
    float needed = ts.totalSpacing + insets;
    for (float v : w) needed += v;
    if (needed > tableContentWidth) tableContentWidth = needed;
    return w;
}

void computeTableIntrinsicWidths(LayoutNode* node, TextMetrics& metrics,
                                 float& minWidth, float& maxWidth) {
    minWidth = 0;
    maxWidth = 0;
    if (!node) return;
    TableStructure ts = buildTableStructure(node, 0.0f, metrics);
    minWidth = ts.minContent;
    maxWidth = ts.maxContent;
}

} // namespace htmlayout::layout
