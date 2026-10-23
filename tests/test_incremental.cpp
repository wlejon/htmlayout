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
    std::vector<Node*> textParents;   // what to mark dirty when a text changes

    Node* alloc() {
        arena.push_back(std::make_unique<Node>());
        return arena.back().get();
    }
};

// root(block, 800px) > grid(2 equal columns) > cell(block, 5px padding) > text
// With `flexCards`, each cell is a column flex card instead:
//   cell(flex column, 5px padding) > [ title(block) > text, fill(flex: 1) ]
void buildGridDoc(GridDoc& d, const std::vector<std::string>& cellTexts,
                  bool flexCards = false) {
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
        initNode(*cell, "div", flexCards ? "flex" : "block");
        cell->style["padding-top"] = "5px";
        cell->style["padding-bottom"] = "5px";
        grid->addChild(cell);
        d.cells.push_back(cell);

        Node* holder = cell;
        if (flexCards) {
            cell->style["flex-direction"] = "column";
            holder = d.alloc();
            initNode(*holder, "div", "block");
            cell->addChild(holder);
        }

        Node* tx = d.alloc();
        tx->isText = true;
        tx->text = s;
        tx->style["font-size"] = "16px";
        holder->addChild(tx);
        d.texts.push_back(tx);
        d.textParents.push_back(holder);

        if (flexCards) {
            Node* fill = d.alloc();
            initNode(*fill, "div", "block");
            fill->style["flex-grow"] = "1";
            fill->style["flex-shrink"] = "1";
            fill->style["flex-basis"] = "0%";
            fill->style["height"] = "10px";
            cell->addChild(fill);
        }
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
                                size_t changed, const char* what,
                                bool flexCards = false) {
    Metrics metrics;
    Viewport vp{800.0f, 600.0f};

    GridDoc incr;
    buildGridDoc(incr, before, flexCards);
    layoutTree(incr.root, vp, metrics);

    incr.texts[changed]->text = after[changed];
    markDirty(incr.textParents[changed]);
    layoutTree(incr.root, vp, metrics);

    GridDoc fresh;
    buildGridDoc(fresh, after, flexCards);
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

    // Stretched column-flex cards with a flex:1 fill: when a row-mate makes
    // the row taller (or shorter), the reused card's fill must follow the
    // card's new stretched height, as it would in a fresh layout.
    checkGridMatchesFullLayout({short_, short_, mid, short_},
                               {short_, tall, mid, short_}, 1, "flex card: neighbour grows", true);
    checkGridMatchesFullLayout({short_, tall, mid, short_},
                               {short_, mid, mid, short_}, 1, "flex card: neighbour shrinks", true);
    checkGridMatchesFullLayout({short_, tall, mid, short_},
                               {short_, short_, mid, short_}, 1, "flex card: back to natural", true);

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
        // The changed item's chain, plus cell 1: the row shrank, so its
        // stretched height changed, and a stretched item's contents are laid
        // out against that height (a flex:1 or % child inside it must follow).
        check(st.laidOut < 6, "only the changed item's chain and its stretched row-mate are laid out again");
    }
}

