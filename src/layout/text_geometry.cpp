#include "layout/text_geometry.h"
#include "layout/box.h"
#include <algorithm>
#include <functional>

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

// Approximate source offset corresponding to an x position inside a run.
// Binary-searches prefix widths in the post-collapse `text`, then scales the
// resulting character index linearly back onto the run's source range.
int srcOffsetAtX(const PlacedTextRun& run, float x, TextMetrics& metrics,
                 const FontKey& fk) {
    int srcLen = run.srcEnd - run.srcStart;
    if (srcLen <= 0) return run.srcStart;

    float local = x - run.x;
    if (local <= 0) return run.srcStart;
    if (local >= run.width) return run.srcEnd;

    if (run.text.empty()) return run.srcStart;

    // Binary search for the display-char boundary closest to `local`.
    // Prefix widths are monotonic so the midpoint comparison works.
    int lo = 0, hi = static_cast<int>(run.text.size());
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        float w = metrics.measureWidth(run.text.substr(0, mid),
                                       fk.family, fk.size, fk.weight);
        if (w <= local) lo = mid;
        else hi = mid - 1;
    }
    // Pick nearest boundary (prev vs. next).
    auto widthAt = [&](int n) {
        return n <= 0 ? 0.0f
             : metrics.measureWidth(run.text.substr(0, n),
                                    fk.family, fk.size, fk.weight);
    };
    float wLo = widthAt(lo);
    float wHi = widthAt(std::min(lo + 1, static_cast<int>(run.text.size())));
    int displayOff = (std::abs(local - wHi) < std::abs(local - wLo))
        ? std::min(lo + 1, static_cast<int>(run.text.size()))
        : lo;

    // Scale back to source range. Whitespace-collapse means displayed length
    // may be less than source length; linear scaling picks a reasonable
    // nearby byte offset.
    if (run.text.empty()) return run.srcStart;
    double ratio = static_cast<double>(displayOff) /
                   static_cast<double>(run.text.size());
    int srcOff = run.srcStart +
                 static_cast<int>(ratio * srcLen + 0.5);
    if (srcOff < run.srcStart) srcOff = run.srcStart;
    if (srcOff > run.srcEnd)   srcOff = run.srcEnd;
    return srcOff;
}

// Reverse mapping: x within a run for a given source offset.
float xAtSrcOffset(const PlacedTextRun& run, int srcOffset,
                   TextMetrics& metrics, const FontKey& fk) {
    if (run.text.empty() || run.srcEnd == run.srcStart) return run.x;
    int srcLen = run.srcEnd - run.srcStart;
    int clamped = std::max(run.srcStart, std::min(srcOffset, run.srcEnd));
    double ratio = static_cast<double>(clamped - run.srcStart) /
                   static_cast<double>(srcLen);
    int prefix = static_cast<int>(ratio * run.text.size() + 0.5);
    prefix = std::max(0, std::min(prefix, static_cast<int>(run.text.size())));
    float w = prefix == 0 ? 0.0f
            : metrics.measureWidth(run.text.substr(0, prefix),
                                   fk.family, fk.size, fk.weight);
    return run.x + w;
}

// Walk the layout tree accumulating offsets so each visited node carries the
// absolute origin of its contentRect. Callers get the abs origin via
// `absOrigin` and can treat box.contentRect coordinates as (abs.x + box.x).
struct OffsetFrame {
    LayoutNode* node;
    float ox;
    float oy;
};

void forEachNode(LayoutNode* root,
                 const std::function<void(const OffsetFrame&)>& fn,
                 float ox = 0, float oy = 0) {
    if (!root) return;
    fn({root, ox, oy});
    float childOx = ox + root->box.contentRect.x - root->scrollLeftPx();
    float childOy = oy + root->box.contentRect.y - root->scrollTopPx();
    for (auto* c : root->children()) {
        if (!c) continue;
        forEachNode(c, fn, childOx, childOy);
    }
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
};

void collectTextNodes(LayoutNode* root, std::vector<TextNodeEntry>& out) {
    forEachNode(root, [&](const OffsetFrame& f) {
        if (f.node->isTextNode() && !f.node->box.textRuns.empty()) {
            out.push_back({f.node, f.ox, f.oy});
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

    // Single text node case: easy path.
    bool sameNode = (startNode == endNode);

    bool inside = sameNode;
    for (auto& e : entries) {
        bool isStart = (e.node == startNode);
        bool isEnd   = (e.node == endNode);

        if (!inside && !isStart) continue;
        if (!inside && isStart) inside = true;

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
            out.push_back(r);
        }

        if (isEnd) { inside = false; break; }
    }

    return out;
}

} // namespace htmlayout::layout
