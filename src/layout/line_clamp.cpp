#include "layout/line_clamp.h"
#include "layout/block.h"
#include "layout/formatting_context.h"
#include "layout/style_cache.h"
#include "layout/style_util.h"
#include "layout/text.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <vector>

namespace htmlayout::layout {

namespace {

// A positive integer token, or 0.
int parseClampCount(const std::string& tok) {
    if (tok.empty()) return 0;
    for (char c : tok)
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    long v = std::strtol(tok.c_str(), nullptr, 10);
    if (v < 1) return 0;
    return v > 1000000 ? 1000000 : static_cast<int>(v);
}

// block-ellipsis: no-ellipsis | auto | <string>. Returns false for an
// unparseable value.
bool parseBlockEllipsis(const std::string& v, LineClampSpec& spec) {
    if (v == "auto") {
        spec.ellipsis = true;
        return true;
    }
    if (v == "no-ellipsis" || v.empty()) {
        spec.ellipsis = false;
        return true;
    }
    if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) {
        spec.ellipsis = true;
        spec.ellipsisText = v.substr(1, v.size() - 2);
        return true;
    }
    return false;
}

LineClampSpec resolveLineClamp(LayoutNode* node) {
    // The line-clamp longhands: max-lines counts, and only takes effect when
    // `continue` discards the rest (collapse; discard, which needs
    // fragmentation, is treated the same; -webkit-legacy is the shorthand's
    // compatibility flag).
    const std::string& cont = styleVal(node, Prop::Continue);
    if (cont == "collapse" || cont == "discard" || cont == "-webkit-legacy") {
        LineClampSpec spec;
        spec.maxLines = parseClampCount(styleVal(node, Prop::MaxLines));
        if (spec.maxLines > 0 && parseBlockEllipsis(styleVal(node, Prop::BlockEllipsis), spec))
            return spec;
    }
    // The legacy property only takes effect on a vertical -webkit-box, which
    // browsers lay out as a block container.
    const std::string& wlc = styleVal(node, Prop::WebkitLineClamp);
    if (wlc.empty() || wlc == "none") return {};
    // The authored display: layout itself sees -webkit-inline-box as
    // inline-block (style_cache.cpp).
    const std::string& disp = styleVal(node->computedStyle(), "display");
    if (disp != "-webkit-box" && disp != "-webkit-inline-box") return {};
    if (styleVal(node, Prop::WebkitBoxOrient) != "vertical") return {};
    LineClampSpec spec;
    spec.maxLines = parseClampCount(wlc);
    return spec;
}

void markDescendantsDirty(LayoutNode* n) {
    auto mark = [](LayoutNode* c) {
        c->box.dirty = true;
        markDescendantsDirty(c);
    };
    for (auto* c : n->children()) mark(c);
    if (auto* p = n->pseudoBefore()) mark(p);
    if (auto* p = n->pseudoAfter()) mark(p);
}

void clearClampFlags(LayoutNode* n) {
    for (auto* c : getLayoutChildren(n)) {
        c->box.clampHidden = false;
        if (!c->isTextNode()) clearClampFlags(c);
    }
}

// Block-level children whose lines count toward the container's: in-flow
// blocks in the same block formatting context.
bool countsLinesOf(LayoutNode* c) {
    if (c->isTextNode()) return false;
    const std::string& d = styleVal(c, Prop::Display);
    if (d != "block" && d != "list-item") return false;
    return !nodeEstablishesBFC(c);
}

struct ClampLine {
    float top, bottom, left, right;
};

void collectLines(LayoutNode* n, float ox, float oy, std::vector<ClampLine>& out) {
    for (const auto& lb : n->box.lineBoxes)
        out.push_back({oy + lb.top, oy + lb.top + lb.height,
                       ox + lb.left, ox + lb.left + lb.width});
    for (auto* c : getLayoutChildren(n))
        if (countsLinesOf(c))
            collectLines(c, ox + c->box.contentRect.x, oy + c->box.contentRect.y, out);
}

// A text node with nothing left to show: one empty run (see line_clamp.h).
void blankText(LayoutNode* t) {
    PlacedTextRun empty;
    empty.srcStart = empty.srcEnd = t->box.textRuns.empty()
        ? 0 : t->box.textRuns.front().srcStart;
    empty.x = t->box.contentRect.x;
    empty.y = t->box.contentRect.y;
    t->box.textRuns.clear();
    t->box.textRuns.push_back(std::move(empty));
    t->box.contentRect.width = 0;
    t->box.contentRect.height = 0;
    t->box.clampHidden = true;
}

// The text node's rect is the union of its placed runs.
void refreshTextRect(LayoutNode* t) {
    auto& runs = t->box.textRuns;
    if (runs.empty()) return;
    float l = runs[0].x, tp = runs[0].y;
    float r = l + runs[0].width, b = tp + runs[0].height;
    for (const auto& run : runs) {
        l = std::min(l, run.x);
        tp = std::min(tp, run.y);
        r = std::max(r, run.x + run.width);
        b = std::max(b, run.y + run.height);
    }
    t->box.contentRect = {l, tp, r - l, b - tp};
}

void hideSubtree(LayoutNode* n) {
    n->box.clampHidden = true;
    for (auto* c : getLayoutChildren(n)) {
        if (c->isTextNode()) { blankText(c); continue; }
        hideSubtree(c);
        c->box.clampHidden = false; // the flag on `n` covers the subtree
    }
}

struct RunRef {
    LayoutNode* text;
    size_t index;
    LayoutNode* fontNode;   // the box whose font the run was shaped in
    float ox;               // the run's coordinate origin in container space
};

struct ElemRef {
    LayoutNode* node;
    float left;             // margin-box left, container space
};

struct ClampWalk {
    float cutY = 0;         // bottom of the last kept line
    float lineTop = 0;      // top of the last kept line
    std::vector<RunRef> lastLineRuns;
    std::vector<ElemRef> lastLineElems;
    float lastLineElemRight = -std::numeric_limits<float>::infinity();
};

void walk(LayoutNode* n, float ox, float oy, ClampWalk& w) {
    for (auto* c : getLayoutChildren(n)) {
        c->box.clampHidden = false;
        if (c->isTextNode()) {
            auto& runs = c->box.textRuns;
            if (runs.empty()) continue;
            std::vector<PlacedTextRun> kept;
            kept.reserve(runs.size());
            std::vector<size_t> onLastLine;
            for (auto& r : runs) {
                float mid = oy + r.y + r.height * 0.5f;
                if (mid >= w.cutY) continue;
                if (mid >= w.lineTop) onLastLine.push_back(kept.size());
                kept.push_back(std::move(r));
            }
            if (kept.empty()) { blankText(c); continue; }
            bool cut = kept.size() != runs.size();
            runs = std::move(kept);
            if (cut) refreshTextRect(c);
            for (size_t k : onLastLine) w.lastLineRuns.push_back({c, k, n, ox});
            continue;
        }
        const std::string& d = styleVal(c, Prop::Display);
        if (d == "none") continue;
        const std::string& pos = styleVal(c, Prop::Position);
        if (pos == "absolute" || pos == "fixed") continue; // out of flow

        const LayoutBox& b = c->box;
        float cx = ox + b.contentRect.x;
        float cy = oy + b.contentRect.y;
        float borderTop = cy - b.padding.top - b.border.top;

        bool inlineLevel = d == "inline" || d == "inline-block" ||
                           d == "inline-flex" || d == "inline-grid" ||
                           d == "inline-table";
        if (!inlineLevel) {
            // Block-level: gone once it starts past the clamp point;
            // otherwise its lines were counted and its content is cut inside.
            if (borderTop >= w.cutY) { hideSubtree(c); continue; }
            if (countsLinesOf(c)) walk(c, cx, cy, w);
            continue;
        }

        float iw = 0, ih = 0;
        bool atomic = d != "inline" || c->intrinsicSize(iw, ih, b.contentRect.width);
        float top = atomic ? borderTop - b.margin.top : cy;
        float height = atomic ? b.fullHeight() + b.margin.top + b.margin.bottom
                              : b.contentRect.height;
        float mid = top + height * 0.5f;
        if (mid >= w.cutY) { hideSubtree(c); continue; }
        if (!atomic) {
            // A non-replaced inline: its box is its first line's font strip;
            // later lines of its content are cut run by run.
            walk(c, cx, cy, w);
            continue;
        }
        if (mid >= w.lineTop) {
            float left = cx - b.padding.left - b.border.left - b.margin.left;
            w.lastLineElems.push_back({c, left});
            w.lastLineElemRight = std::max(w.lastLineElemRight,
                left + b.fullWidth() + b.margin.left + b.margin.right);
        }
    }
}

// UTF-8: drop the last code point.
void popCodePoint(std::string& s) {
    if (s.empty()) return;
    size_t k = s.size() - 1;
    while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    s.resize(k);
}

// Put the ellipsis at the end of the last kept line: after its last text run,
// trimming that run (and, when nothing of it fits, dropping it for the one
// before) until the ellipsis fits inside the line's available width.
void placeEllipsis(ClampWalk& w, const ClampLine& line, const LineClampSpec& spec,
                   TextMetrics& metrics) {
    auto& refs = w.lastLineRuns;
    if (refs.empty()) return;
    auto runOf = [](const RunRef& r) -> PlacedTextRun& {
        return r.text->box.textRuns[r.index];
    };
    std::stable_sort(refs.begin(), refs.end(), [&](const RunRef& a, const RunRef& b) {
        return a.ox + runOf(a).x < b.ox + runOf(b).x;
    });
    {
        const PlacedTextRun& last = runOf(refs.back());
        // The line ends in an atomic inline, which has no run to carry the
        // ellipsis; leave the line as it is.
        if (w.lastLineElemRight > refs.back().ox + last.x + last.width + kFitSlack)
            return;
    }

    float ellipsisStart = std::numeric_limits<float>::infinity();
    for (size_t i = refs.size(); i-- > 0;) {
        const RunRef& ref = refs[i];
        PlacedTextRun& run = runOf(ref);
        LayoutNode* fn = ref.fontNode;
        float fs = resolveLength(styleVal(fn, Prop::FontSize), 16.0f, 16.0f);
        if (fs <= 0.0f) fs = 16.0f;
        const std::string& fam = styleVal(fn, Prop::FontFamily);
        const std::string& wt = styleVal(fn, Prop::FontWeight);
        float ew = metrics.measureWidth(spec.ellipsisText, fam, fs, wt);
        float x0 = ref.ox + run.x;

        std::string t = run.text;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        float tw = t.size() == run.text.size() ? run.width
                 : (t.empty() ? 0.0f : metrics.measureWidth(t, fam, fs, wt));
        while (!t.empty() && x0 + tw + ew > line.right + kFitSlack) {
            popCodePoint(t);
            while (!t.empty() && t.back() == ' ') t.pop_back();
            tw = t.empty() ? 0.0f : metrics.measureWidth(t, fam, fs, wt);
        }
        if (t.empty() && i > 0) {
            // Nothing of this run survives: it goes, and the ellipsis follows
            // whatever precedes it on the line.
            run.text.clear();
            run.width = 0;
            run.srcEnd = run.srcStart;
            refreshTextRect(ref.text);
            continue;
        }
        if (run.srcEnd - run.srcStart == static_cast<int>(run.text.size()))
            run.srcEnd = run.srcStart + static_cast<int>(t.size());
        run.text = t + spec.ellipsisText;
        run.width = tw + ew;
        refreshTextRect(ref.text);
        ellipsisStart = x0 + tw;
        break;
    }
    // Atomic inlines the trimming went back past are cut with the text.
    for (const ElemRef& e : w.lastLineElems)
        if (e.left >= ellipsisStart) hideSubtree(e.node);
}

} // namespace

