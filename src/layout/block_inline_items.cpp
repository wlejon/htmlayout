// block_inline_items.cpp — the line items of a block container's inline
// formatting context (see block_inline.cpp for the line builder).
//
// A non-replaced inline element whose content is all inline-level is
// flattened: its text, its atomic inlines and its nested inline elements join
// the block's one sequence of line items, bracketed by an open and a close
// marker that carry its inline-start and inline-end margin + border + padding.
// Text is cut into the pieces a line may break between — words and the
// collapsed spaces between them, or, for preserved white space that wraps
// (pre-wrap), a word with the white space after it.

#include "layout/block_inline_items.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include "layout/text.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace htmlayout::layout {

StrutMetrics computeStrut(const LayoutNode* node, float fontSize,
                          TextMetrics& metrics) {
    const std::string& fam = styleVal(node, Prop::FontFamily);
    const std::string& wt = styleVal(node, Prop::FontWeight);
    StrutMetrics s;
    s.lineHeight = resolveLineHeight(styleVal(node, Prop::LineHeight), fontSize,
                                     fam, wt, metrics);
    float natural = fontSize > 0 ? metrics.lineHeight(fam, fontSize, wt) : 0.0f;
    if (natural <= 0) natural = fontSize * 1.2f;
    float asc = fontSize > 0 ? metrics.ascent(fam, fontSize, wt) : 0.0f;
    if (fontSize > 0 && (asc <= 0 || asc >= natural)) asc = natural * 0.8f;
    float leading = s.lineHeight - natural;
    float half = std::floor(leading * 0.5f);
    s.ascent = asc;
    s.descent = natural - asc;
    s.above = asc + half;
    s.below = (natural - asc) + (leading - half);
    s.xHeight = fontSize > 0 ? metrics.xHeight(fam, fontSize, wt) : 0.0f;
    return s;
}

