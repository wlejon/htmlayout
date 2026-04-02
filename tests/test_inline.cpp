#include "test_inline.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/text.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <unordered_map>

using namespace htmlayout::layout;
using namespace htmlayout::css;

// ---- Mock LayoutNode ----

struct InlineMockNode : public LayoutNode {
    std::string tag;
    bool isText = false;
    std::string text;
    InlineMockNode* parentNode = nullptr;
    std::vector<InlineMockNode*> childNodes;
    ComputedStyle style;

    std::string tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        return {childNodes.begin(), childNodes.end()};
    }
    const ComputedStyle& computedStyle() const override { return style; }

    void addChild(InlineMockNode* child) {
        child->parentNode = this;
        childNodes.push_back(child);
    }
};

// Fixed-width mock: each character = 10px wide, line height = 20px
struct FixedTextMetrics : public TextMetrics {
    float measureWidth(const std::string& text,
                       const std::string&, float,
                       const std::string&) override {
        return static_cast<float>(text.size()) * 10.0f;
    }
    float lineHeight(const std::string&, float,
                     const std::string&) override {
        return 20.0f;
    }
};

static bool approx(float a, float b, float tol = 0.5f) {
    return std::abs(a - b) < tol;
}

static void initInline(InlineMockNode& node, const std::string& tagName = "span") {
    node.tag = tagName;
    node.style["display"] = "inline";
    node.style["position"] = "static";
    node.style["width"] = "auto";
    node.style["height"] = "auto";
    node.style["min-width"] = "0";
    node.style["min-height"] = "0";
    node.style["max-width"] = "none";
    node.style["max-height"] = "none";
    node.style["margin-top"] = "0";
    node.style["margin-right"] = "0";
    node.style["margin-bottom"] = "0";
    node.style["margin-left"] = "0";
    node.style["padding-top"] = "0";
    node.style["padding-right"] = "0";
    node.style["padding-bottom"] = "0";
    node.style["padding-left"] = "0";
    node.style["border-top-width"] = "0";
    node.style["border-right-width"] = "0";
    node.style["border-bottom-width"] = "0";
    node.style["border-left-width"] = "0";
    node.style["border-top-style"] = "none";
    node.style["border-right-style"] = "none";
    node.style["border-bottom-style"] = "none";
    node.style["border-left-style"] = "none";
    node.style["box-sizing"] = "content-box";
    node.style["font-size"] = "16px";
    node.style["font-family"] = "monospace";
    node.style["font-weight"] = "normal";
    node.style["white-space"] = "normal";
    node.style["text-align"] = "left";
    node.style["vertical-align"] = "baseline";
    node.style["overflow"] = "visible";
}

static void initBlock(InlineMockNode& node, const std::string& tagName = "div") {
    initInline(node, tagName);
    node.style["display"] = "block";
}

// ========== Text Breaking Tests ==========

static void testTextBreakSingleLine() {
    printf("--- Text: single line ---\n");
    FixedTextMetrics metrics;
    auto runs = breakTextIntoRuns("hello world", 200, "mono", 16, "normal", "normal", metrics);
    // "hello world" = 11 chars * 10px = 110px, fits in 200px
    check(runs.size() == 1, "single line: 1 run");
    check(runs[0].text == "hello world", "single line: text preserved");
    check(approx(runs[0].width, 110), "single line: width = 110");
}

static void testTextBreakWrapping() {
    printf("--- Text: word wrapping ---\n");
    FixedTextMetrics metrics;
    // "hello world" = 110px, available = 60px
    // "hello" = 50px fits, "world" = 50px doesn't fit on same line
    auto runs = breakTextIntoRuns("hello world", 60, "mono", 16, "normal", "normal", metrics);
    check(runs.size() == 2, "wrap: 2 lines");
    check(runs[0].text == "hello", "wrap: line 1 = hello");
    check(runs[1].text == "world", "wrap: line 2 = world");
}

static void testTextBreakMultipleWords() {
    printf("--- Text: multiple words wrapping ---\n");
    FixedTextMetrics metrics;
    // "aa bb cc dd" -> words: 20, 20, 20, 20 px + spaces
    // Available 60px: "aa bb" = 50px fits, "cc" would make 80px
    auto runs = breakTextIntoRuns("aa bb cc dd", 60, "mono", 16, "normal", "normal", metrics);
    check(runs.size() == 2, "multi wrap: 2 lines");
    check(runs[0].text == "aa bb", "multi wrap: line 1");
    check(runs[1].text == "cc dd", "multi wrap: line 2");
}

