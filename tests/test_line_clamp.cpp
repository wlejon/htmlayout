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
    // A text node's style is its parent's, as in a real DOM (the cascade
    // gives text the inherited values), so `direction` reaches bidi.
    const ComputedStyle& computedStyle() const override {
        return isText && parentNode ? parentNode->style_ : style_;
    }
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

// The tests write the `line-clamp` shorthand straight into computed styles;
// expand it into its longhands as the cascade would.
void expandClampShorthands(ClampNode* n) {
    auto it = n->style_.find("line-clamp");
    if (it != n->style_.end()) {
        std::string v = it->second;
        n->style_.erase(it);
        for (auto& e : expandShorthand("line-clamp", v)) n->style_[e.property] = e.value;
    }
    for (auto* c : n->childNodes) expandClampShorthands(static_cast<ClampNode*>(c));
}

void layout(ClampNode* root, float width, ClampMetrics& m) {
    expandClampShorthands(root);
    markSubtreeDirty(root);
    layoutTree(root, width, m);
}

std::string expansion(const char* value) {
    std::string s;
    for (auto& e : expandShorthand("line-clamp", value)) {
        if (!s.empty()) s += "; ";
        s += e.property + ": " + e.value;
    }
    return s;
}

void checkExpansion(const char* value, const char* expected) {
    std::string got = expansion(value);
    std::string name = std::string("line-clamp: ") + value + "  ->  " + expected;
    if (got != expected) name += "  [got " + got + "]";
    check(got == expected, name.c_str());
}

void testCascade() {
    printf("--- line-clamp: cascade ---\n");
    check(initialValue("max-lines") == "none", "max-lines: initial value none");
    check(initialValue("block-ellipsis") == "no-ellipsis", "block-ellipsis: initial value no-ellipsis");
    check(initialValue("continue") == "auto", "continue: initial value auto");
    check(initialValue("-webkit-line-clamp") == "none", "-webkit-line-clamp: initial value none");
    check(initialValue("-webkit-box-orient") == "horizontal",
          "-webkit-box-orient: initial value horizontal");
    check(!isInherited("max-lines") && !isInherited("continue") &&
              !isInherited("-webkit-line-clamp"),
          "max-lines / continue: not inherited");
    check(isInherited("block-ellipsis"), "block-ellipsis: inherited");
    check(isShorthandProperty("line-clamp"), "line-clamp is a shorthand");

    checkExpansion("none", "max-lines: none; block-ellipsis: no-ellipsis; continue: auto");
    checkExpansion("auto", "max-lines: none; block-ellipsis: auto; continue: collapse");
    checkExpansion("3", "max-lines: 3; block-ellipsis: auto; continue: collapse");
    checkExpansion("2 no-ellipsis", "max-lines: 2; block-ellipsis: no-ellipsis; continue: collapse");
    checkExpansion("\" [more]\" 1", "max-lines: 1; block-ellipsis: \" [more]\"; continue: collapse");
    checkExpansion("2 auto", "max-lines: 2; block-ellipsis: auto; continue: collapse");
    checkExpansion("2 -webkit-legacy", "max-lines: 2; block-ellipsis: auto; continue: -webkit-legacy");
    checkExpansion("\"...\"", "max-lines: none; block-ellipsis: \"...\"; continue: collapse");
    checkExpansion("inherit", "max-lines: inherit; block-ellipsis: inherit; continue: inherit");
    checkExpansion("0", "");
    checkExpansion("-1", "");
    checkExpansion("2 3", "");
    checkExpansion("2 auto auto", "");
    checkExpansion("-webkit-legacy", "");
    checkExpansion("-webkit-legacy 2", "");
    checkExpansion("2 bogus", "");

    Cascade cascade;
    cascade.addStylesheet(parse(
        ".legacy { display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: 3; }"
        ".std { line-clamp: 2 \"...\"; }"
        ".long { max-lines: 4; continue: collapse; block-ellipsis: \"--\"; }"
        ".bad { line-clamp: 2; line-clamp: 0; }"));
    MockElement legacy; legacy.tag = "div"; legacy.classes = "legacy";
    auto ls = cascade.resolve(legacy);
    check(ls["display"] == "-webkit-box", "cascade: display -webkit-box kept");
    check(ls["-webkit-box-orient"] == "vertical", "cascade: -webkit-box-orient kept");
    check(ls["-webkit-line-clamp"] == "3", "cascade: -webkit-line-clamp kept");
    MockElement std_; std_.tag = "div"; std_.classes = "std";
    auto ss = cascade.resolve(std_);
    check(ss["max-lines"] == "2" && ss["block-ellipsis"] == "\"...\"" &&
              ss["continue"] == "collapse",
          "cascade: line-clamp expands into its longhands");
    MockElement lng; lng.tag = "div"; lng.classes = "long";
    auto lg = cascade.resolve(lng);
    check(lg["max-lines"] == "4" && lg["continue"] == "collapse" && lg["block-ellipsis"] == "\"--\"",
          "cascade: the longhands can be set directly");
    MockElement bad; bad.tag = "div"; bad.classes = "bad";
    check(cascade.resolve(bad)["max-lines"] == "2", "cascade: an invalid line-clamp is dropped");
    auto sheet = parse("@supports (line-clamp: 2) { .x { color: red } }"
                       "@supports (line-clamp: 0) { .y { color: red } }");
    check(sheet.rules.size() == 1 && sheet.rules[0].selector == ".x",
          "@supports: line-clamp validates through its expansion");
}