bool lineClampApplies(LayoutNode* node) {
    return node->box.textTruncated || resolveLineClamp(node).maxLines > 0;
}

bool beginLineClamp(LayoutNode* node, LineClampSpec& spec) {
    spec = resolveLineClamp(node);
    bool wasClamped = node->box.textTruncated;
    node->box.textTruncated = false;
    node->box.lineBoxes.clear();
    bool active = spec.maxLines > 0;
    if (active || wasClamped) markDescendantsDirty(node);
    // A container that stopped clamping still has to clear the flags it left.
    if (!active && wasClamped) spec.maxLines = -1;
    return active || wasClamped;
}

float applyLineClamp(LayoutNode* node, const LineClampSpec& spec,
                     float contentHeight, TextMetrics& metrics) {
    if (spec.maxLines <= 0) {
        clearClampFlags(node);
        return contentHeight;
    }
    std::vector<ClampLine> lines;
    collectLines(node, 0.0f, 0.0f, lines);
    std::stable_sort(lines.begin(), lines.end(),
                     [](const ClampLine& a, const ClampLine& b) { return a.top < b.top; });
    if (lines.size() <= static_cast<size_t>(spec.maxLines)) {
        clearClampFlags(node);
        return contentHeight;
    }
    const ClampLine& last = lines[static_cast<size_t>(spec.maxLines) - 1];
    ClampWalk w;
    w.cutY = last.bottom;
    w.lineTop = last.top;
    walk(node, 0.0f, 0.0f, w);
    if (spec.ellipsis && !spec.ellipsisText.empty())
        placeEllipsis(w, last, spec, metrics);
    node->box.textTruncated = true;
    return last.bottom;
}

} // namespace htmlayout::layout
