// htmlayout performance baseline.
//
// Builds a synthetic document and a utility-heavy stylesheet, then times the
// four things a consumer actually pays for on every frame: parsing the sheet,
// resolving style for the document, laying it out cold, and re-laying it out
// after a single leaf changes. Every scenario reports wall time next to the
// counters that explain it — heap allocations, style-map lookups, and text
// measurements — because a number without those three is just a number, and
// can't say *why* it moved.
//
// The DOM here is deliberately a consumer-shaped bridge: the library never owns
// nodes, so the benchmark implements ElementRef and LayoutNode the same way a
// real embedder does, and pays the same virtual-call and string-view costs.

#include "css/cascade.h"
#include "css/parser.h"
#include "css/selector.h"
#include "css/ua_stylesheet.h"
#include "layout/box.h"
#include "layout/formatting_context.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Allocation counting
//
// Layout and cascade are allocation-bound far more than they are arithmetic-
// bound, so the alloc count is the number that predicts the wall time. Counted
// with relaxed atomics: the increment is a rounding error next to the malloc it
// is counting, and it never has to be exact, only comparable between runs.
// ---------------------------------------------------------------------------
namespace {
std::atomic<uint64_t> g_allocs{0};
std::atomic<uint64_t> g_bytes{0};
bool g_counting = false;
} // namespace

