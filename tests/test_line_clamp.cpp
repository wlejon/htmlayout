// line-clamp (CSS Overflow 4) and the legacy -webkit-line-clamp on a vertical
// -webkit-box: the cascade keeps the properties, block layout counts lines
// through in-flow block descendants, cuts the box's auto height after the Nth
// line, drops the content past it and puts the ellipsis on the last kept line.

#include "test_line_clamp.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include "css/cascade.h"
#include "css/parser.h"
#include "css/properties.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct ClampNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    ClampNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style_; }
};

// Fixed-advance font: every byte is 0.6em wide, lines are 1.2em tall. At
// font-size 10px a four-letter word is 24px, a space 6px, a line 12px, and
// the three-byte U+2026 ellipsis 18px.
struct ClampMetrics : public TextMetrics {
    float measureWidth(std::string_view text, std::string_view, float fontSize,
                       std::string_view) override {
        return static_cast<float>(text.size()) * fontSize * 0.6f;
    }
    float lineHeight(std::string_view, float fontSize, std::string_view) override {
        return fontSize * 1.2f;
    }
};

// Owns a small tree.
struct Tree {
    std::vector<std::unique_ptr<ClampNode>> nodes;

    ClampNode* block(ClampNode* parent, const char* display = "block") {
        auto n = std::make_unique<ClampNode>();
        n->style_["display"] = display;
        n->style_["font-size"] = "10px";
        ClampNode* raw = n.get();
        nodes.push_back(std::move(n));
        if (parent) attach(parent, raw);
        return raw;
    }
    ClampNode* textNode(ClampNode* parent, const std::string& s) {
        auto n = std::make_unique<ClampNode>();
        n->isText = true;
        n->text = s;
        ClampNode* raw = n.get();
        nodes.push_back(std::move(n));
        attach(parent, raw);
        return raw;
    }
    static void attach(ClampNode* parent, ClampNode* child) {
        child->parentNode = parent;
        parent->childNodes.push_back(child);
    }
};

// n four-letter words: "w00a w01a ...".
std::string words(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += 'w';
        s += static_cast<char>('0' + (i / 10) % 10);
        s += static_cast<char>('0' + i % 10);
        s += 'a';
    }
    return s;
}

bool approxEq(float a, float b) { return std::fabs(a - b) < 0.5f; }

const std::string kEllipsis = "\xE2\x80\xA6";

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Text drawn by a text node, runs in order.
std::string drawn(const ClampNode* t) {
    std::string s;
    for (const auto& r : t->box.textRuns) s += r.text;
    return s;
}

// The last run that draws anything (a line-end space trims to an empty run).
const PlacedTextRun& lastDrawnRun(const ClampNode* t) {
    static const PlacedTextRun none;
    for (size_t i = t->box.textRuns.size(); i-- > 0;)
        if (!t->box.textRuns[i].text.empty()) return t->box.textRuns[i];
    return none;
}

float lowestRunTop(const ClampNode* t) {
    float y = -1.0f;
    for (const auto& r : t->box.textRuns)
        if (!r.text.empty()) y = std::max(y, r.y);
    return y;
}

void layout(ClampNode* root, float width, ClampMetrics& m) {
    markSubtreeDirty(root);
    layoutTree(root, width, m);
}

void testCascade() {
    printf("--- line-clamp: cascade ---\n");
    check(initialValue("line-clamp") == "none", "line-clamp: initial value none");
    check(initialValue("-webkit-line-clamp") == "none", "-webkit-line-clamp: initial value none");
    check(initialValue("-webkit-box-orient") == "horizontal",
          "-webkit-box-orient: initial value horizontal");
    check(!isInherited("line-clamp") && !isInherited("-webkit-line-clamp"),
          "line-clamp: not inherited");

    Cascade cascade;
    cascade.addStylesheet(parse(
        ".legacy { display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: 3; }"
        ".std { line-clamp: 2 \"...\"; }"));
    MockElement legacy; legacy.tag = "div"; legacy.classes = "legacy";
    auto ls = cascade.resolve(legacy);
    check(ls["display"] == "-webkit-box", "cascade: display -webkit-box kept");
    check(ls["-webkit-box-orient"] == "vertical", "cascade: -webkit-box-orient kept");
    check(ls["-webkit-line-clamp"] == "3", "cascade: -webkit-line-clamp kept");
    MockElement std_; std_.tag = "div"; std_.classes = "std";
    auto ss = cascade.resolve(std_);
    check(ss["line-clamp"].rfind("2", 0) == 0 &&
          ss["line-clamp"].find("...") != std::string::npos,
          "cascade: line-clamp keeps count and ellipsis string");
}

