// Parent-child margin collapsing (CSS 2.1 §8.3.1) and the geometry it moves:
// when a first child's top margin escapes through its parent, everything the
// parent laid out shifts up with it — boxes, line boxes and placed text runs.

#include "test_margin_collapse.h"
#include "test_helpers.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace htmlayout::layout;
using namespace htmlayout::css;

namespace {

struct McNode : public LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    McNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes;
    ComputedStyle style_;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override {
        return isText && parentNode ? parentNode->style_ : style_;
    }
};

// 10px per byte, 20px lines.
struct McMetrics : public TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

struct Tree {
    std::vector<std::unique_ptr<McNode>> nodes;
    McNode* el(McNode* parent, const char* display = "block") {
        auto n = std::make_unique<McNode>();
        n->style_["display"] = display;
        n->style_["font-size"] = "16px";
        McNode* raw = n.get();
        nodes.push_back(std::move(n));
        if (parent) attach(parent, raw);
        return raw;
    }
    McNode* txt(McNode* parent, const std::string& s) {
        auto n = std::make_unique<McNode>();
        n->isText = true;
        n->text = s;
        McNode* raw = n.get();
        nodes.push_back(std::move(n));
        attach(parent, raw);
        return raw;
    }
    static void attach(McNode* p, McNode* c) {
        c->parentNode = p;
        p->childNodes.push_back(c);
    }
};

bool approx(float a, float b) { return std::fabs(a - b) < 0.5f; }

// A text node's rect is the union of its runs; after any shift the two must
// still agree.
bool runsMatchRect(const McNode* t) {
    if (t->box.textRuns.empty()) return false;
    float top = t->box.textRuns.front().y;
    for (const auto& r : t->box.textRuns) top = std::min(top, r.y);
    return approx(top, t->box.contentRect.y);
}

void testTextBesideCollapsingChild() {
    McMetrics m;
    // <div parent><div child style="margin-top:20px">A</div>tail</div>: the
    // child's margin escapes through the parent (parent margin-top 20, child
    // at the parent's content top), and "tail" sits on the anonymous line
    // right below the child, at y 20 in the parent — rect, run and line box.
    Tree t;
    McNode* root = t.el(nullptr);
    McNode* parent = t.el(root);
    McNode* child = t.el(parent);
    child->style_["margin-top"] = "20px";
    McNode* childText = t.txt(child, "A");
    McNode* tail = t.txt(parent, "tail");
    layoutTree(root, 400, m);

    check(approx(parent->box.margin.top, 20.0f) && approx(parent->box.contentRect.y, 20.0f),
          "collapse: the child's margin becomes the parent's");
    check(approx(child->box.contentRect.y, 0.0f), "collapse: the child sits at the parent's top");
    check(runsMatchRect(childText) && approx(childText->box.textRuns[0].y, 0.0f),
          "collapse: the child's own text moves with it");
    check(approx(tail->box.contentRect.y, 20.0f), "collapse: the text after the child is at 20");
    check(runsMatchRect(tail), "collapse: the text's runs moved with its rect");
    check(parent->box.lineBoxes.size() == 1 && approx(parent->box.lineBoxes[0].top, 20.0f),
          "collapse: the anonymous line box moved too");
    check(approx(parent->box.contentRect.height, 40.0f), "collapse: the parent is two lines tall");

    // Hit testing through the runs finds the text where it is drawn: root
    // content y 20 + 20 = 40..60 is "tail"; 60..80 is below everything.
    LayoutNode* hit = hitTest(root, 5.0f, 50.0f);
    check(hit == tail || hit == parent, "collapse: the tail line hit-tests at its new place");
}

void testTextBesideNestedCollapse() {
    McMetrics m;
    // The margin escapes two levels (grandchild -> mid -> parent); each level's
    // text follows its own shift.
    Tree t;
    McNode* root = t.el(nullptr);
    McNode* parent = t.el(root);
    McNode* mid = t.el(parent);
    McNode* gc = t.el(mid);
    gc->style_["margin-top"] = "20px";
    McNode* gt = t.txt(gc, "G");
    McNode* midTail = t.txt(mid, "midtail");
    McNode* tail = t.txt(parent, "tail");
    layoutTree(root, 400, m);

    check(approx(parent->box.contentRect.y, 20.0f) && approx(mid->box.contentRect.y, 0.0f) &&
              approx(gc->box.contentRect.y, 0.0f),
          "nested collapse: boxes stack from the parent's top");
    check(runsMatchRect(gt), "nested collapse: the grandchild's text");
    check(approx(midTail->box.contentRect.y, 20.0f) && runsMatchRect(midTail),
          "nested collapse: text beside the grandchild");
    check(approx(tail->box.contentRect.y, 40.0f) && runsMatchRect(tail),
          "nested collapse: text beside the middle box");
}

void testInlineBesideCollapsingChild() {
    McMetrics m;
    // An inline element's text is placed relative to the element, so moving
    // the element is enough; the text must not move twice.
    Tree t;
    McNode* root = t.el(nullptr);
    McNode* parent = t.el(root);
    McNode* child = t.el(parent);
    child->style_["margin-top"] = "20px";
    t.txt(child, "A");
    McNode* span = t.el(parent, "inline");
    McNode* spanText = t.txt(span, "span");
    layoutTree(root, 400, m);
    check(approx(span->box.contentRect.y, 20.0f), "inline beside collapse: the span at 20");
    check(approx(spanText->box.contentRect.y, 0.0f) && runsMatchRect(spanText),
          "inline beside collapse: its text stays at the span's top");
}

} // namespace

void testMarginCollapse() {
    printf("--- margin collapsing: parent-child geometry ---\n");
    testTextBesideCollapsingChild();
    testTextBesideNestedCollapse();
    testInlineBesideCollapsingChild();
}
