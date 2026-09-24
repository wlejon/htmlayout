// Inline elements flattened into their block's line boxes: text inside a
// <span> wraps where it stands instead of the span moving whole to the next
// line, and everything inside the span lands in its content coordinates.

#include "test_inline_boxes.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <string>
#include <vector>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct INode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    INode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style_; }
    void addChild(INode* c) { c->parentNode = this; childNodes.push_back(c); }

    INode& init(const char* display = "block") {
        style_["display"] = display;
        style_["position"] = "static";
        style_["width"] = "auto";
        style_["height"] = "auto";
        style_["min-width"] = "auto";
        style_["min-height"] = "auto";
        style_["max-width"] = "none";
        style_["max-height"] = "none";
        for (auto* p : {"margin", "padding"})
            for (auto* s : {"top", "right", "bottom", "left"})
                style_[std::string(p) + "-" + s] = "0";
        for (auto* s : {"top", "right", "bottom", "left"}) {
            style_[std::string("border-") + s + "-width"] = "0";
            style_[std::string("border-") + s + "-style"] = "none";
        }
        style_["box-sizing"] = "content-box";
        style_["font-size"] = "16px";
        style_["font-family"] = "monospace";
        style_["font-weight"] = "normal";
        style_["line-height"] = "normal";
        style_["overflow"] = "visible";
        style_["white-space"] = "normal";
        style_["text-align"] = "left";
        style_["vertical-align"] = "baseline";
        style_["direction"] = "ltr";
        return *this;
    }
    INode& span() { init("inline"); tag = "span"; return *this; }
    INode& textNode(const char* t) { isText = true; text = t; return *this; }
};

// 10px per byte, 20px lines, ascent 16.
struct IMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

bool near(float a, float b, float tol = 0.5f) { return std::abs(a - b) < tol; }

// The run of `t` whose text is `word`, or nullptr.
const PlacedTextRun* runOf(const INode& t, const char* word) {
    for (const auto& r : t.box.textRuns)
        if (r.text.find(word) != std::string::npos) return &r;
    return nullptr;
}

} // namespace

// "voice <span>aaaa bbbb cccc dddd</span>" in 200px: the span's words fill
// the rest of line 1 and only "dddd" wraps. As one atomic item the span (190
// wide) moved whole to line 2.
static void testTextWrapsInsideSpan() {
    printf("--- inline boxes: text wraps inside a span ---\n");
    INode block; block.init(); block.style_["width"] = "200px";
    INode t1; t1.textNode("voice ");
    INode sp; sp.span();
    INode t2; t2.textNode("aaaa bbbb cccc dddd");
    sp.addChild(&t2);
    block.addChild(&t1); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);

    const PlacedTextRun* a = runOf(t2, "aaaa");
    const PlacedTextRun* d = runOf(t2, "dddd");
    check(a && d, "the span's words are placed");
    if (!a || !d) return;
    // Span content coordinates: its box starts at the line's left edge (it
    // wraps) and at the top of line 1.
    check(near(sp.box.contentRect.x, 0) && near(sp.box.contentRect.y, 0),
          "the wrapped span's box starts at the block's content origin");
    check(near(a->x + sp.box.contentRect.x, 60) && near(a->y + sp.box.contentRect.y, 0),
          "'aaaa' follows 'voice ' on line 1");
    check(near(d->x + sp.box.contentRect.x, 0) && near(d->y + sp.box.contentRect.y, 20),
          "only 'dddd' wraps to line 2");
    check(near(block.box.contentRect.height, 40), "two lines");
    check(near(sp.box.contentRect.height, 40), "the span's box covers both of its lines");
}

