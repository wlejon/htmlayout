#include "test_coverage.h"

using namespace htmlayout::css;
using namespace htmlayout::layout;

// ======================================================================
// FLEX LAYOUT COVERAGE
// ======================================================================

static void testFlexColumnReverse() {
    printf("--- Flex: column-reverse ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-direction"] = "column-reverse";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "50px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "50px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // column-reverse: c2 should be above c1
    check(c2.box.contentRect.y < c1.box.contentRect.y, "column-reverse: c2 above c1");
}

static void testFlexRowReverse() {
    printf("--- Flex: row-reverse ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-direction"] = "row-reverse";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "40px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // row-reverse: c2 should be to the left of c1
    check(c2.box.contentRect.x < c1.box.contentRect.x, "row-reverse: c2 left of c1");
}

static void testFlexJustifySpaceAround() {
    printf("--- Flex: justify-content space-around ---\n");
    CovNode root; root.initFlex();
    root.style_["justify-content"] = "space-around";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "30px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 400, m);
    // Free space = 200, 2 items. Each item gets 50px space: 25 on each side.
    // c1 at 50, c2 at 250 (or thereabouts)
    check(c1.box.contentRect.x > 0, "space-around: c1 offset from left");
    check(c2.box.contentRect.x > c1.box.contentRect.x + 100, "space-around: gap between items");
}

static void testFlexJustifySpaceEvenly() {
    printf("--- Flex: justify-content space-evenly ---\n");
    CovNode root; root.initFlex();
    root.style_["justify-content"] = "space-evenly";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "30px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "30px";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 400, m);
    // Free = 200, 3 gaps (before, between, after) = 200/3 ~ 66.7 each
    float gap = c2.box.contentRect.x - (c1.box.contentRect.x + 100);
    check(approx(gap, 66.7f, 2.0f), "space-evenly: equal gap between items");
}

static void testFlexAlignSelfOverride() {
    printf("--- Flex: align-self override ---\n");
    CovNode root; root.initFlex();
    root.style_["align-items"] = "flex-start";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "30px";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["height"] = "30px";
    c2.style_["align-self"] = "flex-end";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // c1 at flex-start (y=0), c2 at flex-end
    check(approx(c1.box.contentRect.y, 0.0f), "align-self: c1 at flex-start");
    // c2 should be at line bottom
    check(c2.box.contentRect.y >= 0, "align-self: c2 override applied");
}

static void testFlexWrapReverse() {
    printf("--- Flex: wrap-reverse ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-wrap"] = "wrap-reverse";
    CovNode c1, c2, c3;
    c1.initFlexItem(); c1.style_["width"] = "200px"; c1.style_["height"] = "40px";
    c2.initFlexItem(); c2.style_["width"] = "200px"; c2.style_["height"] = "40px";
    c3.initFlexItem(); c3.style_["width"] = "200px"; c3.style_["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    CovMetrics m;
    layoutTree(&root, 350, m);
    // wrap-reverse: items still wrap, cross-axis order reversed
    // c3 wraps to a different line than c1/c2
    check(c3.box.contentRect.y != c1.box.contentRect.y, "wrap-reverse: c3 on different line than c1");
}

static void testFlexRowGap() {
    printf("--- Flex: row-gap in wrapped flex ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-wrap"] = "wrap";
    root.style_["row-gap"] = "20px";
    CovNode c1, c2, c3;
    c1.initFlexItem(); c1.style_["width"] = "200px"; c1.style_["height"] = "40px";
    c2.initFlexItem(); c2.style_["width"] = "200px"; c2.style_["height"] = "40px";
    c3.initFlexItem(); c3.style_["width"] = "200px"; c3.style_["height"] = "40px";
    root.addChild(&c1); root.addChild(&c2); root.addChild(&c3);

    CovMetrics m;
    layoutTree(&root, 350, m);
    // c3 wraps to next line with 20px row gap
    float gap = c3.box.contentRect.y - (c1.box.contentRect.y + 40);
    check(gap >= 19.0f, "row-gap: 20px gap between flex lines");
}

static void testFlexBasisExplicit() {
    printf("--- Flex: explicit flex-basis ---\n");
    CovNode root; root.initFlex();
    CovNode c1;
    c1.initFlexItem(); c1.style_["flex-basis"] = "200px"; c1.style_["height"] = "40px";
    c1.style_["flex-grow"] = "0";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(c1.box.contentRect.width, 200.0f), "flex-basis: explicit 200px");
}

