#pragma once
#include "css/cascade.h"
#include "layout/box.h"
#include "layout/style_props.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace htmlayout::layout {

// A node's style, found by array index instead of by hashing its name.
//
// Sampling a cold layout pass puts ~46% of it inside styleVal, the hash of the
// property name, and the second lookup a miss pays to fetch the initial value —
// six times what the pass spends laying anything out. None of that is the map
// being slow at what it does; it is the wrong shape for the question. The styles
// are small (mean 17 properties), layout asks each of them for all 96 properties
// it knows about, so 72% of the asks miss — and every ask walks a bucket array, a
// node, a key string and a value string, four dependent cache misses on a map the
// pass has not touched since the last node.
//
// So ask once. On a node's first visit in a pass, project its ComputedStyle into
// a flat array indexed by Prop; every read after that is a single load. Layout
// visits a node ~2.75 times per pass and re-reads the same properties each time,
// and a node's style cannot change while a synchronous pass is running.
//
// That last clause is also the entire invalidation story, and the reason the cache
// is scoped to one pass instead of kept across them. A consumer may rewrite a
// node's ComputedStyle and NOT mark it dirty: bro does exactly that for a :hover
// that only repaints, and its paint-only list includes `transform`, `opacity` and
// `isolation` — all of which layout and hit testing read. A cache outliving the
// pass would hand hit testing a stale transform in the middle of an animation.
// Keyed to the pass, there is no contract left to get wrong; outside a pass reads
// go to the live map, and hit testing (50us, 0.3 percent of a frame) can afford it.
//
// The slots point straight into the consumer's ComputedStyle. Between passes those
// pointers may well dangle — setComputedStyle() move-assigns a fresh map and frees
// the old value strings, with no dirty mark — but nothing ever reads them: the
// guard below fails on both counts and the next pass rebuilds before any read.
// Copying the values instead, to be "safe", costs a string copy and a destruction
// per property per node per pass and buys nothing the pass scope has not already
// bought.

// Project this node's style into its cache. Called by beginLayoutNode() — and only
// there — because a node being laid out is the one that will be read enough times
// to earn the projection back. See the comment at that call site.
void buildStyleCache(const LayoutNode* node);

// The read that misses the cache: a plain lookup in the node's live ComputedStyle.
//
// Deliberately does NOT build a cache, which is the whole design in one line. Only
// a node that is being laid out gets projected, because only it will be read the
// couple of hundred times that earns the projection back. Every other reader — the
// hit-bounds walk asking all 4,848 nodes for `overflow`, a flex container reading
// `flex-grow` off children it has not laid out yet, a text node whose box belongs
// to its parent — reads a handful of properties and comes through here.
//
// Both alternatives were measured and are worse. Building on first read instead
// made a single-leaf reflow 46% slower; building on first read everywhere except
// the hit-bounds walk still made it 35% slower, and bought 1.7 points of cold
// layout for it. A cold pass lays out every node, so it gets the caches anyway.
const std::string& styleValLive(const LayoutNode* node, Prop p);

// True only while layoutTree() is running.
bool styleCachePassActive();

inline const std::string& styleVal(const LayoutNode* node, Prop p) {
    if (styleCachePassActive() && node->styleCachePass == currentLayoutPass())
        return *node->styleCache->slot[size_t(p)];
    return styleValLive(node, p);
}

// The window in which caches are valid. Called by layoutTree().
void beginStyleCachePass();
void endStyleCachePass();


} // namespace htmlayout::layout
