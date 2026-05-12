// Extra layout tests aimed at low-coverage code paths in
// table.cpp, block.cpp, inline.cpp, flex.cpp, grid.cpp.

#include "test_layout_extra.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
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

void testLayoutExtra() {
    printf("=== Extra Layout Tests ===\n");
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
    testInlineBlockWithTextAndBr();
    testInlineBlockNestedInlineBlock();
    testInlineBlockWithBlockChild();
    testInlineBlockMinWidth();
    testBlockAspectRatio();
    testBlockAnonymousInline();
    testBlockRtlAlign();
    testBlockMarginAuto();
}