void testStandardClamp() {
    printf("--- line-clamp: inline formatting context ---\n");
    ClampMetrics m;
    // 120px: four words per line, 16 words -> 4 lines of 12px.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "2";
        ClampNode* txt = t.textNode(box, words(16));
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, 24.0f), "line-clamp 2: auto height ends after line 2");
        check(box->box.textTruncated, "line-clamp 2: container reports truncation");
        check(lowestRunTop(txt) < 12.0f + 0.5f, "line-clamp 2: no run past line 2");
        // Line 2 is "w04a w05a w06a w07a", ending at 114px: the ellipsis
        // (18px) does not fit behind it, so the last word is trimmed to "w0".
        const auto& last = lastDrawnRun(txt);
        check(last.text == "w0" + kEllipsis, "line-clamp 2: last word trimmed to fit the ellipsis");
        check(last.x + last.width <= 120.0f + 0.01f, "line-clamp 2: ellipsis inside the line");
        check(drawn(txt).find("w08a") == std::string::npos, "line-clamp 2: line 3 text gone");
        check(approxEq(txt->box.contentRect.height, 24.0f), "line-clamp 2: text rect shrinks to kept runs");
    }
    // 200px: six words per line; line 1 ends at 174px, the ellipsis fits.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "1";
        ClampNode* txt = t.textNode(box, words(10));
        layout(root, 200.0f, m);
        check(approxEq(box->box.contentRect.height, 12.0f), "line-clamp 1: one line tall");
        check(drawn(txt) == "w00a w01a w02a w03a w04a w05a" + kEllipsis,
              "line-clamp 1: ellipsis appended after the last word");
    }
    // Fewer lines than the clamp: nothing changes.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "5";
        ClampNode* txt = t.textNode(box, words(16));
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, 48.0f), "line-clamp 5 on 4 lines: full height");
        check(!box->box.textTruncated, "line-clamp 5 on 4 lines: not truncated");
        check(drawn(txt).find(kEllipsis) == std::string::npos, "line-clamp 5 on 4 lines: no ellipsis");
    }
    // block-ellipsis: no-ellipsis, and a custom string.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* a = t.block(root);
        a->style_["line-clamp"] = "2 no-ellipsis";
        ClampNode* ta = t.textNode(a, words(16));
        ClampNode* b = t.block(root);
        b->style_["line-clamp"] = "\" [more]\" 1";
        ClampNode* tb = t.textNode(b, words(16));
        layout(root, 120.0f, m);
        check(approxEq(a->box.contentRect.height, 24.0f), "no-ellipsis: still clamps");
        check(drawn(ta).find(kEllipsis) == std::string::npos &&
              endsWith(drawn(ta), "w07a"), "no-ellipsis: line 2 kept whole, no ellipsis");
        check(approxEq(b->box.contentRect.height, 12.0f), "string ellipsis: clamps to 1 line");
        check(endsWith(drawn(tb), " [more]"), "string ellipsis: custom string carried");
        const auto& last = lastDrawnRun(tb);
        check(last.x + last.width <= 120.0f + 0.01f, "string ellipsis: fits in the line");
    }
    // Invalid values clamp nothing.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "0";
        t.textNode(box, words(16));
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, 48.0f), "line-clamp 0: invalid, ignored");
    }
}

void testWebkitClamp() {
    printf("--- -webkit-line-clamp ---\n");
    ClampMetrics m;
    auto run = [&](const char* display, const char* orient, float expectH, const char* name) {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root, display);
        if (orient) box->style_["-webkit-box-orient"] = orient;
        box->style_["-webkit-line-clamp"] = "3";
        t.textNode(box, words(16));
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, expectH), name);
    };
    run("-webkit-box", "vertical", 36.0f, "-webkit-box vertical: clamps to 3 lines");
    run("-webkit-box", nullptr, 48.0f, "-webkit-box without orient: no clamp");
    run("block", "vertical", 48.0f, "display:block: -webkit-line-clamp ignored");

    // The standard property wins over the legacy one.
    Tree t;
    ClampNode* root = t.block(nullptr);
    ClampNode* box = t.block(root, "-webkit-box");
    box->style_["-webkit-box-orient"] = "vertical";
    box->style_["-webkit-line-clamp"] = "3";
    box->style_["line-clamp"] = "1";
    t.textNode(box, words(16));
    layout(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 12.0f), "line-clamp overrides -webkit-line-clamp");
}