// Padding on an inline element takes room on the line at its start and end,
// and its content lands in its own coordinates.
static void testSpanPaddingAndCoordinates() {
    printf("--- inline boxes: padding and content coordinates ---\n");
    INode block; block.init(); block.style_["width"] = "400px";
    INode ta; ta.textNode("ab");
    INode sp; sp.span();
    sp.style_["padding-left"] = "5px"; sp.style_["padding-right"] = "7px";
    INode tc; tc.textNode("cd");
    INode te; te.textNode("ef");
    sp.addChild(&tc);
    block.addChild(&ta); block.addChild(&sp); block.addChild(&te);
    IMetrics m;
    layoutTree(&block, 800, m);
    check(near(sp.box.contentRect.x, 25) && near(sp.box.contentRect.width, 20),
          "span content box sits after 'ab' and its left padding");
    check(!tc.box.textRuns.empty() && near(tc.box.textRuns[0].x, 0),
          "its text is at its own content origin");
    check(!te.box.textRuns.empty() && near(te.box.textRuns[0].x, 52),
          "the text after it clears the right padding");

    // Nested: the inner span is placed in the outer span's coordinates.
    INode block2; block2.init(); block2.style_["width"] = "400px";
    INode tx; tx.textNode("xx");
    INode outer; outer.span(); outer.style_["padding-left"] = "10px";
    INode ty; ty.textNode("yy");
    INode inner; inner.span();
    INode tz; tz.textNode("zz");
    inner.addChild(&tz);
    outer.addChild(&ty); outer.addChild(&inner);
    block2.addChild(&tx); block2.addChild(&outer);
    layoutTree(&block2, 800, m);
    check(near(outer.box.contentRect.x, 30), "outer span after 'xx' + padding");
    check(near(inner.box.contentRect.x, 20), "inner span is relative to the outer one");
    check(!tz.box.textRuns.empty() && near(tz.box.textRuns[0].x, 0),
          "innermost text relative to the inner span");
}

// Collapsible white space collapses across element boundaries: "ab " followed
// by a span whose text starts with a space leaves one space, not two.
static void testWhitespaceCollapsesAcrossSpan() {
    printf("--- inline boxes: white space collapses across a span edge ---\n");
    INode block; block.init(); block.style_["width"] = "400px";
    INode ta; ta.textNode("ab ");
    INode sp; sp.span();
    INode tc; tc.textNode(" cd");
    sp.addChild(&tc);
    block.addChild(&ta); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    const PlacedTextRun* cd = runOf(tc, "cd");
    check(cd != nullptr, "'cd' is placed");
    if (!cd) return;
    float x = cd->x + sp.box.contentRect.x;
    if (!cd->text.empty() && cd->text.front() == ' ') x += 10.0f;
    check(near(x, 30), "one collapsed space between 'ab' and 'cd'");
}

// When the span's first word does not fit, the break falls before the span
// (the space ahead of it), and its left padding goes with it.
static void testBreakBeforeSpan() {
    printf("--- inline boxes: break before a span keeps its padding ---\n");
    INode block; block.init(); block.style_["width"] = "100px";
    INode ta; ta.textNode("aaaa ");
    INode sp; sp.span(); sp.style_["padding-left"] = "4px";
    INode tb; tb.textNode("bbbbbbbb");
    sp.addChild(&tb);
    block.addChild(&ta); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    check(near(sp.box.contentRect.y, 20), "the span moves to line 2");
    check(near(sp.box.contentRect.x, 4), "with its padding at the start of the line");

    // Relaid out (the block marked dirty), the geometry is the same.
    markDirty(&block);
    layoutTree(&block, 800, m);
    check(near(sp.box.contentRect.y, 20) && near(sp.box.contentRect.x, 4),
          "a second pass places it the same way");
    check(tb.box.textRuns.size() == 1 && near(tb.box.textRuns[0].x, 0),
          "and does not duplicate its runs");
}

// An inline element holding a block-level box is still laid out whole.
static void testBlockInsideInlineStaysAtomic() {
    printf("--- inline boxes: block inside an inline stays whole ---\n");
    INode block; block.init(); block.style_["width"] = "400px";
    INode ta; ta.textNode("ab ");
    INode sp; sp.span();
    INode inner; inner.init("block"); inner.style_["width"] = "50px";
    inner.style_["height"] = "10px";
    sp.addChild(&inner);
    block.addChild(&ta); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    check(near(inner.box.contentRect.width, 50), "the block child keeps its width");
}

