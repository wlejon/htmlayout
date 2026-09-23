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
    LayoutNode* owner;      // the block container whose line box this is
};

void collectLines(LayoutNode* n, float ox, float oy, std::vector<ClampLine>& out) {
    for (const auto& lb : n->box.lineBoxes)
        out.push_back({oy + lb.top, oy + lb.top + lb.height,
                       ox + lb.left, ox + lb.left + lb.width, n});
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
    LayoutNode* text = nullptr;
    size_t index = 0;
    LayoutNode* fontNode = nullptr;  // the box whose font the run was shaped in
    float ox = 0, oy = 0;            // the run's coordinate origin in container space
};

struct ElemRef {
    LayoutNode* node;
    float left, right;      // margin box, container space
};

struct ClampWalk {
    LayoutNode* container = nullptr;  // the clamping block
    float cutY = 0;         // bottom of the last kept line
    float lineTop = 0;      // top of the last kept line
    std::vector<RunRef> lastLineRuns;
    std::vector<ElemRef> lastLineElems;
    RunRef lastKeptText;    // any kept run, for a last line with no text of its own
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
            for (size_t k : onLastLine) w.lastLineRuns.push_back({c, k, n, ox, oy});
            w.lastKeptText = {c, runs.size() - 1, n, ox, oy};
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
            w.lastLineElems.push_back(
                {c, left, left + b.fullWidth() + b.margin.left + b.margin.right});
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

// UTF-8: drop the first code point.
void popFrontCodePoint(std::string& s) {
    size_t k = 1;
    while (k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) ++k;
    s.erase(0, std::min(k, s.size()));
}

struct Font {
    const std::string* family;
    const std::string* weight;
    float size;
};

Font fontOf(LayoutNode* n) {
    float fs = resolveLength(styleVal(n, Prop::FontSize), 16.0f, 16.0f);
    if (fs <= 0.0f) fs = 16.0f;
    return {&styleVal(n, Prop::FontFamily), &styleVal(n, Prop::FontWeight), fs};
}

float measure(TextMetrics& m, const std::string& s, const Font& f) {
    return s.empty() ? 0.0f : m.measureWidth(s, *f.family, f.size, *f.weight);
}

// Put the ellipsis at the inline-end of the last kept line (css-overflow-4
// block-ellipsis), truncating the line the way Chromium's line truncator does:
// items are visited in visual order from the line's end edge (right in an
// ltr block, left in an rtl one); the first that leaves room for the ellipsis
// before that edge keeps it right after itself, and the ones visited before
// it are removed. A text run is first cut short from its end-edge side; an
// atomic inline (inline-block, image, ...) is kept or removed whole. The
// ellipsis is drawn by a text run: appended to the cut run when that run
// runs in the line's direction (so it is its logical end), otherwise a run of
// its own in a text node on the line (or, for a line with no text, in the
// last text node kept before it).
void placeEllipsis(ClampWalk& w, const ClampLine& line, const LineClampSpec& spec,
                   TextMetrics& metrics) {
    const bool rtl = styleVal(line.owner, Prop::Direction) == "rtl";
    auto runOf = [](const RunRef& r) -> PlacedTextRun& {
        return r.text->box.textRuns[r.index];
    };

    struct Item {
        float l, r;
        int run = -1, elem = -1;
    };
    std::vector<Item> items;
    const RunRef* lineCarrier = nullptr;  // a text run on this line
    for (size_t i = 0; i < w.lastLineRuns.size(); i++) {
        const RunRef& ref = w.lastLineRuns[i];
        const PlacedTextRun& run = runOf(ref);
        if (run.text.empty()) continue;  // e.g. a line-end space trimmed away
        if (!lineCarrier) lineCarrier = &ref;
        float l = ref.ox + run.x;
        items.push_back({l, l + run.width, static_cast<int>(i), -1});
    }
    for (size_t i = 0; i < w.lastLineElems.size(); i++) {
        const ElemRef& e = w.lastLineElems[i];
        items.push_back({e.left, e.right, -1, static_cast<int>(i)});
    }
    // End edge first.
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        return rtl ? a.l < b.l : a.r > b.r;
    });

    // The run that will carry a stand-alone ellipsis, and its geometry.
    RunRef carrier;
    float carrierY = 0, carrierH = 0;  // container space
    if (lineCarrier) {
        carrier = *lineCarrier;
        const PlacedTextRun& run = runOf(carrier);
        carrierY = carrier.oy + run.y;
        carrierH = run.height;
    } else if (w.lastKeptText.text) {
        carrier = w.lastKeptText;
        Font f = fontOf(carrier.fontNode);
        carrierH = metrics.lineHeight(*f.family, f.size, *f.weight);
        if (carrierH <= 0.0f) carrierH = f.size * 1.2f;
        carrierY = line.bottom - carrierH;
    }
    const float limit = rtl ? line.left : line.right;
    auto fitsAt = [&](float edge, float ew) {
        return rtl ? edge - ew >= limit - kFitSlack : edge + ew <= limit + kFitSlack;
    };
    // A stand-alone ellipsis run whose end-edge-facing side is at `edge`.
    // The font the ellipsis is drawn in: the carrying run's, or with no text
    // anywhere to carry it, that of the block whose line it ends.
    auto ellipsisFont = [&]() {
        return fontOf(carrier.text ? carrier.fontNode : line.owner);
    };
    auto standAlone = [&](float edge, int srcAt) {
        Font f = ellipsisFont();
        float ew = measure(metrics, spec.ellipsisText, f);
        if (!carrier.text) {
            // No text node to draw it with: the clamp container holds it as
            // its own run, in its content coordinates (see line_clamp.h).
            float h = metrics.lineHeight(*f.family, f.size, *f.weight);
            if (h <= 0.0f) h = f.size * 1.2f;
            PlacedTextRun e;
            e.srcStart = e.srcEnd = kContainerEllipsisSrc;
            e.text = spec.ellipsisText;
            e.width = ew;
            e.x = rtl ? edge - ew : edge;
            e.y = line.bottom - h;
            e.height = h;
            w.container->box.textRuns.push_back(std::move(e));
            return;
        }
        PlacedTextRun e;
        e.srcStart = e.srcEnd = srcAt;
        e.text = spec.ellipsisText;
        e.width = ew;
        e.x = (rtl ? edge - ew : edge) - carrier.ox;
        e.y = carrierY - carrier.oy;
        e.height = carrierH;
        carrier.text->box.textRuns.push_back(std::move(e));
        refreshTextRect(carrier.text);
    };
    auto carrierSrc = [&]() {
        if (!carrier.text) return 0;
        const PlacedTextRun& run = runOf(carrier);
        return run.srcEnd;
    };

    for (size_t i = 0; i < items.size(); i++) {
        const Item& it = items[i];
        const bool lastChance = i + 1 == items.size();
        if (it.elem >= 0) {
            float ew = measure(metrics, spec.ellipsisText, ellipsisFont());
            float edge = rtl ? it.l : it.r;
            if (fitsAt(edge, ew)) {
                standAlone(edge, carrierSrc());
                return;
            }
            hideSubtree(w.lastLineElems[static_cast<size_t>(it.elem)].node);
            continue;
        }

        const RunRef& ref = w.lastLineRuns[static_cast<size_t>(it.run)];
        PlacedTextRun& run = runOf(ref);
        Font f = fontOf(ref.fontNode);
        const float ew = measure(metrics, spec.ellipsisText, f);
        // The run's own direction: a run against the line's (an English word
        // at the end of an Arabic line) is cut from its logical start, since
        // that is the side facing the line's end edge.
        bool runRtl = rtl;
        if (metrics.bidiAware()) {
            std::vector<uint8_t> levels;
            metrics.bidiLevels(run.text, rtl, levels);
            if (!levels.empty()) runRtl = (levels.front() & 1) != 0;
        }
        const bool keepPrefix = runRtl == rtl;
        const float L = ref.ox + run.x, R = L + run.width;

        std::string t = run.text;
        auto trimSpaces = [&]() {
            if (keepPrefix) {
                while (!t.empty() && t.back() == ' ') t.pop_back();
            } else {
                size_t k = t.find_first_not_of(' ');
                t.erase(0, k == std::string::npos ? t.size() : k);
            }
        };
        trimSpaces();
        float tw = t.size() == run.text.size() ? run.width : measure(metrics, t, f);
        while (!t.empty() && !fitsAt(rtl ? R - tw : L + tw, ew)) {
            if (keepPrefix) popCodePoint(t);
            else popFrontCodePoint(t);
            trimSpaces();
            tw = measure(metrics, t, f);
        }
        if (t.empty() && !lastChance) {
            // Nothing of this run survives: it goes, and the ellipsis follows
            // whatever precedes it on the line.
            run.text.clear();
            run.width = 0;
            run.srcEnd = run.srcStart;
            refreshTextRect(ref.text);
            continue;
        }
        const bool srcMatches = run.srcEnd - run.srcStart == static_cast<int>(run.text.size());
        if (srcMatches) {
            if (keepPrefix) run.srcEnd = run.srcStart + static_cast<int>(t.size());
            else run.srcStart = run.srcEnd - static_cast<int>(t.size());
        }
        const float keptL = rtl ? R - tw : L;
        if (keepPrefix) {
            run.text = t + spec.ellipsisText;
            run.width = tw + ew;
            run.x = (rtl ? keptL - ew : keptL) - ref.ox;
            refreshTextRect(ref.text);
        } else {
            run.text = t;
            run.width = tw;
            run.x = keptL - ref.ox;
            refreshTextRect(ref.text);
            carrier = ref;
            carrierY = ref.oy + run.y;
            carrierH = run.height;
            standAlone(rtl ? keptL : keptL + tw, run.srcStart);
        }
        return;
    }
    // Everything on the line went (or it had nothing): the ellipsis alone, at
    // the line's start edge.
    standAlone(rtl ? line.right : line.left, carrierSrc());
}

} // namespace

bool lineClampApplies(LayoutNode* node) {
    return node->lineClamped || resolveLineClamp(node).maxLines > 0;
}

bool beginLineClamp(LayoutNode* node, LineClampSpec& spec) {
    spec = resolveLineClamp(node);
    // Not box.textTruncated: the box is cleared at the start of each pass, so
    // it forgets a clamp from the last one.
    bool wasClamped = node->lineClamped;
    node->lineClamped = false;
    node->box.textTruncated = false;
    if (wasClamped) {
        auto& runs = node->box.textRuns;
        runs.erase(std::remove_if(runs.begin(), runs.end(),
                                  [](const PlacedTextRun& r) {
                                      return r.srcStart == kContainerEllipsisSrc;
                                  }),
                   runs.end());
    }
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
    w.container = node;
    w.cutY = last.bottom;
    w.lineTop = last.top;
    walk(node, 0.0f, 0.0f, w);
    if (spec.ellipsis && !spec.ellipsisText.empty())
        placeEllipsis(w, last, spec, metrics);
    node->box.textTruncated = true;
    node->lineClamped = true;
    return last.bottom;
}

} // namespace htmlayout::layout
