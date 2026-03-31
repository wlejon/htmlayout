#include "test_hittest.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

struct HitMockNode : public LayoutNode {
    std::string tag;
    bool isText = false;
    std::string text;
    HitMockNode* parentNode = nullptr;
    std::vector<HitMockNode*> childNodes;
    ComputedStyle style;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style; }
    void addChild(HitMockNode* c) { c->parentNode = this; childNodes.push_back(c); }
};

struct HitTextMetrics : public TextMetrics {
    float measureWidth(const std::string& t, const std::string&, float, const std::string&) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(const std::string&, float, const std::string&) override { return 20.0f; }
};

static void initBlock(HitMockNode& n, const std::string& tagName = "div") {
    n.tag = tagName;
    n.style["display"] = "block";
    n.style["position"] = "static";
    n.style["width"] = "auto"; n.style["height"] = "auto";
    n.style["min-width"] = "0"; n.style["min-height"] = "0";
    n.style["max-width"] = "none"; n.style["max-height"] = "none";
    n.style["margin-top"] = "0"; n.style["margin-right"] = "0";
    n.style["margin-bottom"] = "0"; n.style["margin-left"] = "0";
    n.style["padding-top"] = "0"; n.style["padding-right"] = "0";
    n.style["padding-bottom"] = "0"; n.style["padding-left"] = "0";
    n.style["border-top-width"] = "0"; n.style["border-right-width"] = "0";
    n.style["border-bottom-width"] = "0"; n.style["border-left-width"] = "0";
    n.style["border-top-style"] = "none"; n.style["border-right-style"] = "none";
    n.style["border-bottom-style"] = "none"; n.style["border-left-style"] = "none";
    n.style["box-sizing"] = "content-box";
    n.style["font-size"] = "16px"; n.style["overflow"] = "visible";
}

static void testHitBasic() {
    printf("--- HitTest: basic ---\n");
    HitMockNode root; initBlock(root);
    root.style["width"] = "200px";
    root.style["height"] = "100px";

    HitTextMetrics m;
    layoutTree(&root, 200, m);

    check(hitTest(&root, 50, 50) == &root, "hit inside root");
    check(hitTest(&root, 250, 50) == nullptr, "miss outside root");
    check(hitTest(&root, 50, 150) == nullptr, "miss below root");
}

static void testHitChild() {
    printf("--- HitTest: child ---\n");
    HitMockNode root; initBlock(root);
    HitMockNode child; initBlock(child);
    child.style["height"] = "50px";
    root.addChild(&child);

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    check(hitTest(&root, 100, 25) == &child, "hit child");
    check(hitTest(&root, 100, 60) == nullptr, "miss below child (root has auto height = 50)");
}

static void testHitNestedChildren() {
    printf("--- HitTest: nested children ---\n");
    HitMockNode root; initBlock(root);
    HitMockNode parent; initBlock(parent);
    parent.style["height"] = "100px";
    HitMockNode child; initBlock(child);
    child.style["height"] = "40px";
    parent.addChild(&child);
    root.addChild(&parent);

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    check(hitTest(&root, 100, 20) == &child, "hit deepest child");
    check(hitTest(&root, 100, 60) == &parent, "hit parent (below child)");
}

static void testHitSiblingZOrder() {
    printf("--- HitTest: z-order (later sibling on top) ---\n");
    HitMockNode root; initBlock(root);
    HitMockNode c1; initBlock(c1, "first");
    c1.style["height"] = "80px";
    HitMockNode c2; initBlock(c2, "second");
    c2.style["height"] = "80px";
    root.addChild(&c1);
    root.addChild(&c2);

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    // c1 at y=0..80, c2 at y=80..160
    check(hitTest(&root, 100, 40) == &c1, "hit first child");
    check(hitTest(&root, 100, 120) == &c2, "hit second child");
}

static void testHitDisplayNone() {
    printf("--- HitTest: display none ---\n");
    HitMockNode root; initBlock(root);
    root.style["height"] = "100px";
    HitMockNode hidden; initBlock(hidden);
    hidden.style["display"] = "none";
    hidden.style["height"] = "100px";
    root.addChild(&hidden);

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    // Hidden element should not be hit
    check(hitTest(&root, 100, 50) == &root, "display:none not hit-testable");
}

static void testHitPointerEventsNone() {
    printf("--- HitTest: pointer-events none ---\n");
    HitMockNode root; initBlock(root);
    HitMockNode child; initBlock(child);
    child.style["height"] = "50px";
    child.style["pointer-events"] = "none";
    root.addChild(&child);

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    // pointer-events:none should pass through to parent
    check(hitTest(&root, 100, 25) == &root, "pointer-events:none passes through to parent");
}

static void testHitWithPadding() {
    printf("--- HitTest: with padding ---\n");
    HitMockNode root; initBlock(root);
    root.style["width"] = "200px";
    root.style["height"] = "100px";
    root.style["padding-top"] = "20px";
    root.style["padding-left"] = "20px";

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    // Root border box starts at (0,0), content at (20,20)
    // Clicking in padding area (10, 10) should hit root
    check(hitTest(&root, 10, 10) == &root, "hit root padding area (no children)");
    check(hitTest(&root, 100, 50) == &root, "hit root content area");
}

static void testHitNull() {
    printf("--- HitTest: null ---\n");
    check(hitTest(nullptr, 50, 50) == nullptr, "null root returns null");
}

void testHitTest() {
    testHitBasic();
    testHitChild();
    testHitNestedChildren();
    testHitSiblingZOrder();
    testHitDisplayNone();
    testHitPointerEventsNone();
    testHitWithPadding();
    testHitNull();
}
