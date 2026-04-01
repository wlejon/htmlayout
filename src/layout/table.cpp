#include "layout/table.h"
#include "layout/formatting_context.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace htmlayout::layout {

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

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
    node->box.border = borderWidth;

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;
    float marginH = node->box.margin.left + node->box.margin.right;

    // Resolve table width
    float specW = resolveLength(styleVal(style, "width"), availableWidth, fontSize);
    const std::string& widthVal = styleVal(style, "width");
    float tableContentWidth;
    if (widthVal != "auto" && !widthVal.empty()) {
        if (styleVal(style, "box-sizing") == "border-box") {
            tableContentWidth = specW - paddingH - borderH;
        } else {
            tableContentWidth = specW;
        }
        if (tableContentWidth < 0) tableContentWidth = 0;
    } else {
        tableContentWidth = availableWidth - marginH - paddingH - borderH;
        if (tableContentWidth < 0) tableContentWidth = 0;
    }

    // Border spacing
    float borderSpacing = resolveLength(styleVal(style, "border-spacing"), availableWidth, fontSize);
    bool collapse = (styleVal(style, "border-collapse") == "collapse");
    if (collapse) borderSpacing = 0;

    // Collect rows: iterate children, handling direct cells, row groups, and rows.
    // Each row is a vector of LayoutNode* cells.
    struct TableRow {
        LayoutNode* rowNode = nullptr; // the <tr> node, or nullptr for anonymous rows
        std::vector<LayoutNode*> cells;
    };
    std::vector<TableRow> rows;
    std::vector<LayoutNode*> captions;

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
                for (auto* groupChild : child->children()) {
                    if (groupChild->isTextNode()) continue;
                    auto& gcs = groupChild->computedStyle();
                    const std::string& gd = styleVal(gcs, "display");
                    if (gd == "none") { groupChild->box = LayoutBox{}; continue; }
                    if (isTableRow(gd)) {
                        TableRow row;
                        row.rowNode = groupChild;
                        for (auto* cell : getLayoutChildren(groupChild)) {
                            if (cell->isTextNode()) continue;
                            auto& cellStyle = cell->computedStyle();
                            if (styleVal(cellStyle, "display") == "none") {
                                cell->box = LayoutBox{};
                                continue;
                            }
                            row.cells.push_back(cell);
                        }
                        rows.push_back(std::move(row));
                    }
                }
                // Set the row group node box later
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

    // Pre-scan to estimate column count including colspans
    for (auto& row : rows) {
        size_t cols = 0;
        for (auto* cell : row.cells) {
            auto& cs = cell->computedStyle();
            int cs_val = static_cast<int>(resolveLength(styleVal(cs, "colspan"), 0, fontSize));
            cols += (cs_val > 1) ? cs_val : 1;
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

            auto& cs = cell->computedStyle();
            int cspan = static_cast<int>(resolveLength(styleVal(cs, "colspan"), 0, fontSize));
            int rspan = static_cast<int>(resolveLength(styleVal(cs, "rowspan"), 0, fontSize));
            if (cspan < 1) cspan = 1;
            if (rspan < 1) rspan = 1;

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

    // Phase 1: Determine column widths.
    // Only non-spanning cells (colspan=1) contribute directly.
    std::vector<float> colWidths(numCols, 0.0f);
    float totalSpacing = borderSpacing * (numCols + 1);
    float cellAvailWidth = (tableContentWidth - totalSpacing) / numCols;
    if (cellAvailWidth < 0) cellAvailWidth = 0;

    for (auto& ci : cellInfos) {
        float w = cellAvailWidth * ci.colspan + borderSpacing * (ci.colspan - 1);
        layoutNode(ci.node, w, metrics);
        float cellFullW = ci.node->box.fullWidth() + ci.node->box.margin.left + ci.node->box.margin.right;
        if (ci.colspan == 1) {
            colWidths[ci.gridCol] = std::max(colWidths[ci.gridCol], cellFullW);
        }
    }

    // Distribute spanning cell widths: if a spanning cell needs more than the sum
    // of its columns, expand columns equally
    for (auto& ci : cellInfos) {
        if (ci.colspan <= 1) continue;
        float cellFullW = ci.node->box.fullWidth() + ci.node->box.margin.left + ci.node->box.margin.right;
        float spannedWidth = borderSpacing * (ci.colspan - 1);
        for (size_t c = 0; c < ci.colspan; c++) spannedWidth += colWidths[ci.gridCol + c];
        if (cellFullW > spannedWidth) {
            float extra = (cellFullW - spannedWidth) / ci.colspan;
            for (size_t c = 0; c < ci.colspan; c++) colWidths[ci.gridCol + c] += extra;
        }
    }

    // Distribute remaining space proportionally
    float totalColWidth = 0;
    for (float w : colWidths) totalColWidth += w;
    float distributable = tableContentWidth - totalSpacing - totalColWidth;
    if (distributable > 0 && totalColWidth > 0) {
        for (size_t c = 0; c < numCols; c++) {
            colWidths[c] += distributable * (colWidths[c] / totalColWidth);
        }
    } else if (distributable > 0 && totalColWidth == 0) {
        float perCol = distributable / numCols;
        for (size_t c = 0; c < numCols; c++) colWidths[c] = perCol;
    }

    // Phase 2: Layout cells with final widths and determine row heights
    std::vector<float> rowHeights(numRows, 0.0f);

    for (auto& ci : cellInfos) {
        float cw = borderSpacing * (ci.colspan - 1);
        for (size_t c = 0; c < ci.colspan; c++) cw += colWidths[ci.gridCol + c];
        layoutNode(ci.node, cw, metrics);

        auto& cs = ci.node->computedStyle();
        const std::string& cellW = styleVal(cs, "width");
        if (cellW == "auto" || cellW.empty()) {
            ci.node->box.contentRect.width = cw -
                ci.node->box.padding.left - ci.node->box.padding.right -
                ci.node->box.border.left - ci.node->box.border.right -
                ci.node->box.margin.left - ci.node->box.margin.right;
            if (ci.node->box.contentRect.width < 0) ci.node->box.contentRect.width = 0;
        }

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

    // Layout top captions
    for (auto* cap : topCaptions) {
        layoutNode(cap, tableContentWidth, metrics);
        cap->box.contentRect.x = cap->box.margin.left + cap->box.padding.left + cap->box.border.left;
        cap->box.contentRect.y = cursorY + cap->box.margin.top + cap->box.padding.top + cap->box.border.top;
        cursorY += cap->box.fullHeight() + cap->box.margin.top + cap->box.margin.bottom;
    }

    std::vector<float> rowYPositions(numRows);
    for (size_t r = 0; r < numRows; r++) {
        cursorY += borderSpacing;
        rowYPositions[r] = cursorY;

        // Position the row node if it exists (only for original rows)
        if (r < rows.size() && rows[r].rowNode) {
            rows[r].rowNode->box.margin = {};
            rows[r].rowNode->box.padding = {};
            rows[r].rowNode->box.border = {};
            rows[r].rowNode->box.contentRect.x = 0;
            rows[r].rowNode->box.contentRect.y = cursorY;
            rows[r].rowNode->box.contentRect.width = tableContentWidth;
            rows[r].rowNode->box.contentRect.height = rowHeights[r];
        }

        cursorY += rowHeights[r];
    }
    cursorY += borderSpacing; // bottom spacing

    // Compute column X positions
    std::vector<float> colXPositions(numCols);
    {
        float cx = borderSpacing;
        for (size_t c = 0; c < numCols; c++) {
            colXPositions[c] = cx;
            cx += colWidths[c] + borderSpacing;
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
        if (!hasRowNode) {
            cellRelY = rowYPositions[ci.gridRow];
        }

        cell->box.contentRect.x = cellRelX + cell->box.margin.left +
            cell->box.padding.left + cell->box.border.left;
        cell->box.contentRect.y = cellRelY + cell->box.margin.top +
            cell->box.padding.top + cell->box.border.top;

        // Stretch cell height to total spanned row height and apply vertical-align
        float targetH = totalH - cell->box.margin.top - cell->box.margin.bottom -
            cell->box.padding.top - cell->box.padding.bottom -
            cell->box.border.top - cell->box.border.bottom;
        float contentH = cell->box.contentRect.height;
        if (targetH > contentH) {
            auto& cs = cell->computedStyle();
            const std::string& valign = styleVal(cs, "vertical-align");
            if (valign == "middle") {
                cell->box.contentRect.y += (targetH - contentH) / 2.0f;
            } else if (valign == "bottom") {
                cell->box.contentRect.y += (targetH - contentH);
            }
            // else top (default): no offset
            cell->box.contentRect.height = targetH;
        }
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
