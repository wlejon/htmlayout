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

    void addChild(InlineMockNode* child) {
        child->parentNode = this;
        childNodes.push_back(child);
    }
};

// Fixed-width mock: each character = 10px wide, line height = 20px
struct FixedTextMetrics : public TextMetrics {
    float measureWidth(std::string_view text,
                       std::string_view, float,
                       std::string_view) override {
        return static_cast<float>(text.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float,
                     std::string_view) override {
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
    // Interior spaces collapse to single. Leading/trailing whitespace is
    // preserved as a single space so inline boundaries (e.g. "foo "
    // + <em>bar</em>) render with the expected gap.
    check(runs[0].text == " hello world ", "collapse: extra spaces removed, edges kept");
}

static void testTextTransform() {
    printf("--- Text: text-transform applied during layout ---\n");
    // text-transform must be applied at layout time (not just paint) so the
    // run text — which drives measurement, wrapping, and inline-block sizing —
    // matches the painted glyphs. Regression: "uppercase" captions were being
    // measured at their lowercase width, so backgrounds were too short.
    check(applyTextTransform("rendering live", "uppercase") == "RENDERING LIVE",
          "applyTextTransform: uppercase");
    check(applyTextTransform("HELLO World", "lowercase") == "hello world",
          "applyTextTransform: lowercase");
    check(applyTextTransform("hello world", "capitalize") == "Hello World",
          "applyTextTransform: capitalize");
    check(applyTextTransform("Hello", "none") == "Hello",
          "applyTextTransform: none is a no-op");

    FixedTextMetrics metrics;
    auto runs = breakTextIntoRuns("hello world", 500, "mono", 16, "normal",
                                  "normal", metrics, "normal", "normal", 0, 0,
                                  "uppercase");
    check(runs.size() == 1 && runs[0].text == "HELLO WORLD",
          "breakTextIntoRuns: run text is transformed");
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

    // Justify expands the collapsed inter-word SPACES only (like Chromium).
    // Adjacent inline-blocks with no whitespace between them offer no
    // justification opportunities, so line 1 stays left-packed.
    check(approx(ib1.box.contentRect.x, 0), "justify: line1 first item at x=0");
    check(approx(ib2.box.contentRect.x, 100), "justify: no synthetic gap without spaces (x=100)");
    check(approx(ib3.box.contentRect.x, 200), "justify: line1 third item at x=200");
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
    // Letter-spacing is added after EVERY character including the last
    // (CSS Text; Chromium's measured box carries the trailing slot). For
    // "abc" at 10px/char with 2px spacing, the box is 3*10 + 3*2 = 36.
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

// ========== Line box vertical geometry (CSS2 §10.8) ==========

// Metrics whose vertical values scale with font-size so mixed-size line
// geometry is observable: natural line box = 1.25 * fontSize, and the
// default TextMetrics::ascent (0.8 * natural) = exactly fontSize.
// Width stays 10px/char regardless of size.
struct ScaledTextMetrics : public TextMetrics {
    float measureWidth(std::string_view text,
                       std::string_view, float,
                       std::string_view) override {
        return static_cast<float>(text.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float fontSize,
                     std::string_view) override {
        return fontSize * 1.25f;
    }
};

static void testMixedFontSizeLineBox() {
    printf("--- Line box: mixed font sizes, per-box line-height ---\n");
    // <div style="width:150; font-size:16px; line-height:1.5">
    //   "aaaa"<span style="font-size:32px">BB</span>" ccccc ddddd"
    // Block font: natural 20, ascent 16, LH 24 -> strut above 18 / below 6.
    // Big span:   natural 40, ascent 32, LH 48 -> above 36 / below 12.
    // Line 1 ("aaaa" + span, 60px): above 36, below 12 -> 48px tall.
    // Line 2 (wrapped text only):   24px tall. Total 72.
    InlineMockNode root;
    initBlock(root);
    root.style["width"] = "150px";
    root.style["line-height"] = "1.5";

    InlineMockNode t1;
    t1.isText = true;
    t1.text = "aaaa";
    root.addChild(&t1);

    InlineMockNode big;
    initInline(big);
    big.style["font-size"] = "32px";
    big.style["line-height"] = "1.5";
    root.addChild(&big);

    InlineMockNode bigText;
    bigText.isText = true;
    bigText.text = "BB";
    big.addChild(&bigText);

    InlineMockNode t2;
    t2.isText = true;
    t2.text = " ccccc ddddd";
    root.addChild(&t2);

    ScaledTextMetrics m;
    layoutTree(&root, 500, m);

    check(approx(root.box.contentRect.height, 72),
          "mixed sizes: block height = 48 + 24 (big span grows only its line)");
    // Baseline of line 1 sits at max-above = 36. Text hangs ascent above it.
    check(t1.box.textRuns.size() == 1 && approx(t1.box.textRuns[0].y, 20),
          "mixed sizes: 16px run top = baseline(36) - ascent(16)");
    check(approx(big.box.contentRect.y, 4),
          "mixed sizes: 32px span top = baseline(36) - ascent(32)");
    check(approx(big.box.contentRect.height, 40),
          "mixed sizes: inline span rect is its natural font box, not line-height");
    // Word-granularity wrapping: " ccccc" still fits on line 1 after the big
    // span (40 + 20 + 60 = 120 <= 150), so only "ddddd" wraps to line 2. On
    // the wrapped line its leading collapsible space is dropped, the run
    // starts at x=0, and it sits on line 2's baseline (48 + 18 - 16 = 50).
    check(!t2.box.textRuns.empty(), "wrap: wrapped text produced runs");
    const auto& wrapped = t2.box.textRuns.back();
    check(approx(wrapped.x, 0),
          "wrap: wrapped-line run starts at x=0 (leading space trimmed)");
    check(approx(wrapped.width, 50),
          "wrap: wrapped run is 'ddddd' (50px), the leading space excluded");
    check(approx(wrapped.y, 50),
          "wrap: line 2 run top = 48 + strutAbove(18) - ascent(16)");
}

static void testInlineBlockBaselineAlignment() {
    printf("--- Line box: inline-block aligns by last-line baseline ---\n");
    // <div style="font-size:16px; line-height:1.5">
    //   "xx"<ib style="width:50;height:30">yy</ib>
    // ib internal line: h = max(natural 20, LH 24) = 24, half-leading 2,
    // baseline = 2 + 16 = 18 from its content top. In the outer line the ib
    // spans [18 above, 12 below]; strut is [18, 6] -> line 30px tall.
    InlineMockNode root;
    initBlock(root);
    root.style["width"] = "300px";
    root.style["line-height"] = "1.5";

    InlineMockNode t1;
    t1.isText = true;
    t1.text = "xx";
    root.addChild(&t1);

    InlineMockNode ib;
    initInline(ib);
    ib.style["display"] = "inline-block";
    ib.style["width"] = "50px";
    ib.style["height"] = "30px";
    ib.style["line-height"] = "1.5";
    root.addChild(&ib);

    InlineMockNode ibText;
    ibText.isText = true;
    ibText.text = "yy";
    ib.addChild(&ibText);

    ScaledTextMetrics m;
    layoutTree(&root, 500, m);

    check(approx(ib.box.baselineOffset, 18),
          "ib: internal baseline = half-leading(2) + ascent(16)");
    check(approx(root.box.contentRect.height, 30),
          "ib: line height = ib above(18) + ib below(12)");
    check(approx(ib.box.contentRect.y, 0),
          "ib: top of box at line top (its baseline defines max-above)");
    check(t1.box.textRuns.size() == 1 && approx(t1.box.textRuns[0].y, 2),
          "ib: sibling text top = shared baseline(18) - ascent(16)");
}

static void testPlainTextLineUsesLineHeight() {
    printf("--- Line box: text-only line respects CSS line-height ---\n");
    InlineMockNode root;
    initBlock(root);
    root.style["width"] = "300px";
    root.style["line-height"] = "2";   // 32px lines from 16px font

    InlineMockNode t1;
    t1.isText = true;
    t1.text = "hello";
    root.addChild(&t1);

    ScaledTextMetrics m;
    layoutTree(&root, 500, m);

    check(approx(root.box.contentRect.height, 32),
          "line-height 2: block height = 32");
    // Half-leading = (32 - 20) / 2 = 6; run top = 6.
    check(t1.box.textRuns.size() == 1 && approx(t1.box.textRuns[0].y, 6),
          "line-height 2: run centered by half-leading");
}

static void testSubSuperBaselineShift() {
    printf("--- Line box: vertical-align sub/super shifts baseline ---\n");
    // Block: font 16, line-height 1.5 -> LH 24, natural 20, ascent 16,
    // strut above 18 / below 6.
    // super: shift up = 16/3 + 1 = 6.333; the raised extent grows the line
    //   above the baseline: above = 18 + 6.333 = 24.333, below stays 6.
    // sub: shift down = 16/5 + 1 = 4.2; grows below: above 18, below 10.2.
    {
        InlineMockNode root;
        initBlock(root);
        root.style["width"] = "500px";
        root.style["line-height"] = "1.5";

        InlineMockNode t1;
        t1.isText = true;
        t1.text = "xx";
        root.addChild(&t1);

        InlineMockNode sup;
        initInline(sup);
        sup.style["vertical-align"] = "super";
        sup.style["line-height"] = "1.5";
        root.addChild(&sup);

        InlineMockNode supText;
        supText.isText = true;
        supText.text = "s";
        sup.addChild(&supText);

        ScaledTextMetrics m;
        layoutTree(&root, 500, m);

        check(approx(root.box.contentRect.height, 30.333f),
              "super: line height = above(18+6.333) + below(6)");
        check(approx(sup.box.contentRect.y, 2.0f),
              "super: box top = baseline(24.333) - ascent(16) - shift(6.333)");
        check(t1.box.textRuns.size() == 1 && approx(t1.box.textRuns[0].y, 8.333f),
              "super: sibling text stays on the (unmoved) line baseline");
    }
    {
        InlineMockNode root;
        initBlock(root);
        root.style["width"] = "500px";
        root.style["line-height"] = "1.5";

        InlineMockNode t1;
        t1.isText = true;
        t1.text = "xx";
        root.addChild(&t1);

        InlineMockNode sub;
        initInline(sub);
        sub.style["vertical-align"] = "sub";
        sub.style["line-height"] = "1.5";
        root.addChild(&sub);

        InlineMockNode subText;
        subText.isText = true;
        subText.text = "s";
        sub.addChild(&subText);

        ScaledTextMetrics m;
        layoutTree(&root, 500, m);

        check(approx(root.box.contentRect.height, 28.2f),
              "sub: line height = above(18) + below(6+4.2)");
        check(approx(sub.box.contentRect.y, 6.2f),
              "sub: box top = baseline(18) - ascent(16) + shift(4.2)");
        check(t1.box.textRuns.size() == 1 && approx(t1.box.textRuns[0].y, 2.0f),
              "sub: sibling text stays on the (unmoved) line baseline");
    }
}

// ========== Max-content ↔ layout consistency (regression guard) ==========

// Super-additive metric: each measureWidth() call rounds its result UP, so
// measuring a whole string is strictly narrower than the sum of its words'
// individual measurements plus per-space measurements. This reproduces the
// real-font behaviour where a table cell sized to a single whole-string
// max-content measurement can't fit the word-by-word line the IFC builder
// reconstructs — the exact table-cell double-wrap regression this guards.
struct RoundUpTextMetrics : public TextMetrics {
    float measureWidth(std::string_view text,
                       std::string_view, float,
                       std::string_view) override {
        return std::ceil(static_cast<float>(text.size()) * 10.4f);
    }
    float lineHeight(std::string_view, float,
                     std::string_view) override {
        return 20.0f;
    }
};

static void testMaxContentSingleLineConsistency() {
    printf("--- Max-content: box sized to max-content fits on one line ---\n");
    InlineMockNode root;
    initBlock(root);

    InlineMockNode textNode;
    textNode.isText = true;
    // Several words so the per-word rounding accumulates past the whole-string
    // measurement — a naive whole-string max-content would be too small.
    textNode.text = "this column has substantially more text";
    root.addChild(&textNode);

    RoundUpTextMetrics metrics;

    // Sanity: the whole-string measurement really is narrower than the word-mode
    // reconstruction, so this metric exercises the bug.
    float mn = 0, mx = 0;
    measureWordModeIntrinsics(textNode.text, "serif", 16.0f, "normal", 0, 0,
                              "none", metrics, mn, mx);
    float whole = metrics.measureWidth(
        "this column has substantially more text", "serif", 16.0f, "normal");
    check(mx > whole,
          "max-content: word-mode reconstruction exceeds whole-string measure");

    // Size the block to its own max-content and lay it out.
    float maxc = computeMaxContentWidth(&root, metrics);
    root.style["width"] = std::to_string(maxc) + "px";
    layoutTree(&root, 1000, metrics);

    check(approx(root.box.contentRect.height, 20),
          "max-content: text stays on ONE line (height = one line-height)");
    // Every placed run sits on the same line (single y).
    bool oneLine = true;
    if (!textNode.box.textRuns.empty()) {
        float y0 = textNode.box.textRuns.front().y;
        for (const auto& r : textNode.box.textRuns)
            if (!approx(r.y, y0)) oneLine = false;
    }
    check(oneLine, "max-content: all text runs share one baseline (no wrap)");
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
    testTextTransform();

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

    // Line box vertical geometry (CSS2 §10.8)
    testMixedFontSizeLineBox();
    testInlineBlockBaselineAlignment();
    testPlainTextLineUsesLineHeight();
    testSubSuperBaselineShift();

    // Intrinsic-sizing ↔ layout consistency
    testMaxContentSingleLineConsistency();
}
