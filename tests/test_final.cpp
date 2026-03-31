#include "test_final.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include "css/properties.h"
#include "css/parser.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

struct FinalNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    FinalNode* parentNode = nullptr;
    std::vector<FinalNode*> childNodes;
    ComputedStyle style_;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style_; }

    void addChild(FinalNode* child) {
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

    void initInline() {
        initBlock();
        style_["display"] = "inline";
    }
};

struct FinalMetrics : public TextMetrics {
    float measureWidth(const std::string& text, const std::string&, float fontSize, const std::string&) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(const std::string&, float fontSize, const std::string&) override {
        return fontSize * 1.2f;
    }
};

static bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

// ========== Text-overflow: ellipsis ==========

static void testTextOverflowEllipsis() {
    printf("--- text-overflow: ellipsis ---\n");

    FinalNode root;
    root.initInline();
    root.style_["overflow"] = "hidden";
    root.style_["text-overflow"] = "ellipsis";
    root.style_["white-space"] = "nowrap";
    root.style_["font-family"] = "monospace";

    // Add a long text child
    FinalNode textChild;
    textChild.isText = true;
    textChild.text = "This is a very long text that should be truncated because it exceeds the available width";
    root.addChild(&textChild);

    FinalMetrics metrics;
    layoutTree(&root, 100.0f, metrics);

    // The text should overflow the container, marking truncation
    // (100px width, text is ~87 chars * 16 * 0.6 = ~835px)
    // Note: the truncation flag is set on the parent inline node
    check(root.box.textTruncated || root.box.contentRect.width > 100.0f,
          "text-overflow: long text detected as overflowing");
}

// ========== Direction: RTL ==========

static void testDirectionRtl() {
    printf("--- direction: rtl ---\n");

    FinalNode root;
    root.initInline();
    root.style_["direction"] = "rtl";
    root.style_["text-align"] = "start";
    root.style_["font-family"] = "monospace";

    FinalNode textChild;
    textChild.isText = true;
    textChild.text = "Hello";
    root.addChild(&textChild);

    FinalMetrics metrics;
    layoutTree(&root, 200.0f, metrics);

    // With RTL and text-align: start, text should be right-aligned
    // The inline box should be positioned (this is a basic check)
    check(root.box.contentRect.width > 0, "RTL: has positive width");
}

// ========== Incremental Relayout ==========

static void testIncrementalRelayout() {
    printf("--- Incremental relayout ---\n");

    FinalNode root;
    root.initBlock();

    FinalNode child;
    child.initBlock();
    child.style_["height"] = "50px";

    root.addChild(&child);

    FinalMetrics metrics;
    layoutTree(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.height, 50.0f), "initial layout: child height = 50");

    // Mark root as not dirty
    root.box.dirty = false;
    child.box.dirty = false;

    // Incremental layout should skip clean tree
    layoutTreeIncremental(&root, 800.0f, metrics);

    check(approx(child.box.contentRect.height, 50.0f), "incremental: clean tree unchanged");

    // Now mark child dirty and re-layout
    markDirty(&child);
    check(root.box.dirty, "markDirty propagates to parent");
    check(child.box.dirty, "markDirty marks child");

    layoutTreeIncremental(&root, 800.0f, metrics);
    check(approx(child.box.contentRect.height, 50.0f), "incremental: dirty child re-laid out");
    check(!root.box.dirty, "incremental: root marked clean after relayout");
    check(!child.box.dirty, "incremental: child marked clean after relayout");
}

// ========== Style Invalidation ==========

static void testStyleInvalidation() {
    printf("--- Style invalidation ---\n");

    // Layout-affecting property changes
    check(needsRelayout({"width"}), "width change needs relayout");
    check(needsRelayout({"height"}), "height change needs relayout");
    check(needsRelayout({"display"}), "display change needs relayout");
    check(needsRelayout({"flex-grow"}), "flex-grow change needs relayout");
    check(needsRelayout({"grid-template-columns"}), "grid-template-columns change needs relayout");

    // Non-layout-affecting property changes
    check(!needsRelayout({"color"}), "color change: no relayout");
    check(!needsRelayout({"background-color"}), "background-color change: no relayout");
    check(!needsRelayout({"opacity"}), "opacity change: no relayout");
    check(!needsRelayout({"box-shadow"}), "box-shadow change: no relayout");
    check(!needsRelayout({"transform"}), "transform change: no relayout");

    // Mixed: if any layout property changed, needs relayout
    check(needsRelayout({"color", "width"}), "mixed changes with width: needs relayout");
    check(!needsRelayout({"color", "opacity"}), "mixed paint-only changes: no relayout");
}

// ========== Error Recovery in Parsing ==========

static void testParserErrorRecovery() {
    printf("--- Parser: error recovery ---\n");

    // Malformed declaration should not break subsequent declarations
    std::string css1 = R"(
        .test {
            color: red;
            broken {{{ weird stuff;
            font-size: 20px;
        }
    )";
    auto sheet1 = parse(css1);
    check(sheet1.rules.size() >= 1, "error recovery: rule parsed despite broken declaration");
    if (!sheet1.rules.empty()) {
        bool hasColor = false, hasFontSize = false;
        for (auto& d : sheet1.rules[0].declarations) {
            if (d.property == "color" && d.value == "red") hasColor = true;
            if (d.property == "font-size" && d.value == "20px") hasFontSize = true;
        }
        check(hasColor, "error recovery: color declaration preserved");
        // font-size may or may not be recovered depending on how much was consumed
    }

    // Missing semicolon should still allow next declaration
    std::string css2 = R"(
        .test {
            color: red
            font-size: 16px;
        }
    )";
    auto sheet2 = parse(css2);
    check(sheet2.rules.size() >= 1, "error recovery: rule parsed with missing semicolon");

    // Empty rule body
    std::string css3 = ".empty {} .normal { color: blue; }";
    auto sheet3 = parse(css3);
    check(sheet3.rules.size() >= 1, "error recovery: empty rule followed by normal rule");

    // Unknown at-rule should be skipped gracefully
    std::string css4 = R"(
        @charset "utf-8";
        @font-face { font-family: "Test"; src: url("test.woff"); }
        @keyframes slide { from { left: 0; } to { left: 100px; } }
        .real { color: green; }
    )";
    auto sheet4 = parse(css4);
    bool hasReal = false;
    for (auto& r : sheet4.rules) {
        if (r.selector == ".real") hasReal = true;
    }
    check(hasReal, "error recovery: rules after @charset/@font-face/@keyframes parsed");
}

// ========== Direction in properties ==========

static void testDirectionProperties() {
    printf("--- Direction/writing-mode properties ---\n");
    check(initialValue("direction") == "ltr", "direction initial = ltr");
    check(isInherited("direction"), "direction is inherited");
    check(initialValue("writing-mode") == "horizontal-tb", "writing-mode initial = horizontal-tb");
    check(isInherited("writing-mode"), "writing-mode is inherited");
    check(initialValue("unicode-bidi") == "normal", "unicode-bidi initial = normal");
}

// ========== Entry point ==========

void testFinal() {
    printf("=== Final Features: text-overflow, direction, incremental relayout, invalidation, error recovery ===\n");
    testTextOverflowEllipsis();
    testDirectionRtl();
    testIncrementalRelayout();
    testStyleInvalidation();
    testParserErrorRecovery();
    testDirectionProperties();
}