void testLonghandLayout() {
    printf("--- line-clamp: layout reads the longhands ---\n");
    ClampMetrics m;
    auto make = [](Tree& t) {
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        t.textNode(box, words(16));
        return box;
    };
    {
        Tree t;
        ClampNode* box = make(t);
        box->style_["max-lines"] = "2";
        box->style_["continue"] = "collapse";
        box->style_["block-ellipsis"] = "auto";
        layout(t.nodes[0].get(), 120, m);
        check(approxEq(box->box.contentRect.height, 24) && box->box.textTruncated,
              "max-lines + continue: collapse clamps");
    }
    {
        Tree t;
        ClampNode* box = make(t);
        box->style_["max-lines"] = "2";
        layout(t.nodes[0].get(), 120, m);
        check(approxEq(box->box.contentRect.height, 48) && !box->box.textTruncated,
              "max-lines alone (continue: auto) does not clamp");
    }
    {
        Tree t;
        ClampNode* box = make(t);
        box->style_["max-lines"] = "1";
        box->style_["continue"] = "collapse";
        box->style_["block-ellipsis"] = "no-ellipsis";
        layout(t.nodes[0].get(), 120, m);
        const ClampNode* text = static_cast<ClampNode*>(box->childNodes[0]);
        check(approxEq(box->box.contentRect.height, 12) && !endsWith(drawn(text), kEllipsis),
              "block-ellipsis: no-ellipsis clamps without an ellipsis");
    }
    {
        Tree t;
        ClampNode* box = make(t);
        box->style_["max-lines"] = "1";
        box->style_["continue"] = "-webkit-legacy";
        box->style_["block-ellipsis"] = "\"+\"";
        layout(t.nodes[0].get(), 120, m);
        const ClampNode* text = static_cast<ClampNode*>(box->childNodes[0]);
        check(approxEq(box->box.contentRect.height, 12) && endsWith(drawn(text), "+"),
              "continue: -webkit-legacy clamps, with a string block-ellipsis");
    }
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

    // -webkit-inline-box is inline-level: it sits on the line after the text
    // before it, and still clamps its own content.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        t.textNode(root, "ab ");
        ClampNode* ib = t.block(root, "-webkit-inline-box");
        ib->style_["-webkit-box-orient"] = "vertical";
        ib->style_["-webkit-line-clamp"] = "2";
        ib->style_["width"] = "60px";
        ClampNode* txt = t.textNode(ib, words(8));   // two words a line: 4 lines
        t.textNode(root, " cd");
        layout(root, 400.0f, m);
        check(approxEq(ib->box.contentRect.x, 18.0f),
              "-webkit-inline-box: placed inline after the preceding text");
        check(approxEq(ib->box.contentRect.height, 24.0f) && ib->box.textTruncated,
              "-webkit-inline-box: clamps its own lines");
        check(endsWith(drawn(txt), kEllipsis), "-webkit-inline-box: carries the ellipsis");
        check(root->box.lineBoxes.size() == 1, "-webkit-inline-box: one line in the parent");
    }
    // An inline-block is a block container too: line-clamp applies to it, and
    // taking the clamp away restores its lines.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        t.textNode(root, "ab ");
        ClampNode* ib = t.block(root, "inline-block");
        ib->style_["line-clamp"] = "2";
        ib->style_["width"] = "60px";
        ClampNode* txt = t.textNode(ib, words(8));
        layout(root, 400.0f, m);
        check(approxEq(ib->box.contentRect.height, 24.0f) && endsWith(drawn(txt), kEllipsis),
              "inline-block: line-clamp clamps it");
        ib->style_["line-clamp"] = "none";
        layout(root, 400.0f, m);
        check(approxEq(ib->box.contentRect.height, 48.0f) && !ib->box.textTruncated &&
                  !endsWith(drawn(txt), kEllipsis),
              "inline-block: removing the clamp restores its lines");
    }
    {
        Cascade cascade;
        cascade.addStylesheet(parse(".i { display: -webkit-inline-box; }"
                                    ".abs { position: absolute; }"));
        MockElement e; e.tag = "span"; e.classes = "i abs";
        check(cascade.resolve(e)["display"] == "-webkit-box",
              "-webkit-inline-box blockifies to -webkit-box");
        auto sheet = parse("@supports (display: -webkit-inline-box) { .x { color: red } }");
        check(sheet.rules.size() == 1, "@supports accepts display: -webkit-inline-box");
    }

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

