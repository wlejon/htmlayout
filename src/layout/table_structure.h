#pragma once
#include "layout/box.h"
#include <cstddef>
#include <vector>

namespace htmlayout::layout {

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

float cellFontSize(LayoutNode* cell, float fallback);
float getCellBorderL(LayoutNode* c, float fs);
float getCellBorderR(LayoutNode* c, float fs);
float getCellBorderT(LayoutNode* c, float fs);
float getCellBorderB(LayoutNode* c, float fs);

void cellSharedHalfBorders(const TableStructure& ts, LayoutNode* cell,
                           size_t gridRow, size_t gridCol,
                           size_t colspan, size_t rowspan,
                           float& hL, float& hR, float& hT, float& hB);

float cellPadBorderH(const TableStructure& ts, const CellInfo& ci);

TableStructure buildTableStructure(LayoutNode* node, float availableWidth,
                                   TextMetrics& metrics);

std::vector<float> fixedLayoutColumnWidths(const TableStructure& ts,
                                           float& tableContentWidth);

} // namespace htmlayout::layout
