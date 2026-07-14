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
#include "layout/style_util.h"
#include "sampler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <cstdlib>
#include <memory>
#include <unordered_map>
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
    // What is actually being measured, so a big measureCalls can be read as
    // *why*. A consumer's shaper sits behind a cache (bro's does), so the
    // number that matters is not the call count alone but how much of it is
    // re-asking for something already known — a single space, or a word that
    // was measured moments ago in another sizing phase.
    long long spaceCalls = 0;   // measureWidth(" ") — a constant per font
    long long distinctAsks = 0; // (text, size) pairs never seen before
    std::unordered_map<std::string, int> seen;

    float measureWidth(std::string_view text, std::string_view, float fontSize,
                       std::string_view) override {
        measureCalls++;
        if (text == " ") spaceCalls++;
        std::string k(text);
        k += '@';
        k += std::to_string((int)fontSize);
        if (seen.emplace(k, 1).second) distinctAsks++;
        float w = 0;
        for (char c : text) w += (c == ' ' ? 0.30f : 0.55f) * fontSize;
        return w;
    }
    float lineHeight(std::string_view, float fontSize, std::string_view) override {
        return fontSize * 1.25f;
    }
    void resetProfile() { spaceCalls = 0; distinctAsks = 0; seen.clear(); }
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
    uint64_t coldLookups = 0, coldMisses = 0, coldAllocs = 0;
    {
        auto m = allocMark();
        metrics.measureCalls = 0;
        metrics.resetProfile();
#ifdef HTMLAYOUT_STYLE_PROFILE
        layout::resetStyleLookupHistogram();
#endif
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
        printf("  %-26s text: layout asked %llu, shaper saw %llu (%.0fx absorbed); "
               "%lld of the shaper's were measureWidth(\" \")\n",
               "", (unsigned long long)st.textMeasures, (unsigned long long)st.textShaped,
               st.textShaped ? (double)st.textMeasures / st.textShaped : 0.0,
               metrics.spaceCalls);
        coldAllocs = a.allocs;
        coldLookups = st.styleLookups;
        coldMisses = st.styleMisses;
        printf("  %-26s style-is-strings: %llu map lookups (%.0f%% of them misses, "
               "which cost double) + %llu length re-parses\n",
               "", (unsigned long long)st.styleLookups,
               100.0 * st.styleMisses / st.styleLookups,
               (unsigned long long)st.lengthResolves);
#ifdef HTMLAYOUT_STYLE_PROFILE
        auto hist = layout::styleLookupHistogram();
        uint64_t total = 0;
        for (auto& [p, n] : hist) total += n;
        printf("  %-26s which properties (top 30 of %zu, %llu lookups):\n", "",
               hist.size(), (unsigned long long)total);
        uint64_t running = 0;
        for (size_t i = 0; i < hist.size(); i++) {
            running += hist[i].second;
            if (i < 20)
                printf("  %-26s   %-24s %8llu  %5.1f%%  (cum %5.1f%%)\n", "",
                       hist[i].first.c_str(), (unsigned long long)hist[i].second,
                       100.0 * hist[i].second / total, 100.0 * running / total);
        }
        auto sites = layout::styleLookupSiteHistogram();
        printf("  %-26s which call sites (top 30 of %zu):\n", "", sites.size());
        running = 0;
        for (size_t i = 0; i < sites.size(); i++) {
            running += sites[i].second;
            if (i < 30)
                printf("  %-26s   %-44s %8llu  %5.1f%%  (cum %5.1f%%)\n", "",
                       sites[i].first.c_str(), (unsigned long long)sites[i].second,
                       100.0 * sites[i].second / total, 100.0 * running / total);
        }
#endif
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

    // ---- 9. What a style read actually costs ----
    //
    // Layout does 671,647 styleVal() lookups on a cold pass. Whether that is worth
    // restructuring ComputedStyle over depends on what one costs, so price it
    // directly against a real element's style rather than guessing from the
    // aggregate. Both halves: finding the value (a hash + probe) and then turning
    // the text back into a number, which layout redoes on every single read.
    {
        // How big is a ComputedStyle, really? The whole choice of container turns
        // on this: the cascade only stores what it actually set, and every other
        // property falls back to its initial value, so the map may be far smaller
        // than the ~350 properties CSS defines — and an unordered_map is a bad
        // shape for a handful of entries.
        Dom* card = nullptr;
        size_t totalProps = 0, maxProps = 0, elems = 0;
        std::vector<size_t> sizes;
        std::function<void(Dom*)> walk = [&](Dom* d) {
            if (!d->isText) {
                elems++;
                totalProps += d->style.size();
                sizes.push_back(d->style.size());
                if (d->style.size() > maxProps) { maxProps = d->style.size(); card = d; }
            }
            for (auto& c : d->owned) walk(c.get());
        };
        walk(doc.get());
        std::sort(sizes.begin(), sizes.end());
        const auto& st = card->style;
        const int N = 2000000;

        // Price a hit and a miss apart. They are not the same operation: a hit is
        // one hash + probe, a miss is a hash + failed probe and then a second hash
        // lookup in the property registry to fetch the initial value.
        auto t0 = Clock::now();
        double sink = 0;
        for (int i = 0; i < N; i++) sink += layout::styleVal(st, "display").size();
        double hitNs = msSince(t0) * 1e6 / N;

        auto tm = Clock::now();
        for (int i = 0; i < N; i++) sink += layout::styleVal(st, "caption-side").size();
        double missNs = msSince(tm) * 1e6 / N;

        // The mix layout actually runs at, from the pass that just happened.
        const auto& ls = layout::lastLayoutStats();
        (void)ls;
        double lookupNs = (hitNs + missNs) / 2.0;

        const std::string& len = layout::styleVal(st, "font-size");
        auto t1 = Clock::now();
        for (int i = 0; i < N; i++) sink += layout::resolveLength(len, 1280.0f, 16.0f);
        double parseNs = msSince(t1) * 1e6 / N;

        printf("  %-26s %6.1f ns per styleVal()   %6.1f ns per resolveLength()\n",
               "cost of one style read", lookupNs, parseNs);
        printf("  %-26s at 671,647 lookups/pass that is %.1f ms of the cold pass "
               "in lookup alone%s\n", "", lookupNs * 671647 / 1e6,
               sink == 12345.0 ? "!" : "");

        // ---- 10. What the alternatives to that lookup would cost ----
        //
        // Three ways to find a property, priced against the same real style so the
        // choice of representation is made on numbers. The question each answers:
        //
        //   unordered_map   what we do now: hash the name, chase a bucket list
        //   sorted vector   intern the name to a u16 id, binary search ~N entries
        //   flat array      the id IS the index — one load, no search at all
        //
        // The last is the floor: it is what a typed per-node style costs to read.
        printf("  %-26s ComputedStyle size over %zu elements: mean %.1f, median %zu, "
               "max %zu properties\n", "", elems, (double)totalProps / elems,
               sizes[sizes.size() / 2], maxProps);

        // Intern: property name -> dense id, ids sorted so a lookup can bisect.
        std::vector<std::string> names;
        for (const auto& [k, v] : st) names.push_back(k);
        std::sort(names.begin(), names.end());
        std::vector<std::pair<uint16_t, const std::string*>> flat;
        for (size_t i = 0; i < names.size(); i++)
            flat.emplace_back(static_cast<uint16_t>(i), &st.find(names[i])->second);
        // Ask for the same two properties as above, by their interned ids.
        uint16_t idA = 0, idB = 0;
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == "display") idA = static_cast<uint16_t>(i);
            if (names[i] == "border-bottom-width") idB = static_cast<uint16_t>(i);
        }

        auto t2 = Clock::now();
        for (int i = 0; i < N; i++) {
            auto hit = [&](uint16_t id) {
                auto it = std::lower_bound(flat.begin(), flat.end(), id,
                    [](const auto& e, uint16_t k) { return e.first < k; });
                return it->second->size();
            };
            sink += hit(idA);
            sink += hit(idB);
        }
        double bsearchNs = msSince(t2) * 1e6 / (2.0 * N);

        std::vector<const std::string*> byId(names.size());
        for (auto& [id, v] : flat) byId[id] = v;
        auto t3 = Clock::now();
        for (int i = 0; i < N; i++) {
            sink += byId[idA]->size();
            sink += byId[idB]->size();
        }
        double arrayNs = msSince(t3) * 1e6 / (2.0 * N);

        // Weight by the mix the cold pass actually ran at, not 50/50.
        double missRate = (double)coldMisses / coldLookups;
        double nowNs = hitNs * (1.0 - missRate) + missNs * missRate;
        printf("  %-26s at the cold pass's real mix (%.0f%% miss): %.1f ns/lookup\n",
               "", 100.0 * missRate, nowNs);
        printf("  %-26s %6.1f ns  unordered_map (now)      -> %5.1f ms/pass\n", "",
               nowNs, nowNs * coldLookups / 1e6);
        printf("  %-26s %6.1f ns  sorted vector + u16 id   -> %5.1f ms/pass\n", "",
               bsearchNs, bsearchNs * coldLookups / 1e6);
        printf("  %-26s %6.1f ns  flat array, id = index   -> %5.1f ms/pass%s\n", "",
               arrayNs, arrayNs * coldLookups / 1e6, sink == 12345.0 ? "!" : "");
        (void)lookupNs;
    }

    // ---- 11. What the pass's allocations cost ----
    //
    // The cold pass makes ~199,000 heap allocations for 2,828 laid-out nodes —
    // seventy per node. Style lookups are the loud number, but a malloc is not
    // cheap either, and 199,000 of anything deserves to be priced rather than
    // assumed. Time a realistic churn: allocate and free the sizes layout actually
    // allocates (small vectors and strings), in the same alloc/free-immediately
    // pattern, so the figure can be multiplied out against the pass's count.
    {
        const int N = 1000000;
        auto t0 = Clock::now();
        size_t sink = 0;
        for (int i = 0; i < N; i++) {
            // The shape layout allocates in: a handful of pointers (getLayoutChildren
            // returns a vector<LayoutNode*> by value at every one of its 27 call
            // sites) and a short string.
            std::vector<void*> v;
            v.reserve(8);
            sink += v.capacity();
        }
        double vecNs = msSince(t0) * 1e6 / N;
        printf("  %-26s %6.1f ns  one small vector alloc+free%s\n",
               "cost of one allocation", vecNs, sink == 1 ? "!" : "");
        printf("  %-26s at %llu allocs/pass that is %.1f ms of the %s cold pass\n", "",
               (unsigned long long)coldAllocs, vecNs * coldAllocs / 1e6,
               "53ms");
    }

    // ---- 12. Where the pass actually spends its time ----
    //
    // Everything above prices a suspect that was already suspected. Summed, they
    // came to about 16ms of a 53ms pass; the rest had no candidate at all, and no
    // amount of pricing known suspects will produce one. So: sample the pass and
    // let it say. Relayout the whole tree in a loop (markSubtreeDirty makes every
    // pass a cold one) so the profile has thousands of samples to work with rather
    // than the few hundred one 53ms pass would yield.
    {
        g_counting = false; // don't attribute samples to the alloc counter
        bench::Sampler prof(50);
        prof.start();
        auto t0 = Clock::now();
        int passes = 0;
        while (msSince(t0) < 3000.0) {
            layout::markSubtreeDirty(root);
            layout::layoutTree(root, viewportW, metrics);
            passes++;
        }
        prof.stop();
        char title[128];
        snprintf(title, sizeof title, "cold layout self-time (%d passes)", passes);
        prof.report(title, 30);
    }

    printf("\n");
    return 0;
}