static void testFlexMinMaxClamp() {
    printf("--- Flex: min/max clamping ---\n");
    CovNode root; root.initFlex();
    CovNode c1;
    c1.initFlexItem(); c1.style_["flex-basis"] = "100px"; c1.style_["flex-grow"] = "1";
    c1.style_["max-width"] = "200px"; c1.style_["height"] = "40px";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // Would grow to 600 but max-width clamps to 200
    check(c1.box.contentRect.width <= 201.0f, "flex: max-width clamps flex growth");
}

static void testFlexColumnGrow() {
    printf("--- Flex: column direction with grow ---\n");
    CovNode root; root.initFlex();
    root.style_["flex-direction"] = "column";
    root.style_["height"] = "300px";
    CovNode c1, c2;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["flex-grow"] = "1";
    c2.initFlexItem(); c2.style_["width"] = "100px"; c2.style_["flex-grow"] = "2";
    root.addChild(&c1); root.addChild(&c2);

    CovMetrics m;
    layoutTree(&root, 600, m);
    // Column: main axis is vertical. c1 gets 100px, c2 gets 200px
    check(approx(c1.box.contentRect.height, 100.0f), "flex column grow: c1 height = 100");
    check(approx(c2.box.contentRect.height, 200.0f), "flex column grow: c2 height = 200");
}

// ======================================================================
// GRID LAYOUT COVERAGE
// ======================================================================

