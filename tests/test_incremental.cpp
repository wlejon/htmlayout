#include "test_incremental.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>

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

}  // namespace

void testIncrementalLayout() {
    testMarkDirtyWalksPastStuckAncestor();
    testTableCellTextRelayout();
    testCleanSubtreeIsStable();
}