// white-space: pre-wrap wraps where it stands too: each word with the space
// after it is a piece of the line, and the space hangs at a line end.
static void testPreWrapWrapsInsideSpan() {
    printf("--- inline boxes: pre-wrap text wraps inside a span ---\n");
    INode block; block.init(); block.style_["width"] = "200px";
    block.style_["white-space"] = "pre-wrap";
    INode t1; t1.textNode("voice ");
    INode sp; sp.span(); sp.style_["white-space"] = "pre-wrap";
    INode t2; t2.textNode("aaaa bbbb cccc dddd");
    sp.addChild(&t2);
    block.addChild(&t1); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    const PlacedTextRun* a = runOf(t2, "aaaa");
    const PlacedTextRun* c = runOf(t2, "cccc");
    const PlacedTextRun* d = runOf(t2, "dddd");
    check(a && c && d, "the span's words are placed");
    if (!a || !c || !d) return;
    check(near(a->x + sp.box.contentRect.x, 60) && near(a->y + sp.box.contentRect.y, 0),
          "'aaaa' follows 'voice ' on line 1");
    check(near(c->y + sp.box.contentRect.y, 0),
          "'cccc ' stays on line 1, its space hanging at the edge");
    check(near(d->x + sp.box.contentRect.x, 0) && near(d->y + sp.box.contentRect.y, 20),
          "only 'dddd' wraps to line 2");
    check(a->text == "aaaa " && a->srcStart == 0 && a->srcEnd == 5,
          "a piece keeps its preserved space and source range");
}

// overflow-wrap: break-word moves a long word to its own line first (the
// space before it is a break opportunity) and only then cuts it at the edge.
static void testBreakWordInsideSpan() {
    printf("--- inline boxes: overflow-wrap cuts a word inside a span ---\n");
    INode block; block.init(); block.style_["width"] = "100px";
    block.style_["overflow-wrap"] = "break-word";
    INode t1; t1.textNode("ab ");
    INode sp; sp.span(); sp.style_["overflow-wrap"] = "break-word";
    INode t2; t2.textNode("cccccccccccccccc");
    sp.addChild(&t2);
    block.addChild(&t1); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    check(t2.box.textRuns.size() == 2, "the word is cut in two");
    if (t2.box.textRuns.size() != 2) return;
    const auto& r0 = t2.box.textRuns[0];
    const auto& r1 = t2.box.textRuns[1];
    float ox = sp.box.contentRect.x, oy = sp.box.contentRect.y;
    check(near(r0.x + ox, 0) && near(r0.y + oy, 20) && near(r0.width, 100),
          "its first piece fills line 2");
    check(near(r1.x + ox, 0) && near(r1.y + oy, 40) && near(r1.width, 60),
          "the rest is on line 3");
    check(r0.srcStart == 0 && r0.srcEnd == 10 && r1.srcStart == 10 && r1.srcEnd == 16,
          "the pieces cover the source in order");

    // Not in a span, with text before it on the line and no break
    // opportunity anywhere: cut at the edge of the line it starts on.
    INode block2; block2.init(); block2.style_["width"] = "100px";
    block2.style_["overflow-wrap"] = "break-word";
    INode t3; t3.textNode("dddddddddddddddd");
    block2.addChild(&t3);
    layoutTree(&block2, 800, m);
    check(t3.box.textRuns.size() == 2 && near(t3.box.textRuns[0].width, 100) &&
          near(t3.box.textRuns[1].y, 20),
          "a lone long word breaks at the line edge");
}