static void testTextBreakNowrap() {
    printf("--- Text: nowrap ---\n");
    FixedTextMetrics metrics;
    auto runs = breakTextIntoRuns("hello world test", 50, "mono", 16, "normal", "nowrap", metrics);
    check(runs.size() == 1, "nowrap: 1 run (no wrapping)");
    check(runs[0].text == "hello world test", "nowrap: all text on one line");
}

static void testTextBreakPre() {
    printf("--- Text: pre ---\n");
    FixedTextMetrics metrics;
    auto runs = breakTextIntoRuns("line1\nline2\nline3", 500, "mono", 16, "normal", "pre", metrics);
    check(runs.size() == 3, "pre: 3 lines from newlines");
    check(runs[0].text == "line1", "pre: line 1");
    check(runs[1].text == "line2", "pre: line 2");
    check(runs[2].text == "line3", "pre: line 3");
}

static void testTextBreakWhitespaceCollapse() {
    printf("--- Text: whitespace collapse ---\n");
    FixedTextMetrics metrics;
    auto runs = breakTextIntoRuns("  hello   world  ", 500, "mono", 16, "normal", "normal", metrics);
    check(runs.size() == 1, "collapse: 1 run");
    check(runs[0].text == "hello world", "collapse: extra spaces removed");
}

// ========== Inline Layout Tests ==========

static void testInlineTextLayout() {
    printf("--- Inline: text layout ---\n");
    InlineMockNode root;
    initInline(root);

    InlineMockNode textNode;
    textNode.isText = true;
    textNode.text = "hello world";
    root.addChild(&textNode);

    FixedTextMetrics metrics;
    layoutTree(&root, 500, metrics);

    check(approx(root.box.contentRect.width, 110), "inline text: width = 110px");
    check(approx(root.box.contentRect.height, 20), "inline text: height = 20px (one line)");
}

static void testInlineTextWrapping() {
    printf("--- Inline: text wrapping ---\n");
    InlineMockNode root;
    initInline(root);

    InlineMockNode textNode;
    textNode.isText = true;
    textNode.text = "hello world";
    root.addChild(&textNode);

    FixedTextMetrics metrics;
    // Available width only fits "hello" (50px)
    layoutTree(&root, 60, metrics);

    check(approx(root.box.contentRect.height, 40), "wrapped text: height = 40px (two lines)");
}

static void testInlineBlockChild() {
    printf("--- Inline: inline-block child ---\n");
    InlineMockNode root;
    initInline(root);

    InlineMockNode inlineBlock;
    initInline(inlineBlock);
    inlineBlock.style["display"] = "inline-block";
    inlineBlock.style["width"] = "100px";
    inlineBlock.style["height"] = "50px";

    root.addChild(&inlineBlock);

    FixedTextMetrics metrics;
    layoutTree(&root, 500, metrics);

    check(approx(inlineBlock.box.contentRect.width, 100), "inline-block: width = 100px");
    check(approx(inlineBlock.box.contentRect.height, 50), "inline-block: height = 50px");
}

static void testInlineTextAlignCenter() {
    printf("--- Inline: text-align center ---\n");
    InlineMockNode root;
    initBlock(root); // block container — text-align applies here
    root.style["text-align"] = "center";

    InlineMockNode textNode;
    textNode.isText = true;
    textNode.text = "hi";  // 20px wide
    root.addChild(&textNode);

    InlineMockNode inlineBlock;
    initInline(inlineBlock);
    inlineBlock.style["display"] = "inline-block";
    inlineBlock.style["width"] = "20px";
    inlineBlock.style["height"] = "20px";
    root.addChild(&inlineBlock);

    FixedTextMetrics metrics;
    layoutTree(&root, 200, metrics);

    // Total inline content = 20px text + 20px inline-block = 40px
    // Available = 200px, center offset = (200-40)/2 = 80px
    // The inline-block should be offset from left
    check(inlineBlock.box.contentRect.x >= 80, "text-align center: inline-block offset from left");
}

static void testInlineTextAlignRight() {
    printf("--- Inline: text-align right ---\n");
    InlineMockNode root;
    initBlock(root); // block container — text-align applies here
    root.style["text-align"] = "right";

    InlineMockNode inlineBlock;
    initInline(inlineBlock);
    inlineBlock.style["display"] = "inline-block";
    inlineBlock.style["width"] = "50px";
    inlineBlock.style["height"] = "20px";

    root.addChild(&inlineBlock);

    FixedTextMetrics metrics;
    layoutTree(&root, 200, metrics);

    // With right-align, x should be near 200 - 50 = 150
    check(approx(inlineBlock.box.contentRect.x, 150), "text-align right: inline-block at right edge");
}