// Deep box comparison: contentRect, padding, border, margin, hitBounds, and escapeAbsBounds.
bool boxesAgreeDeep(Node* a, Node* b, std::string& where) {
    const std::string& dispA = a->style.count("display") ? a->style.at("display") : "";
    if (dispA == "none") return true;

    auto rectEq = [](const Rect& r1, const Rect& r2) {
        if (r1.width < 0 && r2.width < 0) return true;
        return approx(r1.x, r2.x) && approx(r1.y, r2.y) &&
               approx(r1.width, r2.width) && approx(r1.height, r2.height);
    };
    auto edgesEq = [](const Edges& e1, const Edges& e2) {
        return approx(e1.top, e2.top) && approx(e1.right, e2.right) &&
               approx(e1.bottom, e2.bottom) && approx(e1.left, e2.left);
    };

    if (!rectEq(a->box.contentRect, b->box.contentRect)) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "<%s> contentRect incr(%.1f,%.1f %.1fx%.1f) vs fresh(%.1f,%.1f %.1fx%.1f)",
                 a->isText ? "#text" : a->tag.c_str(),
                 a->box.contentRect.x, a->box.contentRect.y,
                 a->box.contentRect.width, a->box.contentRect.height,
                 b->box.contentRect.x, b->box.contentRect.y,
                 b->box.contentRect.width, b->box.contentRect.height);
        where = buf;
        return false;
    }
    if (!a->isText) {
        if (!edgesEq(a->box.padding, b->box.padding) ||
            !edgesEq(a->box.border, b->box.border) ||
            !edgesEq(a->box.margin, b->box.margin)) {
            where = "<" + a->tag + "> edges mismatch";
            return false;
        }
        if (!rectEq(a->box.hitBounds, b->box.hitBounds)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "<%s> hitBounds incr(%.1f,%.1f %.1fx%.1f) vs fresh(%.1f,%.1f %.1fx%.1f)",
                     a->tag.c_str(),
                     a->box.hitBounds.x, a->box.hitBounds.y,
                     a->box.hitBounds.width, a->box.hitBounds.height,
                     b->box.hitBounds.x, b->box.hitBounds.y,
                     b->box.hitBounds.width, b->box.hitBounds.height);
            where = buf;
            return false;
        }
        if (!rectEq(a->escapeAbsBounds, b->escapeAbsBounds)) {
            where = "<" + a->tag + "> escapeAbsBounds mismatch";
            return false;
        }
    }
    for (size_t i = 0; i < a->childNodes.size(); i++) {
        if (!boxesAgreeDeep(static_cast<Node*>(a->childNodes[i]),
                            static_cast<Node*>(b->childNodes[i]), where))
            return false;
    }
    return true;
}

// Clone a Node subtree with clean LayoutNode state (for fresh full-tree oracle comparison).
struct ClonedTree {
    std::vector<std::unique_ptr<Node>> nodes;
    Node* root = nullptr;

    Node* cloneRec(const Node* src) {
        nodes.push_back(std::make_unique<Node>());
        Node* dst = nodes.back().get();
        dst->tag = src->tag;
        dst->isText = src->isText;
        dst->text = src->text;
        dst->style = src->style;
        for (auto* ch : src->childNodes) {
            Node* c = cloneRec(static_cast<const Node*>(ch));
            dst->addChild(c);
        }
        return dst;
    }
};

bool verifyAgainstFreshOracle(Node* incrRoot, const Viewport& vp, Metrics& metrics,
                              std::string& diffOut) {
    ClonedTree fresh;
    fresh.root = fresh.cloneRec(incrRoot);
    layoutTree(fresh.root, vp, metrics);
    return boxesAgreeDeep(incrRoot, fresh.root, diffOut);
}