// word-break: break-all breaks between any two letters, so a word fills the
// rest of the line it starts on.
static void testBreakAllFillsLine() {
    printf("--- inline boxes: break-all fills the line ---\n");
    INode block; block.init(); block.style_["width"] = "100px";
    block.style_["word-break"] = "break-all";
    INode t1; t1.textNode("ab ");
    INode sp; sp.span(); sp.style_["word-break"] = "break-all";
    INode t2; t2.textNode("cccccccccccccccc");
    sp.addChild(&t2);
    block.addChild(&t1); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    check(t2.box.textRuns.size() == 2, "the word is cut in two");
    if (t2.box.textRuns.size() != 2) return;
    float ox = sp.box.contentRect.x, oy = sp.box.contentRect.y;
    const auto& r0 = t2.box.textRuns[0];
    const auto& r1 = t2.box.textRuns[1];
    check(near(r0.x + ox, 30) && near(r0.y + oy, 0) && near(r0.width, 70),
          "its first piece fills the rest of line 1");
    check(near(r1.x + ox, 0) && near(r1.y + oy, 20) && near(r1.width, 90),
          "the rest starts line 2");
}

// A block that mixes block-level and inline children lays each inline run
// out in an anonymous block, through the same line builder: a span's text
// wraps where it stands there too.
static void testMixedBlockInlineWraps() {
    printf("--- inline boxes: spans wrap inside an anonymous block ---\n");
    INode block; block.init(); block.style_["width"] = "200px";
    INode head; head.init(); head.style_["height"] = "10px";
    INode t1; t1.textNode("voice ");
    INode sp; sp.span(); sp.style_["padding-left"] = "4px";
    INode t2; t2.textNode("aaaa bbbb cccc dddd");
    sp.addChild(&t2);
    block.addChild(&head); block.addChild(&t1); block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    const PlacedTextRun* a = runOf(t2, "aaaa");
    const PlacedTextRun* c = runOf(t2, "cccc");
    check(a && c, "the span's words are placed");
    if (!a || !c) return;
    check(near(a->x + sp.box.contentRect.x, 64) && near(a->y + sp.box.contentRect.y, 10),
          "'aaaa' follows 'voice ' and the span's padding on the first line");
    check(near(c->x + sp.box.contentRect.x, 0) && near(c->y + sp.box.contentRect.y, 30),
          "'cccc' (past the edge by the padding) wraps to the second line");
    check(near(block.box.contentRect.height, 50), "the block holds both lines");
}

// A right-to-left inline element starts on the right: in a right-to-left
// paragraph its padding-right sits beside its first word (rightmost) and its
// padding-left beside its last, on the left.
static void testRtlSpanPadding() {
    printf("--- inline boxes: right-to-left span padding ---\n");
    INode block; block.init(); block.style_["width"] = "400px";
    block.style_["direction"] = "rtl";
    block.style_["text-align"] = "start";
    INode sp; sp.span(); sp.style_["direction"] = "rtl";
    sp.style_["padding-left"] = "5px"; sp.style_["padding-right"] = "7px";
    INode t; t.textNode("cd ef");
    t.style_["direction"] = "rtl";   // a text node's style is its parent's
    sp.addChild(&t);
    block.addChild(&sp);
    IMetrics m;
    layoutTree(&block, 800, m);
    const PlacedTextRun* cd = runOf(t, "cd");
    const PlacedTextRun* ef = runOf(t, "ef");
    check(cd && ef, "both words are placed");
    if (!cd || !ef) return;
    float ox = sp.box.contentRect.x;
    check(near(ox, 343) && near(sp.box.contentRect.width, 50),
          "the content box sits between the two paddings at the right edge");
    check(near(ef->x + ox, 343), "the last word follows padding-left, on the left");
    check(near(cd->x + ox, 373), "the first word ends at padding-right, on the right");
}

void testInlineBoxes() {
    printf("=== Inline boxes ===\n");
    testPreWrapWrapsInsideSpan();
    testBreakWordInsideSpan();
    testBreakAllFillsLine();
    testMixedBlockInlineWraps();
    testRtlSpanPadding();
    testTextWrapsInsideSpan();
    testSpanPaddingAndCoordinates();
    testWhitespaceCollapsesAcrossSpan();
    testBreakBeforeSpan();
    testBlockInsideInlineStaysAtomic();
}