// Blink geometry, measured against Chromium: a symbolic marker
// (disc/circle/square) is a round(ascent)-wide box plus the 7px marker
// padding; an ordinal marker is the marker text ("12.") plus one space
// advance. The painter (bro's DrawTraversal) fills this reserved box.
float insideMarkerInlineSize(LayoutNode* node, float fontSize, TextMetrics& metrics) {
    if (styleVal(node, Prop::Display) != "list-item") return 0.0f;
    if (styleVal(node, Prop::ListStylePosition) != "inside") return 0.0f;
    std::string type = styleVal(node, Prop::ListStyleType);
    if (type.empty()) type = "disc";
    if (type == "none") return 0.0f;
    const std::string& fam = styleVal(node, Prop::FontFamily);
    const std::string& wt = styleVal(node, Prop::FontWeight);
    if (fontSize <= 0) return 0.0f;

    // Disclosure triangle (<summary>): Blink's DisclosureSymbolSize is
    // 0.66em, with a 0.4em end margin (kClosureMarkerMarginEm).
    if (type == "disclosure-open" || type == "disclosure-closed")
        return 0.66f * fontSize + 0.4f * fontSize;

    // Blink (list_marker.cc): symbol width = (A*2/3 + 1)/2 + 2 in INTEGER
    // arithmetic on the rounded ascent A, and the inside marker box carries
    // margin-start -1px and margin-end 1em (kCUAMarkerMarginEm). Verified
    // against Chromium at font sizes 10..40px.
    if (type == "disc" || type == "circle" || type == "square") {
        int a = static_cast<int>(std::lround(metrics.ascent(fam, fontSize, wt)));
        float symbol = static_cast<float>((a * 2 / 3 + 1) / 2 + 2);
        return symbol - 1.0f + fontSize;
    }

    // Ordinal marker: position among list-item siblings, honoring
    // <ol start> and <li value>.
    int idx = 1;
    if (LayoutNode* parent = node->parent()) {
        std::string startAttr(parent->attribute("start"));
        if (!startAttr.empty()) idx = std::atoi(startAttr.c_str());
        for (LayoutNode* sib : parent->children()) {
            if (styleVal(sib, Prop::Display) != "list-item")
                continue;
            std::string valAttr(sib->attribute("value"));
            if (!valAttr.empty()) idx = std::atoi(valAttr.c_str());
            if (sib == node) break;
            ++idx;
        }
    }

    auto toAlpha = [](int n) {
        std::string s;
        while (n > 0) {
            int rem = (n - 1) % 26;
            s.insert(s.begin(), static_cast<char>('a' + rem));
            n = (n - 1) / 26;
        }
        return s.empty() ? std::string("a") : s;
    };
    auto toRoman = [](int n) {
        if (n <= 0 || n >= 4000) return std::to_string(n);
        static const int vals[] = {1000, 900, 500, 400, 100, 90,
                                   50, 40, 10, 9, 5, 4, 1};
        static const char* syms[] = {"m", "cm", "d", "cd", "c", "xc",
                                     "l", "xl", "x", "ix", "v", "iv", "i"};
        std::string s;
        for (int i = 0; i < 13; ++i)
            while (n >= vals[i]) { s += syms[i]; n -= vals[i]; }
        return s;
    };
    auto toUpper = [](std::string s) {
        for (auto& ch : s)
            ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch)));
        return s;
    };

    std::string text;
    if (type == "decimal") {
        text = std::to_string(idx);
    } else if (type == "decimal-leading-zero") {
        text = (idx >= 0 && idx < 10 ? "0" : "") + std::to_string(idx);
    } else if (type == "lower-alpha" || type == "lower-latin") {
        text = toAlpha(idx);
    } else if (type == "upper-alpha" || type == "upper-latin") {
        text = toUpper(toAlpha(idx));
    } else if (type == "lower-roman") {
        text = toRoman(idx);
    } else if (type == "upper-roman") {
        text = toUpper(toRoman(idx));
    } else {
        text = std::to_string(idx);
    }
    text += ".";
    return metrics.measureWidth(text, fam, fontSize, wt) +
           metrics.measureWidth(" ", fam, fontSize, wt);
}

