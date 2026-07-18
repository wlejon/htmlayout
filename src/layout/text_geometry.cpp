#include "layout/text_geometry.h"
#include "layout/box.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

namespace htmlayout::layout {

// ---------------------------------------------------------------------------
// Style helpers (private; text runs inherit their parent's font styling).
// ---------------------------------------------------------------------------

namespace {

const std::string& styleProp(const css::ComputedStyle& style, const std::string& name) {
    static const std::string empty;
    auto it = style.find(name);
    return it != style.end() ? it->second : empty;
}

// Parse "12px", "1em" etc. in a layout-geometry context. Selection hit
// testing doesn't need the full cascade; it only needs the font metrics for
// prefix-width measurement. Defaults kept conservative.
struct FontKey {
    std::string family = "sans-serif";
    float       size = 16;
    std::string weight = "400";
};

FontKey resolveFont(LayoutNode* textNode) {
    FontKey fk;
    if (!textNode) return fk;
    // Text nodes inherit their parent's computed style through the adapter.
    const auto& style = textNode->computedStyle();
    const std::string& fam = styleProp(style, "font-family");
    if (!fam.empty()) fk.family = fam;
    const std::string& sz = styleProp(style, "font-size");
    if (!sz.empty()) {
        try { fk.size = std::stof(sz); } catch (...) {}
    }
    const std::string& wt = styleProp(style, "font-weight");
    if (!wt.empty()) fk.weight = wt;
    return fk;
}

// A run's source range and its displayed text are two different strings: the
// source keeps the whitespace the display collapsed away, so they share
// endpoints but not interior offsets. These two convert between them the only
// way the run records enough information for — proportionally — which is exact
// at the endpoints and approximate in between, exactly as before. The change
// in this file is not how source and display map onto each other; it is that
// display offsets now become x through the cluster map instead of through a
// prefix width.
int displayToSrc(const PlacedTextRun& run, int displayByte) {
    const int srcLen = run.srcEnd - run.srcStart;
    if (run.text.empty() || srcLen <= 0) return run.srcStart;
    const double ratio = static_cast<double>(displayByte) /
                         static_cast<double>(run.text.size());
    int srcOff = run.srcStart + static_cast<int>(ratio * srcLen + 0.5);
    return std::max(run.srcStart, std::min(srcOff, run.srcEnd));
}

int srcToDisplay(const PlacedTextRun& run, int srcOffset) {
    const int srcLen = run.srcEnd - run.srcStart;
    if (run.text.empty() || srcLen <= 0) return 0;
    const int clamped = std::max(run.srcStart, std::min(srcOffset, run.srcEnd));
    const double ratio = static_cast<double>(clamped - run.srcStart) /
                         static_cast<double>(srcLen);
    int b = static_cast<int>(ratio * run.text.size() + 0.5);
    return std::max(0, std::min(b, static_cast<int>(run.text.size())));
}

// Source offset corresponding to an x position inside a run.
//
// Asks the metrics consumer which byte `x` names. A shaping consumer answers
// from the cluster map, so the result can never land inside a ligature or a
// combining sequence; the interface default reproduces the prefix-width
// binary search this function used to do inline.
int srcOffsetAtX(const PlacedTextRun& run, float x, TextMetrics& metrics,
                 const FontKey& fk) {
    if (run.srcEnd <= run.srcStart) return run.srcStart;

    const float local = x - run.x;
    if (local <= 0) return run.srcStart;
    if (local >= run.width) return run.srcEnd;
    if (run.text.empty()) return run.srcStart;

    const int displayByte = metrics.offsetAtCaretX(run.text, local,
                                                   fk.family, fk.size, fk.weight);
    return displayToSrc(run, displayByte);
}

// Reverse mapping: the caret positions within a run for a source offset.
//
// Returns the pair rather than a float even though only `primary` is ever
// filled today: a source offset at a direction boundary has two visual x
// positions, and callers that collapse the answer to one float here would all
// have to be rewritten when that becomes possible. Callers that genuinely want
// one number call caretXAtSrcOffset below and say so at the call site.
TextMetrics::CaretXPair caretsAtSrcOffset(const PlacedTextRun& run, int srcOffset,
                                          TextMetrics& metrics, const FontKey& fk) {
    TextMetrics::CaretXPair out;
    out.primary.x = run.x;
    if (run.text.empty() || run.srcEnd == run.srcStart) return out;

    const int displayByte = srcToDisplay(run, srcOffset);
    TextMetrics::CaretXPair c = metrics.caretXAtOffset(run.text, displayByte,
                                                       fk.family, fk.size, fk.weight);
    c.primary.x += run.x;
    c.secondary.x += run.x;
    return c;
}

float xAtSrcOffset(const PlacedTextRun& run, int srcOffset,
                   TextMetrics& metrics, const FontKey& fk) {
    return caretsAtSrcOffset(run, srcOffset, metrics, fk).primary.x;
}

// Walk the layout tree accumulating offsets so each visited node carries the
// absolute origin of its contentRect. Callers get the abs origin via
// `absOrigin` and can treat box.contentRect coordinates as (abs.x + box.x).
// `clip` is the absolute visible region inherited from the nearest scrolling
// / overflow-clipping ancestor; it starts as a giant rect at the root and is
// intersected each time the walk descends into a clipping node.
struct OffsetFrame {
    LayoutNode* node;
    float ox;
    float oy;
    Rect  clip;   // absolute clip rect (bounds inherited from ancestors)
};

// Does this element clip descendants to its padding box? Matches paint-time
// rules: any non-visible value on overflow / overflow-x / overflow-y.
bool clipsOverflow(const css::ComputedStyle& style) {
    auto check = [](const std::string& v) {
        return !v.empty() && v != "visible";
    };
    if (check(styleProp(style, "overflow"))) return true;
    if (check(styleProp(style, "overflow-x"))) return true;
    if (check(styleProp(style, "overflow-y"))) return true;
    return false;
}

Rect intersectRect(const Rect& a, const Rect& b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.width,  b.x + b.width);
    float y2 = std::min(a.y + a.height, b.y + b.height);
    if (x2 <= x1 || y2 <= y1) return {0, 0, 0, 0};
    return {x1, y1, x2 - x1, y2 - y1};
}

// Sentinel "no clipping" rect — large enough to contain any realistic
// layout without needing special-case logic in the intersect path.
Rect infiniteClip() {
    return {-1e7f, -1e7f, 2e7f, 2e7f};
}

void forEachNode(LayoutNode* root,
                 const std::function<void(const OffsetFrame&)>& fn,
                 float ox, float oy, const Rect& clip) {
    if (!root) return;
    fn({root, ox, oy, clip});
    float childOx = ox + root->box.contentRect.x - root->scrollLeftPx();
    float childOy = oy + root->box.contentRect.y - root->scrollTopPx();
    // If this node clips its overflow, child visibility is restricted to its
    // content+padding box (border-box minus border). Children inherit the
    // tighter clip.
    Rect childClip = clip;
    if (clipsOverflow(root->computedStyle())) {
        Rect own;
        own.x = ox + root->box.contentRect.x - root->box.padding.left;
        own.y = oy + root->box.contentRect.y - root->box.padding.top;
        own.width  = root->box.contentRect.width
                   + root->box.padding.left + root->box.padding.right;
        own.height = root->box.contentRect.height
                   + root->box.padding.top + root->box.padding.bottom;
        childClip = intersectRect(clip, own);
    }
    for (auto* c : root->children()) {
        if (!c) continue;
        forEachNode(c, fn, childOx, childOy, childClip);
    }
}

void forEachNode(LayoutNode* root,
                 const std::function<void(const OffsetFrame&)>& fn) {
    forEachNode(root, fn, 0.0f, 0.0f, infiniteClip());
}

// Find the offset frame for `target` by walking the tree. Returns origin
// relative to `root` so the target's contentRect plus (ox,oy) gives absolute
// coords. Returns false if not found.
bool findOrigin(LayoutNode* root, LayoutNode* target, float& ox, float& oy) {
    if (!root) return false;
    std::function<bool(LayoutNode*, float, float)> rec =
        [&](LayoutNode* n, float cx, float cy) -> bool {
        if (!n) return false;
        if (n == target) { ox = cx; oy = cy; return true; }
        float nx = cx + n->box.contentRect.x - n->scrollLeftPx();
        float ny = cy + n->box.contentRect.y - n->scrollTopPx();
        for (auto* c : n->children()) {
            if (!c) continue;
            if (rec(c, nx, ny)) return true;
        }
        return false;
    };
    return rec(root, 0, 0);
}

// Collect text nodes in document order alongside their parent offset. Used by
// hitTestText (for a hit point) and by getSelectionRects (for range walking).
struct TextNodeEntry {
    LayoutNode* node;
    float ox;  // parent-origin x (absolute)
    float oy;
    Rect  clip; // absolute clip rect (scroll containers / overflow:hidden)
};

void collectTextNodes(LayoutNode* root, std::vector<TextNodeEntry>& out) {
    forEachNode(root, [&](const OffsetFrame& f) {
        if (f.node->isTextNode() && !f.node->box.textRuns.empty()) {
            out.push_back({f.node, f.ox, f.oy, f.clip});
        }
    });
}

} // namespace