void testPositionedHitBoundsInMovedCleanSubtree() {
    printf("--- Incremental: positioned descendant hitBounds in moved clean subtree ---\n");
    Node root, header, cleanWrap, innerBlock, absBadge, fixedHud;
    initNode(root, "div", "block");
    root.style["position"] = "relative";
    root.style["width"] = "800px";
    root.style["height"] = "600px";

    initNode(header, "div", "block");
    header.style["height"] = "40px";

    initNode(cleanWrap, "div", "block");
    cleanWrap.style["overflow"] = "hidden";
    cleanWrap.style["height"] = "100px";

    initNode(innerBlock, "div", "block");
    innerBlock.style["height"] = "30px";

    initNode(absBadge, "div", "block");
    absBadge.style["position"] = "absolute";
    absBadge.style["top"] = "15px";
    absBadge.style["left"] = "250px";
    absBadge.style["width"] = "60px";
    absBadge.style["height"] = "25px";

    initNode(fixedHud, "div", "block");
    fixedHud.style["position"] = "fixed";
    fixedHud.style["bottom"] = "20px";
    fixedHud.style["right"] = "30px";
    fixedHud.style["width"] = "90px";
    fixedHud.style["height"] = "35px";

    root.addChild(&header);
    root.addChild(&cleanWrap);
    cleanWrap.addChild(&innerBlock);
    cleanWrap.addChild(&absBadge);
    cleanWrap.addChild(&fixedHud);

    Metrics metrics;
    Viewport vp{800.0f, 600.0f};
    layoutTree(&root, vp, metrics);

    // Mutate only `header` height so `cleanWrap` moves vertically without being dirty.
    const char* heights[] = {"120px", "10px", "200px", "40px"};
    for (const char* h : heights) {
        header.style["height"] = h;
        markDirty(&header);
        layoutTree(&root, vp, metrics);

        std::string diff;
        bool ok = verifyAgainstFreshOracle(&root, vp, metrics, diff);
        check(ok, (std::string("positioned hitBounds after header=") + h +
                   (ok ? "" : " [" + diff + "]")).c_str());

        // Hit-test directly on absBadge (250 + 30, 15 + 12) and fixedHud (800 - 30 - 45, 600 - 20 - 15)
        LayoutNode* hitAbs = hitTest(&root, 260.0f, 25.0f);
        check(hitAbs == &absBadge, "hitTest finds absBadge after clean parent moved");
        LayoutNode* hitFix = hitTest(&root, 725.0f, 565.0f);
        check(hitFix == &fixedHud, "hitTest finds fixedHud after clean parent moved");
    }
}

