#include "layout/style_cache.h"
#include "css/properties.h"
#include "layout/style_util.h"
#include <unordered_map>

namespace htmlayout::layout {

namespace {

// name -> Prop. Consulted only when building a node's cache — once per property
// the node's style actually holds, per pass — never when reading one.
const std::unordered_map<std::string_view, Prop>& propIndex() {
    static const std::unordered_map<std::string_view, Prop> m = [] {
        std::unordered_map<std::string_view, Prop> t;
        t.reserve(kPropCount * 2);
        for (size_t i = 0; i < kPropCount; i++) t.emplace(kPropNames[i], Prop(i));
        return t;
    }();
    return m;
}

// Prop -> its initial value, resolved once. These strings outlive every node, so
// a slot for a property the cascade did not set can point straight at one and a
// miss becomes as cheap as a hit.
const std::array<std::string, kPropCount>& initialValues() {
    static const std::array<std::string, kPropCount> v = [] {
        std::array<std::string, kPropCount> t;
        for (size_t i = 0; i < kPropCount; i++)
            t[i] = css::initialValueRef(kPropNames[i]);
        return t;
    }();
    return v;
}

// Single-threaded, one tree at a time — the same reasoning as g_layoutPass, which
// this is read alongside. See styleVal() in the header for why a cache is trusted
// only inside the pass that built it.
bool g_passActive = false;

} // namespace

bool styleCachePassActive() { return g_passActive; }
void beginStyleCachePass() { g_passActive = true; }
void endStyleCachePass() { g_passActive = false; }

const std::string& styleValLive(const LayoutNode* node, Prop p) {
    return styleVal(node->computedStyle(), kPropNames[size_t(p)]);
}

void buildStyleCache(const LayoutNode* node) {
    if (!g_passActive) return;  // nothing outside a pass may trust a cache anyway

    auto& cache = node->styleCache;
    if (!cache) cache = std::make_unique<NodeStyleCache>();

    // Every slot starts at its property's initial value, so a property the cascade
    // never set costs a read exactly what a set one does. That is what removes the
    // miss path — and 72% of layout's reads were misses, each paying a failed probe
    // and then a second hash lookup in the property registry to find this value.
    const auto& initial = initialValues();
    for (size_t i = 0; i < kPropCount; i++) cache->slot[i] = &initial[i];

    // Then point the ones it did set at the live value. This is the only hashing
    // this node's style costs for the entire pass: once per property it actually
    // holds (a mean of 17), against the ~250 reads layout is about to make of it.
    const auto& cs = node->computedStyle();
    const auto& index = propIndex();
    for (const auto& [name, value] : cs) {
        auto it = index.find(std::string_view(name));
        if (it != index.end()) cache->slot[size_t(it->second)] = &value;
    }

    node->styleCachePass = currentLayoutPass();
}

} // namespace htmlayout::layout