// ---------------------------------------------------------------------------
// hitTestText
// ---------------------------------------------------------------------------

TextHit hitTestText(LayoutNode* root, float x, float y, TextMetrics& metrics) {
    TextHit result;
    if (!root) return result;

    std::vector<TextNodeEntry> entries;
    collectTextNodes(root, entries);

    // Find a text node whose placed runs contain (x,y). If the hit is outside
    // every run, pick the closest run by vertical+horizontal distance — so
    // click-on-line behaves sensibly even when the pointer lands on padding.
    const PlacedTextRun* bestRun = nullptr;
    const TextNodeEntry* bestEntry = nullptr;
    float bestDist2 = std::numeric_limits<float>::max();

    for (auto& e : entries) {
        for (auto& run : e.node->box.textRuns) {
            float rx = e.ox + run.x;
            float ry = e.oy + run.y;
            float rw = run.width;
            float rh = run.height;
            // Reject runs entirely outside their scroll container's visible
            // region — otherwise a drag over empty viewport area can snap to
            // text scrolled far off-screen.
            Rect runRect{rx, ry, rw, rh};
            Rect visible = intersectRect(runRect, e.clip);
            if (visible.width <= 0 || visible.height <= 0) continue;
            if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
                // Exact hit — pick this run.
                PlacedTextRun shifted = run;
                shifted.x = rx;
                shifted.y = ry;
                FontKey fk = resolveFont(e.node);
                result.node = e.node;
                result.srcOffset = srcOffsetAtX(shifted, x, metrics, fk);
                return result;
            }
            // Distance to closest point inside the rect.
            float dx = (x < rx) ? (rx - x) : (x > rx + rw ? (x - (rx + rw)) : 0);
            float dy = (y < ry) ? (ry - y) : (y > ry + rh ? (y - (ry + rh)) : 0);
            float d2 = dx*dx + dy*dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestRun = &run;
                bestEntry = &e;
            }
        }
    }

    if (bestRun && bestEntry) {
        PlacedTextRun shifted = *bestRun;
        shifted.x = bestEntry->ox + bestRun->x;
        shifted.y = bestEntry->oy + bestRun->y;
        FontKey fk = resolveFont(bestEntry->node);
        result.node = bestEntry->node;
        result.srcOffset = srcOffsetAtX(shifted, x, metrics, fk);
    }
    return result;
}