static void testInlineMultipleInlineBlocks() {
    printf("--- Inline: multiple inline-blocks ---\n");
    InlineMockNode root;
    initInline(root);

    InlineMockNode ib1, ib2, ib3;
    initInline(ib1); ib1.style["display"] = "inline-block";
    ib1.style["width"] = "80px"; ib1.style["height"] = "30px";
    initInline(ib2); ib2.style["display"] = "inline-block";
    ib2.style["width"] = "80px"; ib2.style["height"] = "30px";
    initInline(ib3); ib3.style["display"] = "inline-block";
    ib3.style["width"] = "80px"; ib3.style["height"] = "30px";

    root.addChild(&ib1);
    root.addChild(&ib2);
    root.addChild(&ib3);

    FixedTextMetrics metrics;
    // 3 x 80px = 240px, available = 200px → 2 fit on line 1, 1 wraps
    layoutTree(&root, 200, metrics);

    // First two on line 1, third wraps to line 2
    check(approx(ib1.box.contentRect.y, ib2.box.contentRect.y),
          "ib1 and ib2 on same line (same y)");
    check(ib3.box.contentRect.y > ib1.box.contentRect.y,
          "ib3 wraps to next line (greater y)");
    check(approx(root.box.contentRect.height, 60), "total height = 2 lines x 30px = 60");
}

static void testInlineDisplayNone() {
    printf("--- Inline: display none child ---\n");
    InlineMockNode root;
    initInline(root);

    InlineMockNode ib1;
    initInline(ib1); ib1.style["display"] = "inline-block";
    ib1.style["width"] = "50px"; ib1.style["height"] = "30px";

    InlineMockNode hidden;
    initInline(hidden); hidden.style["display"] = "none";
    hidden.style["width"] = "200px"; hidden.style["height"] = "200px";

    root.addChild(&ib1);
    root.addChild(&hidden);

    FixedTextMetrics metrics;
    layoutTree(&root, 500, metrics);

    check(approx(root.box.contentRect.height, 30), "display:none child doesn't affect height");
    check(approx(hidden.box.contentRect.width, 0), "hidden child has zero width");
}

static void testInlineTextAlignJustify() {
    printf("--- Inline: text-align justify ---\n");
    // 4 inline-block items that wrap to 2 lines in 300px container.
    // Items are 100px each, so line 1 gets 3 items (300px, full width),
    // and line 2 gets 1 item (last line, not justified).
    // Actually, for justify to work we need free space on non-last lines.
    // Use 350px container with 100px items: 3 fit on line 1 (300px), 1 wraps.
    // Extra space on line 1 = 350 - 300 = 50px, distributed across 2 gaps = 25px each.
    InlineMockNode root;
    initBlock(root, "div"); // block container — text-align applies here
    root.style["text-align"] = "justify";

    InlineMockNode ib1, ib2, ib3, ib4;
    for (auto* ib : {&ib1, &ib2, &ib3, &ib4}) {
        initInline(*ib, "span");
        ib->style["display"] = "inline-block";
        ib->style["width"] = "100px";
        ib->style["height"] = "30px";
        root.addChild(ib);
    }

    FixedTextMetrics metrics;
    layoutTree(&root, 350, metrics);

    // Line 1: ib1(0), ib2(100+25=125), ib3(225+100=225+25=250)
    check(approx(ib1.box.contentRect.x, 0), "justify: line1 first item at x=0");
    check(approx(ib2.box.contentRect.x, 125), "justify: line1 second item at x=125 (25px gap)");
    check(approx(ib3.box.contentRect.x, 250), "justify: line1 third item at x=250");
    // Line 2 (last line): ib4 at x=0 (not justified)
    check(approx(ib4.box.contentRect.x, 0), "justify: last line not justified (x=0)");
}

static void testInlineAbsolutePositioning() {
    printf("--- Inline: absolute positioning ---\n");
    InlineMockNode root;
    initBlock(root);
    root.style["position"] = "relative";
    root.style["width"] = "400px";
    root.style["height"] = "200px";

    InlineMockNode absChild;
    initBlock(absChild);
    absChild.style["position"] = "absolute";
    absChild.style["width"] = "80px";
    absChild.style["height"] = "40px";
    absChild.style["top"] = "10px";
    absChild.style["left"] = "20px";

    root.addChild(&absChild);

    FixedTextMetrics metrics;
    layoutTree(&root, 500.0f, metrics);

    check(approx(absChild.box.contentRect.x, 20.0f), "inline abs: x = left:20");
    check(approx(absChild.box.contentRect.y, 10.0f), "inline abs: y = top:10");
    check(approx(absChild.box.contentRect.width, 80.0f), "inline abs: width preserved");
    check(approx(absChild.box.contentRect.height, 40.0f), "inline abs: height preserved");
}

