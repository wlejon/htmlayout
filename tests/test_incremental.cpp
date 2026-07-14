#include "test_incremental.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct Node : public LayoutNode {
    std::string tag;
    bool isText = false;
    std::string text;
    Node* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style; }

    void addChild(Node* child) {
        child->parentNode = this;
        childNodes.push_back(child);
    }
};

struct Metrics : public TextMetrics {
    float measureWidth(std::string_view text, std::string_view, float fontSize,
                       std::string_view) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(std::string_view, float fontSize, std::string_view) override {
        return fontSize * 1.2f;
    }
};

bool approx(float a, float b, float tol = 0.01f) { return std::abs(a - b) < tol; }

void initNode(Node& n, const std::string& tagName, const std::string& display) {
    n.tag = tagName;
    n.style["display"] = display;
    n.style["position"] = "static";
    n.style["width"] = "auto";
    n.style["height"] = "auto";
    n.style["min-width"] = "0";
    n.style["min-height"] = "0";
    n.style["max-width"] = "none";
    n.style["max-height"] = "none";
    n.style["margin-top"] = "0";
    n.style["margin-right"] = "0";
    n.style["margin-bottom"] = "0";
    n.style["margin-left"] = "0";
    n.style["padding-top"] = "0";
    n.style["padding-right"] = "0";
    n.style["padding-bottom"] = "0";
    n.style["padding-left"] = "0";
    n.style["border-top-width"] = "0";
    n.style["border-right-width"] = "0";
    n.style["border-bottom-width"] = "0";
    n.style["border-left-width"] = "0";
    n.style["border-top-style"] = "none";
    n.style["border-right-style"] = "none";
    n.style["border-bottom-style"] = "none";
    n.style["border-left-style"] = "none";
    n.style["box-sizing"] = "content-box";
    n.style["font-size"] = "16px";
    n.style["overflow"] = "visible";
}

Node makeText(const std::string& s) {
    Node t;
    t.isText = true;
    t.text = s;
    t.style["font-size"] = "16px";
    return t;
}

// A dirty node whose box the formatting context writes by hand (table rows and
// row groups) never passes through layoutNode(), so its dirty flag is never
// cleared. markDirty() from below it must still reach the root — otherwise the
// container above it stays clean and its whole subtree is skipped as unchanged.
void testMarkDirtyWalksPastStuckAncestor() {
    printf("--- Incremental: markDirty reaches the root past a stuck ancestor ---\n");
    Node root, mid, leaf;
    initNode(root, "div", "block");
    initNode(mid, "div", "block");
    initNode(leaf, "div", "block");
    root.addChild(&mid);
    mid.addChild(&leaf);

    root.box.dirty = false;
    mid.box.dirty = true;  // stuck: nothing ever clears this one
    leaf.box.dirty = false;

    markDirty(&leaf);
    check(leaf.box.dirty, "leaf marked dirty");
    check(root.box.dirty, "root marked dirty through the stuck ancestor");
}

// Text inside a table cell must re-lay out when only that cell is marked dirty.
// The cell's parents (tr, tbody) are positioned by layoutTable directly, so they
// are the stuck-dirty nodes the walk above has to see past.
void testTableCellTextRelayout() {
    printf("--- Incremental: table cell text updates on relayout ---\n");
    Node root, table, tbody, tr, td;
    initNode(root, "div", "block");
    initNode(table, "table", "table");
    initNode(tbody, "tbody", "table-row-group");
    initNode(tr, "tr", "table-row");
    initNode(td, "td", "table-cell");
    Node text = makeText("0");

    root.addChild(&table);
    table.addChild(&tbody);
    tbody.addChild(&tr);
    tr.addChild(&td);
    td.addChild(&text);

    Metrics metrics;
    Viewport vp{800.0f, 600.0f};
    layoutTree(&root, vp, metrics);
    float w1 = td.box.contentRect.width;
    check(approx(w1, 9.6f), "cell sized to '0'");

    text.text = "888888";
    markDirty(&td);
    layoutTree(&root, vp, metrics);
    check(approx(td.box.contentRect.width, 57.6f), "cell resized to '888888'");
    check(approx(text.box.contentRect.width, 57.6f), "text box resized to '888888'");
}

// The whole point of the incremental pass: an untouched subtree keeps its boxes.
void testCleanSubtreeIsStable() {
    printf("--- Incremental: clean subtree keeps its boxes ---\n");
    Node root, a, b;
    initNode(root, "div", "block");
    initNode(a, "div", "block");
    initNode(b, "div", "block");
    a.style["height"] = "40px";
    b.style["height"] = "25px";
    root.addChild(&a);
    root.addChild(&b);

    Metrics metrics;
    Viewport vp{800.0f, 600.0f};
    layoutTree(&root, vp, metrics);
    float bY = b.box.contentRect.y;
    check(approx(bY, 40.0f), "b below a");

    layoutTree(&root, vp, metrics);  // nothing dirty
    check(approx(b.box.contentRect.y, bY), "b unmoved by a no-op pass");

    a.style["height"] = "60px";
    markDirty(&a);
    layoutTree(&root, vp, metrics);
    check(approx(b.box.contentRect.y, 60.0f), "b follows a's new height");
}

// ---------------------------------------------------------------------------
// Grid items are reused across passes, which means the boxes a grid hands back
// have to be indistinguishable from the ones it would have computed. Grid makes
// that hard on itself: it writes its own decisions (justify stretch width, align
// stretch height) straight into an item's box, then reads boxes back as the item
// contributions that size its auto tracks. Read a *reused* box back and the
// contribution is last pass's stretched size, so a track could only ever grow.
//
// So test it differentially: run the incremental pass, build the same document
// from scratch, and require every box to agree. A ratchet shows up immediately
// as a row that didn't shrink.