void* operator new(size_t n) {
    if (g_counting) {
        g_allocs.fetch_add(1, std::memory_order_relaxed);
        g_bytes.fetch_add(n, std::memory_order_relaxed);
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace {

struct AllocSnapshot {
    uint64_t allocs;
    uint64_t bytes;
};
AllocSnapshot allocMark() {
    return {g_allocs.load(std::memory_order_relaxed),
            g_bytes.load(std::memory_order_relaxed)};
}
AllocSnapshot allocSince(AllocSnapshot m) {
    return {g_allocs.load(std::memory_order_relaxed) - m.allocs,
            g_bytes.load(std::memory_order_relaxed) - m.bytes};
}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ---------------------------------------------------------------------------
// The benchmark DOM
//
// ElementRef::parent() and LayoutNode::parent() have the same signature and
// different return types, so one class cannot implement both. A real consumer
// hits this too and solves it the same way: one node, two views onto it.
// ---------------------------------------------------------------------------
struct Dom;

struct ElemView : htmlayout::css::ElementRef {
    Dom* d = nullptr;
    std::string_view tagName() const override;
    std::string_view id() const override;
    std::string_view className() const override;
    std::string_view getAttribute(std::string_view name) const override;
    bool hasAttribute(std::string_view name) const override;
    ElementRef* parent() const override;
    std::span<ElementRef* const> children() const override;
    bool hasTextChildren() const override;
    int childIndex() const override;
    int childIndexOfType() const override;
    int siblingCount() const override;
    int siblingCountOfType() const override;
};

struct NodeView : htmlayout::layout::LayoutNode {
    Dom* d = nullptr;
    std::string_view tagName() const override;
    bool isTextNode() const override;
    std::string_view textContent() const override;
    LayoutNode* parent() const override;
    std::span<LayoutNode* const> children() const override;
    const htmlayout::css::ComputedStyle& computedStyle() const override;
};

struct Dom {
    std::string tag;
    std::string domId;
    std::string klass;
    std::string text;
    bool isText = false;

    Dom* parentDom = nullptr;
    std::vector<std::unique_ptr<Dom>> owned;

    htmlayout::css::ComputedStyle style;

    ElemView ev;
    NodeView nv;
    std::vector<htmlayout::css::ElementRef*> elemKids;    // elements only
    std::vector<htmlayout::layout::LayoutNode*> nodeKids; // elements + text

    Dom(std::string t, std::string c = {}, std::string i = {})
        : tag(std::move(t)), domId(std::move(i)), klass(std::move(c)) {
        ev.d = this;
        nv.d = this;
    }

    Dom* add(std::unique_ptr<Dom> child) {
        Dom* raw = child.get();
        raw->parentDom = this;
        if (!raw->isText) elemKids.push_back(&raw->ev);
        nodeKids.push_back(&raw->nv);
        owned.push_back(std::move(child));
        return raw;
    }
    Dom* addElem(std::string t, std::string c = {}, std::string i = {}) {
        return add(std::make_unique<Dom>(std::move(t), std::move(c), std::move(i)));
    }
    Dom* addText(std::string t) {
        auto n = std::make_unique<Dom>("#text");
        n->isText = true;
        n->text = std::move(t);
        return add(std::move(n));
    }
};

std::string_view ElemView::tagName() const { return d->tag; }
std::string_view ElemView::id() const { return d->domId; }
std::string_view ElemView::className() const { return d->klass; }
std::string_view ElemView::getAttribute(std::string_view) const { return {}; }
bool ElemView::hasAttribute(std::string_view) const { return false; }
htmlayout::css::ElementRef* ElemView::parent() const {
    return d->parentDom ? &d->parentDom->ev : nullptr;
}
std::span<htmlayout::css::ElementRef* const> ElemView::children() const {
    return d->elemKids;
}
bool ElemView::hasTextChildren() const {
    for (auto& c : d->owned)
        if (c->isText) return true;
    return false;
}
int ElemView::childIndex() const {
    if (!d->parentDom) return 0;
    int i = 0;
    for (auto* c : d->parentDom->elemKids) {
        if (c == &d->ev) return i;
        i++;
    }
    return 0;
}
int ElemView::childIndexOfType() const {
    if (!d->parentDom) return 0;
    int i = 0;
    for (auto* c : d->parentDom->elemKids) {
        auto* e = static_cast<ElemView*>(c);
        if (e == &d->ev) return i;
        if (e->d->tag == d->tag) i++;
    }
    return 0;
}
int ElemView::siblingCount() const {
    return d->parentDom ? static_cast<int>(d->parentDom->elemKids.size()) : 1;
}
int ElemView::siblingCountOfType() const {
    if (!d->parentDom) return 1;
    int n = 0;
    for (auto* c : d->parentDom->elemKids)
        if (static_cast<ElemView*>(c)->d->tag == d->tag) n++;
    return n;
}

std::string_view NodeView::tagName() const { return d->tag; }
bool NodeView::isTextNode() const { return d->isText; }
std::string_view NodeView::textContent() const { return d->text; }
htmlayout::layout::LayoutNode* NodeView::parent() const {
    return d->parentDom ? &d->parentDom->nv : nullptr;
}
std::span<htmlayout::layout::LayoutNode* const> NodeView::children() const {
    return d->nodeKids;
}
const htmlayout::css::ComputedStyle& NodeView::computedStyle() const {
    return d->style;
}

// ---------------------------------------------------------------------------
// Text metrics: a deterministic stand-in for a real shaper.
//
// A real shaper costs 100x what this does, so treat `measureCalls` — not the
// time this contributes — as the honest signal for text cost.
// ---------------------------------------------------------------------------
struct BenchMetrics : htmlayout::layout::TextMetrics {
    float measureWidth(std::string_view text, std::string_view, float fontSize,
                       std::string_view) override {
        measureCalls++;
        float w = 0;
        for (char c : text) w += (c == ' ' ? 0.30f : 0.55f) * fontSize;
        return w;
    }
    float lineHeight(std::string_view, float fontSize, std::string_view) override {
        return fontSize * 1.25f;
    }
};

// ---------------------------------------------------------------------------
// The document: a card grid, the shape most real app pages actually are.
// ---------------------------------------------------------------------------
std::unique_ptr<Dom> buildDocument(int cards) {
    auto html = std::make_unique<Dom>("html");
    Dom* body = html->addElem("body");

    Dom* header = body->addElem("header", "site-header");
    header->addElem("h1", "title")->addText("Dashboard");
    Dom* nav = header->addElem("nav", "nav");
    for (int i = 0; i < 6; i++)
        nav->addElem("a", "nav-link")->addText("Section " + std::to_string(i));

    Dom* main = body->addElem("main", "content");
    Dom* grid = main->addElem("section", "cards");
    for (int i = 0; i < cards; i++) {
        Dom* card = grid->addElem("div", i % 3 == 0 ? "card card-wide" : "card");
        card->addElem("h3", "card-title")->addText("Card number " + std::to_string(i));
        card->addElem("p", "card-text")
            ->addText("The quick brown fox jumps over the lazy dog while the "
                      "engine reflows a paragraph of perfectly ordinary text.");
        Dom* meta = card->addElem("div", "card-meta");
        for (int t = 0; t < 3; t++)
            meta->addElem("span", "tag")->addText("tag" + std::to_string(t));
    }

    Dom* aside = body->addElem("aside", "sidebar");
    Dom* list = aside->addElem("ul", "list");
    for (int i = 0; i < 12; i++)
        list->addElem("li", "list-item")->addText("Sidebar entry " + std::to_string(i));

    body->addElem("footer", "site-footer")->addText("Footer content here");
    return html;
}

int countNodes(Dom* d) {
    int n = 1;
    for (auto& c : d->owned) n += countNodes(c.get());
    return n;
}

// A real sheet is mostly rules that do not match. The handwritten rules below
// are the ones that do; the generated utility classes are the thousands that
// don't, which is what the rule buckets exist to reject cheaply.
std::string buildStylesheet(int utilityRules) {
    std::string css = R"(
* { box-sizing: border-box; }
body { margin: 0; font-family: sans-serif; font-size: 16px; color: #222; }
.site-header { display: flex; padding: 16px 24px; border-bottom: 1px solid #ddd; }
.title { font-size: 24px; font-weight: 700; margin: 0; }
.nav { display: flex; gap: 12px; margin-left: auto; }
.nav-link { padding: 8px 12px; color: #0066cc; text-decoration: none; }
.content { display: block; padding: 24px; }
.cards { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 16px; }
.card { display: block; padding: 16px; border: 1px solid #e0e0e0; border-radius: 8px; background: #fff; }
.card-wide { grid-column: span 2; }
.card-title { font-size: 18px; font-weight: 600; margin: 0 0 8px 0; }
.card-text { font-size: 14px; line-height: 1.5; margin: 0 0 12px 0; color: #555; }
.card-meta { display: flex; gap: 8px; }
.tag { display: inline-block; padding: 2px 8px; font-size: 12px; background: #f0f0f0; border-radius: 4px; }
.sidebar { display: block; width: 240px; padding: 16px; }
.list { margin: 0; padding: 0; }
.list-item { padding: 6px 0; border-bottom: 1px solid #eee; font-size: 14px; }
.site-footer { padding: 24px; text-align: center; color: #888; font-size: 13px; }
.card:hover { border-color: #0066cc; }
.card .card-title { letter-spacing: 0.01em; }
.cards > .card:first-child { border-top-width: 2px; }
)";
    css.reserve(css.size() + static_cast<size_t>(utilityRules) * 48);
    for (int i = 0; i < utilityRules; i++) {
        css += ".u-" + std::to_string(i) + " { margin-top: " +
               std::to_string(i % 32) + "px; }\n";
    }
    return css;
}

// ---------------------------------------------------------------------------
// Cascade: resolve the whole document, parent style before child.
// ---------------------------------------------------------------------------
void styleTree(const htmlayout::css::Cascade& cascade, Dom* d,
               const htmlayout::css::ComputedStyle* parentStyle) {
    if (d->isText) {
        // Text inherits wholesale from its parent; layout reads font/color off it.
        if (parentStyle) d->style = *parentStyle;
        return;
    }
    d->style = cascade.resolve(d->ev, {}, parentStyle);
    for (auto& c : d->owned) styleTree(cascade, c.get(), &d->style);
}

Dom* findDeepLeaf(Dom* d) {
    for (auto& c : d->owned) {
        if (c->klass == "card-title") return c.get();
        if (Dom* f = findDeepLeaf(c.get())) return f;
    }
    return nullptr;
}

struct Row {
    const char* name;
    double ms;
    uint64_t allocs;
    uint64_t bytes;
    uint64_t lookups;
    uint64_t measures;
};

void printRow(const Row& r) {
    printf("  %-26s %9.2f ms  %11llu allocs  %8llu KB  %11llu lookups  %9llu measures\n",
           r.name, r.ms, (unsigned long long)r.allocs,
           (unsigned long long)(r.bytes / 1024), (unsigned long long)r.lookups,
           (unsigned long long)r.measures);
}

} // namespace

int main(int argc, char** argv) {
    using namespace htmlayout;

    int cards = argc > 1 ? std::atoi(argv[1]) : 400;
    int utilityRules = argc > 2 ? std::atoi(argv[2]) : 2000;
    const float viewportW = 1280.0f;

    auto doc = buildDocument(cards);
    std::string css = buildStylesheet(utilityRules);
    int nodes = countNodes(doc.get());

    printf("=== htmlayout baseline ===\n");
    printf("document: %d nodes (%d cards)   stylesheet: %zu KB (%d utility rules)\n",
           nodes, cards, css.size() / 1024, utilityRules);
    printf("viewport: %.0fpx\n\n", viewportW);

    BenchMetrics metrics;
    g_counting = true;

    // ---- 1. Parse the stylesheet ----
    css::Stylesheet sheet;
    {
        auto m = allocMark();
        auto t0 = Clock::now();
        sheet = css::parse(css);
        double ms = msSince(t0);
        auto a = allocSince(m);
        printRow({"parse stylesheet", ms, a.allocs, a.bytes, 0, 0});
    }

    // ---- 2. Build the cascade (bucket the rules) ----
    // The UA sheet goes in first, exactly as a real consumer does. Without it
    // `html` and `body` compute to display:inline and the whole document lays
    // out through an inline formatting context — a shape no real page has.
    css::Cascade cascade;
    {
        auto m = allocMark();
        auto t0 = Clock::now();
        cascade.addStylesheet(css::defaultUserAgentStylesheet(), nullptr, nullptr,
                              css::Origin::UserAgent);
        cascade.addStylesheet(sheet);
        double ms = msSince(t0);
        auto a = allocSince(m);
        printRow({"build cascade", ms, a.allocs, a.bytes, 0, 0});
    }

    // ---- 3. Resolve style for every element ----
    {
        auto m = allocMark();
        auto t0 = Clock::now();
        styleTree(cascade, doc.get(), nullptr);
        double ms = msSince(t0);
        auto a = allocSince(m);
        printRow({"style document (cold)", ms, a.allocs, a.bytes, 0, 0});
        printf("  %-26s %9.1f allocs/element\n", "",
               static_cast<double>(a.allocs) / nodes);
    }

    // ---- 4. Cold layout ----
    layout::LayoutNode* root = &doc->nv;
    {
        auto m = allocMark();
        metrics.measureCalls = 0;
        auto t0 = Clock::now();
        layout::layoutTree(root, viewportW, metrics);
        double ms = msSince(t0);
        auto a = allocSince(m);
        const auto& st = layout::lastLayoutStats();
        printRow({"layout (cold)", ms, a.allocs, a.bytes, st.styleLookups,
                  metrics.measureCalls});
        printf("  %-26s laidOut=%u reused=%u visits=%u  tree=%.2fms abs=%.2fms hit=%.2fms\n",
               "", st.laidOut, st.reused, st.visits, st.treeMs, st.absoluteMs,
               st.hitBoundsMs);
    }

    // ---- 5. Re-layout with nothing dirty (the pure reuse path) ----
    {
        auto m = allocMark();
        metrics.measureCalls = 0;
        auto t0 = Clock::now();
        layout::layoutTree(root, viewportW, metrics);
        double ms = msSince(t0);
        auto a = allocSince(m);
        const auto& st = layout::lastLayoutStats();
        printRow({"layout (no-op reflow)", ms, a.allocs, a.bytes, st.styleLookups,
                  metrics.measureCalls});
        printf("  %-26s laidOut=%u reused=%u visits=%u\n", "", st.laidOut,
               st.reused, st.visits);
    }

    // ---- 6. Re-layout after one leaf changes (the incremental path) ----
    {
        Dom* leaf = findDeepLeaf(doc.get());
        leaf->style["font-size"] = "19px";
        layout::markDirty(&leaf->nv);

        auto m = allocMark();
        metrics.measureCalls = 0;
        auto t0 = Clock::now();
        layout::layoutTree(root, viewportW, metrics);
        double ms = msSince(t0);
        auto a = allocSince(m);
        const auto& st = layout::lastLayoutStats();
        printRow({"layout (1 leaf dirty)", ms, a.allocs, a.bytes, st.styleLookups,
                  metrics.measureCalls});
        printf("  %-26s laidOut=%u reused=%u visits=%u  "
               "failDirty=%u failAvailW=%u failOverride=%u\n",
               "", st.laidOut, st.reused, st.visits, st.reuseFailDirty,
               st.reuseFailAvailW, st.reuseFailOverride);
    }

    // ---- 7. Re-layout after a viewport change (the resize path) ----
    {
        auto m = allocMark();
        metrics.measureCalls = 0;
        auto t0 = Clock::now();
        layout::layoutTree(root, viewportW - 200.0f, metrics);
        double ms = msSince(t0);
        auto a = allocSince(m);
        const auto& st = layout::lastLayoutStats();
        printRow({"layout (resize)", ms, a.allocs, a.bytes, st.styleLookups,
                  metrics.measureCalls});
        printf("  %-26s laidOut=%u reused=%u visits=%u\n", "", st.laidOut,
               st.reused, st.visits);
    }

    // ---- 8. Hit testing (one mouse move) ----
    {
        auto m = allocMark();
        layout::layoutStatsMut().styleLookups = 0;
        auto t0 = Clock::now();
        volatile int hits = 0;
        for (int i = 0; i < 1000; i++) {
            float x = static_cast<float>((i * 37) % 1200);
            float y = static_cast<float>((i * 91) % 2000);
            if (layout::hitTest(root, x, y)) hits++;
        }
        double ms = msSince(t0);
        auto a = allocSince(m);
        uint64_t lookups = layout::layoutStatsMut().styleLookups;
        printRow({"hitTest x1000", ms, a.allocs, a.bytes, lookups, 0});
        // styleVal() is called a fixed number of times per node the walk reaches,
        // so lookups/1000 says how much of the tree each hit test actually
        // touched — i.e. whether the hitBounds prune is pruning at all.
        printf("  %-26s %9.1f lookups/hittest  %9.1f allocs/hittest  (document is %d nodes)\n",
               "", static_cast<double>(lookups) / 1000.0,
               static_cast<double>(a.allocs) / 1000.0, nodes);
    }

    g_counting = false;
    printf("\n");
    return 0;
}
