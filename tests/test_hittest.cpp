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
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override {
        return childNodes;
    }
    const ComputedStyle& computedStyle() const override { return style; }
    void addChild(HitMockNode* c) { c->parentNode = this; childNodes.push_back(c); }
};

struct HitTextMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
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

static void testHitPointerEventsNoneParent() {
    printf("--- HitTest: pointer-events none on parent, auto on child ---\n");
    // Common overlay pattern: wrapper opts out of hit-testing so siblings
    // underneath stay clickable, and interactive children opt back in.
    HitMockNode root; initBlock(root);
    HitMockNode overlay; initBlock(overlay);
    overlay.style["height"] = "100px";
    overlay.style["pointer-events"] = "none";
    HitMockNode button; initBlock(button);
    button.style["height"] = "30px";
    button.style["pointer-events"] = "auto";
    overlay.addChild(&button);
    root.addChild(&overlay);

    HitTextMetrics m;
    layoutTree(&root, 400, m);

    // Click on the button should reach the button, not stop at the overlay.
    check(hitTest(&root, 100, 15) == &button,
          "pointer-events:auto child is hittable under pointer-events:none parent");
    // Click on the overlay outside the button should fall through to root.
    check(hitTest(&root, 100, 60) == &root,
          "pointer-events:none parent is not itself a hit target");
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

static void testHitBodyOverflowPropagation() {
    printf("--- HitTest: body overflow propagates to the viewport ---\n");
    // CSS 2.1 §11.1.1. `body { overflow: hidden }` with everything inside it
    // out of flow — the layout the three.js editor and countless other apps
    // use. body's own box is then zero-height, so treating it as a clipper
    // rejects the whole document at the first step: the page paints and
    // swallows every click.
    HitMockNode html; initBlock(html, "html");
    HitMockNode body; initBlock(body, "body");
    body.style["overflow"] = "hidden";
    HitMockNode bar; initBlock(bar, "div");
    bar.style["position"] = "absolute";
    bar.style["top"] = "0"; bar.style["left"] = "0";
    bar.style["width"] = "200px"; bar.style["height"] = "32px";
    body.addChild(&bar);
    html.addChild(&body);

    HitTextMetrics m;
    layoutTree(&html, 200, m);

    check(body.box.fullHeight() == 0.0f,
          "body's own box is zero-height (all children out of flow)");
    check(hitTest(&html, 100, 16) == &bar,
          "absolutely positioned child of an overflow:hidden body is hittable");

    // The donation only reaches <body> while the root is itself visible. Give
    // the root a clip of its own and it keeps it — and a zero-height root then
    // legitimately swallows the point.
    html.style["overflow"] = "hidden";
    layoutTree(&html, 200, m);
    check(hitTest(&html, 100, 16) == nullptr,
          "an overflow:hidden root still clips (it is the donor, not a donee)");
}

static void testHitBlockInInlineRegrow() {
    printf("--- HitTest: a block inside an inline that grows ---\n");
    // The inline formatting context lays block-in-inline children out itself,
    // by calling the block engine directly rather than through layoutNode().
    // Nothing claimed them for the pass, so the hit-bounds cache read them as
    // "shape unchanged" and kept the bounds they had the first time — every
    // descendant past those stale bounds vanished from hit testing while
    // painting normally. (The three.js editor wraps its whole property sidebar
    // in one <span>; its controls stopped responding as soon as a panel grew.)
    HitMockNode root; initBlock(root);
    root.style["width"] = "300px";
    HitMockNode span; initBlock(span, "span");
    span.style["display"] = "inline";
    HitMockNode panel; initBlock(panel, "div");
    panel.style["height"] = "40px";
    HitMockNode inner; initBlock(inner, "div");
    inner.style["height"] = "40px";
    panel.addChild(&inner);
    span.addChild(&panel);
    root.addChild(&span);

    HitTextMetrics m;
    layoutTree(&root, 300, m);
    check(hitTest(&root, 100, 20) == &inner, "the inner block is hittable to begin with");

    // Grow it, the way a panel does when its contents are rebuilt.
    inner.style["height"] = "400px";
    panel.style["height"] = "400px";
    markSubtreeDirty(&root);
    layoutTree(&root, 300, m);

    check(inner.box.fullHeight() == 400.0f, "the inner block really did grow");
    check(hitTest(&root, 100, 20) == &inner, "still hittable near the top");
    check(hitTest(&root, 100, 300) == &inner,
          "and hittable in the part that only exists after the regrow");
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
    testHitPointerEventsNoneParent();
    testHitWithPadding();
    testHitBodyOverflowPropagation();
    testHitBlockInInlineRegrow();
    testHitNull();
}