struct GridDoc {
    std::vector<std::unique_ptr<Node>> arena;
    Node* root = nullptr;
    std::vector<Node*> cells;
    std::vector<Node*> texts;

    Node* alloc() {
        arena.push_back(std::make_unique<Node>());
        return arena.back().get();
    }
};

// root(block, 800px) > grid(2 equal columns) > cell(block, 5px padding) > text
void buildGridDoc(GridDoc& d, const std::vector<std::string>& cellTexts) {
    d.root = d.alloc();
    initNode(*d.root, "div", "block");
    d.root->style["width"] = "800px";

    Node* grid = d.alloc();
    initNode(*grid, "div", "grid");
    grid->style["grid-template-columns"] = "1fr 1fr";
    grid->style["row-gap"] = "10px";
    grid->style["column-gap"] = "10px";
    d.root->addChild(grid);

    for (const auto& s : cellTexts) {
        Node* cell = d.alloc();
        initNode(*cell, "div", "block");
        cell->style["padding-top"] = "5px";
        cell->style["padding-bottom"] = "5px";
        grid->addChild(cell);
        d.cells.push_back(cell);

        Node* tx = d.alloc();
        tx->isText = true;
        tx->text = s;
        tx->style["font-size"] = "16px";
        cell->addChild(tx);
        d.texts.push_back(tx);
    }
}

// Compare every box in two structurally identical trees.
bool boxesAgree(Node* a, Node* b, std::string& where) {
    const auto& ra = a->box.contentRect;
    const auto& rb = b->box.contentRect;
    if (!approx(ra.x, rb.x) || !approx(ra.y, rb.y) ||
        !approx(ra.width, rb.width) || !approx(ra.height, rb.height)) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "<%s> incremental (%.1f,%.1f %.1fx%.1f) vs fresh (%.1f,%.1f %.1fx%.1f)",
                 a->isText ? "#text" : a->tag.c_str(),
                 ra.x, ra.y, ra.width, ra.height, rb.x, rb.y, rb.width, rb.height);
        where = buf;
        return false;
    }
    for (size_t i = 0; i < a->childNodes.size(); i++)
        if (!boxesAgree(static_cast<Node*>(a->childNodes[i]),
                        static_cast<Node*>(b->childNodes[i]), where))
            return false;
    return true;
}

// Lay `doc` out incrementally after `mutate`, build the same document fresh, and
// require the two to agree box for box.
void checkGridMatchesFullLayout(const std::vector<std::string>& before,
                                const std::vector<std::string>& after,
                                size_t changed, const char* what) {
    Metrics metrics;
    Viewport vp{800.0f, 600.0f};

    GridDoc incr;
    buildGridDoc(incr, before);
    layoutTree(incr.root, vp, metrics);

    incr.texts[changed]->text = after[changed];
    markDirty(incr.cells[changed]);
    layoutTree(incr.root, vp, metrics);

    GridDoc fresh;
    buildGridDoc(fresh, after);
    layoutTree(fresh.root, vp, metrics);

    std::string where;
    bool ok = boxesAgree(incr.root, fresh.root, where);
    std::string label = std::string(what) + ": incremental grid matches a full layout";
    if (!ok) label += " [" + where + "]";
    check(ok, label.c_str());
}

void testGridItemReuse() {
    printf("--- Incremental: grid item reuse ---\n");

    // Long enough to wrap in a ~395px column; short enough not to.
    const std::string tall  = "the quick brown fox jumps over the lazy dog and keeps "
                              "on running well past the end of the line";
    const std::string short_ = "brief";
    const std::string mid   = "a somewhat longer line that wraps exactly once here ok";

    // The ratchet case. Cell 0 is the tallest item in row 0, so it alone sizes
    // the row, and align-stretch writes that row height into cell 1's box. Make
    // cell 0 short: the row must shrink, which it cannot do if the track sizing
    // reads cell 1's stretched box back as its content contribution.
    checkGridMatchesFullLayout({tall, short_, mid, short_},
                               {short_, short_, mid, short_}, 0, "row shrinks");

    // And the other direction, which a ratchet would get right by luck.
    checkGridMatchesFullLayout({short_, short_, mid, short_},
                               {tall, short_, mid, short_}, 0, "row grows");

    // Changing the item that is *not* driving its row must leave the row alone —
    // this is the one where cell 1's box is stretched and cell 0's is reused.
    checkGridMatchesFullLayout({tall, short_, mid, short_},
                               {tall, mid, mid, short_}, 1, "non-driving item");

    // A row must shrink when its driver shrinks even though a stretched sibling
    // still holds the old height — same as the first case, but in row 1, so the
    // rows above it are all clean and reused.
    checkGridMatchesFullLayout({short_, short_, tall, short_},
                               {short_, short_, short_, short_}, 2, "second row shrinks");

    // The reuse has to actually be happening, or the checks above pass vacuously.
    {
        Metrics metrics;
        Viewport vp{800.0f, 600.0f};
        GridDoc d;
        buildGridDoc(d, {tall, short_, mid, short_});
        layoutTree(d.root, vp, metrics);

        d.texts[0]->text = short_;
        markDirty(d.cells[0]);
        layoutTree(d.root, vp, metrics);
        const auto& st = lastLayoutStats();
        check(st.reused > 0, "grid items are reused across passes");
        check(st.laidOut < 5, "only the changed item's chain is laid out again");
    }
}

}  // namespace

void testIncrementalLayout() {
    testMarkDirtyWalksPastStuckAncestor();
    testTableCellTextRelayout();
    testCleanSubtreeIsStable();
    testGridItemReuse();
}