// ---------------------------------------------------------------------------
// getCaretRect
// ---------------------------------------------------------------------------

bool getCaretRect(LayoutNode* root, LayoutNode* textNode, int srcOffset,
                  TextMetrics& metrics, float& x, float& y, float& height) {
    if (!textNode || textNode->box.textRuns.empty()) return false;
    float ox = 0, oy = 0;
    if (!findOrigin(root, textNode, ox, oy)) {
        ox = 0; oy = 0;
    }

    const auto& runs = textNode->box.textRuns;
    // Pick the run containing srcOffset. If srcOffset lies between runs (e.g.
    // at a wrap boundary), prefer the preceding run so the caret stays at the
    // end of the previous line rather than teleporting to the next line.
    const PlacedTextRun* chosen = &runs.front();
    for (auto& run : runs) {
        if (srcOffset <= run.srcEnd) {
            chosen = &run;
            if (srcOffset >= run.srcStart) break;
            // srcOffset < run.srcStart → before this run; keep previous.
            break;
        }
        chosen = &run;
    }
    if (srcOffset < chosen->srcStart) srcOffset = chosen->srcStart;
    if (srcOffset > chosen->srcEnd)   srcOffset = chosen->srcEnd;

    FontKey fk = resolveFont(textNode);
    PlacedTextRun shifted = *chosen;
    shifted.x = ox + chosen->x;
    shifted.y = oy + chosen->y;
    x = xAtSrcOffset(shifted, srcOffset, metrics, fk);
    y = shifted.y;
    height = chosen->height;
    return true;
}