static void testInlineAbsoluteBottomRight() {
    printf("--- Inline: absolute bottom/right ---\n");
    InlineMockNode root;
    initBlock(root);
    root.style["position"] = "relative";
    root.style["width"] = "400px";
    root.style["height"] = "200px";

    InlineMockNode absChild;
    initBlock(absChild);
    absChild.style["position"] = "absolute";
    absChild.style["width"] = "100px";
    absChild.style["height"] = "50px";
    absChild.style["bottom"] = "10px";
    absChild.style["right"] = "20px";

    root.addChild(&absChild);

    FixedTextMetrics metrics;
    layoutTree(&root, 500.0f, metrics);

    // x = 400 - 20 - 100 = 280
    check(approx(absChild.box.contentRect.x, 280.0f), "inline abs bottom/right: x = 280");
    // y = 200 - 10 - 50 = 140
    check(approx(absChild.box.contentRect.y, 140.0f), "inline abs bottom/right: y = 140");
}

// ========== text spacing and indent tests ==========

static void testLetterSpacing() {
    printf("--- Inline: letter-spacing ---\n");
    // Each char = 10px + 2px letter-spacing = 12px per char
    // "abc" = 3 chars * 12 = 36px (vs 30px without)
    FixedTextMetrics m;
    auto runs = breakTextIntoRuns("abc", 200, "serif", 16, "normal", "normal", m,
                                  "normal", "normal", 2.0f, 0);
    check(runs.size() == 1, "letter-spacing: single run");
    check(approx(runs[0].width, 36, 1), "letter-spacing: width includes spacing");
}

static void testWordSpacing() {
    printf("--- Inline: word-spacing ---\n");
    // "a b" = word "a" (10px) + space (10px base + 5px word-spacing) + word "b" (10px)
    // Total = 10 + 15 + 10 = 35px (vs 30px without)
    FixedTextMetrics m;
    auto runs = breakTextIntoRuns("a b", 200, "serif", 16, "normal", "normal", m,
                                  "normal", "normal", 0, 5.0f);
    check(runs.size() == 1, "word-spacing: single run");
    check(runs[0].width > 30, "word-spacing: wider than without spacing");
}

static void testTextIndent() {
    printf("--- Inline: text-indent ---\n");
    // Create an inline formatting context with text-indent
    InlineMockNode root;
    root.tag = "p";
    root.style["display"] = "block";
    root.style["position"] = "static";
    root.style["width"] = "200px"; root.style["height"] = "auto";
    root.style["min-width"] = "0"; root.style["min-height"] = "0";
    root.style["max-width"] = "none"; root.style["max-height"] = "none";
    root.style["margin-top"] = "0"; root.style["margin-right"] = "0";
    root.style["margin-bottom"] = "0"; root.style["margin-left"] = "0";
    root.style["padding-top"] = "0"; root.style["padding-right"] = "0";
    root.style["padding-bottom"] = "0"; root.style["padding-left"] = "0";
    root.style["border-top-width"] = "0"; root.style["border-right-width"] = "0";
    root.style["border-bottom-width"] = "0"; root.style["border-left-width"] = "0";
    root.style["border-top-style"] = "none"; root.style["border-right-style"] = "none";
    root.style["border-bottom-style"] = "none"; root.style["border-left-style"] = "none";
    root.style["box-sizing"] = "content-box";
    root.style["font-size"] = "16px";
    root.style["font-family"] = "serif";
    root.style["font-weight"] = "normal";
    root.style["text-align"] = "left";
    root.style["text-indent"] = "30px";
    root.style["overflow"] = "visible";
    root.style["letter-spacing"] = "normal";
    root.style["word-spacing"] = "normal";

    InlineMockNode textNode;
    textNode.isText = true;
    textNode.text = "Hello World";
    textNode.tag = "";
    root.addChild(&textNode);

    FixedTextMetrics m;
    layoutTree(&root, 400, m);

    // First text run should be indented by 30px
    check(approx(textNode.box.contentRect.x, 30, 2), "text-indent: first line indented 30px");
}

// ========== Entry point ==========

void testInlineLayout() {
    // Text breaking
    testTextBreakSingleLine();
    testTextBreakWrapping();
    testTextBreakMultipleWords();
    testTextBreakNowrap();
    testTextBreakPre();
    testTextBreakWhitespaceCollapse();

    // Inline layout
    testInlineTextLayout();
    testInlineTextWrapping();
    testInlineBlockChild();
    testInlineTextAlignCenter();
    testInlineTextAlignRight();
    testInlineMultipleInlineBlocks();
    testInlineDisplayNone();
    testInlineTextAlignJustify();

    // Absolute positioning in inline containers
    testInlineAbsolutePositioning();
    testInlineAbsoluteBottomRight();

    // Text spacing and indent
    testLetterSpacing();
    testWordSpacing();
    testTextIndent();
}
