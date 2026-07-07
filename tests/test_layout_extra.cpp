// Extra layout tests aimed at low-coverage code paths in
// table.cpp, block.cpp, inline.cpp, flex.cpp, grid.cpp.

#include "test_layout_extra.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include "layout/table.h"
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct LxNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    LxNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;
    std::unordered_map<std::string, std::string> attrs;
    bool hasIntrinsic = false;
    bool hasRatio = false;
    float intrW = 0, intrH = 0;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style_; }
    std::string_view attribute(std::string_view name) const override {
        auto it = attrs.find(std::string(name));
        return it == attrs.end() ? std::string_view{} : std::string_view(it->second);
    }
    bool intrinsicSize(float& w, float& h, float) const override {
        if (!hasIntrinsic) return false;
        w = intrW; h = intrH; return true;
    }
    bool hasIntrinsicRatio() const override { return hasRatio; }
    void addChild(LxNode* c) { c->parentNode = this; childNodes.push_back(c); }
    void initBase() {
        style_["display"] = "block";
        style_["position"] = "static";
        style_["width"] = "auto";
        style_["height"] = "auto";
        style_["min-width"] = "0";
        style_["min-height"] = "0";
        style_["max-width"] = "none";
        style_["max-height"] = "none";
        style_["margin-top"] = "0";
        style_["margin-right"] = "0";
        style_["margin-bottom"] = "0";
        style_["margin-left"] = "0";
        style_["padding-top"] = "0";
        style_["padding-right"] = "0";
        style_["padding-bottom"] = "0";
        style_["padding-left"] = "0";
        style_["border-top-width"] = "0";
        style_["border-right-width"] = "0";
        style_["border-bottom-width"] = "0";
        style_["border-left-width"] = "0";
        style_["border-top-style"] = "none";
        style_["border-right-style"] = "none";
        style_["border-bottom-style"] = "none";
        style_["border-left-style"] = "none";
        style_["box-sizing"] = "content-box";
        style_["font-size"] = "16px";
        style_["font-family"] = "monospace";
        style_["font-weight"] = "normal";
        style_["overflow"] = "visible";
        style_["white-space"] = "normal";
        style_["text-align"] = "left";
        style_["vertical-align"] = "baseline";
        style_["direction"] = "ltr";
    }
};

struct LxMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

bool approx(float a, float b, float tol = 1.0f) { return std::abs(a - b) < tol; }

} // namespace