namespace ifc {

namespace {

// The baseline shift vertical-align gives inline element `el` (context
// `self`) inside `parent` — CSS2 §10.8.1, Blink's offsets.
float inlineShift(LayoutNode* el, const InlineCtx& parent, const InlineCtx& self) {
    const std::string& va = styleVal(el, Prop::VerticalAlign);
    if (va.empty() || va == "baseline" || va == "top" || va == "bottom") return 0.0f;
    if (va == "super") return parent.fontSize / 3.0f + 1.0f;
    if (va == "sub") return -(parent.fontSize / 5.0f + 1.0f);
    if (va == "middle")
        return parent.strut.xHeight * 0.5f + self.strut.lineHeight * 0.5f - self.strut.above;
    if (va == "text-top") return parent.strut.ascent - self.strut.above;
    if (va == "text-bottom") return self.strut.below - parent.strut.descent;
    // <length> / <percentage> of the element's own line-height.
    return resolveLength(va, self.strut.lineHeight, self.fontSize);
}

bool isBr(LayoutNode* n) { return n->tagName() == "br" || n->tagName() == "BR"; }

bool isAtomicInlineDisplay(const std::string& d) {
    return d == "inline-block" || d == "inline-flex" || d == "inline-grid";
}

// Whether inline element `el` can be flattened into its block's lines: all of
// its in-flow content is inline-level, all the way down.
bool flowsInline(LayoutNode* el) {
    for (auto* c : getLayoutChildren(el)) {
        if (c->isTextNode()) continue;
        const std::string& d = styleVal(c, Prop::Display);
        if (d == "none") continue;
        const std::string& pos = styleVal(c, Prop::Position);
        if (pos == "absolute" || pos == "fixed") continue;
        if (isBr(c) || isAtomicInlineDisplay(d)) continue;
        if (d == "inline") {
            float iw = 0, ih = 0;
            if (c->intrinsicSize(iw, ih, 0.0f) || flowsInline(c)) continue;
        }
        return false;
    }
    return true;
}

bool isBlank(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f'; }

size_t utf8Len(const std::string& s, size_t i) {
    unsigned char b = static_cast<unsigned char>(s[i]);
    size_t n = 1;
    if      ((b & 0xE0) == 0xC0) n = 2;
    else if ((b & 0xF0) == 0xE0) n = 3;
    else if ((b & 0xF8) == 0xF0) n = 4;
    return std::min(n, s.size() - i);
}

} // namespace

InlineCtx makeCtx(LayoutNode* box, float fontSize, TextMetrics& metrics) {
    InlineCtx c;
    c.box = box;
    c.fontSize = fontSize;
    c.family = &styleVal(box, Prop::FontFamily);
    c.weight = &styleVal(box, Prop::FontWeight);
    c.whiteSpace = &styleVal(box, Prop::WhiteSpace);
    c.transform = &styleVal(box, Prop::TextTransform);
    // A collapsed space between words carries letter-spacing after it plus
    // word-spacing.
    c.ls = resolveLength(styleVal(box, Prop::LetterSpacing), 0, fontSize);
    c.ws = resolveLength(styleVal(box, Prop::WordSpacing), 0, fontSize);
    c.spaceWidth = metrics.measureWidth(" ", *c.family, fontSize, *c.weight) + c.ls + c.ws;
    const std::string& oWrap = styleVal(box, Prop::OverflowWrap);
    const std::string& wBreak = styleVal(box, Prop::WordBreak);
    if (wBreak == "break-all") c.breakMode = 2;
    else if (oWrap == "break-word" || oWrap == "anywhere" || wBreak == "break-word")
        c.breakMode = 1;
    c.strut = computeStrut(box, fontSize, metrics);
    return c;
}

bool ItemCollector::hasContent() const {
    for (const auto& it : items)
        if (it.edge == 0 && !it.isFloat) return true;
    return false;
}

// The last content item ends in collapsible white space, so white space that
// follows (across element boundaries) collapses into it. Nothing is kept next
// to a float either: Chromium renders no space on either side of one.
bool ItemCollector::contentEndsInSpace() const {
    for (size_t i = items.size(); i-- > 0;) {
        const IFCItem& it = items[i];
        if (it.edge != 0) continue;
        if (it.isFloat) return true;
        if (it.isElement || it.forceBreak || it.preserved) return false;
        return !it.text.empty() &&
               std::isspace(static_cast<unsigned char>(it.text.back()));
    }
    return false;
}

void ItemCollector::collapsedSpace(LayoutNode* child, const InlineCtx& c) {
    // A run of collapsible white space with no words: one space between
    // inline siblings. Skipped when nothing precedes it (leading white space
    // must not push the first item rightward) and when the content before
    // already ends in a space it collapses into.
    if (!hasContent() || contentEndsInSpace()) return;
    IFCItem it{};
    it.width = c.spaceWidth;
    it.height = 0.0f; // doesn't grow line height
    it.node = child;
    it.owner = c.box;
    it.text = " ";
    it.shift = c.shift;
    it.spaceW = c.spaceWidth;
    it.canBreakBefore = true;
    it.canBreakAfter  = true;
    items.push_back(std::move(it));
}

void ItemCollector::forcedBreak(LayoutNode* child, const InlineCtx& c, LayoutNode* owner) {
    IFCItem brk{};
    brk.height = c.strut.lineHeight;
    brk.above = c.strut.above;
    brk.below = c.strut.below;
    brk.baseline = c.strut.ascent;
    brk.shift = c.shift;
    brk.node = child;
    brk.owner = owner;
    brk.forceBreak = true;
    items.push_back(std::move(brk));
}

// Collapsible text that wraps between words: one item per word, the collapsed
// spaces between them items of their own.
void ItemCollector::collapsingText(LayoutNode* child, const InlineCtx& c,
                                   const std::string& piece, int srcBase) {
    auto runs = breakTextIntoRuns(piece, 0.5f, *c.family, c.fontSize, *c.weight,
                                  "normal", metrics, "normal", "normal",
                                  c.ls, c.ws, *c.transform);
    if (runs.empty()) {
        for (char ch : piece)
            if (std::isspace(static_cast<unsigned char>(ch))) { collapsedSpace(child, c); break; }
        return;
    }
    size_t emitted = 0;
    int prevSrcEnd = 0;
    for (auto& run : runs)
        emitRun(child, c, run, emitted, prevSrcEnd, /*words=*/true, /*collapsing=*/true,
                srcBase);
}

void ItemCollector::text(LayoutNode* child, const InlineCtx& c) {
    // Fresh layout pass — clear any previously placed runs.
    child->box.textRuns.clear();
    const std::string src(child->textContent());
    const std::string& ws = *c.whiteSpace;

    if (ws == "pre-wrap" || ws == "break-spaces") {
        auto runs = segmentPreservedText(src, *c.family, c.fontSize, *c.weight, metrics,
                                         c.ls, c.ws, *c.transform);
        size_t emitted = 0;
        int prevSrcEnd = 0;
        for (auto& run : runs) {
            if (!(run.text.empty() && run.width == 0))
                emitRun(child, c, run, emitted, prevSrcEnd, false, false, 0);
            if (run.forceBreakAfter) forcedBreak(child, c, c.box);
        }
        return;
    }
    if (ws == "pre-line") {
        // Spaces collapse and wrap as in `normal`; each preserved newline is a
        // forced break.
        size_t cursor = 0;
        while (cursor <= src.size()) {
            size_t nl = src.find('\n', cursor);
            size_t end = (nl == std::string::npos) ? src.size() : nl;
            if (end > cursor)
                collapsingText(child, c, src.substr(cursor, end - cursor),
                               static_cast<int>(cursor));
            if (nl == std::string::npos) break;
            forcedBreak(child, c, c.box);
            cursor = nl + 1;
        }
        return;
    }
    if (ws == "pre" || ws == "nowrap") {
        // No soft wraps: each source line (pre) or the whole collapsed text
        // (nowrap) is one run.
        auto runs = breakTextIntoRuns(src, std::max(childAvailable, 1.0f), *c.family,
                                      c.fontSize, *c.weight, ws, metrics, "normal", "normal",
                                      c.ls, c.ws, *c.transform);
        const bool collapsing = ws == "nowrap";
        if (runs.empty() && collapsing) {
            for (char ch : src)
                if (std::isspace(static_cast<unsigned char>(ch))) { collapsedSpace(child, c); break; }
            return;
        }
        size_t emitted = 0;
        int prevSrcEnd = 0;
        for (auto& run : runs) {
            if (!(run.text.empty() && run.width == 0))
                emitRun(child, c, run, emitted, prevSrcEnd, false, collapsing, 0);
            if (run.forceBreakAfter) forcedBreak(child, c, c.box);
        }
        return;
    }
    collapsingText(child, c, src, 0);
}

void ItemCollector::emitRun(LayoutNode* child, const InlineCtx& c, const TextRun& run,
                            size_t& emitted, int& prevSrcEnd, bool words, bool collapsing,
                            int srcBase) {
    const std::string& ws = *c.whiteSpace;
    const bool preserved = ws == "pre" || ws == "pre-wrap" || ws == "break-spaces";
    const bool wraps = ws != "pre" && ws != "nowrap";
    if (words && emitted > 0 && run.canBreakBefore) {
        // The collapsed whitespace between two words: its own item so wrap
        // decisions exclude the trailing space, line-edge trimming can drop
        // it, and justify can expand it. Its width is the gap as the splitter
        // measured it in context, not an isolated " " (the words either side
        // already absorbed the kerning that straddles it); its height is
        // real, because caret and selection geometry discard a zero-height
        // run.
        IFCItem sp{};
        sp.width = run.spaceBefore > 0.0f ? run.spaceBefore : c.spaceWidth;
        sp.height = run.height;
        sp.above = c.strut.above;
        sp.below = c.strut.below;
        sp.baseline = c.strut.ascent;
        sp.shift = c.shift;
        sp.node = child;
        sp.owner = c.box;
        sp.text = " ";
        sp.spaceW = c.spaceWidth;
        sp.srcStart = prevSrcEnd;
        sp.srcEnd = srcBase + run.srcStart;
        sp.canBreakBefore = true;
        sp.canBreakAfter  = true;
        items.push_back(std::move(sp));
    }
    IFCItem it{};
    it.width = run.width;
    // The run's own box is the font's natural height (ascent + descent); the
    // line box grows to the line-height via above / below instead.
    it.height = run.height;
    it.above = c.strut.above;
    it.below = c.strut.below;
    it.baseline = c.strut.ascent;
    it.shift = c.shift;
    it.node = child;
    it.owner = c.box;
    it.text = run.text;
    it.spaceW = c.spaceWidth;
    it.srcStart = srcBase + run.srcStart;
    it.srcEnd = srcBase + run.srcEnd;
    it.canBreakBefore = run.canBreakBefore;
    it.canBreakAfter  = run.canBreakAfter;
    it.preserved = preserved;
    it.hangW = run.hangWidth;
    it.breakMode = wraps ? c.breakMode : 0;
    it.family = c.family;
    it.weight = c.weight;
    it.fontSize = c.fontSize;
    it.ls = c.ls;
    it.ws = c.ws;
    // White space where this text meets the text before it, across an
    // element boundary, collapses into the space already there.
    if (collapsing && emitted == 0 && !it.text.empty() &&
        it.text.front() == ' ' && contentEndsInSpace()) {
        it.text.erase(it.text.begin());
        it.width = std::max(0.0f, it.width - c.spaceWidth);
    }
    prevSrcEnd = it.srcEnd;
    items.push_back(std::move(it));
    ++emitted;
}

// An atomic inline-level box (inline-block & co., a replaced element, or an
// inline element that cannot be flattened): laid out on its own and placed as
// one item.
void ItemCollector::atomic(LayoutNode* child, const std::string& d, const InlineCtx& c) {
    layoutNode(child, childAvailable, metrics);

    IFCItem it{};
    it.width = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
    it.height = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;
    it.node = child;
    it.owner = c.box;
    it.isElement = true;
    it.shift = c.shift;

    const std::string& va = styleVal(child, Prop::VerticalAlign);
    if (va == "top") it.valign = 1;
    else if (va == "middle") it.valign = 2;
    else if (va == "bottom") it.valign = 3;
    else if (va == "super") it.shift += c.fontSize / 3.0f + 1.0f;
    else if (va == "sub") it.shift -= c.fontSize / 5.0f + 1.0f;

    float iw = 0, ih = 0;
    bool replaced = child->intrinsicSize(iw, ih, childAvailable);

    if (replaced) {
        // Replaced elements keep their replaced-baseline rules regardless of
        // computed display (an <input> is inline-block per the UA sheet but
        // has no line boxes of its own to take a baseline from).
        std::string_view rtag = child->tagName();
        bool isTextarea = (rtag == "textarea" || rtag == "TEXTAREA");
        if (child->hasIntrinsicRatio() || isTextarea) {
            // Replaced media (img/canvas/video/svg) and the multi-line
            // <textarea> align by the bottom margin edge, so the strut's
            // descent hangs below the control (Blink's line heights).
            it.baseline = it.height;
            it.above = it.height;
            it.below = 0;
        } else {
            std::string_view rtype = child->attribute("type");
            bool isCheckRadio = (rtag == "input" || rtag == "INPUT") &&
                                (rtype == "checkbox" || rtype == "radio");
            if (isCheckRadio) {
                // Checkbox/radio carry no text; browsers align them by the
                // bottom of the border box.
                it.baseline = it.height - child->box.margin.bottom;
            } else {
                // Single-line form controls align by the control's internal
                // text baseline — top border + padding + the control font's
                // ascent.
                float cfs = resolveLength(styleVal(child, Prop::FontSize), 16.0f, 16.0f);
                if (cfs <= 0.0f) cfs = 16.0f;
                const std::string& cfam = styleVal(child, Prop::FontFamily);
                const std::string& cwt  = styleVal(child, Prop::FontWeight);
                float casc = metrics.ascent(cfam, cfs, cwt);
                if (casc <= 0.0f) casc = cfs * 0.8f;
                it.baseline = child->box.margin.top + child->box.border.top +
                              child->box.padding.top + casc;
            }
            if (it.baseline > it.height) it.baseline = it.height;
            if (it.baseline < 0) it.baseline = 0;
            it.above = it.baseline;
            it.below = it.height - it.baseline;
        }
    } else if (isAtomicInlineDisplay(d)) {
        // Atomic inline: baseline is the last line box's baseline when the
        // box has in-flow inline content and visible overflow; otherwise the
        // bottom margin edge (CSS2 §10.8.1). The whole margin box
        // participates in line sizing — no half-leading.
        const std::string& ov = styleVal(child, Prop::Overflow);
        bool visibleOv = ov.empty() || ov == "visible";
        if (visibleOv && child->box.baselineOffset >= 0) {
            it.baseline = child->box.margin.top + child->box.border.top +
                child->box.padding.top + child->box.baselineOffset;
            if (it.baseline > it.height) it.baseline = it.height;
        } else {
            it.baseline = it.height;
        }
        it.above = it.baseline;
        it.below = it.height - it.baseline;
    } else {
        // Non-replaced inline element laid out whole (it holds block-level
        // content): its contribution comes from its own font and
        // line-height, NOT from its box height, and it is positioned by
        // aligning its first-line baseline with the line's.
        float cfs = resolveLength(styleVal(child, Prop::FontSize), c.fontSize, c.fontSize);
        if (cfs <= 0) cfs = c.fontSize;
        StrutMetrics cs = computeStrut(child, cfs, metrics);
        float b = (child->box.baselineOffset >= 0)
            ? child->box.baselineOffset : cs.ascent;
        it.baseline = b;
        it.baselineFromContent = true;
        if (child->box.inlineExtentAbove >= 0) {
            // Strip-boxed inline (see layoutInline): the line grows to the
            // element's leaded box unioned with its content extents.
            it.above = std::max(cs.above, child->box.inlineExtentAbove);
            it.below = std::max(cs.below, child->box.inlineExtentBelow);
        } else {
            it.above = std::max(cs.above, b);
            it.below = std::max(cs.below, child->box.contentRect.height - b);
        }
        if (va == "middle") {
            // Blink centers the child's LEADED inline box on baseline +
            // xHeight/2 of the parent; the margin-box form (valign == 2)
            // stays for atomic inlines.
            it.valign = 0;
            it.shift += c.strut.xHeight * 0.5f + cs.lineHeight * 0.5f - cs.above;
        }
    }
    if (va == "text-top") {
        // Top of the child's inline box meets the parent's content-area top:
        // a baseline shift, so the line box still grows through the shifted
        // extents.
        it.valign = 0;
        it.shift += c.strut.ascent - it.above;
    } else if (va == "text-bottom") {
        it.valign = 0;
        it.shift += it.below - c.strut.descent;
    } else if (!va.empty() && va != "baseline" && va != "top" &&
               va != "middle" && va != "bottom" &&
               va != "sub" && va != "super") {
        // <length> / <percentage>: raise the baseline by the value (negative
        // lowers). Percentages resolve against the element's own line-height.
        float vfs = resolveLength(styleVal(child, Prop::FontSize), c.fontSize, c.fontSize);
        if (vfs <= 0) vfs = c.fontSize;
        float vlh = resolveLineHeight(styleVal(child, Prop::LineHeight), vfs,
                                      styleVal(child, Prop::FontFamily),
                                      styleVal(child, Prop::FontWeight), metrics);
        it.valign = 0;
        it.shift += resolveLength(va, vlh, vfs);
    }
    // Atomic inlines always offer a soft-wrap opportunity on both sides (CSS
    // Text §5.3) — adjacent inline-blocks with no whitespace still wrap. An
    // inline element laid out whole keeps the text-driven break rules so
    // "word<span>.</span>" stays together.
    bool atomicInline = replaced || isAtomicInlineDisplay(d);
    it.canBreakBefore = atomicInline;
    it.canBreakAfter = atomicInline;
    items.push_back(std::move(it));
}

// A flattenable inline element: open marker, its content, close marker.
void ItemCollector::inlineBox(LayoutNode* el, const InlineCtx& parent) {
    claimForParentFlow(el);
    float fs = resolveLength(styleVal(el, Prop::FontSize), parent.fontSize, parent.fontSize);
    if (fs <= 0.0f) fs = parent.fontSize;
    el->box.margin = resolveEdges(el, kMarginProps, childAvailable, fs);
    el->box.padding = resolveEdges(el, kPaddingProps, childAvailable, fs);
    el->box.border = resolveBorders(el, childAvailable, fs);
    el->box.baselineOffset = -1.0f;
    el->box.inlineExtentAbove = -1.0f;
    el->box.inlineExtentBelow = -1.0f;

    InlineCtx c = makeCtx(el, fs, metrics);
    c.shift = parent.shift + inlineShift(el, parent, c);

    InlineBox ib;
    ib.node = el;
    ib.owner = parent.box;
    ib.ascent = c.strut.ascent;
    ib.descent = c.strut.descent;
    ib.shift = c.shift;
    // The element's inline-start side follows its own direction (CSS Writing
    // Modes §2.4): a right-to-left element starts on the right.
    ib.rtl = styleVal(el, Prop::Direction) == "rtl";
    const size_t boxIndex = boxes.size();
    boxes.push_back(ib);

    auto marker = [&](int edge, float width) {
        IFCItem m{};
        m.width = width;
        m.node = el;
        m.owner = parent.box;
        m.edge = edge;
        m.rtlEdge = ib.rtl;
        // The element's leaded box sits on every line it touches, an empty
        // one included (CSS2 §10.8).
        m.above = c.strut.above;
        m.below = c.strut.below;
        m.baseline = c.strut.ascent;
        m.shift = c.shift;
        items.push_back(std::move(m));
    };
    const LayoutBox& b = el->box;
    const float leftSide = b.margin.left + b.border.left + b.padding.left;
    const float rightSide = b.margin.right + b.border.right + b.padding.right;
    boxes[boxIndex].open = items.size();
    marker(+1, ib.rtl ? rightSide : leftSide);
    children(el, getLayoutChildren(el), c);
    boxes[boxIndex].close = items.size();
    marker(-1, ib.rtl ? leftSide : rightSide);
}

void ItemCollector::children(LayoutNode* parent, const std::vector<LayoutNode*>& list,
                             const InlineCtx& c) {
    for (auto* child : list) {
        if (child->isTextNode()) { text(child, c); continue; }
        const std::string& d = styleVal(child, Prop::Display);
        if (d == "none") { clearHiddenBox(child); continue; }
        if (parent != block) {
            child->viewportHeight = parent->viewportHeight;
            child->availableHeight = parent->availableHeight;
        }
        const std::string& cp = styleVal(child, Prop::Position);
        if (cp == "absolute" || cp == "fixed") {
            // Out of flow: no item, but remember where in the item sequence
            // it sat so its static position can be read off the line that
            // ends up carrying that item. See LayoutNode::staticPosX.
            pendingStatic.push_back({child, items.size(), parent});
            continue;
        }
        if (acceptFloats && parent == block) {
            const std::string& f = styleVal(child, Prop::Float);
            if (f == "left" || f == "right") {
                // A collapsed space immediately before a float renders no gap.
                if (!items.empty() && isSpaceItem(items.back())) items.pop_back();
                IFCItem it{};
                it.node = child;
                it.owner = block;
                it.isFloat = true;
                items.push_back(std::move(it));
                continue;
            }
        }
        if (isBr(child)) {
            child->box = LayoutBox{};
            // Natural font line-height so getBoundingClientRect returns the
            // inline content height for the <br>, not 0.
            child->box.contentRect.height =
                metrics.lineHeight(*c.family, c.fontSize, *c.weight);
            forcedBreak(child, c, parent);
            continue;
        }
        if (d == "inline") {
            float iw = 0, ih = 0;
            if (!child->intrinsicSize(iw, ih, childAvailable) && flowsInline(child)) {
                inlineBox(child, c);
                continue;
            }
        }
        atomic(child, d, c);
    }
}

bool splitTextItem(std::vector<IFCItem>& items, size_t i, float maxWidth,
                   bool mustFit, std::vector<InlineBox>& boxes,
                   std::vector<PendingStatic>& pendingStatic,
                   TextMetrics& metrics) {
    const IFCItem& it = items[i];
    if (it.isElement || it.forceBreak || it.isFloat || it.edge != 0 || it.dropped ||
        !it.family || !it.node)
        return false;
    const std::string& t = it.text;
    size_t firstSolid = 0;
    while (firstSolid < t.size() && isBlank(t[firstSolid])) ++firstSolid;
    if (firstSolid >= t.size()) return false;

    // A cut lands before a character that is not white space, after at least
    // one that is not. Widths grow with the cut, so walk until one overflows.
    size_t best = 0, first = 0;
    float bestW = 0;
    for (size_t k = firstSolid + utf8Len(t, firstSolid); k < t.size(); k += utf8Len(t, k)) {
        if (isBlank(t[k])) continue;
        float w = measureRunWidth(t.substr(0, k), *it.family, it.fontSize, *it.weight,
                                  metrics, it.ls, it.ws);
        if (!first) { first = k; if (!mustFit) { best = k; bestW = w; } }
        if (w > maxWidth + kFitSlack) break;
        best = k;
        bestW = w;
    }
    if (!best) return false;

    // The cut in source bytes: text and source agree on everything but white
    // space (collapsing only ever touches white space), so count the
    // characters before the cut that are not.
    int solidBefore = 0;
    for (size_t j = 0; j < best; ++j)
        if (!isBlank(t[j])) ++solidBefore;
    std::string_view src = it.node->textContent();
    int s = it.srcStart, seen = 0;
    while (s < it.srcEnd && s < static_cast<int>(src.size())) {
        if (!std::isspace(static_cast<unsigned char>(src[s]))) {
            if (seen == solidBefore) break;
            ++seen;
        }
        ++s;
    }

    IFCItem rest = it;
    rest.text = t.substr(best);
    rest.width = std::max(0.0f, it.width - bestW);
    rest.srcStart = s;
    rest.canBreakBefore = true;
    IFCItem& head = items[i];
    head.text = head.text.substr(0, best);
    head.width = bestW;
    head.srcEnd = s;
    head.canBreakAfter = true;
    head.hangW = 0;
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(rest));
    for (auto& b : boxes) {
        if (b.open > i) ++b.open;
        if (b.close > i) ++b.close;
    }
    for (auto& ps : pendingStatic)
        if (ps.itemIndex != static_cast<size_t>(-1) && ps.itemIndex > i) ++ps.itemIndex;
    return true;
}

} // namespace ifc
} // namespace htmlayout::layout