// ---------------------------------------------------------------------------
// getSelectionRects
// ---------------------------------------------------------------------------

std::vector<Rect> getSelectionRects(LayoutNode* root,
                                    LayoutNode* startNode, int startOff,
                                    LayoutNode* endNode, int endOff,
                                    TextMetrics& metrics) {
    std::vector<Rect> out;
    if (!root || !startNode || !endNode) return out;

    // Walk text nodes in tree order; determine whether each is before start,
    // in range, or after end by comparing against the two anchor nodes.
    std::vector<TextNodeEntry> entries;
    collectTextNodes(root, entries);

    // Walk entries in tree order. `inside` turns on at startNode and off
    // after endNode. When startNode == endNode, both flags are true on the
    // same iteration and the inner loop clips by both startOff and endOff —
    // do NOT pre-seed `inside` in that case, otherwise every preceding text
    // node would be treated as in-range and emit full-run highlight rects.
    bool inside = false;
    for (auto& e : entries) {
        bool isStart = (e.node == startNode);
        bool isEnd   = (e.node == endNode);

        if (!inside && !isStart) continue;
        if (isStart) inside = true;

        FontKey fk = resolveFont(e.node);
        for (auto& run : e.node->box.textRuns) {
            float rx = e.ox + run.x;
            float ry = e.oy + run.y;
            int runStart = run.srcStart;
            int runEnd   = run.srcEnd;

            // Clip by startOff if we're in the starting text node.
            int clippedStart = runStart;
            int clippedEnd   = runEnd;
            if (isStart && runEnd <= startOff) continue;
            if (isStart && runStart < startOff) clippedStart = startOff;
            if (isEnd && runStart >= endOff) {
                inside = false;
                break;
            }
            if (isEnd && runEnd > endOff) clippedEnd = endOff;
            if (clippedEnd <= clippedStart) continue;

            PlacedTextRun shifted = run;
            shifted.x = rx;
            shifted.y = ry;
            float x1 = xAtSrcOffset(shifted, clippedStart, metrics, fk);
            float x2 = xAtSrcOffset(shifted, clippedEnd, metrics, fk);
            if (x2 < x1) std::swap(x1, x2);
            Rect r;
            r.x = x1;
            r.y = ry;
            r.width = std::max(0.0f, x2 - x1);
            r.height = run.height;
            // Clip to the nearest scroll/overflow-hidden ancestor so text that
            // is scrolled out of view (or lies outside a clipped container)
            // doesn't bleed over neighboring UI.
            Rect clipped = intersectRect(r, e.clip);
            if (clipped.width <= 0 || clipped.height <= 0) continue;
            out.push_back(clipped);
        }

        if (isEnd) { inside = false; break; }
    }

    return out;
}

} // namespace htmlayout::layout
