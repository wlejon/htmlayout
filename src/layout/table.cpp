#include "layout/table.h"
#include "layout/table_structure.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

void layoutTable(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    float fontSize = resolveLength(styleVal(node, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    // Resolve container edges
    node->box.margin = resolveEdges(node, kMarginProps, availableWidth, fontSize);
    node->box.padding = resolveEdges(node, kPaddingProps, availableWidth, fontSize);

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
    float specW = resolveLength(styleVal(node, Prop::Width), availableWidth, fontSize);
    const std::string& widthVal = styleVal(node, Prop::Width);
    float availContent = availableWidth - marginH - paddingH - borderH;
    if (availContent < 0) availContent = 0;
    bool widthAuto = (widthVal == "auto" || widthVal.empty());
    float tableContentWidth;
    if (!widthAuto) {
        if (styleVal(node, Prop::BoxSizing) == "border-box") {
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
        float specH = resolveLength(styleVal(node, Prop::Height), 0, fontSize);
        const std::string& heightVal = styleVal(node, Prop::Height);
        if (heightVal != "auto" && !heightVal.empty()) {
            if (styleVal(node, Prop::BoxSizing) == "border-box") {
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

    // table-layout: fixed only takes effect on a table whose width is not
    // auto (CSS2 §17.5.2); cell content then never sizes a column.
    const bool fixedLayout = !widthAuto && styleVal(node, Prop::TableLayout) == "fixed";

    // If width is auto, shrink-to-fit: min(available, max-content), floored at min-content.
    if (fixedLayout) {
        // Sized below, from the columns alone.
    } else if (widthAuto) {
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
    if (fixedLayout) colWidths = fixedLayoutColumnWidths(ts, tableContentWidth);

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
            float cfs = resolveLength(styleVal(ci.node, Prop::FontSize), fontSize, fontSize);
            if (cfs <= 0) cfs = fontSize;
            Edges newPad = resolveEdges(ci.node, kPaddingProps, tableContentWidth, cfs);
            Edges newMar = resolveEdges(ci.node, kMarginProps, tableContentWidth, cfs);
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
                float borderH = ci.node->box.border.left + ci.node->box.border.right;
                float newContentW = std::max(0.0f,
                    layoutW - newPad.left - newPad.right - newMar.left - newMar.right - borderH);
                float prevOverride = ci.node->overrideContentWidth;
                ci.node->overrideContentWidth = newContentW;
                layoutNode(ci.node, tableContentWidth, metrics);
                ci.node->overrideContentWidth = prevOverride;
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
        if (styleVal(cap, Prop::CaptionSide) == "bottom") {
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
        const bool rtl = (styleVal(node, Prop::Direction) == "rtl");
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
        // start from the cell's pure flow height (the line-box stack), NOT
        // just the union of child boxes. The union undershoots for inline
        // content whose line boxes are taller than the boxes themselves (a
        // 14px inline-block on a 20px line flows 20px tall; Chromium centers
        // the line stack, and with text the union was off by the
        // half-leading). flowHeight, not naturalHeight: naturalHeight folds
        // in an explicit cell height, which would zero the shift entirely.
        // The child-box union below stays as a floor for overflowing content.
        float intrinsicH = std::max(0.0f, cell->box.flowHeight);
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
            const std::string& valign = styleVal(cell, Prop::VerticalAlign);
            float shiftY = 0.0f;
            if (valign == "middle") shiftY = availableH / 2.0f;
            else if (valign == "bottom") shiftY = availableH;
            // else top (default): no shift
            if (shiftY > 0.0f) shiftContentY(cell, shiftY);
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
        const std::string& marginLeftVal  = styleVal(node, Prop::MarginLeft);
        const std::string& marginRightVal = styleVal(node, Prop::MarginRight);
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

    float specH = resolveLength(styleVal(node, Prop::Height), 0, fontSize);
    const std::string& heightVal = styleVal(node, Prop::Height);
    if (heightVal != "auto" && !heightVal.empty()) {
        if (styleVal(node, Prop::BoxSizing) == "border-box") {
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