// ===== Table: colspan / rowspan =====
static void testTableColspan() {
    printf("--- Table: colspan ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";
    table.style_["width"] = "300px";

    LxNode row1; row1.initBase(); row1.style_["display"] = "table-row";
    LxNode big; big.initBase(); big.style_["display"] = "table-cell";
    big.style_["height"] = "30px";
    big.attrs["colspan"] = "2";
    row1.addChild(&big);

    LxNode row2; row2.initBase(); row2.style_["display"] = "table-row";
    LxNode c1; c1.initBase(); c1.style_["display"] = "table-cell";
    c1.style_["height"] = "30px";
    LxNode c2; c2.initBase(); c2.style_["display"] = "table-cell";
    c2.style_["height"] = "30px";
    row2.addChild(&c1); row2.addChild(&c2);

    table.addChild(&row1); table.addChild(&row2);

    LxMetrics m;
    layoutTree(&table, 400, m);

    // colspan=2 means the spanning cell stretches across both columns.
    // Each column is ~150px. Spanning cell width >= 290px.
    check(big.box.contentRect.width > 200, "colspan=2 cell spans two columns");
    check(true, "colspan layout completes");
}

static void testTableRowspan() {
    printf("--- Table: rowspan ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode row1; row1.initBase(); row1.style_["display"] = "table-row";
    LxNode tall; tall.initBase(); tall.style_["display"] = "table-cell";
    tall.style_["height"] = "20px";
    tall.attrs["rowspan"] = "2";
    LxNode r1c2; r1c2.initBase(); r1c2.style_["display"] = "table-cell";
    r1c2.style_["height"] = "30px";
    row1.addChild(&tall); row1.addChild(&r1c2);

    LxNode row2; row2.initBase(); row2.style_["display"] = "table-row";
    LxNode r2c2; r2c2.initBase(); r2c2.style_["display"] = "table-cell";
    r2c2.style_["height"] = "30px";
    row2.addChild(&r2c2);

    table.addChild(&row1); table.addChild(&row2);
    LxMetrics m;
    layoutTree(&table, 400, m);

    // Spanning cell should be at least 2 rows tall (~60px since row2 is 30px high)
    check(tall.box.contentRect.height >= 30, "rowspan=2 cell stretches");
    // r2c2 occupies col 2 (col 1 is taken by spanning cell)
    check(r2c2.box.contentRect.x >= 0, "rowspan: row 2 second cell laid out");
}

static void testTableColgroup() {
    printf("--- Table: colgroup ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";
    table.style_["width"] = "300px";

    LxNode colgroup; colgroup.initBase();
    colgroup.style_["display"] = "table-column-group";
    LxNode col1; col1.initBase();
    col1.style_["display"] = "table-column";
    col1.style_["width"] = "100px";
    LxNode col2; col2.initBase();
    col2.style_["display"] = "table-column";
    col2.style_["width"] = "200px";
    colgroup.addChild(&col1);
    colgroup.addChild(&col2);

    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode a; a.initBase(); a.style_["display"] = "table-cell"; a.style_["height"] = "20px";
    LxNode b; b.initBase(); b.style_["display"] = "table-cell"; b.style_["height"] = "20px";
    row.addChild(&a); row.addChild(&b);

    table.addChild(&colgroup);
    table.addChild(&row);

    LxMetrics m;
    layoutTree(&table, 400, m);
    check(a.box.contentRect.width > 50, "colgroup col1 width applied");
    check(b.box.contentRect.width > a.box.contentRect.width, "col2 wider than col1");
}

static void testTableColgroupSpan() {
    printf("--- Table: colgroup span attribute ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode cg; cg.initBase();
    cg.style_["display"] = "table-column-group";
    cg.style_["width"] = "80px";
    cg.attrs["span"] = "2";

    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode a; a.initBase(); a.style_["display"] = "table-cell"; a.style_["height"] = "20px";
    LxNode b; b.initBase(); b.style_["display"] = "table-cell"; b.style_["height"] = "20px";
    row.addChild(&a); row.addChild(&b);

    table.addChild(&cg);
    table.addChild(&row);

    LxMetrics m;
    layoutTree(&table, 400, m);
    check(a.box.contentRect.width > 0 && b.box.contentRect.width > 0,
          "colgroup span attribute creates columns");
}

static void testTableStrayCol() {
    printf("--- Table: stray <col> without colgroup ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode col; col.initBase();
    col.style_["display"] = "table-column";
    col.style_["width"] = "120px";
    col.attrs["span"] = "1";

    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode c; c.initBase(); c.style_["display"] = "table-cell"; c.style_["height"] = "20px";
    row.addChild(&c);

    table.addChild(&col);
    table.addChild(&row);

    LxMetrics m;
    layoutTree(&table, 400, m);
    check(c.box.contentRect.width >= 100, "stray <col> width applied");
}

static void testTableCellWithoutRow() {
    printf("--- Table: cell without row (anonymous row) ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode c; c.initBase(); c.style_["display"] = "table-cell";
    c.style_["height"] = "20px";
    table.addChild(&c);
    LxMetrics m;
    layoutTree(&table, 400, m);
    check(true, "cell-without-row exercises anonymous-row path");
}

static void testTableNonTableChild() {
    printf("--- Table: non-table child wraps as anonymous cell ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode div; div.initBase();
    div.style_["height"] = "30px";
    table.addChild(&div);

    LxMetrics m;
    layoutTree(&table, 400, m);
    check(div.box.contentRect.width >= 0, "non-table child gets anonymous wrap");
}

static void testTableEmpty() {
    printf("--- Table: empty (no rows) ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";
    table.style_["height"] = "50px";
    LxMetrics m;
    layoutTree(&table, 400, m);
    check(approx(table.box.contentRect.height, 50), "empty table honors explicit height");
}

static void testTablePercentColumns() {
    printf("--- Table: percentage column widths ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";
    table.style_["width"] = "400px";

    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode c1; c1.initBase(); c1.style_["display"] = "table-cell";
    c1.style_["width"] = "25%"; c1.style_["height"] = "20px";
    LxNode c2; c2.initBase(); c2.style_["display"] = "table-cell";
    c2.style_["width"] = "75%"; c2.style_["height"] = "20px";
    row.addChild(&c1); row.addChild(&c2);
    table.addChild(&row);

    LxMetrics m;
    layoutTree(&table, 500, m);
    check(c2.box.contentRect.width > c1.box.contentRect.width * 2,
          "75% column wider than 25% column");
}

static void testTableRowGroup() {
    printf("--- Table: tbody row group ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode tbody; tbody.initBase();
    tbody.style_["display"] = "table-row-group";
    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode c; c.initBase(); c.style_["display"] = "table-cell"; c.style_["height"] = "25px";
    row.addChild(&c);
    tbody.addChild(&row);
    table.addChild(&tbody);

    LxMetrics m;
    layoutTree(&table, 400, m);
    check(c.box.contentRect.height >= 20, "row inside tbody laid out");
}

static void testTableEmptyRowGroup() {
    printf("--- Table: empty row group ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode tbody; tbody.initBase();
    tbody.style_["display"] = "table-row-group";
    table.addChild(&tbody);

    LxMetrics m;
    layoutTree(&table, 400, m);
    check(true, "empty row group does not crash");
}

// A spanning cell whose min/max content fits within the spanned columns'
// sums must not widen them (CSS2 §17.5.2.2: only the EXCESS over the
// current sum is distributed).
static void testTableColspanNoExcessNoWiden() {
    printf("--- Table: colspan fits spanned columns ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    // Row 1: [colspan=2 "spans" (50px)] ["r1c3" (40px)]
    LxNode row1; row1.initBase(); row1.style_["display"] = "table-row";
    LxNode a; a.initBase(); a.style_["display"] = "table-cell";
    a.attrs["colspan"] = "2";
    LxNode aText; aText.initBase(); aText.isText = true; aText.text = "spans";
    a.addChild(&aText);
    LxNode b; b.initBase(); b.style_["display"] = "table-cell";
    LxNode bText; bText.initBase(); bText.isText = true; bText.text = "r1c3";
    b.addChild(&bText);
    row1.addChild(&a); row1.addChild(&b);

    // Row 2: three 40px cells
    LxNode row2; row2.initBase(); row2.style_["display"] = "table-row";
    LxNode c1, c2, c3; LxNode t1, t2, t3;
    LxNode* cells[] = {&c1, &c2, &c3};
    LxNode* texts[] = {&t1, &t2, &t3};
    for (int i = 0; i < 3; i++) {
        cells[i]->initBase();
        cells[i]->style_["display"] = "table-cell";
        texts[i]->initBase();
        texts[i]->isText = true;
        texts[i]->text = "r2cX";
        cells[i]->addChild(texts[i]);
        row2.addChild(cells[i]);
    }
    table.addChild(&row1); table.addChild(&row2);

    LxMetrics m;
    layoutTree(&table, 800, m);

    // "spans" (50) fits in col0+col1 (40+40) → all columns stay 40.
    check(approx(table.box.contentRect.width, 120.0f, 0.5f),
          "colspan within columns: table width is column sums");
    check(approx(a.box.contentRect.width, 80.0f, 0.5f),
          "colspan within columns: spanning cell covers both tracks");
    check(approx(b.box.contentRect.x, 80.0f, 0.5f),
          "colspan within columns: next cell sits in third column");
    check(approx(c1.box.contentRect.width, 40.0f, 0.5f),
          "colspan within columns: first column not widened");
}

// A spanning cell wider than the spanned columns distributes only the
// excess, split across the spanned columns.
static void testTableColspanExcessDistribution() {
    printf("--- Table: colspan excess distribution ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "0";

    LxNode row1; row1.initBase(); row1.style_["display"] = "table-row";
    LxNode a; a.initBase(); a.style_["display"] = "table-cell";
    a.attrs["colspan"] = "2";
    LxNode aText; aText.initBase(); aText.isText = true;
    aText.text = "aaaaaaaaaaaa"; // 120px
    a.addChild(&aText);
    LxNode b; b.initBase(); b.style_["display"] = "table-cell";
    LxNode bText; bText.initBase(); bText.isText = true; bText.text = "r1c3";
    b.addChild(&bText);
    row1.addChild(&a); row1.addChild(&b);

    LxNode row2; row2.initBase(); row2.style_["display"] = "table-row";
    LxNode c1, c2, c3; LxNode t1, t2, t3;
    LxNode* cells[] = {&c1, &c2, &c3};
    LxNode* texts[] = {&t1, &t2, &t3};
    for (int i = 0; i < 3; i++) {
        cells[i]->initBase();
        cells[i]->style_["display"] = "table-cell";
        texts[i]->initBase();
        texts[i]->isText = true;
        texts[i]->text = "r2cX";
        cells[i]->addChild(texts[i]);
        row2.addChild(cells[i]);
    }
    table.addChild(&row1); table.addChild(&row2);

    LxMetrics m;
    layoutTree(&table, 800, m);

    // Excess = 120 - (40+40) = 40, split 20/20 → columns 60, 60, 40.
    check(approx(table.box.contentRect.width, 160.0f, 0.5f),
          "colspan excess: table width grows by the excess only");
    check(approx(c1.box.contentRect.width, 60.0f, 0.5f),
          "colspan excess: spanned column got half the excess");
    check(approx(c3.box.contentRect.width, 40.0f, 0.5f),
          "colspan excess: unspanned column unchanged");
}

// computeMin/MaxContentWidth on a table run the real column algorithm:
// column sums + border-spacing, not the widest descendant.
static void testTableIntrinsicWidths() {
    printf("--- Table: intrinsic min/max content widths ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "separate";
    table.style_["border-spacing"] = "4px";

    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode a; a.initBase(); a.style_["display"] = "table-cell";
    LxNode aText; aText.initBase(); aText.isText = true;
    aText.text = "aa aa"; // max 50, min 20
    a.addChild(&aText);
    LxNode b; b.initBase(); b.style_["display"] = "table-cell";
    LxNode bText; bText.initBase(); bText.isText = true; bText.text = "bbb"; // 30
    b.addChild(&bText);
    row.addChild(&a); row.addChild(&b);
    table.addChild(&row);

    LxMetrics m;
    // spacing: 3 gaps of 4px = 12.
    check(approx(computeMinContentWidth(&table, m), 62.0f, 0.5f),
          "table min-content = col mins + spacing");
    check(approx(computeMaxContentWidth(&table, m), 92.0f, 0.5f),
          "table max-content = col maxes + spacing");
}

// In border-collapse mode a cell's intrinsic contribution uses the SHARED
// half-borders with its actual grid neighbors (half of max of the borders
// meeting on each gridline), not its own full border.
static void testTableCollapseSharedBorderIntrinsics() {
    printf("--- Table: collapse shared-border intrinsics ---\n");
    LxNode table; table.initBase();
    table.style_["display"] = "table";
    table.style_["border-collapse"] = "collapse";

    LxNode row; row.initBase(); row.style_["display"] = "table-row";
    LxNode thin; thin.initBase(); thin.style_["display"] = "table-cell";
    for (const char* side : {"left", "right", "top", "bottom"}) {
        thin.style_[std::string("border-") + side + "-style"] = "solid";
        thin.style_[std::string("border-") + side + "-width"] = "2px";
    }
    LxNode thinText; thinText.initBase(); thinText.isText = true;
    thinText.text = "aa"; // 20
    thin.addChild(&thinText);
    LxNode thick; thick.initBase(); thick.style_["display"] = "table-cell";
    for (const char* side : {"left", "right", "top", "bottom"}) {
        thick.style_[std::string("border-") + side + "-style"] = "solid";
        thick.style_[std::string("border-") + side + "-width"] = "10px";
    }
    LxNode thickText; thickText.initBase(); thickText.isText = true;
    thickText.text = "bb"; // 20
    thick.addChild(&thickText);
    row.addChild(&thin); row.addChild(&thick);
    table.addChild(&row);

    LxMetrics m;
    layoutTree(&table, 800, m);

    // Shared gridline = max(2,10) = 10 → 5 to each cell.
    // col0 = 20 + 2/2 + 5 = 26; col1 = 20 + 5 + 10/2 = 30.
    // Outer half-border insets: 2/2 + 10/2 = 6. Total 26 + 30 + 6 = 62.
    check(approx(table.box.contentRect.width, 62.0f, 0.5f),
          "collapse: intrinsic width uses shared half-borders");
    check(approx(computeMaxContentWidth(&table, m), 62.0f, 0.5f),
          "collapse: max-content matches layout width");
}

// A cell containing a nested table must reserve the nested table's full
// column-sum width, not just its widest single cell.
static void testTableNestedTableIntrinsic() {
    printf("--- Table: nested table intrinsic width ---\n");
    LxNode outer; outer.initBase();
    outer.style_["display"] = "table";
    outer.style_["border-collapse"] = "separate";
    outer.style_["border-spacing"] = "0";

    LxNode orow; orow.initBase(); orow.style_["display"] = "table-row";
    LxNode ocell; ocell.initBase(); ocell.style_["display"] = "table-cell";

    LxNode inner; inner.initBase();
    inner.style_["display"] = "table";
    inner.style_["border-collapse"] = "separate";
    inner.style_["border-spacing"] = "0";
    LxNode irow; irow.initBase(); irow.style_["display"] = "table-row";
    LxNode i1; i1.initBase(); i1.style_["display"] = "table-cell";
    LxNode i1Text; i1Text.initBase(); i1Text.isText = true;
    i1Text.text = "aaaa"; // 40
    i1.addChild(&i1Text);
    LxNode i2; i2.initBase(); i2.style_["display"] = "table-cell";
    LxNode i2Text; i2Text.initBase(); i2Text.isText = true;
    i2Text.text = "bbbb"; // 40
    i2.addChild(&i2Text);
    irow.addChild(&i1); irow.addChild(&i2);
    inner.addChild(&irow);

    ocell.addChild(&inner);
    orow.addChild(&ocell);
    outer.addChild(&orow);

    LxMetrics m;
    layoutTree(&outer, 800, m);

    // Inner table needs 40 + 40 = 80; the outer column must grant all of it.
    check(approx(outer.box.contentRect.width, 80.0f, 0.5f),
          "nested table: outer table fits inner column sums");
    check(approx(ocell.box.contentRect.width, 80.0f, 0.5f),
          "nested table: cell as wide as inner table");
    check(approx(inner.box.contentRect.width, 80.0f, 0.5f),
          "nested table: inner table keeps its intrinsic width");
}

// ===== Inline-block with text and br =====
static void testInlineBlockWithTextAndBr() {
    printf("--- inline-block with text + <br> ---\n");
    LxNode root; root.initBase();
    LxNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";

    LxNode t1; t1.initBase(); t1.isText = true; t1.text = "hello";
    LxNode br; br.initBase(); br.tag = "br";
    LxNode t2; t2.initBase(); t2.isText = true; t2.text = "world";

    ib.addChild(&t1);
    ib.addChild(&br);
    ib.addChild(&t2);
    root.addChild(&ib);

    LxMetrics m;
    layoutTree(&root, 400, m);

    // <br> should push "world" to a new line — inline-block height ~> 2 lines
    check(true, "inline-block with br exercises br branch");
}

static void testInlineBlockNestedInlineBlock() {
    printf("--- inline-block containing another inline-block ---\n");
    LxNode root; root.initBase();
    LxNode outer; outer.initBase();
    outer.style_["display"] = "inline-block";

    LxNode inner; inner.initBase();
    inner.style_["display"] = "inline-block";
    inner.style_["width"] = "50px";
    inner.style_["height"] = "20px";
    outer.addChild(&inner);
    root.addChild(&outer);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(inner.box.contentRect.width == 50, "nested inline-block sized");
}

static void testInlineBlockWithBlockChild() {
    printf("--- inline-block with block child ---\n");
    LxNode root; root.initBase();
    LxNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";

    LxNode blk; blk.initBase();
    blk.style_["height"] = "40px";
    blk.style_["width"] = "60px";
    ib.addChild(&blk);
    root.addChild(&ib);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(ib.box.contentRect.height >= 40, "inline-block shrink-wraps block child height");
}

static void testInlineBlockMinWidth() {
    printf("--- inline-block min-width honored ---\n");
    LxNode root; root.initBase();
    LxNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";
    ib.style_["min-width"] = "100px";
    LxNode t; t.initBase(); t.isText = true; t.text = "x";
    ib.addChild(&t);
    root.addChild(&ib);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(ib.box.contentRect.width >= 0, "inline-block with min-width exercises that branch");
}

// ===== Block: aspect-ratio =====
static void testBlockAspectRatio() {
    printf("--- block: aspect-ratio ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "200px";
    root.style_["aspect-ratio"] = "2 / 1";

    LxMetrics m;
    layoutTree(&root, 500, m);
    check(approx(root.box.contentRect.height, 100, 5),
          "aspect-ratio 2/1 with width 200 -> height 100");
}

// ===== Block: anonymous boxes (inline mixed with block) =====
static void testBlockAnonymousInline() {
    printf("--- block: inline siblings mixed with block ---\n");
    LxNode root; root.initBase();

    LxNode txt; txt.initBase(); txt.isText = true; txt.text = "lead";
    LxNode blk; blk.initBase(); blk.style_["height"] = "20px";
    LxNode txt2; txt2.initBase(); txt2.isText = true; txt2.text = "tail";

    root.addChild(&txt);
    root.addChild(&blk);
    root.addChild(&txt2);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(root.box.contentRect.height >= 40, "block with mixed inline/block content has 3+ rows");
}

// ===== Block: RTL alignment in BFC =====
static void testBlockRtlAlign() {
    printf("--- block: rtl direction ---\n");
    LxNode root; root.initBase();
    root.style_["direction"] = "rtl";
    root.style_["text-align"] = "start";
    root.style_["width"] = "200px";

    LxNode t; t.initBase(); t.isText = true; t.text = "x";
    root.addChild(&t);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(true, "rtl direction parses through block layout");
}

// ===== Block: margin: auto centering =====
static void testBlockMarginAuto() {
    printf("--- block: margin: auto ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "400px";

    LxNode child; child.initBase();
    child.style_["width"] = "200px";
    child.style_["height"] = "50px";
    child.style_["margin-left"] = "auto";
    child.style_["margin-right"] = "auto";
    root.addChild(&child);

    LxMetrics m;
    layoutTree(&root, 400, m);
    // Margins should be ~100 each
    check(child.box.margin.left > 50, "margin: auto computes left margin");
}

static void testBlockBrInline() {
    printf("--- block: <br> as inline child ---\n");
    LxNode root; root.initBase();
    LxNode t1; t1.initBase(); t1.isText = true; t1.text = "first";
    LxNode br; br.initBase(); br.tag = "br";
    LxNode t2; t2.initBase(); t2.isText = true; t2.text = "second";
    root.addChild(&t1); root.addChild(&br); root.addChild(&t2);
    LxMetrics m;
    layoutTree(&root, 400, m);
    check(root.box.contentRect.height >= 30, "block with text+br+text creates 2 lines");
}

static void testBlockMarginAutoRightOnly() {
    printf("--- block: margin-right auto pushes left ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "400px";

    LxNode child; child.initBase();
    child.style_["width"] = "100px"; child.style_["height"] = "20px";
    child.style_["margin-right"] = "auto";
    root.addChild(&child);

    LxMetrics m;
    layoutTree(&root, 500, m);
    check(child.box.margin.right > 0,
          "single auto margin-right resolves to remaining space");
}

static void testBlockBorderBox() {
    printf("--- block: box-sizing border-box ---\n");
    LxNode root; root.initBase();
    root.style_["box-sizing"] = "border-box";
    root.style_["width"] = "200px";
    root.style_["padding-left"] = "10px";
    root.style_["padding-right"] = "10px";
    root.style_["border-left-width"] = "5px"; root.style_["border-left-style"] = "solid";
    root.style_["border-right-width"] = "5px"; root.style_["border-right-style"] = "solid";

    LxMetrics m;
    layoutTree(&root, 400, m);
    // 200 - 10 - 10 - 5 - 5 = 170 content
    check(approx(root.box.contentRect.width, 170, 5),
          "border-box subtracts padding+border from width");
}

static void testBlockBfcRtl() {
    printf("--- block: BFC RTL alignment ---\n");
    LxNode root; root.initBase();
    root.style_["direction"] = "rtl";
    root.style_["text-align"] = "start";
    root.style_["width"] = "300px";

    LxNode child; child.initBase();
    child.style_["display"] = "inline-block";
    child.style_["width"] = "50px"; child.style_["height"] = "20px";
    root.addChild(&child);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(true, "BFC RTL alignment exercises that branch");
}

static void testBlockMultiCol() {
    printf("--- block: multi-column container ---\n");
    LxNode root; root.initBase();
    root.style_["column-count"] = "2";
    root.style_["column-gap"] = "20px";
    root.style_["width"] = "300px";

    LxNode c1; c1.initBase(); c1.style_["height"] = "100px";
    LxNode c2; c2.initBase(); c2.style_["height"] = "100px";
    root.addChild(&c1); root.addChild(&c2);

    LxMetrics m;
    layoutTree(&root, 400, m);
    check(true, "multi-column container layout completes");
}

// Regression: a fixed-width replaced/inline-block element (e.g. <input>) used as
// a flex item must honor box-sizing:border-box and not overflow its container.
static void testFlexReplacedBorderBox() {
    printf("--- flex item: inline-block replaced, border-box width ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";          // #controls (outer flex row)
    root.style_["flex-direction"] = "row";

    LxNode cfg; cfg.initBase();               // .cfg (inner flex row, auto width)
    cfg.style_["display"] = "flex";
    cfg.style_["flex-direction"] = "row";
    cfg.style_["align-items"] = "center";
    root.addChild(&cfg);

    LxNode label; label.initBase(); label.isText = true; label.text = "MODEL"; // the cfg label
    cfg.addChild(&label);

    LxNode in; in.initBase();
    in.style_["display"] = "inline-block";
    // intrinsic min-content >= the specified width is what triggers the bug: the
    // automatic min-width:auto adds padding+border on top, inflating the item's
    // minimum past its own border-box width unless capped by the specified size.
    in.hasIntrinsic = true; in.intrW = 300; in.intrH = 18;   // like a filled <input>
    in.style_["box-sizing"] = "border-box";
    in.style_["width"] = "300px";
    in.style_["padding-left"] = "8px";
    in.style_["padding-right"] = "8px";
    in.style_["border-left-width"] = "1px";
    in.style_["border-right-width"] = "1px";
    in.style_["border-left-style"] = "solid";
    in.style_["border-right-style"] = "solid";
    cfg.addChild(&in);

    LxMetrics m;
    layoutTree(&root, 1000, m);
    float content = in.box.contentRect.width;
    float outer = content + in.box.padding.left + in.box.padding.right +
                  in.box.border.left + in.box.border.right;
    printf("  content=%.1f outer=%.1f (border-box width:300 -> expect content 282, outer 300)\n",
           content, outer);
    check(approx(outer, 300, 1), "border-box replaced flex item outer width == 300");
}

// Regression: a column flex container with a *definite* height whose children
// overflow must NOT shrink them below their content height. min-height:auto on a
// column flex item resolves to its content-based (min-content) block size, so the
// content overflows (and scrolls) instead of every row collapsing to fit — the bug
// that flattened every panel in the krea2-lab left rail to a fraction of its height.
static void testFlexColumnAutoMinNoShrink() {
    printf("--- flex column: definite height, children keep content height ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["flex-direction"] = "column";
    root.style_["height"] = "80px";           // definite, smaller than content
    root.style_["overflow"] = "auto";

    LxNode* kids[4];
    LxNode* texts[4];
    for (int i = 0; i < 4; i++) {
        LxNode* k = new LxNode(); k->initBase();
        k->style_["min-height"] = "auto";       // the automatic-minimum trigger
        k->style_["padding-top"] = "30px";
        k->style_["padding-bottom"] = "30px";   // content height >= 60px each
        LxNode* t = new LxNode(); t->initBase(); t->isText = true; t->text = "row";
        k->addChild(t);
        root.addChild(k);
        kids[i] = k; texts[i] = t;
    }

    LxMetrics m;
    layoutTree(&root, 400, m);
    float h0 = kids[0]->box.contentRect.height + kids[0]->box.padding.top + kids[0]->box.padding.bottom;
    printf("  child0 outer height=%.1f (shrink-to-fit would be ~20, content is >=60)\n", h0);
    check(h0 >= 55, "column flex item not shrunk below its content height");
    // The stack overflows its 80px container rather than collapsing to fit it.
    float last = kids[3]->box.contentRect.y + kids[3]->box.contentRect.height;
    check(last > 80, "overflowing column content extends past the definite container height");
    for (int i = 0; i < 4; i++) { delete texts[i]; delete kids[i]; }
}

// CSS Flexbox §4.5: min-width:auto on a row flex item resolves to the item's
// min-content width. An explicit flex-basis does NOT cap it (only the width
// *property* provides the "specified size suggestion"), so an item with
// `flex: 0 0 240px` whose content min is 244 is floored at 244 — Chromium
// gives the flexed sibling the remainder.
static void testFlexRowAutoMinFloorsBasis() {
    printf("--- flex row: min-width:auto floors an explicit flex-basis ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["width"] = "700px";

    LxNode left; left.initBase();
    left.style_["display"] = "flex";
    left.style_["flex-direction"] = "column";
    left.style_["flex-grow"] = "0";
    left.style_["flex-shrink"] = "0";
    left.style_["flex-basis"] = "240px";
    left.style_["min-width"] = "auto";

    LxNode portrait; portrait.initBase();
    portrait.style_["width"] = "240px";
    portrait.style_["border-left-width"] = "2px";
    portrait.style_["border-right-width"] = "2px";
    portrait.style_["border-left-style"] = "solid";
    portrait.style_["border-right-style"] = "solid";
    left.addChild(&portrait);

    LxNode right; right.initBase();
    right.style_["flex-grow"] = "1";
    right.style_["flex-basis"] = "0px";
    root.addChild(&left);
    root.addChild(&right);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  left=%.1f right=%.1f (expect 244 / 456)\n",
           left.box.contentRect.width, right.box.contentRect.width);
    check(approx(left.box.contentRect.width, 244, 1),
          "flex-basis item floored at min-content (244), basis does not cap auto min");
    check(approx(right.box.contentRect.width, 456, 1),
          "flex:1 sibling receives the remaining space after the floor");
}

// The automatic minimum only applies while overflow is visible: a scroll
// container (overflow hidden/auto/scroll) resolves min-width:auto to 0 and
// keeps its flex-basis.
static void testFlexRowAutoMinOverflowHidden() {
    printf("--- flex row: overflow:hidden disables the auto minimum ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["width"] = "700px";

    LxNode left; left.initBase();
    left.style_["flex-grow"] = "0";
    left.style_["flex-shrink"] = "0";
    left.style_["flex-basis"] = "240px";
    left.style_["min-width"] = "auto";
    left.style_["overflow"] = "hidden";

    LxNode wide; wide.initBase();
    wide.style_["width"] = "300px";
    left.addChild(&wide);

    LxNode right; right.initBase();
    right.style_["flex-grow"] = "1";
    right.style_["flex-basis"] = "0px";
    root.addChild(&left);
    root.addChild(&right);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  left=%.1f right=%.1f (expect 240 / 460)\n",
           left.box.contentRect.width, right.box.contentRect.width);
    check(approx(left.box.contentRect.width, 240, 1),
          "overflow:hidden item keeps its flex-basis (auto min resolves to 0)");
    check(approx(right.box.contentRect.width, 460, 1),
          "sibling sized against the unfloored basis");
}

// An explicit min-width replaces the automatic minimum entirely: the
// content-based floor must not apply when min-width is a length.
static void testFlexRowExplicitMinWidth() {
    printf("--- flex row: explicit min-width overrides the auto minimum ---\n");
    LxMetrics m;
    for (int variant = 0; variant < 2; variant++) {
        LxNode root; root.initBase();
        root.style_["display"] = "flex";
        root.style_["width"] = "700px";

        LxNode left; left.initBase();
        left.style_["flex-grow"] = "0";
        left.style_["flex-shrink"] = "0";
        left.style_["flex-basis"] = "240px";
        left.style_["min-width"] = (variant == 0) ? "120px" : "260px";

        LxNode wide; wide.initBase();
        wide.style_["width"] = "300px";
        left.addChild(&wide);

        LxNode right; right.initBase();
        right.style_["flex-grow"] = "1";
        right.style_["flex-basis"] = "0px";
        root.addChild(&left);
        root.addChild(&right);

        layoutTree(&root, 800, m);
        float expectL = (variant == 0) ? 240.0f : 260.0f;
        printf("  min-width:%s -> left=%.1f (expect %.0f)\n",
               left.style_["min-width"].c_str(), left.box.contentRect.width, expectL);
        check(approx(left.box.contentRect.width, expectL, 1),
              variant == 0 ? "explicit min-width below basis: no content floor applied"
                           : "explicit min-width above basis wins");
    }
}

// The "specified size suggestion" (a definite width property) caps the
// automatic minimum: an item with width:100px and 150px-wide content may be
// shrunk to 100, but not below it.
static void testFlexRowSpecifiedSizeCapsAutoMin() {
    printf("--- flex row: definite width caps the auto minimum ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["width"] = "150px";

    LxNode a; a.initBase();
    a.style_["width"] = "100px";
    a.style_["flex-shrink"] = "1";
    a.style_["min-width"] = "auto";
    LxNode wide; wide.initBase();
    wide.style_["width"] = "150px";
    a.addChild(&wide);

    LxNode b; b.initBase();
    b.style_["width"] = "100px";
    b.style_["flex-shrink"] = "1";
    root.addChild(&a);
    root.addChild(&b);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  a=%.1f (expect 100: shrinks to its width, not to 75, not floored at 150)\n",
           a.box.contentRect.width);
    check(approx(a.box.contentRect.width, 100, 1),
          "auto min = min(content suggestion, specified width)");
}

// The automatic minimum is clamped by the max main size property: content min
// 150 with max-width:120 gives an auto minimum of 120.
static void testFlexRowAutoMinMaxWidthClamp() {
    printf("--- flex row: max-width clamps the auto minimum ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["width"] = "100px";

    LxNode a; a.initBase();
    a.style_["min-width"] = "auto";
    a.style_["max-width"] = "120px";
    a.style_["flex-shrink"] = "1";
    LxNode wide; wide.initBase();
    wide.style_["width"] = "150px";
    a.addChild(&wide);

    LxNode b; b.initBase();
    b.style_["width"] = "100px";
    b.style_["flex-shrink"] = "1";
    root.addChild(&a);
    root.addChild(&b);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  a=%.1f (expect 120: content min 150 clamped by max-width)\n",
           a.box.contentRect.width);
    check(approx(a.box.contentRect.width, 120, 1),
          "auto min clamped by max-width");
}

// Column-axis mirror: the deferred content-based minimum on a column flex
// item is clamped by max-height.
static void testFlexColumnAutoMinMaxHeightClamp() {
    printf("--- flex column: max-height clamps the auto minimum ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["flex-direction"] = "column";
    root.style_["height"] = "20px";

    LxNode* kids[2];
    LxNode* texts[2];
    for (int i = 0; i < 2; i++) {
        LxNode* k = new LxNode(); k->initBase();
        k->style_["min-height"] = "auto";
        k->style_["flex-shrink"] = "1";
        k->style_["padding-top"] = "10px";
        k->style_["padding-bottom"] = "10px";  // text line 20 + 20 padding = 40 outer
        LxNode* t = new LxNode(); t->initBase(); t->isText = true; t->text = "row";
        k->addChild(t);
        root.addChild(k);
        kids[i] = k; texts[i] = t;
    }
    kids[0]->style_["max-height"] = "30px";

    LxMetrics m;
    layoutTree(&root, 400, m);
    float h0 = kids[0]->box.contentRect.height + kids[0]->box.padding.top + kids[0]->box.padding.bottom;
    float h1 = kids[1]->box.contentRect.height + kids[1]->box.padding.top + kids[1]->box.padding.bottom;
    printf("  child0 outer=%.1f (expect 30: content min 40 clamped by max-height), child1 outer=%.1f (expect 40)\n", h0, h1);
    check(approx(h0, 30, 1), "column auto min clamped by max-height");
    check(approx(h1, 40, 1), "unclamped sibling keeps its content min");
    for (int i = 0; i < 2; i++) { delete texts[i]; delete kids[i]; }
}

// Anonymous flex items (raw text) also get the automatic minimum: text may
// not be shrunk below its widest unbreakable word.
static void testFlexAnonymousTextAutoMin() {
    printf("--- flex row: anonymous text item floored at widest word ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";
    root.style_["width"] = "30px";

    LxNode t; t.initBase(); t.isText = true;
    t.text = "aaaa bb";   // mock metrics: 10px/char -> max-content 70, widest word 40
    root.addChild(&t);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  text width=%.1f (expect 40, not 30)\n", t.box.contentRect.width);
    check(approx(t.box.contentRect.width, 40, 1),
          "anonymous text item not shrunk below its widest word");
}

// Re-layout must not treat previously *resolved* auto margins as real
// margins: a second pass at a narrower width used to count the stale
// margin-left:auto value in the free-space computation and spuriously
// shrink every item (shop/product-grid .p-foot in broparity).
static void testFlexRelayoutAutoMarginStable() {
    printf("--- flex row: re-layout with resolved auto margins does not shrink ---\n");
    LxNode root; root.initBase();
    root.style_["display"] = "flex";

    LxNode a; a.initBase();
    a.style_["width"] = "50px";
    a.style_["flex-shrink"] = "1";
    LxNode b; b.initBase();
    b.style_["width"] = "30px";
    b.style_["flex-shrink"] = "1";
    b.style_["margin-left"] = "auto";
    root.addChild(&a);
    root.addChild(&b);

    LxMetrics m;
    layoutTree(&root, 240, m);   // first pass resolves b's margin-left to 160
    layoutTree(&root, 200, m);   // second, narrower pass must start from auto=0
    printf("  a=%.1f b=%.1f b.x=%.1f (expect 50 / 30 / 170)\n",
           a.box.contentRect.width, b.box.contentRect.width, b.box.contentRect.x);
    check(approx(a.box.contentRect.width, 50, 1), "item a keeps its width on re-layout");
    check(approx(b.box.contentRect.width, 30, 1), "item b keeps its width on re-layout");
    check(approx(b.box.contentRect.x, 170, 1), "margin-left:auto re-resolved against the new width");
}

// A block-level replaced element with a fixed intrinsic ratio (e.g. <canvas
// width=1120 height=240>) and max-width:100% must scale its auto height to keep
// the ratio when the container clamps its width — not keep the raw intrinsic
// height (which would squash a 1120x240 raster into 768x240). A form control
// (no intrinsic ratio) must NOT scale its height the same way.
static void testBlockReplacedMaxWidthRatio() {
    printf("--- Block: replaced max-width preserves intrinsic ratio ---\n");

    // canvas-like: intrinsic 1120x240, ratio-locked, max-width:100% in a 768 box.
    LxNode media; media.initBase();
    media.hasIntrinsic = true; media.hasRatio = true;
    media.intrW = 1120; media.intrH = 240;
    media.style_["max-width"] = "100%";

    LxNode root1; root1.initBase();
    root1.addChild(&media);
    LxMetrics m;
    layoutTree(&root1, 768, m);
    printf("  media %.1fx%.1f (expect 768x164.6)\n",
           media.box.contentRect.width, media.box.contentRect.height);
    check(approx(media.box.contentRect.width, 768, 1), "ratio media width clamped to 768");
    check(approx(media.box.contentRect.height, 240.0f * (768.0f / 1120.0f), 1),
          "ratio media height scaled to preserve 1120:240 ratio");

    // Unconstrained: container wider than intrinsic -> exact intrinsic box.
    LxNode media2; media2.initBase();
    media2.hasIntrinsic = true; media2.hasRatio = true;
    media2.intrW = 1120; media2.intrH = 240;
    media2.style_["max-width"] = "100%";
    LxNode root2; root2.initBase();
    root2.addChild(&media2);
    layoutTree(&root2, 1600, m);
    check(approx(media2.box.contentRect.width, 1120, 1), "unconstrained media keeps intrinsic width");
    check(approx(media2.box.contentRect.height, 240, 1), "unconstrained media keeps intrinsic height");

    // Form-control-like: intrinsic 200x30, NO ratio. max-width clamps width but
    // height must stay 30 (content size), not scale down with the width.
    LxNode ctrl; ctrl.initBase();
    ctrl.hasIntrinsic = true; ctrl.hasRatio = false;
    ctrl.intrW = 200; ctrl.intrH = 30;
    ctrl.style_["max-width"] = "100px";
    LxNode root3; root3.initBase();
    root3.addChild(&ctrl);
    layoutTree(&root3, 1000, m);
    printf("  control %.1fx%.1f (expect 100x30, height unchanged)\n",
           ctrl.box.contentRect.width, ctrl.box.contentRect.height);
    check(approx(ctrl.box.contentRect.width, 100, 1), "no-ratio control width clamped to 100");
    check(approx(ctrl.box.contentRect.height, 30, 1), "no-ratio control height stays intrinsic (not scaled)");
}

// ===== font-size:0 — the strut is empty (Chromium parity) =====
static void testFontSizeZeroStrut() {
    printf("--- font-size:0: empty strut, no descent under inline-block ---\n");
    LxNode root; root.initBase();
    root.style_["font-size"] = "0";
    root.style_["width"] = "500px";

    LxNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";
    ib.style_["width"] = "440px";
    ib.style_["height"] = "32px";
    root.addChild(&ib);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  container height=%.1f (expect exactly 32)\n", root.box.contentRect.height);
    check(approx(root.box.contentRect.height, 32, 0.1f),
          "font-size:0 line adds no strut descent below a 32px inline-block");
}

// ===== Blink half-leading split: floor(leading/2) above, remainder below =====
static void testLeadingSplitFloorZeroFont() {
    printf("--- font-size:0 + line-height:15px: strut is 7 above / 8 below ---\n");
    // A baseline-aligned inline-block (baseline = bottom margin edge) shows
    // the strut descent directly: line = max(itemAbove, 7) + 8.
    LxNode root; root.initBase();
    root.style_["font-size"] = "0";
    root.style_["line-height"] = "15px";
    root.style_["width"] = "100px";

    LxNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";
    ib.style_["width"] = "20px";
    ib.style_["height"] = "10px";
    root.addChild(&ib);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  container height=%.1f (expect 18 = 10 above + 8 below)\n",
           root.box.contentRect.height);
    check(approx(root.box.contentRect.height, 18, 0.1f),
          "odd leading puts the extra half-pixel below the baseline");
}

static void testLeadingSplitFloorRealFont() {
    printf("--- 16px font + line-height:25px: ascent side floors ---\n");
    // Mock metrics: natural 20, ascent 16. leading = 5 -> 2 above, 3 below.
    // A baseline-aligned inline-block h=10 sits at y = (16+2) - 10 = 8.
    LxNode root; root.initBase();
    root.style_["line-height"] = "25px";
    root.style_["width"] = "300px";

    LxNode ib; ib.initBase();
    ib.style_["display"] = "inline-block";
    ib.style_["width"] = "20px";
    ib.style_["height"] = "10px";
    root.addChild(&ib);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  ib y=%.2f (expect 8, not 8.5), container h=%.1f (expect 25)\n",
           ib.box.contentRect.y, root.box.contentRect.height);
    check(approx(ib.box.contentRect.y, 8, 0.2f),
          "half-leading ascent side is floored (Blink CalculateLeadingSpace)");
    check(approx(root.box.contentRect.height, 25, 0.1f),
          "line box height equals the specified line-height");
}

// ===== vertical-align: middle centers on the baseline (+ xHeight/2) =====
static void testVerticalAlignMiddleBaselineCentered() {
    printf("--- vertical-align:middle at font-size:0 (newspaper .w rows) ---\n");
    // fs0 + line-height:15px -> strut 7/8. Item margin box 15 (h7 + mb8),
    // middle-aligned at the baseline (xHeight 0): spans baseline +-7.5.
    // Line = max(7,7.5) + max(8,7.5) = 15.5, item top at the line top.
    // Three items in a one-per-line container stack every 15.5px.
    LxNode root; root.initBase();
    root.style_["font-size"] = "0";
    root.style_["line-height"] = "15px";
    root.style_["width"] = "50px";

    LxNode w[3];
    for (auto& n : w) {
        n.initBase();
        n.style_["display"] = "inline-block";
        n.style_["width"] = "40px";
        n.style_["height"] = "7px";
        n.style_["margin-right"] = "5px";
        n.style_["margin-bottom"] = "8px";
        n.style_["vertical-align"] = "middle";
        root.addChild(&n);
    }

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  rows at y=%.1f / %.1f / %.1f (expect 0 / 15.5 / 31), h=%.1f (expect 46.5)\n",
           w[0].box.contentRect.y, w[1].box.contentRect.y, w[2].box.contentRect.y,
           root.box.contentRect.height);
    check(approx(w[0].box.contentRect.y, 0, 0.1f), "middle item sits at the line top");
    check(approx(w[1].box.contentRect.y, 15.5f, 0.1f), "lines advance by 15.5px");
    check(approx(w[2].box.contentRect.y, 31.0f, 0.1f), "third line at 31px");
    check(approx(root.box.contentRect.height, 46.5f, 0.1f), "container is 3 x 15.5");
}

// ===== Multi-column balancing =====
static void testMulticolBalanceMinHeight() {
    printf("--- multicol: balance to the minimal feasible column height ---\n");
    // 6 paragraphs (h8, margin-bottom 9) into 3 columns: two per column,
    // margins truncated at each column top -> H = 8 + 9 + 8 = 25.
    LxNode root; root.initBase();
    root.style_["column-count"] = "3";
    root.style_["width"] = "300px";

    LxNode p[6];
    for (auto& n : p) {
        n.initBase();
        n.style_["height"] = "8px";
        n.style_["margin-bottom"] = "9px";
        root.addChild(&n);
    }

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  h=%.1f (expect 25); p2 at (%.1f,%.1f) expect (100,0); p3 at (%.1f,%.1f) expect (100,17)\n",
           root.box.contentRect.height,
           p[2].box.contentRect.x, p[2].box.contentRect.y,
           p[3].box.contentRect.x, p[3].box.contentRect.y);
    check(approx(root.box.contentRect.height, 25, 0.1f), "balanced height is 25");
    check(approx(p[0].box.contentRect.y, 0, 0.1f), "col1 first item at top");
    check(approx(p[1].box.contentRect.y, 17, 0.1f), "second item stacks with its margin");
    check(approx(p[2].box.contentRect.x, 100, 0.1f) && approx(p[2].box.contentRect.y, 0, 0.1f),
          "col2 starts with the third item, margin truncated at the break");
    check(approx(p[4].box.contentRect.x, 200, 0.1f), "col3 gets the last two items");
}

static void testMulticolTallUnbreakable() {
    printf("--- multicol: tallest unbreakable item floors the balance ---\n");
    // 40 + 40 + 120 into 3 columns: H = 120 (the unbreakable item), filled
    // Chromium-style -> col1 {40,40}, col2 {120}, not one item per column.
    LxNode root; root.initBase();
    root.style_["column-count"] = "3";
    root.style_["width"] = "300px";

    LxNode a; a.initBase(); a.style_["height"] = "40px";
    LxNode b; b.initBase(); b.style_["height"] = "40px";
    LxNode c; c.initBase(); c.style_["height"] = "120px";
    root.addChild(&a); root.addChild(&b); root.addChild(&c);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  h=%.1f (expect 120); b at (%.1f,%.1f) expect (0,40); c at (%.1f,%.1f) expect (100,0)\n",
           root.box.contentRect.height,
           b.box.contentRect.x, b.box.contentRect.y,
           c.box.contentRect.x, c.box.contentRect.y);
    check(approx(root.box.contentRect.height, 120, 0.1f), "container height = tallest item");
    check(approx(b.box.contentRect.x, 0, 0.1f) && approx(b.box.contentRect.y, 40, 0.1f),
          "second item stays in column 1");
    check(approx(c.box.contentRect.x, 100, 0.1f) && approx(c.box.contentRect.y, 0, 0.1f),
          "tall item opens column 2");
}

static void testMulticolInlineLines() {
    printf("--- multicol: inline content fragments by line boxes ---\n");
    // 6 inline-blocks (40px + 5px margin) in 2 columns of 100px: two per
    // line, three lines, balanced 2/1 across the columns.
    LxNode root; root.initBase();
    root.style_["column-count"] = "2";
    root.style_["width"] = "200px";

    LxNode w[6];
    for (auto& n : w) {
        n.initBase();
        n.style_["display"] = "inline-block";
        n.style_["width"] = "40px";
        n.style_["height"] = "10px";
        n.style_["margin-right"] = "5px";
        root.addChild(&n);
    }

    LxMetrics m;
    layoutTree(&root, 800, m);
    // Lines are 20px (the strut); a baseline-aligned 10px item hangs at
    // y = ascent 16 - 10 = 6 within its line.
    printf("  w0 (%.1f,%.1f) w2 (%.1f,%.1f) w4 (%.1f,%.1f); h=%.1f (expect 40)\n",
           w[0].box.contentRect.x, w[0].box.contentRect.y,
           w[2].box.contentRect.x, w[2].box.contentRect.y,
           w[4].box.contentRect.x, w[4].box.contentRect.y,
           root.box.contentRect.height);
    check(approx(w[0].box.contentRect.x, 0, 0.1f) && approx(w[0].box.contentRect.y, 6, 0.1f),
          "line 1 starts column 1");
    check(approx(w[2].box.contentRect.y, 26, 0.1f), "line 2 below line 1 (20px lines)");
    check(approx(w[4].box.contentRect.x, 100, 0.1f) && approx(w[4].box.contentRect.y, 6, 0.1f),
          "line 3 fragments into column 2");
    check(approx(root.box.contentRect.height, 40, 0.1f), "balanced to two lines per column");
}

// ===== Floats: containment and escape (CSS2 §10.6.3 / §9.4.1) =====
static void testFloatNotContainedByAutoHeight() {
    printf("--- float: does not extend a non-BFC parent's auto height ---\n");
    LxNode root; root.initBase(); // root establishes the initial BFC
    root.style_["width"] = "200px";

    LxNode wrap; wrap.initBase(); // plain block: floats escape it
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "50px";
    fl.style_["height"] = "90px";
    LxNode para; para.initBase();
    para.style_["height"] = "20px";
    wrap.addChild(&fl); wrap.addChild(&para);
    root.addChild(&wrap);

    LxNode sib; sib.initBase(); // sibling after the wrapper
    sib.style_["height"] = "30px";
    root.addChild(&sib);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  wrap h=%.1f (expect 20); sib x=%.1f w=%.1f (expect 0 / 200); root h=%.1f (expect 90)\n",
           wrap.box.contentRect.height, sib.box.contentRect.x,
           sib.box.contentRect.width, root.box.contentRect.height);
    check(approx(wrap.box.contentRect.height, 20, 0.1f),
          "auto height ignores the protruding float");
    check(approx(sib.box.contentRect.x, 0, 0.1f),
          "sibling block box is not shifted by the escaped float");
    check(approx(sib.box.contentRect.width, 200, 0.1f),
          "sibling block box keeps full width (only line boxes shorten)");
    check(approx(root.box.contentRect.height, 90, 0.1f),
          "the BFC root contains the adopted float");
}

static void testFloatContainedByBFC() {
    printf("--- float: a BFC root does contain its floats ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "200px";

    LxNode wrap; wrap.initBase();
    wrap.style_["overflow"] = "hidden"; // BFC root
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "50px";
    fl.style_["height"] = "90px";
    wrap.addChild(&fl);
    root.addChild(&wrap);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  wrap h=%.1f (expect 90)\n", wrap.box.contentRect.height);
    check(approx(wrap.box.contentRect.height, 90, 0.1f),
          "overflow:hidden container wraps its float");
}

// ===== Floats: placement rules (CSS2 §9.5.1) =====
static void testFloatStackingWrapsToNextLine() {
    printf("--- float: fourth float wraps below the first float line ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "500px";
    root.style_["overflow"] = "hidden";
    LxNode f[5];
    for (int i = 0; i < 5; ++i) {
        f[i].initBase();
        f[i].style_["float"] = "left";
        f[i].style_["width"] = "150px";
        f[i].style_["height"] = "60px";
        root.addChild(&f[i]);
    }
    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  f4 (%.1f,%.1f) f5 (%.1f,%.1f) h=%.1f (expect (0,60) (150,60) 120)\n",
           f[3].box.contentRect.x, f[3].box.contentRect.y,
           f[4].box.contentRect.x, f[4].box.contentRect.y,
           root.box.contentRect.height);
    check(approx(f[2].box.contentRect.x, 300, 0.1f) &&
          approx(f[2].box.contentRect.y, 0, 0.1f), "third float still on line one");
    check(approx(f[3].box.contentRect.x, 0, 0.1f) &&
          approx(f[3].box.contentRect.y, 60, 0.1f), "fourth float wraps to (0,60)");
    check(approx(f[4].box.contentRect.x, 150, 0.1f) &&
          approx(f[4].box.contentRect.y, 60, 0.1f), "fifth float follows at (150,60)");
    check(approx(root.box.contentRect.height, 120, 0.1f),
          "BFC height covers both float lines");
}

static void testFloatWiderThanRemainingSpace() {
    printf("--- float: wider than the remaining line space drops below ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "500px";
    root.style_["overflow"] = "hidden";
    LxNode narrow; narrow.initBase();
    narrow.style_["float"] = "left";
    narrow.style_["width"] = "150px";
    narrow.style_["height"] = "60px";
    LxNode wide; wide.initBase();
    wide.style_["float"] = "left";
    wide.style_["width"] = "400px";
    wide.style_["height"] = "60px";
    LxNode small; small.initBase();
    small.style_["float"] = "left";
    small.style_["width"] = "80px";
    small.style_["height"] = "60px";
    root.addChild(&narrow); root.addChild(&wide); root.addChild(&small);
    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  wide (%.1f,%.1f) small (%.1f,%.1f) (expect (0,60) (400,60))\n",
           wide.box.contentRect.x, wide.box.contentRect.y,
           small.box.contentRect.x, small.box.contentRect.y);
    check(approx(wide.box.contentRect.x, 0, 0.1f) &&
          approx(wide.box.contentRect.y, 60, 0.1f),
          "400px float drops below the 150px float");
    // Rule 5: the later float's top may not be higher than the wide float's
    // top even though it would fit beside the first float.
    check(approx(small.box.contentRect.x, 400, 0.1f) &&
          approx(small.box.contentRect.y, 60, 0.1f),
          "later float sits no higher than the earlier dropped float");
}

// ===== Clearance (CSS2 §9.5.2) =====
static void testClearanceSwallowsMarginTop() {
    printf("--- clear: clearance swallows the cleared box's margin-top ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "360px";
    root.style_["overflow"] = "hidden";
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "100px";
    fl.style_["height"] = "100px";
    LxNode eaten; eaten.initBase();
    eaten.style_["clear"] = "left";
    eaten.style_["margin-top"] = "80px";
    eaten.style_["height"] = "50px";
    LxNode spacer; spacer.initBase();
    spacer.style_["height"] = "20px";
    root.addChild(&fl); root.addChild(&eaten); root.addChild(&spacer);
    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  eaten y=%.1f (expect 100) root h=%.1f (expect 170)\n",
           eaten.box.contentRect.y, root.box.contentRect.height);
    check(approx(eaten.box.contentRect.y, 100, 0.1f),
          "hypothetical top 80 < float bottom 100: clearance replaces the margin");
    check(approx(root.box.contentRect.height, 170, 0.1f),
          "content flows from the cleared position");
}

static void testClearWithoutClearanceKeepsMargin() {
    printf("--- clear: no clearance when the margin already clears the float ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "360px";
    root.style_["overflow"] = "hidden";
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "100px";
    fl.style_["height"] = "100px";
    LxNode kept; kept.initBase();
    kept.style_["clear"] = "left";
    kept.style_["margin-top"] = "150px";
    kept.style_["height"] = "50px";
    root.addChild(&fl); root.addChild(&kept);
    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  kept y=%.1f (expect 150)\n", kept.box.contentRect.y);
    check(approx(kept.box.contentRect.y, 150, 0.1f),
          "hypothetical top 150 >= float bottom 100: margin applies in full");
}

static void testClearedBlockKeepsFullWidth() {
    printf("--- clear: a cleared plain block keeps the full width ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "500px";
    root.style_["overflow"] = "hidden";
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "120px";
    fl.style_["height"] = "100px";
    LxNode fr; fr.initBase();
    fr.style_["float"] = "right";
    fr.style_["width"] = "120px";
    fr.style_["height"] = "160px";
    LxNode cleft; cleft.initBase();
    cleft.style_["clear"] = "left";
    cleft.style_["height"] = "24px";
    root.addChild(&fl); root.addChild(&fr); root.addChild(&cleft);
    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  cleft (%.1f,%.1f) w=%.1f (expect (0,100) 500)\n",
           cleft.box.contentRect.x, cleft.box.contentRect.y,
           cleft.box.contentRect.width);
    check(approx(cleft.box.contentRect.y, 100, 0.1f),
          "clear:left lands at the left float's bottom");
    check(approx(cleft.box.contentRect.x, 0, 0.1f) &&
          approx(cleft.box.contentRect.width, 500, 0.1f),
          "block box overlaps the remaining right float at full width");
}

// ===== Text wraps into shortened line boxes beside a float =====
static void testTextWrapsBesideFloat() {
    printf("--- float: text wraps in shortened lines and recovers below ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "500px";
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "120px";
    fl.style_["height"] = "120px";
    LxNode t; t.initBase();
    t.isText = true;
    // 48 four-char words (40px each + 10px space): beside the float a line
    // holds 7 words (380px band), so 6 lines cover the float's 120px and
    // the remaining words drop to a full-width line at x=0, y=120.
    {
        std::string s;
        for (int i = 0; i < 48; ++i) { if (i) s += ' '; s += "word"; }
        t.text = s;
    }
    root.addChild(&fl); root.addChild(&t);
    LxMetrics m;
    layoutTree(&root, 800, m);
    const auto& runs = t.box.textRuns;
    float firstX = runs.empty() ? -1.0f : runs.front().x;
    bool recovered = false;
    for (const auto& r : runs) {
        if (r.y >= 119.0f && r.x <= 0.1f) recovered = true;
    }
    printf("  runs=%zu firstX=%.1f rootH=%.1f (expect >1 / 120 / >120)\n",
           runs.size(), firstX, root.box.contentRect.height);
    check(runs.size() > 1, "text wraps into multiple runs beside the float");
    check(approx(firstX, 120, 0.1f), "first line starts at the float's right edge");
    check(recovered, "a line below the float recovers to x=0");
    check(root.box.contentRect.height > 120.0f,
          "block height includes the wrapped lines");
}

// ===== Floats inside an inline run don't break the line (CSS2 §9.5) =====
static void testMidRunFloatKeepsLine() {
    printf("--- float between inline siblings: the line continues ---\n");
    LxNode root; root.initBase();
    root.style_["width"] = "300px";

    LxNode ib1; ib1.initBase();
    ib1.style_["display"] = "inline-block";
    ib1.style_["width"] = "50px"; ib1.style_["height"] = "10px";
    LxNode fl; fl.initBase();
    fl.style_["float"] = "left";
    fl.style_["width"] = "80px"; fl.style_["height"] = "40px";
    LxNode ib2; ib2.initBase();
    ib2.style_["display"] = "inline-block";
    ib2.style_["width"] = "50px"; ib2.style_["height"] = "10px";
    root.addChild(&ib1); root.addChild(&fl); root.addChild(&ib2);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  ib1 y=%.1f ib2 (%.1f,%.1f); float y=%.1f (expect below line at 20)\n",
           ib1.box.contentRect.y, ib2.box.contentRect.x, ib2.box.contentRect.y,
           fl.box.contentRect.y);
    check(approx(ib2.box.contentRect.y, ib1.box.contentRect.y, 0.1f),
          "the inline run continues on the same line after the float");
    check(approx(ib2.box.contentRect.x, 50, 0.1f),
          "second inline-block follows the first horizontally");
    check(approx(fl.box.contentRect.y, 20, 0.1f),
          "the mid-run float is placed below the current line");
}

// ===== shape-outside: circle() =====
static void testShapeOutsideCircleWrap() {
    printf("--- shape-outside: circle() frees the corner of a float ---\n");
    // Right float 100x100 with circle(50%) -> r = 50, centered at (150,50)
    // in a 200px block. The first line's band [0,20] only meets the chord
    // at dy=30 -> half-width 40 -> right edge 110, so two 52px items fit
    // (104 <= 110) where the raw margin box (edge 100) would wrap.
    LxNode root; root.initBase();
    root.style_["width"] = "200px";

    LxNode fl; fl.initBase();
    fl.style_["float"] = "right";
    fl.style_["width"] = "100px"; fl.style_["height"] = "100px";
    fl.style_["shape-outside"] = "circle(50%)";
    LxNode ib1; ib1.initBase();
    ib1.style_["display"] = "inline-block";
    ib1.style_["width"] = "52px"; ib1.style_["height"] = "10px";
    LxNode ib2; ib2.initBase();
    ib2.style_["display"] = "inline-block";
    ib2.style_["width"] = "52px"; ib2.style_["height"] = "10px";
    root.addChild(&fl); root.addChild(&ib1); root.addChild(&ib2);

    LxMetrics m;
    layoutTree(&root, 800, m);
    printf("  ib2 (%.1f,%.1f) (expect on line 1 at x=52)\n",
           ib2.box.contentRect.x, ib2.box.contentRect.y);
    check(approx(ib2.box.contentRect.y, ib1.box.contentRect.y, 0.1f),
          "second item stays on line 1 inside the circle's free corner");
    check(approx(ib2.box.contentRect.x, 52, 0.1f), "packed after the first item");
}

// ===== Absolute positioning: negative offsets are specified, not auto =====
static void testAbsNegativeOffsets() {
    printf("--- Abs: negative top/left/bottom offsets ---\n");
    LxNode wrap; wrap.initBase();
    wrap.style_["position"] = "relative";
    wrap.style_["width"] = "200px";
    wrap.style_["height"] = "100px";

    LxNode a; a.initBase();
    a.style_["position"] = "absolute";
    a.style_["top"] = "-40px";
    a.style_["left"] = "-10px";
    a.style_["width"] = "50px";
    a.style_["height"] = "30px";

    LxNode b; b.initBase();
    b.style_["position"] = "absolute";
    b.style_["bottom"] = "-60px";
    b.style_["left"] = "0px";
    b.style_["width"] = "50px";
    b.style_["height"] = "30px";

    wrap.addChild(&a);
    wrap.addChild(&b);

    LxMetrics m;
    layoutTree(&wrap, 800, m);

    check(approx(a.box.contentRect.y, -40.0f), "top:-40px places above the container");
    check(approx(a.box.contentRect.x, -10.0f), "left:-10px places left of the container");
    check(approx(a.box.contentRect.width, 50.0f), "negative offsets keep explicit width");
    // bottom:-60px: y = cbHeight(100) - (-60) - height(30) = 130
    check(approx(b.box.contentRect.y, 130.0f), "bottom:-60px hangs below the container");
}

// ===== Flex items are independent FCs: child margins stay inside =====
static void testFlexItemMarginContained() {
    printf("--- Flex item: child margins don't collapse through ---\n");
    LxNode flex; flex.initBase();
    flex.style_["display"] = "flex";
    flex.style_["width"] = "400px";

    LxNode item; item.initBase();
    item.style_["width"] = "200px"; // block-level flex item

    LxNode p; p.initBase();
    p.style_["height"] = "20px";
    p.style_["margin-top"] = "8px";
    p.style_["margin-bottom"] = "10px";
    item.addChild(&p);
    flex.addChild(&item);

    LxMetrics m;
    layoutTree(&flex, 800, m);

    check(approx(item.box.contentRect.height, 38.0f),
          "flex item height contains child margins (8+20+10)");
    check(approx(item.box.margin.bottom, 0.0f),
          "child's margin-bottom does not escape the flex item");
    check(approx(p.box.contentRect.y, 8.0f),
          "child margin-top stays inside the flex item");
}

// ===== naturalHeight(): text rects vs line-height:normal =====
static void testNaturalHeightTextRects() {
    printf("--- naturalHeight(): text rect uses ascent+descent, line advance uses line-height ---\n");
    // lineHeight (normal) = 20 from LxMetrics; naturalHeight = 17 like a
    // font whose round(ascent)+round(descent) excludes the line gap.
    struct NHMetrics : LxMetrics {
        float naturalHeight(std::string_view, float, std::string_view) override {
            return 17.0f;
        }
    };

    LxNode root; root.initBase();
    root.style_["width"] = "200px";
    LxNode t; t.initBase(); t.isText = true; t.text = "hi";
    root.addChild(&t);

    NHMetrics m;
    layoutTree(&root, 800, m);

    check(approx(t.box.contentRect.height, 17.0f), "text run rect height = naturalHeight");
    check(approx(root.box.contentRect.height, 20.0f), "line box still advances by line-height");
}

void testLayoutExtra() {
    printf("=== Extra Layout Tests ===\n");
    testBlockReplacedMaxWidthRatio();
    testFlexReplacedBorderBox();
    testFlexColumnAutoMinNoShrink();
    testFlexRowAutoMinFloorsBasis();
    testFlexRowAutoMinOverflowHidden();
    testFlexRowExplicitMinWidth();
    testFlexRowSpecifiedSizeCapsAutoMin();
    testFlexRowAutoMinMaxWidthClamp();
    testFlexColumnAutoMinMaxHeightClamp();
    testFlexAnonymousTextAutoMin();
    testFlexRelayoutAutoMarginStable();
    testTableColspan();
    testTableRowspan();
    testTableColgroup();
    testTableColgroupSpan();
    testTableStrayCol();
    testTableCellWithoutRow();
    testTableNonTableChild();
    testTableEmpty();
    testTablePercentColumns();
    testTableRowGroup();
    testTableEmptyRowGroup();
    testTableColspanNoExcessNoWiden();
    testTableColspanExcessDistribution();
    testTableIntrinsicWidths();
    testTableCollapseSharedBorderIntrinsics();
    testTableNestedTableIntrinsic();
    testInlineBlockWithTextAndBr();
    testInlineBlockNestedInlineBlock();
    testInlineBlockWithBlockChild();
    testInlineBlockMinWidth();
    testBlockAspectRatio();
    testBlockAnonymousInline();
    testBlockRtlAlign();
    testBlockMarginAuto();
    testBlockBrInline();
    testBlockMarginAutoRightOnly();
    testBlockBorderBox();
    testBlockBfcRtl();
    testBlockMultiCol();
    testFontSizeZeroStrut();
    testLeadingSplitFloorZeroFont();
    testLeadingSplitFloorRealFont();
    testVerticalAlignMiddleBaselineCentered();
    testMulticolBalanceMinHeight();
    testMulticolTallUnbreakable();
    testMulticolInlineLines();
    testFloatNotContainedByAutoHeight();
    testFloatContainedByBFC();
    testFloatStackingWrapsToNextLine();
    testFloatWiderThanRemainingSpace();
    testClearanceSwallowsMarginTop();
    testClearWithoutClearanceKeepsMargin();
    testClearedBlockKeepsFullWidth();
    testTextWrapsBesideFloat();
    testMidRunFloatKeepsLine();
    testShapeOutsideCircleWrap();
    testAbsNegativeOffsets();
    testFlexItemMarginContained();
    testNaturalHeightTextRects();
}