void testGridAndFlexAlignmentMutationOracle() {
    printf("--- Incremental: differential oracle for Grid & Flex alignment/stretch mutations ---\n");
    Metrics metrics;
    Viewport vp{800.0f, 600.0f};

    // Build a composite document with both a Grid section and a Flex section
    GridDoc doc;
    Node* root = doc.alloc();
    initNode(*root, "div", "block");
    root->style["width"] = "800px";

    // Section 1: Grid container with 2 columns, explicit height, and mixed items
    Node* grid = doc.alloc();
    initNode(*grid, "div", "grid");
    grid->style["grid-template-columns"] = "1fr 2fr";
    grid->style["column-gap"] = "12px";
    grid->style["row-gap"] = "10px";
    grid->style["align-items"] = "stretch";
    grid->style["justify-items"] = "stretch";
    root->addChild(grid);

    Node* gItem0 = doc.alloc();
    initNode(*gItem0, "div", "flex");
    gItem0->style["flex-direction"] = "column";
    gItem0->style["padding-top"] = "4px";
    gItem0->style["padding-bottom"] = "4px";
    grid->addChild(gItem0);
    Node* gItem0TextWrap = doc.alloc();
    initNode(*gItem0TextWrap, "div", "block");
    Node* gItem0Text = doc.alloc();
    gItem0Text->isText = true;
    gItem0Text->text = "short label";
    gItem0Text->style["font-size"] = "16px";
    gItem0TextWrap->addChild(gItem0Text);
    gItem0->addChild(gItem0TextWrap);
    Node* gItem0Fill = doc.alloc();
    initNode(*gItem0Fill, "div", "block");
    gItem0Fill->style["flex-grow"] = "1";
    gItem0Fill->style["height"] = "12px";
    gItem0->addChild(gItem0Fill);

    Node* gItem1 = doc.alloc();
    initNode(*gItem1, "div", "block");
    gItem1->style["height"] = "90px";
    Node* gItem1Pct = doc.alloc();
    initNode(*gItem1Pct, "div", "block");
    gItem1Pct->style["height"] = "50%";
    gItem1->addChild(gItem1Pct);
    grid->addChild(gItem1);

    // Section 2: Row Flex container with definite height and nested items
    Node* flexRow = doc.alloc();
    initNode(*flexRow, "div", "flex");
    flexRow->style["flex-direction"] = "row";
    flexRow->style["height"] = "140px";
    flexRow->style["gap"] = "8px";
    flexRow->style["align-items"] = "stretch";
    root->addChild(flexRow);

    Node* fItem0 = doc.alloc();
    initNode(*fItem0, "div", "flex");
    fItem0->style["flex-direction"] = "column";
    fItem0->style["flex-grow"] = "1";
    fItem0->style["flex-basis"] = "0px";
    Node* fItem0Child = doc.alloc();
    initNode(*fItem0Child, "div", "block");
    fItem0Child->style["flex-grow"] = "1";
    fItem0Child->style["height"] = "20px";
    fItem0->addChild(fItem0Child);
    flexRow->addChild(fItem0);

    Node* fItem1 = doc.alloc();
    initNode(*fItem1, "div", "block");
    fItem1->style["width"] = "120px";
    Node* fItem1Text = doc.alloc();
    fItem1Text->isText = true;
    fItem1Text->text = "flex item text";
    fItem1Text->style["font-size"] = "16px";
    fItem1->addChild(fItem1Text);
    flexRow->addChild(fItem1);

    // Section 3: Column Flex container with auto height and flex-grow item
    Node* flexCol = doc.alloc();
    initNode(*flexCol, "div", "flex");
    flexCol->style["flex-direction"] = "column";
    flexCol->style["align-items"] = "stretch";
    root->addChild(flexCol);

    Node* fcItem0 = doc.alloc();
    initNode(*fcItem0, "div", "block");
    fcItem0->style["flex-grow"] = "1";
    fcItem0->style["flex-basis"] = "0px";
    Node* fcItem0Text = doc.alloc();
    fcItem0Text->isText = true;
    fcItem0Text->text = "column flex child";
    fcItem0Text->style["font-size"] = "16px";
    fcItem0->addChild(fcItem0Text);
    flexCol->addChild(fcItem0);

    layoutTree(root, vp, metrics);

    struct Step {
        const char* desc;
        void (*apply)(Node* root, Node* grid, Node* gItem0, Node* gItem0TextWrap,
                      Node* gItem0Text, Node* gItem1, Node* flexRow, Node* fItem0,
                      Node* fItem1, Node* flexCol, Node* fcItem0);
    };

    Step steps[] = {
        {"grid align-items: center (un-stretch)", [](Node*, Node* grid, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*) {
            grid->style["align-items"] = "center";
            markDirty(grid);
        }},
        {"grid align-items: end", [](Node*, Node* grid, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*) {
            grid->style["align-items"] = "end";
            markDirty(grid);
        }},
        {"grid align-items: stretch (re-stretch)", [](Node*, Node* grid, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*) {
            grid->style["align-items"] = "stretch";
            markDirty(grid);
        }},
        {"grid item0 dirty while stretched", [](Node*, Node*, Node*, Node* wrap, Node* txt, Node*, Node*, Node*, Node*, Node*, Node*) {
            txt->text = "longer label text that wraps across lines in column 0 of the grid";
            markDirty(wrap);
        }},
        {"grid item1 shrinks", [](Node*, Node*, Node*, Node*, Node*, Node* g1, Node*, Node*, Node*, Node*, Node*) {
            g1->style["height"] = "30px";
            markDirty(g1);
        }},
        {"flexRow align-items: center (un-stretch definite row)", [](Node*, Node*, Node*, Node*, Node*, Node*, Node* fr, Node*, Node*, Node*, Node*) {
            fr->style["align-items"] = "center";
            markDirty(fr);
        }},
        {"flexRow align-items: flex-end", [](Node*, Node*, Node*, Node*, Node*, Node*, Node* fr, Node*, Node*, Node*, Node*) {
            fr->style["align-items"] = "flex-end";
            markDirty(fr);
        }},
        {"flexRow align-items: stretch (re-stretch definite row)", [](Node*, Node*, Node*, Node*, Node*, Node*, Node* fr, Node*, Node*, Node*, Node*) {
            fr->style["align-items"] = "stretch";
            markDirty(fr);
        }},
        {"flexRow height auto -> definite -> auto", [](Node*, Node*, Node*, Node*, Node*, Node*, Node* fr, Node*, Node*, Node*, Node*) {
            fr->style["height"] = "auto";
            markDirty(fr);
        }},
        {"flexCol height definite 120px -> auto", [](Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node* fc, Node*) {
            fc->style["height"] = "120px";
            markDirty(fc);
        }},
        {"flexCol height back to auto", [](Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node* fc, Node*) {
            fc->style["height"] = "auto";
            markDirty(fc);
        }},
        {"flexCol align-items: center -> stretch", [](Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node* fc, Node*) {
            fc->style["align-items"] = "center";
            markDirty(fc);
        }},
        {"flexCol align-items back to stretch", [](Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node* fc, Node*) {
            fc->style["align-items"] = "stretch";
            markDirty(fc);
        }},
        {"display:none toggle on fItem1", [](Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node* f1, Node*, Node*) {
            f1->style["display"] = "none";
            markDirty(f1);
        }},
        {"restore fItem1 display:block", [](Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node*, Node* f1, Node*, Node*) {
            f1->style["display"] = "block";
            markDirty(f1);
        }},
    };

    for (const auto& s : steps) {
        s.apply(root, grid, gItem0, gItem0TextWrap, gItem0Text, gItem1,
                flexRow, fItem0, fItem1, flexCol, fcItem0);
        layoutTree(root, vp, metrics);
        std::string diff;
        bool ok = verifyAgainstFreshOracle(root, vp, metrics, diff);
        std::string msg = std::string(s.desc) + (ok ? "" : " [" + diff + "]");
        check(ok, msg.c_str());
    }

    // Randomized multi-step mutation fuzzing against the fresh full-tree oracle
    uint32_t rng = 0xC0FFEE11u;
    auto nextRng = [&]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    const char* aligns[] = {"stretch", "center", "flex-start", "flex-end"};
    const char* heights[] = {"auto", "45px", "90px", "150px"};
    const char* gaps[] = {"0px", "6px", "16px"};
    const char* sampleTexts[] = {
        "x",
        "medium length text item",
        "a very long text run that wraps across multiple lines inside its column container box"
    };

    bool allFuzzOk = true;
    std::string firstFuzzFailure;
    for (int step = 0; step < 50; ++step) {
        switch (nextRng() % 6) {
            case 0:
                grid->style["align-items"] = aligns[nextRng() % 4];
                grid->style["column-gap"] = gaps[nextRng() % 3];
                markDirty(grid);
                break;
            case 1:
                gItem0Text->text = sampleTexts[nextRng() % 3];
                markDirty(gItem0TextWrap);
                break;
            case 2:
                gItem1->style["height"] = heights[nextRng() % 4];
                markDirty(gItem1);
                break;
            case 3:
                flexRow->style["align-items"] = aligns[nextRng() % 4];
                flexRow->style["height"] = heights[nextRng() % 4];
                markDirty(flexRow);
                break;
            case 4:
                fItem1Text->text = sampleTexts[nextRng() % 3];
                markDirty(fItem1);
                break;
            case 5:
                flexCol->style["height"] = heights[nextRng() % 4];
                flexCol->style["align-items"] = aligns[nextRng() % 4];
                markDirty(flexCol);
                break;
        }
        Viewport stepVp = (step % 10 == 9) ? Viewport{640.0f, 480.0f} : Viewport{800.0f, 600.0f};
        layoutTree(root, stepVp, metrics);
        std::string diff;
        if (!verifyAgainstFreshOracle(root, stepVp, metrics, diff)) {
            allFuzzOk = false;
            firstFuzzFailure = "step " + std::to_string(step) + ": " + diff;
            break;
        }
    }
    check(allFuzzOk, (std::string("50-step PRNG differential mutation sequence matches fresh layout") +
                      (allFuzzOk ? "" : " [" + firstFuzzFailure + "]")).c_str());
}

}  // namespace

void testIncrementalLayout() {
    testMarkDirtyWalksPastStuckAncestor();
    testTableCellTextRelayout();
    testCleanSubtreeIsStable();
    testGridItemReuse();
    testPositionedHitBoundsInMovedCleanSubtree();
    testGridAndFlexAlignmentMutationOracle();
}