void testNestedBlocks() {
    printf("--- line-clamp: lines through block descendants ---\n");
    ClampMetrics m;
    Tree t;
    ClampNode* root = t.block(nullptr);
    root->style_["height"] = "200px";
    ClampNode* box = t.block(root);
    box->style_["line-clamp"] = "3";
    ClampNode* p1 = t.block(box);
    ClampNode* t1 = t.textNode(p1, words(8));      // lines 1-2
    ClampNode* p2 = t.block(box);
    ClampNode* t2 = t.textNode(p2, words(6));      // lines 3-4
    ClampNode* p3 = t.block(box);
    ClampNode* t3 = t.textNode(p3, words(2));      // line 5
    ClampNode* ib = t.block(p3, "inline-block");
    ib->style_["width"] = "20px";
    ib->style_["height"] = "5px";
    layout(root, 120.0f, m);

    check(approxEq(box->box.contentRect.height, 36.0f), "nested: height ends after line 3");
    check(!endsWith(drawn(t1), kEllipsis) && endsWith(drawn(t1), "w07a"), "nested: first paragraph whole");
    check(drawn(t2) == "w00a w01a w02a w0" + kEllipsis, "nested: ellipsis on line 3 in the second paragraph");
    check(!p2->box.clampHidden, "nested: straddling paragraph stays visible");
    check(p3->box.clampHidden, "nested: paragraph past the clamp point hidden");
    check(t3->box.textRuns.size() == 1 && t3->box.textRuns[0].text.empty(),
          "nested: hidden paragraph's text has no drawable runs");

    // Hidden content is not hit-testable; the kept lines are.
    float p3y = box->box.contentRect.y + p3->box.contentRect.y + 6.0f;
    LayoutNode* hit = hitTest(root, 5.0f, p3y);
    check(hit != p3 && hit != t3 && hit != ib, "nested: hidden paragraph not hit");
    LayoutNode* hit2 = hitTest(root, 5.0f, box->box.contentRect.y + 6.0f);
    check(hit2 == p1 || hit2 == t1, "nested: kept paragraph still hit");
}

void testMixedContent() {
    printf("--- line-clamp: anonymous lines and atomic inlines ---\n");
    ClampMetrics m;
    // Text beside a block child runs through anonymous line boxes.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "3";
        ClampNode* lead = t.textNode(box, words(8));  // anonymous lines 1-2
        ClampNode* p = t.block(box);
        ClampNode* pt = t.textNode(p, words(8));      // lines 3-4
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, 36.0f), "anonymous: height ends after line 3");
        check(endsWith(drawn(lead), "w07a"), "anonymous: leading lines whole");
        check(endsWith(drawn(pt), kEllipsis) && drawn(pt).find("w04a") == std::string::npos,
              "anonymous: block child cut after its first line, with ellipsis");
    }
    // An inline-block on a line past the clamp point is hidden.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "1";
        t.textNode(box, words(4) + " ");
        ClampNode* ib = t.block(box, "inline-block");
        ib->style_["width"] = "30px";
        ib->style_["height"] = "10px";
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, 12.0f), "atomic: clamps to line 1");
        check(ib->box.clampHidden, "atomic: inline-block on line 2 hidden");
    }
}

void testRelayout() {
    printf("--- line-clamp: relayout ---\n");
    ClampMetrics m;
    Tree t;
    ClampNode* root = t.block(nullptr);
    ClampNode* box = t.block(root);
    box->style_["line-clamp"] = "2";
    ClampNode* p1 = t.block(box);
    ClampNode* t1 = t.textNode(p1, words(8));
    ClampNode* p2 = t.block(box);
    t.textNode(p2, words(4));
    layout(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 24.0f) && p2->box.clampHidden,
          "relayout: clamped to 2");

    // Only the container is marked: its clamped descendants must still be
    // laid out afresh rather than reused with last pass's cut.
    box->style_["line-clamp"] = "3";
    markDirty(box);
    layoutTree(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 36.0f), "relayout: 2 -> 3 grows to 3 lines");
    check(endsWith(drawn(t1), "w07a"), "relayout: 2 -> 3 restores the cut text");
    check(!p2->box.clampHidden, "relayout: 2 -> 3 un-hides line 3");

    box->style_["line-clamp"] = "none";
    markDirty(box);
    layoutTree(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 36.0f) && !box->box.textTruncated,
          "relayout: none restores full height");
    check(drawn(t1).find(kEllipsis) == std::string::npos, "relayout: none drops the ellipsis");

    // A second pass with nothing marked reuses the clamped subtree as is.
    box->style_["line-clamp"] = "1";
    markDirty(box);
    layoutTree(root, 120.0f, m);
    layoutTree(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 12.0f) &&
          drawn(t1) == "w00a w01a w02a w0" + kEllipsis,
          "relayout: clean pass keeps a single ellipsis");

    // A flex item is laid out more than once per pass; each visit clamps
    // fresh content, so the ellipsis is not applied twice.
    Tree f;
    ClampNode* flex = f.block(nullptr, "flex");
    flex->style_["align-items"] = "flex-start";
    ClampNode* item = f.block(flex);
    item->style_["width"] = "120px";
    item->style_["line-clamp"] = "2";
    ClampNode* it = f.textNode(item, words(16));
    layout(flex, 400.0f, m);
    check(approxEq(item->box.contentRect.height, 24.0f), "flex item: clamped to 2 lines");
    // (the space ending line 1 is trimmed at the wrap)
    check(drawn(it) == "w00a w01a w02a w03aw04a w05a w06a w0" + kEllipsis,
          "flex item: one ellipsis after repeated visits");
}

} // namespace

void testLineClamp() {
    printf("\n=== line-clamp ===\n");
    testCascade();
    testStandardClamp();
    testWebkitClamp();
    testNestedBlocks();
    testMixedContent();
    testRelayout();
}