// The run that draws the ellipsis (its text ends with it), or null.
const PlacedTextRun* ellipsisRun(const ClampNode* t, const std::string& e = kEllipsis) {
    for (const auto& r : t->box.textRuns)
        if (endsWith(r.text, e)) return &r;
    return nullptr;
}

// Uppercase ASCII letters are right-to-left, everything else takes the
// paragraph's direction except lowercase letters and digits, which are
// left-to-right (enough of UAX #9 for a mixed line).
struct BidiClampMetrics : ClampMetrics {
    bool bidiAware() const override { return true; }
    void bidiLevels(std::string_view text, bool rtlBase, std::vector<uint8_t>& out) override {
        out.resize(text.size());
        for (size_t i = 0; i < text.size(); i++) {
            char c = text[i];
            bool r = c >= 'A' && c <= 'Z';
            bool l = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            out[i] = r ? 1 : l ? (rtlBase ? 2 : 0) : (rtlBase ? 1 : 0);
        }
    }
};

void testEllipsisPlacement() {
    printf("--- line-clamp: ellipsis placement ---\n");
    ClampMetrics m;
    // RTL: the line's inline end is its left edge. Line 2 is right-aligned
    // over [6, 120]; the ellipsis goes at the left edge, and text is cut from
    // the logical end of the line, which is its visual left.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["direction"] = "rtl";
        box->style_["line-clamp"] = "2";
        ClampNode* txt = t.textNode(box, words(16));
        layout(root, 120.0f, m);
        check(approxEq(box->box.contentRect.height, 24.0f), "rtl: clamped to 2 lines");
        // Reordered right to left: w04a at the right edge, w07a at the left.
        const PlacedTextRun* e = ellipsisRun(txt);
        check(e != nullptr, "rtl: an ellipsis is placed");
        if (e) {
            check(approxEq(e->x, 0.0f), "rtl: the ellipsis sits at the left (inline-end) edge");
            check(e->text == "w0" + kEllipsis && approxEq(e->x + e->width, 30.0f),
                  "rtl: the logically last word is cut from its end, in place");
        }
        bool startKept = false;
        for (const auto& r : txt->box.textRuns)
            if (r.text == "w04a" && approxEq(r.y, 12.0f)) startKept = approxEq(r.x + r.width, 120.0f);
        check(startKept, "rtl: the line's logical start stays at the right edge");
    }
    // LTR line ending in an RTL run (bidi-aware metrics): that run is cut from
    // its logical start, the side facing the line's right edge, and the
    // ellipsis is a run of its own right after what is kept.
    {
        BidiClampMetrics bm;
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "1";
        ClampNode* a = t.textNode(box, "w00a w01a ");
        ClampNode* span = t.block(box, "inline");
        span->style_["direction"] = "rtl";
        span->style_["unicode-bidi"] = "isolate";
        ClampNode* b = t.textNode(span, "ABCDEFGHIJ");
        t.textNode(box, " w02a w03a w04a");
        layout(root, 130.0f, bm);   // line 1: "w00a w01a " (60) + 10 caps (60) = 120
        const PlacedTextRun* e = ellipsisRun(b);
        const PlacedTextRun* kept = nullptr;
        for (const auto& r : b->box.textRuns)
            if (!r.text.empty() && r.text != kEllipsis) kept = &r;
        check(e && e->text == kEllipsis, "mixed: the ellipsis is its own run beside the rtl run");
        check(kept && kept->text.size() < 10 && kept->text.back() == 'J',
              "mixed: the rtl run keeps its logical end (its visual left side)");
        if (e && kept) {
            check(approxEq(kept->x + kept->width, e->x) && e->x + e->width <= 130.0f + 0.01f,
                  "mixed: the ellipsis follows the kept text and fits the line");
        }
        check(drawn(a) == "w00a w01a ", "mixed: the ltr text before it is untouched");
    }
    // A line ending in an atomic inline that leaves room: the ellipsis goes
    // after it (carried by a text run on the line), and it stays.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "1";
        ClampNode* txt = t.textNode(box, "w00a w01a ");
        ClampNode* ib = t.block(box, "inline-block");
        ib->style_["width"] = "20px";
        ib->style_["height"] = "10px";
        t.textNode(box, " w02a w03a");
        layout(root, 100.0f, m);   // line 1: text to 60, the box to 80
        check(approxEq(box->box.contentRect.height, 12.0f), "atomic end: clamped to 1 line");
        check(!ib->box.clampHidden, "atomic end: the inline-block stays");
        const PlacedTextRun* e = ellipsisRun(txt);
        check(e && e->text == kEllipsis && approxEq(e->x, 80.0f),
              "atomic end: the ellipsis follows the inline-block");
        check(drawn(txt) == "w00a w01a " + kEllipsis, "atomic end: the text itself is not cut");
    }
    // ... and one that leaves no room is removed, the ellipsis following the
    // text before it.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "1";
        ClampNode* txt = t.textNode(box, "w00a w01a ");
        ClampNode* ib = t.block(box, "inline-block");
        ib->style_["width"] = "20px";
        ib->style_["height"] = "10px";
        t.textNode(box, " w02a w03a");
        layout(root, 90.0f, m);    // the box ends at 80; 80 + 18 > 90
        check(ib->box.clampHidden, "atomic end, no room: the inline-block is removed");
        check(drawn(txt) == "w00a w01a" + kEllipsis, "atomic end, no room: ellipsis after the text");
    }
    // A last line holding only an atomic inline: the ellipsis is drawn by the
    // last text kept before it, on that line.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["line-clamp"] = "2";
        ClampNode* txt = t.textNode(box, "w00a w01a w02a ");
        ClampNode* ib = t.block(box, "inline-block");
        ib->style_["width"] = "75px";
        ib->style_["height"] = "10px";
        ClampNode* after = t.textNode(box, " w03a w04a");
        layout(root, 100.0f, m);   // line 2 is the box alone, 0..75 (" w03a" wraps)
        check(!ib->box.clampHidden, "atomic-only line: the inline-block stays");
        // Whichever kept text node carries it (here the collapsible space
        // after the box, which is still on line 2).
        const PlacedTextRun* e = ellipsisRun(after);
        if (!e) e = ellipsisRun(txt);
        check(e && e->text == kEllipsis && approxEq(e->x, 75.0f),
              "atomic-only line: the ellipsis follows it");
        if (e) {
            const float line2Bottom = box->box.lineBoxes.size() >= 2
                ? box->box.lineBoxes[1].top + box->box.lineBoxes[1].height : -1.0f;
            check(approxEq(e->y + e->height, line2Bottom), "atomic-only line: on the second line");
        }
    }
    // RTL line ending (at its left) in an atomic inline with room.
    {
        Tree t;
        ClampNode* root = t.block(nullptr);
        ClampNode* box = t.block(root);
        box->style_["direction"] = "rtl";
        box->style_["line-clamp"] = "1";
        ClampNode* txt = t.textNode(box, "w00a w01a ");
        ClampNode* ib = t.block(box, "inline-block");
        ib->style_["width"] = "20px";
        ib->style_["height"] = "10px";
        t.textNode(box, " w02a w03a");
        layout(root, 100.0f, m);   // right-aligned: the box at 20..40, text 40..100
        const PlacedTextRun* e = ellipsisRun(txt);
        check(!ib->box.clampHidden && e && e->text == kEllipsis && approxEq(e->x + e->width, 20.0f),
              "rtl atomic end: the ellipsis sits left of the inline-block");
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
    expandClampShorthands(box);
    markDirty(box);
    layoutTree(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 36.0f), "relayout: 2 -> 3 grows to 3 lines");
    check(endsWith(drawn(t1), "w07a"), "relayout: 2 -> 3 restores the cut text");
    check(!p2->box.clampHidden, "relayout: 2 -> 3 un-hides line 3");

    box->style_["line-clamp"] = "none";
    expandClampShorthands(box);
    markDirty(box);
    layoutTree(root, 120.0f, m);
    check(approxEq(box->box.contentRect.height, 36.0f) && !box->box.textTruncated,
          "relayout: none restores full height");
    check(drawn(t1).find(kEllipsis) == std::string::npos, "relayout: none drops the ellipsis");

    // A second pass with nothing marked reuses the clamped subtree as is.
    box->style_["line-clamp"] = "1";
    expandClampShorthands(box);
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
    testLonghandLayout();
    testStandardClamp();
    testWebkitClamp();
    testNestedBlocks();
    testMixedContent();
    testEllipsisPlacement();
    testRelayout();
}