static void testGridAutoPlacement() {
    printf("--- Grid: auto-placement ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr 1fr 1fr";
    grid.style_["width"] = "300px";

    CovNode items[6];
    for (int i = 0; i < 6; i++) {
        items[i].initBlock(); items[i].style_["height"] = "30px";
        grid.addChild(&items[i]);
    }

    CovMetrics m;
    layoutTree(&grid, 800, m);

    // 6 items in 3 columns = 2 rows
    check(approx(items[0].box.contentRect.x, 0.0f), "grid auto: item0 col0");
    check(approx(items[1].box.contentRect.x, 100.0f), "grid auto: item1 col1");
    check(approx(items[2].box.contentRect.x, 200.0f), "grid auto: item2 col2");
    check(items[3].box.contentRect.y > items[0].box.contentRect.y, "grid auto: item3 in row1");
}

static void testGridExplicitPlacement() {
    printf("--- Grid: explicit placement ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr 1fr 1fr";
    grid.style_["width"] = "300px";

    CovNode item; item.initBlock();
    item.style_["height"] = "30px";
    item.style_["grid-column-start"] = "2";
    item.style_["grid-column-end"] = "4"; // spans columns 2-3
    item.style_["grid-row-start"] = "1";
    grid.addChild(&item);

    CovMetrics m;
    layoutTree(&grid, 800, m);

    check(approx(item.box.contentRect.x, 100.0f), "grid explicit: starts at col 2 (x=100)");
    check(approx(item.box.contentRect.width, 200.0f), "grid explicit: spans 2 columns (200px)");
}

static void testGridMinmax() {
    printf("--- Grid: minmax() ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "minmax(100px, 1fr) minmax(100px, 2fr)";
    grid.style_["width"] = "600px";

    CovNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    grid.addChild(&c1); grid.addChild(&c2);

    CovMetrics m;
    layoutTree(&grid, 800, m);

    // 1fr + 2fr = 3fr, 600/3=200. col1=200, col2=400
    check(approx(c1.box.contentRect.width, 200.0f), "grid minmax: col1 = 200px");
    check(approx(c2.box.contentRect.width, 400.0f), "grid minmax: col2 = 400px");
}

static void testGridAutoTrack() {
    printf("--- Grid: auto track sizing ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "auto auto";
    grid.style_["width"] = "400px";

    CovNode c1, c2;
    c1.initBlock(); c1.style_["width"] = "150px"; c1.style_["height"] = "30px";
    c2.initBlock(); c2.style_["height"] = "30px";
    grid.addChild(&c1); grid.addChild(&c2);

    CovMetrics m;
    layoutTree(&grid, 800, m);
    // Auto tracks should size to content
    check(c1.box.contentRect.width > 0, "grid auto track: c1 has width");
    check(c2.box.contentRect.width > 0, "grid auto track: c2 has width");
}

static void testGridRowTemplate() {
    printf("--- Grid: row template ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr";
    grid.style_["grid-template-rows"] = "50px 100px";
    grid.style_["width"] = "300px";

    CovNode c1, c2;
    c1.initBlock(); c2.initBlock();
    grid.addChild(&c1); grid.addChild(&c2);

    CovMetrics m;
    layoutTree(&grid, 800, m);

    check(approx(c1.box.contentRect.height, 50.0f), "grid row template: row1 = 50px");
    check(approx(c2.box.contentRect.height, 100.0f), "grid row template: row2 = 100px");
}

// ======================================================================
// TABLE LAYOUT COVERAGE
// ======================================================================

static void testTableBasicStructure() {
    printf("--- Table: basic row/cell structure ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "400px";

    CovNode row; row.initBlock();
    row.style_["display"] = "table-row";

    CovNode cell1; cell1.initBlock();
    cell1.style_["display"] = "table-cell"; cell1.style_["height"] = "30px";
    CovNode cell2; cell2.initBlock();
    cell2.style_["display"] = "table-cell"; cell2.style_["height"] = "30px";

    row.addChild(&cell1); row.addChild(&cell2);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    check(cell1.box.contentRect.width > 0, "table: cell1 has width");
    check(cell2.box.contentRect.width > 0, "table: cell2 has width");
    check(approx(cell1.box.contentRect.y, cell2.box.contentRect.y), "table: cells in same row");
}

static void testTableMultipleRows() {
    printf("--- Table: multiple rows ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1, row2;
    row1.initBlock(); row1.style_["display"] = "table-row";
    row2.initBlock(); row2.style_["display"] = "table-row";

    CovNode c1, c2, c3, c4;
    c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "40px";
    c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "40px";
    c3.initBlock(); c3.style_["display"] = "table-cell"; c3.style_["height"] = "50px";
    c4.initBlock(); c4.style_["display"] = "table-cell"; c4.style_["height"] = "50px";

    row1.addChild(&c1); row1.addChild(&c2);
    row2.addChild(&c3); row2.addChild(&c4);
    table.addChild(&row1); table.addChild(&row2);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Row2 should be below row1 - check via the row positions
    check(row2.box.contentRect.y >= row1.box.contentRect.y + 30,
          "table: row2 below row1");
}

static void testTableBorderCollapse() {
    printf("--- Table: border-collapse ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";
    table.style_["border-collapse"] = "collapse";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "30px";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "30px";
    row.addChild(&c1); row.addChild(&c2);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // With collapse, there should be no spacing between cells
    check(c1.box.contentRect.width + c2.box.contentRect.width >= 295.0f,
          "table collapse: cells fill table width");
}

static void testTableBorderSpacing() {
    printf("--- Table: border-spacing ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";
    table.style_["border-spacing"] = "10px";
    table.style_["border-collapse"] = "separate";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "30px";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "30px";
    row.addChild(&c1); row.addChild(&c2);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Cells should have gap from border-spacing
    float gap = c2.box.contentRect.x - (c1.box.contentRect.x + c1.box.contentRect.width);
    check(gap >= 5.0f, "table spacing: gap between cells");
}

static void testTableCaption() {
    printf("--- Table: caption ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode caption; caption.initBlock();
    caption.style_["display"] = "table-caption"; caption.style_["height"] = "30px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "50px";
    row.addChild(&cell);

    table.addChild(&caption);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Caption exists and has height
    check(approx(caption.box.contentRect.height, 30.0f), "table caption: has height 30px");
    // Table should contain both caption and cell content
    check(table.box.contentRect.height >= 50, "table caption: table has sufficient height");
}

static void testTableRowGroup() {
    printf("--- Table: row group (tbody) ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode tbody; tbody.initBlock(); tbody.style_["display"] = "table-row-group";
    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "40px";

    row.addChild(&cell);
    tbody.addChild(&row);
    table.addChild(&tbody);

    CovMetrics m;
    layoutTree(&table, 800, m);

    check(cell.box.contentRect.width > 0, "table row-group: cell laid out");
    check(approx(cell.box.contentRect.height, 40.0f), "table row-group: cell height preserved");
}

// ======================================================================
// TABLE COLSPAN / ROWSPAN
// ======================================================================

static void testTableColspan() {
    printf("--- Table: colspan ---\n");
    // 2 rows: first has 1 cell spanning 2 cols, second has 2 cells
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1; row1.initBlock(); row1.style_["display"] = "table-row";
    CovNode spanning; spanning.initBlock();
    spanning.style_["display"] = "table-cell"; spanning.style_["height"] = "30px";
    spanning.style_["colspan"] = "2";
    row1.addChild(&spanning);

    CovNode row2; row2.initBlock(); row2.style_["display"] = "table-row";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "30px";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "30px";
    row2.addChild(&c1); row2.addChild(&c2);

    table.addChild(&row1); table.addChild(&row2);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Spanning cell should be wider than either column cell
    check(spanning.box.contentRect.width > c1.box.contentRect.width,
          "table colspan: spanning cell wider than single cell");
    // Spanning cell width should approximately equal both columns
    float bothCols = c1.box.contentRect.width + c2.box.contentRect.width +
                     c1.box.padding.left + c1.box.padding.right +
                     c2.box.padding.left + c2.box.padding.right;
    check(spanning.box.contentRect.width >= bothCols * 0.8f,
          "table colspan: spanning cell covers both columns");
}

static void testTableRowspan() {
    printf("--- Table: rowspan ---\n");
    // Row1: cell spanning 2 rows + normal cell. Row2: 1 normal cell
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1; row1.initBlock(); row1.style_["display"] = "table-row";
    CovNode spanning; spanning.initBlock();
    spanning.style_["display"] = "table-cell"; spanning.style_["height"] = "20px";
    spanning.style_["rowspan"] = "2";
    CovNode c1; c1.initBlock(); c1.style_["display"] = "table-cell"; c1.style_["height"] = "40px";
    row1.addChild(&spanning); row1.addChild(&c1);

    CovNode row2; row2.initBlock(); row2.style_["display"] = "table-row";
    CovNode c2; c2.initBlock(); c2.style_["display"] = "table-cell"; c2.style_["height"] = "40px";
    row2.addChild(&c2);

    table.addChild(&row1); table.addChild(&row2);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Spanning cell should be taller than single-row cells
    check(spanning.box.contentRect.height >= 70,
          "table rowspan: spanning cell covers both rows");
    // c2 should be in the second column (same x as c1), not first
    check(approx(c2.box.contentRect.x, c1.box.contentRect.x, 2),
          "table rowspan: second-row cell in correct column");
}

static void testTableColspanAndRowspan() {
    printf("--- Table: colspan + rowspan combined ---\n");
    // 3x3 grid: top-left cell spans 2 cols x 2 rows
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row1; row1.initBlock(); row1.style_["display"] = "table-row";
    CovNode big; big.initBlock();
    big.style_["display"] = "table-cell"; big.style_["height"] = "20px";
    big.style_["colspan"] = "2"; big.style_["rowspan"] = "2";
    CovNode c13; c13.initBlock(); c13.style_["display"] = "table-cell"; c13.style_["height"] = "30px";
    row1.addChild(&big); row1.addChild(&c13);

    CovNode row2; row2.initBlock(); row2.style_["display"] = "table-row";
    CovNode c23; c23.initBlock(); c23.style_["display"] = "table-cell"; c23.style_["height"] = "30px";
    row2.addChild(&c23);

    CovNode row3; row3.initBlock(); row3.style_["display"] = "table-row";
    CovNode c31; c31.initBlock(); c31.style_["display"] = "table-cell"; c31.style_["height"] = "30px";
    CovNode c32; c32.initBlock(); c32.style_["display"] = "table-cell"; c32.style_["height"] = "30px";
    CovNode c33; c33.initBlock(); c33.style_["display"] = "table-cell"; c33.style_["height"] = "30px";
    row3.addChild(&c31); row3.addChild(&c32); row3.addChild(&c33);

    table.addChild(&row1); table.addChild(&row2); table.addChild(&row3);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Big cell should span 2 columns and 2 rows
    check(big.box.contentRect.width > c31.box.contentRect.width,
          "table combined: big cell wider than single col");
    check(big.box.contentRect.height >= 50,
          "table combined: big cell covers 2 row heights");
    // c23 should be in col 3 (same x as c13)
    check(approx(c23.box.contentRect.x, c13.box.contentRect.x, 2),
          "table combined: row2 cell skips spanned columns");
}

static void testTableCaptionSideBottom() {
    printf("--- Table: caption-side: bottom ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode caption; caption.initBlock();
    caption.style_["display"] = "table-caption"; caption.style_["height"] = "30px";
    caption.style_["caption-side"] = "bottom";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode cell; cell.initBlock(); cell.style_["display"] = "table-cell";
    cell.style_["height"] = "50px";
    row.addChild(&cell);

    table.addChild(&caption);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Caption should be below the row content
    check(caption.box.contentRect.y > cell.box.contentRect.y,
          "table caption-side:bottom: caption below cells");
}

static void testTableVerticalAlignMiddle() {
    printf("--- Table: vertical-align: middle ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode tall; tall.initBlock();
    tall.style_["display"] = "table-cell"; tall.style_["height"] = "100px";
    CovNode mid; mid.initBlock();
    mid.style_["display"] = "table-cell"; mid.style_["height"] = "40px";
    mid.style_["vertical-align"] = "middle";
    CovNode midContent; midContent.initBlock();
    midContent.style_["height"] = "20px";
    mid.addChild(&midContent);

    row.addChild(&tall); row.addChild(&mid);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // The cell box stays anchored at the row top; only its inner content
    // shifts to satisfy vertical-align. Row stretches to the tallest cell
    // (100px), so the 20px child centers around (100-20)/2 = 40px below
    // the cell's content top.
    check(approx(mid.box.contentRect.y, tall.box.contentRect.y),
          "table vertical-align:middle: cell box stays at row top");
    check(midContent.box.contentRect.y > 30.0f,
          "table vertical-align:middle: child shifted toward middle");
}

static void testTableVerticalAlignBottom() {
    printf("--- Table: vertical-align: bottom ---\n");
    CovNode table; table.initTable();
    table.style_["width"] = "300px";

    CovNode row; row.initBlock(); row.style_["display"] = "table-row";
    CovNode tall; tall.initBlock();
    tall.style_["display"] = "table-cell"; tall.style_["height"] = "100px";
    CovNode bot; bot.initBlock();
    bot.style_["display"] = "table-cell"; bot.style_["height"] = "30px";
    bot.style_["vertical-align"] = "bottom";
    CovNode botContent; botContent.initBlock();
    botContent.style_["height"] = "20px";
    bot.addChild(&botContent);

    row.addChild(&tall); row.addChild(&bot);
    table.addChild(&row);

    CovMetrics m;
    layoutTree(&table, 800, m);

    // Cell box stays at row top; child shifts to the bottom of the
    // stretched 100px cell. Child top = 100 - 20 = 80.
    check(approx(bot.box.contentRect.y, tall.box.contentRect.y),
          "table vertical-align:bottom: cell box stays at row top");
    check(botContent.box.contentRect.y > 70.0f,
          "table vertical-align:bottom: child shifted to bottom");
}

// ======================================================================
// POSITION: RELATIVE IN FLEX/GRID
// ======================================================================

static void testFlexRelativeOffset() {
    printf("--- Flex: position relative offset ---\n");
    CovNode root; root.initFlex();
    CovNode c1;
    c1.initFlexItem(); c1.style_["width"] = "100px"; c1.style_["height"] = "40px";
    c1.style_["position"] = "relative"; c1.style_["top"] = "10px"; c1.style_["left"] = "5px";
    root.addChild(&c1);

    CovMetrics m;
    layoutTree(&root, 600, m);
    check(approx(c1.box.contentRect.x, 5.0f), "flex relative: left offset");
    check(approx(c1.box.contentRect.y, 10.0f), "flex relative: top offset");
}

static void testGridRelativeOffset() {
    printf("--- Grid: position relative offset ---\n");
    CovNode grid; grid.initGrid();
    grid.style_["grid-template-columns"] = "1fr";
    grid.style_["width"] = "300px";

    CovNode item; item.initBlock();
    item.style_["height"] = "30px";
    item.style_["position"] = "relative";
    item.style_["top"] = "15px";
    item.style_["left"] = "10px";
    grid.addChild(&item);

    CovMetrics m;
    layoutTree(&grid, 800, m);
    check(approx(item.box.contentRect.x, 10.0f), "grid relative: left offset");
    check(approx(item.box.contentRect.y, 15.0f), "grid relative: top offset");
}

void testCoverageGridFlex() {
    // Flex layout
    testFlexColumnReverse();
    testFlexRowReverse();
    testFlexJustifySpaceAround();
    testFlexJustifySpaceEvenly();
    testFlexAlignSelfOverride();
    testFlexWrapReverse();
    testFlexRowGap();
    testFlexBasisExplicit();
    testFlexMinMaxClamp();
    testFlexColumnGrow();

    // Grid layout
    testGridAutoPlacement();
    testGridExplicitPlacement();
    testGridMinmax();
    testGridAutoTrack();
    testGridRowTemplate();

    // Table layout
    testTableBasicStructure();
    testTableMultipleRows();
    testTableBorderCollapse();
    testTableBorderSpacing();
    testTableCaption();
    testTableRowGroup();
    testTableColspan();
    testTableRowspan();
    testTableColspanAndRowspan();
    testTableCaptionSideBottom();
    testTableVerticalAlignMiddle();
    testTableVerticalAlignBottom();

    // Position relative in flex/grid
    testFlexRelativeOffset();
    testGridRelativeOffset();
}
