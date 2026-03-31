#include "test_remaining.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include "layout/multicol.h"
#include "css/selector.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

static bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

// ========== Mock Node ==========

struct RemMockNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    RemMockNode* parentNode = nullptr;
    std::vector<RemMockNode*> childNodes;
    ComputedStyle style_;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style_; }

    void addChild(RemMockNode* child) {
        child->parentNode = this;
        childNodes.push_back(child);
    }

    void initBlock() {
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
        style_["overflow"] = "visible";
    }
};

struct RemTextMetrics : public TextMetrics {
    // Each character is 10px wide, so "hello" = 50px
    float measureWidth(const std::string& t, const std::string&, float, const std::string&) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(const std::string&, float, const std::string&) override {
        return 20.0f;
    }
};

// ========== Intrinsic Sizing Tests ==========

static void testMinContentWidth() {
    printf("--- min-content width ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "min-content";

    // Add a text child: "hello world" => words "hello" (50px) and "world" (50px)
    // min-content = widest word = 50px
    RemMockNode textNode;
    textNode.isText = true;
    textNode.text = "hello world";
    textNode.style_["display"] = "inline";
    root.addChild(&textNode);

    RemTextMetrics m;
    layoutTree(&root, 800, m);

    check(approx(root.box.contentRect.width, 50.0f), "min-content: widest word = 50px");
}

static void testMaxContentWidth() {
    printf("--- max-content width ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "max-content";

    // "hello world" collapsed = "hello world" = 11 chars = 110px
    RemMockNode textNode;
    textNode.isText = true;
    textNode.text = "hello world";
    textNode.style_["display"] = "inline";
    root.addChild(&textNode);

    RemTextMetrics m;
    layoutTree(&root, 800, m);

    check(approx(root.box.contentRect.width, 110.0f), "max-content: full text = 110px");
}

static void testFitContentWidth() {
    printf("--- fit-content width ---\n");

    // fit-content = min(max-content, max(min-content, available))
    // text: "hello world" => min-content=50, max-content=110
    // available=80 => fit-content = min(110, max(50, 80)) = min(110, 80) = 80
    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "fit-content";

    RemMockNode textNode;
    textNode.isText = true;
    textNode.text = "hello world";
    textNode.style_["display"] = "inline";
    root.addChild(&textNode);

    RemTextMetrics m;
    layoutTree(&root, 80, m);

    check(approx(root.box.contentRect.width, 80.0f), "fit-content: clamped to available 80px");

    // With larger available space (200): fit-content = min(110, max(50, 200)) = min(110, 200) = 110
    RemMockNode root2;
    root2.initBlock();
    root2.style_["width"] = "fit-content";

    RemMockNode textNode2;
    textNode2.isText = true;
    textNode2.text = "hello world";
    textNode2.style_["display"] = "inline";
    root2.addChild(&textNode2);

    layoutTree(&root2, 200, m);

    check(approx(root2.box.contentRect.width, 110.0f), "fit-content: clamped to max-content 110px");
}

static void testIntrinsicSizingKeyword() {
    printf("--- isIntrinsicSizingKeyword ---\n");
    check(isIntrinsicSizingKeyword("min-content"), "min-content is intrinsic");
    check(isIntrinsicSizingKeyword("max-content"), "max-content is intrinsic");
    check(isIntrinsicSizingKeyword("fit-content"), "fit-content is intrinsic");
    check(!isIntrinsicSizingKeyword("auto"), "auto is not intrinsic");
    check(!isIntrinsicSizingKeyword("100px"), "100px is not intrinsic");
}

// ========== Multi-column Tests ==========

static void testMulticolBasic() {
    printf("--- Multi-column: basic ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "400px";
    root.style_["column-count"] = "2";
    root.style_["column-gap"] = "0";

    // Four children, 50px height each = 200px total
    // Split into 2 columns: 100px each
    RemMockNode c1, c2, c3, c4;
    c1.initBlock(); c1.style_["height"] = "50px";
    c2.initBlock(); c2.style_["height"] = "50px";
    c3.initBlock(); c3.style_["height"] = "50px";
    c4.initBlock(); c4.style_["height"] = "50px";
    root.addChild(&c1);
    root.addChild(&c2);
    root.addChild(&c3);
    root.addChild(&c4);

    RemTextMetrics m;
    layoutTree(&root, 800, m);

    // Children should be in two columns: first two in col 0, last two in col 1
    // Col width = 400/2 = 200px each
    check(c1.box.contentRect.x < 200.0f, "multicol: child1 in left column");
    check(c3.box.contentRect.x >= 200.0f || c4.box.contentRect.x >= 200.0f,
          "multicol: some children in right column");

    // Container height should be roughly half of total
    check(root.box.contentRect.height <= 110.0f, "multicol: container height reduced by columns");
}

static void testMulticolWithGap() {
    printf("--- Multi-column: with gap ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "420px";
    root.style_["column-count"] = "2";
    root.style_["column-gap"] = "20px";

    RemMockNode c1, c2;
    c1.initBlock(); c1.style_["height"] = "50px";
    c2.initBlock(); c2.style_["height"] = "50px";
    root.addChild(&c1);
    root.addChild(&c2);

    RemTextMetrics m;
    layoutTree(&root, 800, m);

    // Column width = (420 - 20) / 2 = 200px
    // c1 at x=0, c2 at x=200+20=220
    check(c1.box.contentRect.width <= 200.0f, "multicol gap: col width = 200px");
}

static void testMulticolColumnWidth() {
    printf("--- Multi-column: column-width ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "600px";
    root.style_["column-width"] = "150px";
    root.style_["column-gap"] = "0";

    RemMockNode c1, c2, c3, c4;
    c1.initBlock(); c1.style_["height"] = "40px";
    c2.initBlock(); c2.style_["height"] = "40px";
    c3.initBlock(); c3.style_["height"] = "40px";
    c4.initBlock(); c4.style_["height"] = "40px";
    root.addChild(&c1);
    root.addChild(&c2);
    root.addChild(&c3);
    root.addChild(&c4);

    RemTextMetrics m;
    layoutTree(&root, 800, m);

    // 600px / 150px = 4 columns
    check(isMulticolContainer(root.style_), "isMulticolContainer returns true");
    check(root.box.contentRect.width > 0, "multicol column-width: has width");
}

// ========== Pseudo-element Selector Tests ==========

static void testPseudoElementSelectors() {
    printf("--- Pseudo-element selectors: placeholder, selection, marker ---\n");

    // Parse ::placeholder
    auto sel1 = parseSelector("input::placeholder");
    check(!sel1.chain.entries.empty(), "::placeholder parses");
    bool hasPlaceholder = false;
    for (auto& s : sel1.chain.entries[0].compound.simples) {
        if (s.type == SimpleSelectorType::PseudoElement && s.value == "placeholder") {
            hasPlaceholder = true;
        }
    }
    check(hasPlaceholder, "::placeholder pseudo-element found in selector");

    // Parse ::selection
    auto sel2 = parseSelector("p::selection");
    bool hasSelection = false;
    for (auto& s : sel2.chain.entries[0].compound.simples) {
        if (s.type == SimpleSelectorType::PseudoElement && s.value == "selection") {
            hasSelection = true;
        }
    }
    check(hasSelection, "::selection pseudo-element found in selector");

    // Parse ::marker
    auto sel3 = parseSelector("li::marker");
    bool hasMarker = false;
    for (auto& s : sel3.chain.entries[0].compound.simples) {
        if (s.type == SimpleSelectorType::PseudoElement && s.value == "marker") {
            hasMarker = true;
        }
    }
    check(hasMarker, "::marker pseudo-element found in selector");

    // Specificity: pseudo-elements count as type selectors (0,0,1)
    // "li::marker" = 1 tag + 1 pseudo-element = (0,0,2)
    check(sel3.specificity == 2, "li::marker specificity = (0,0,2)");
}

// ========== Transform-aware Hit Testing ==========

static void testTransformHitTest() {
    printf("--- Transform-aware hit testing ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "400px";
    root.style_["height"] = "400px";

    RemMockNode child;
    child.initBlock();
    child.style_["width"] = "100px";
    child.style_["height"] = "100px";
    child.style_["transform"] = "translate(50px, 50px)";
    child.tag = "transformed";
    root.addChild(&child);

    RemTextMetrics m;
    layoutTree(&root, 400, m);

    // Child is at (0,0) in layout but transformed by (50,50)
    // So visually it's at (50,50)-(150,150)

    // Hit at (75, 75) should hit the transformed child
    auto* hit1 = hitTest(&root, 75, 75);
    check(hit1 != nullptr && hit1->tagName() == "transformed",
          "transform hit test: point at (75,75) hits translated child");

    // Hit at (25, 25) should NOT hit the child (it moved away from origin)
    auto* hit2 = hitTest(&root, 25, 25);
    check(hit2 == nullptr || hit2->tagName() != "transformed",
          "transform hit test: point at (25,25) misses translated child");
}

static void testTransformHitTestTranslateXY() {
    printf("--- Transform hit test: translateX/translateY ---\n");

    RemMockNode root;
    root.initBlock();
    root.style_["width"] = "400px";
    root.style_["height"] = "400px";

    RemMockNode child;
    child.initBlock();
    child.style_["width"] = "100px";
    child.style_["height"] = "100px";
    child.style_["transform"] = "translateX(200px)";
    child.tag = "tx";
    root.addChild(&child);

    RemTextMetrics m;
    layoutTree(&root, 400, m);

    // Child laid out at x=0, translated to x=200
    auto* hit = hitTest(&root, 250, 50);
    check(hit != nullptr && hit->tagName() == "tx",
          "translateX hit test: hits at translated position");

    auto* miss = hitTest(&root, 50, 50);
    check(miss == nullptr || miss->tagName() != "tx",
          "translateX hit test: misses at original position");
}

// ========== Entry Point ==========

void testRemaining() {
    printf("=== Remaining Features Tests ===\n");

    // Intrinsic sizing
    testIntrinsicSizingKeyword();
    testMinContentWidth();
    testMaxContentWidth();
    testFitContentWidth();

    // Multi-column
    testMulticolBasic();
    testMulticolWithGap();
    testMulticolColumnWidth();

    // Pseudo-element selectors
    testPseudoElementSelectors();

    // Transform-aware hit testing
    testTransformHitTest();
    testTransformHitTestTranslateXY();
}
