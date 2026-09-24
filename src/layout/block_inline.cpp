// block_inline.cpp — the inline formatting context of a block container whose
// in-flow children are all inline-level (CSS2 §9.4.2, §10.8).
//
// Inline elements are not laid out as boxes of their own here. A non-replaced
// inline element (<span>, <b>, <a>, ...) whose content is all inline-level is
// *flattened*: its text, its atomic inlines and its nested inline elements
// join the block's one sequence of line items, bracketed by an open and a
// close marker that carry its inline-start and inline-end margin + border +
// padding. Lines are broken over that single sequence, so an inline element's
// text wraps where it stands — mid-line, after whatever precedes it — instead
// of being one box that has to fit whole or move to the next line.
//
// Once the lines are placed, each inline element's box is the union of its
// fragments (what Chromium reports for a wrapped inline), and everything
// inside it is rebased into its content coordinates, which is where the rest
// of the tree (painting, hit testing, caret and selection geometry) expects a
// child's geometry to be.
//
// An inline element holding block-level content (a float, a <div>) is still
// laid out as one atomic item by layoutInline().

#include "layout/block_internal.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include "layout/text.h"
#include "layout/bidi_line.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

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

namespace {

struct IFCItem {
    float width = 0, height = 0;
    LayoutNode* node = nullptr;
    bool isElement = false;
    bool forceBreak = false;
    // CSS2 §10.8 vertical geometry. Every inline-level item spans
    // [baseline - above, baseline + below] in the line box after baseline
    // alignment; the line box height is the union of these extents (plus the
    // block's strut). `baseline` is the distance from the item's positioned
    // top (run top for text, margin-box top for atomic inlines, content top
    // for non-replaced inline elements — see baselineFromContent) down to its
    // baseline.
    float above = 0;
    float below = 0;
    float baseline = 0;
    // True when `baseline` is measured from the child's contentRect top
    // (non-replaced inline elements laid out whole — their padding/border sit
    // outside the line-height geometry); false when measured from the
    // margin-box top (inline-blocks and replaced elements).
    bool baselineFromContent = false;
    // vertical-align: 0 = baseline, 1 = top, 2 = middle, 3 = bottom.
    // Non-baseline items don't move the line's baseline; they only grow the
    // line box when taller than it.
    int valign = 0;
    // Baseline shift (positive raises): vertical-align sub/super/<length> of
    // the item itself plus that of every inline element it sits in.
    float shift = 0;
    // For text runs: the post-processing display string and the source byte
    // range, so the placed run can be recorded on the text node.
    std::string text;
    int srcStart = 0;
    int srcEnd = 0;
    // Soft-wrap opportunities on each side. Text runs inherit these from the
    // word-boundary splitter; atomic inlines always offer them.
    bool canBreakBefore = false;
    bool canBreakAfter  = false;
    // The element whose content box this item's geometry ends up in: the
    // block for its own children, an inline element for what sits inside it.
    LayoutNode* owner = nullptr;
    // +1 opens inline element `node`, -1 closes it (width = its inline-start /
    // inline-end margin + border + padding); 0 for content.
    int edge = 0;
    // Line baseline the item was placed against, block content coordinates.
    float placedBaseline = 0;
    float placedX = 0;
    bool placed = false;
    // A collapsed space in the item's own font, for trimming white space at
    // the line edges.
    float spaceW = 0;
    // Collapsible white space at the very end of the content: takes no room
    // and draws nothing.
    bool dropped = false;
};

bool isSpaceItem(const IFCItem& it) {
    return !it.isElement && !it.forceBreak && it.edge == 0 && !it.dropped &&
           it.text == " ";
}

// The font, white-space handling and line contribution of the text directly
// inside one inline box — the block itself, or a flattened inline element.
struct InlineCtx {
    LayoutNode* box = nullptr;
    float fontSize = 16.0f;
    const std::string* family = nullptr;
    const std::string* weight = nullptr;
    const std::string* whiteSpace = nullptr;
    const std::string* oWrap = nullptr;
    const std::string* wBreak = nullptr;
    const std::string* transform = nullptr;
    float ls = 0, ws = 0;
    float spaceWidth = 0;   // a collapsed space: glyph + letter- + word-spacing
    bool wordMode = true;
    StrutMetrics strut;     // the box's leaded extents about its baseline
    float shift = 0;        // baseline shift accumulated from vertical-align
};

InlineCtx makeCtx(LayoutNode* box, float fontSize, TextMetrics& metrics) {
    InlineCtx c;
    c.box = box;
    c.fontSize = fontSize;
    c.family = &styleVal(box, Prop::FontFamily);
    c.weight = &styleVal(box, Prop::FontWeight);
    c.whiteSpace = &styleVal(box, Prop::WhiteSpace);
    c.oWrap = &styleVal(box, Prop::OverflowWrap);
    c.wBreak = &styleVal(box, Prop::WordBreak);
    c.transform = &styleVal(box, Prop::TextTransform);
    // A collapsed space between words carries letter-spacing after it plus
    // word-spacing.
    c.ls = resolveLength(styleVal(box, Prop::LetterSpacing), 0, fontSize);
    c.ws = resolveLength(styleVal(box, Prop::WordSpacing), 0, fontSize);
    c.spaceWidth = metrics.measureWidth(" ", *c.family, fontSize, *c.weight) + c.ls + c.ws;
    const bool canBreakWord = *c.oWrap == "break-word" || *c.oWrap == "anywhere" ||
                              *c.wBreak == "break-all";
    // Collapsing white-space wraps in the line builder: request word-
    // granularity runs (the splitter greedily packs to the given width, so a
    // tiny width yields one word per run) and re-insert the collapsed
    // inter-word spaces as separate items. Break-word / break-all and
    // non-collapsing modes keep the splitter's own line packing (it owns
    // mid-word break decisions).
    c.wordMode = (c.whiteSpace->empty() || *c.whiteSpace == "normal") && !canBreakWord;
    c.strut = computeStrut(box, fontSize, metrics);
    return c;
}

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

// A flattened inline element: its bracket in the item sequence and the
// geometry its fragments resolve to.
struct InlineBox {
    LayoutNode* node = nullptr;
    LayoutNode* owner = nullptr;   // its parent box (block or inline element)
    size_t open = 0, close = 0;    // item indices of its markers
    float ascent = 0, descent = 0; // its font's natural box about the baseline
    float shift = 0;
    // Content rect in the block's content coordinates.
    float x = 0, y = 0, w = 0, h = 0;
};

// Collects the line items of the block's inline content, descending into
// flattenable inline elements.
struct ItemCollector {
    LayoutNode* block;
    float childAvailable;
    TextMetrics& metrics;
    std::vector<IFCItem>& items;
    std::vector<InlineBox>& boxes;
    struct PendingStatic { LayoutNode* node; size_t itemIndex; LayoutNode* owner; };
    std::vector<PendingStatic>& pendingStatic;

    // Anything but markers emitted so far.
    bool hasContent() const {
        for (const auto& it : items)
            if (it.edge == 0) return true;
        return false;
    }
    // The last content item ends in collapsible white space, so white space
    // that follows (across element boundaries) collapses into it.
    bool contentEndsInSpace() const {
        for (size_t i = items.size(); i-- > 0;) {
            const IFCItem& it = items[i];
            if (it.edge != 0) continue;
            if (it.isElement || it.forceBreak) return false;
            return !it.text.empty() &&
                   std::isspace(static_cast<unsigned char>(it.text.back()));
        }
        return false;
    }

    void text(LayoutNode* child, const InlineCtx& c) {
        auto runs = breakTextIntoRuns(std::string(child->textContent()),
            c.wordMode ? 0.5f : childAvailable,
            *c.family, c.fontSize, *c.weight, *c.whiteSpace, metrics,
            *c.oWrap, *c.wBreak, c.ls, c.ws, *c.transform);
        // Fresh layout pass — clear any previously placed runs.
        child->box.textRuns.clear();
        const bool collapsing = c.whiteSpace->empty() || *c.whiteSpace == "normal";
        // Pure-whitespace text node (scanWords returned no words): collapses
        // to a single space contribution between inline siblings. Skip when
        // there's no prior content so leading whitespace doesn't push the
        // first item rightward, and when the content before already ends in
        // a space it collapses into.
        if (runs.empty() && collapsing) {
            bool anyWs = false;
            for (char ch : child->textContent())
                if (std::isspace(static_cast<unsigned char>(ch))) { anyWs = true; break; }
            if (anyWs && hasContent() && !contentEndsInSpace()) {
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
            return;
        }
        size_t emitted = 0;
        int prevSrcEnd = 0;
        for (auto& run : runs) {
            if (!(run.text.empty() && run.width == 0))
                emitRun(child, c, run, emitted, prevSrcEnd, collapsing);
            // A preserved newline (white-space: pre/pre-wrap/pre-line)
            // becomes a synthetic break-only item AFTER the run's content,
            // mirroring how <br> is handled.
            if (run.forceBreakAfter) {
                IFCItem brk{};
                brk.height = c.strut.lineHeight;
                brk.above = c.strut.above;
                brk.below = c.strut.below;
                brk.baseline = c.strut.ascent;
                brk.shift = c.shift;
                brk.node = child;
                brk.owner = c.box;
                brk.forceBreak = true;
                items.push_back(std::move(brk));
            }
        }
    }

    template <typename Run>
    void emitRun(LayoutNode* child, const InlineCtx& c, const Run& run,
                 size_t& emitted, int& prevSrcEnd, bool collapsing) {
        {
            if (c.wordMode && emitted > 0 && run.canBreakBefore) {
                // The collapsed whitespace between two words: its own item so
                // wrap decisions exclude the trailing space, line-edge
                // trimming can drop it, and justify can expand it. Its width
                // is the gap as the splitter measured it in context, not an
                // isolated " " (the words either side already absorbed the
                // kerning that straddles it); its height is real, because
                // caret and selection geometry discard a zero-height run.
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
                sp.srcEnd = run.srcStart;
                sp.canBreakBefore = true;
                sp.canBreakAfter  = true;
                items.push_back(std::move(sp));
            }
            IFCItem it{};
            it.width = run.width;
            // The run's own box is the font's natural height (ascent +
            // descent); the line box grows to the line-height via above /
            // below instead.
            it.height = run.height;
            it.above = c.strut.above;
            it.below = c.strut.below;
            it.baseline = c.strut.ascent;
            it.shift = c.shift;
            it.node = child;
            it.owner = c.box;
            it.text = run.text;
            it.spaceW = c.spaceWidth;
            it.srcStart = run.srcStart;
            it.srcEnd = run.srcEnd;
            it.canBreakBefore = run.canBreakBefore;
            it.canBreakAfter  = run.canBreakAfter;
            // White space where this text meets the text before it, across an
            // element boundary, collapses into the space already there.
            if (collapsing && emitted == 0 && !it.text.empty() &&
                it.text.front() == ' ' && contentEndsInSpace()) {
                it.text.erase(it.text.begin());
                it.width = std::max(0.0f, it.width - c.spaceWidth);
            }
            prevSrcEnd = run.srcEnd;
            items.push_back(std::move(it));
            ++emitted;
        }
    }

    // An atomic inline-level box (inline-block & co., a replaced element, or
    // an inline element that cannot be flattened): laid out on its own and
    // placed as one item.
    void atomic(LayoutNode* child, const std::string& d, const InlineCtx& c) {
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
            // Replaced elements keep their replaced-baseline rules regardless
            // of computed display (an <input> is inline-block per the UA sheet
            // but has no line boxes of its own to take a baseline from).
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
                    // Checkbox/radio carry no text; browsers align them by
                    // the bottom of the border box.
                    it.baseline = it.height - child->box.margin.bottom;
                } else {
                    // Single-line form controls align by the control's
                    // internal text baseline — top border + padding + the
                    // control font's ascent.
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
            // box has in-flow inline content and visible overflow; otherwise
            // the bottom margin edge (CSS2 §10.8.1). The whole margin box
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
            // Top of the child's inline box meets the parent's content-area
            // top: a baseline shift, so the line box still grows through the
            // shifted extents.
            it.valign = 0;
            it.shift += c.strut.ascent - it.above;
        } else if (va == "text-bottom") {
            it.valign = 0;
            it.shift += it.below - c.strut.descent;
        } else if (!va.empty() && va != "baseline" && va != "top" &&
                   va != "middle" && va != "bottom" &&
                   va != "sub" && va != "super") {
            // <length> / <percentage>: raise the baseline by the value
            // (negative lowers). Percentages resolve against the element's
            // own line-height.
            float vfs = resolveLength(styleVal(child, Prop::FontSize), c.fontSize, c.fontSize);
            if (vfs <= 0) vfs = c.fontSize;
            float vlh = resolveLineHeight(styleVal(child, Prop::LineHeight), vfs,
                                          styleVal(child, Prop::FontFamily),
                                          styleVal(child, Prop::FontWeight), metrics);
            it.valign = 0;
            it.shift += resolveLength(va, vlh, vfs);
        }
        // Atomic inlines always offer a soft-wrap opportunity on both sides
        // (CSS Text §5.3) — adjacent inline-blocks with no whitespace still
        // wrap. An inline element laid out whole keeps the text-driven break
        // rules so "word<span>.</span>" stays together.
        bool atomicInline = replaced || isAtomicInlineDisplay(d);
        it.canBreakBefore = atomicInline;
        it.canBreakAfter = atomicInline;
        items.push_back(std::move(it));
    }

    // A flattenable inline element: open marker, its content, close marker.
    void inlineBox(LayoutNode* el, const InlineCtx& parent) {
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
        const size_t boxIndex = boxes.size();
        boxes.push_back(ib);

        auto marker = [&](int edge, float width) {
            IFCItem m{};
            m.width = width;
            m.node = el;
            m.owner = parent.box;
            m.edge = edge;
            // The element's leaded box sits on every line it touches, an
            // empty one included (CSS2 §10.8).
            m.above = c.strut.above;
            m.below = c.strut.below;
            m.baseline = c.strut.ascent;
            m.shift = c.shift;
            items.push_back(std::move(m));
        };
        const LayoutBox& b = el->box;
        boxes[boxIndex].open = items.size();
        marker(+1, b.margin.left + b.border.left + b.padding.left);
        children(el, c);
        boxes[boxIndex].close = items.size();
        marker(-1, b.margin.right + b.border.right + b.padding.right);
    }

    void children(LayoutNode* parent, const InlineCtx& c) {
        for (auto* child : getLayoutChildren(parent)) {
            if (child->isTextNode()) { text(child, c); continue; }
            const std::string& d = styleVal(child, Prop::Display);
            if (d == "none") { clearHiddenBox(child); continue; }
            if (parent != block) {
                child->viewportHeight = parent->viewportHeight;
                child->availableHeight = parent->availableHeight;
            }
            const std::string& cp = styleVal(child, Prop::Position);
            if (cp == "absolute" || cp == "fixed") {
                // Out of flow: no item, but remember where in the item
                // sequence it sat so its static position can be read off the
                // line that ends up carrying that item. See
                // LayoutNode::staticPosX.
                pendingStatic.push_back({child, items.size(), parent});
                continue;
            }
            if (isBr(child)) {
                child->box = LayoutBox{};
                // Natural font line-height so getBoundingClientRect returns
                // the inline content height for the <br>, not 0.
                child->box.contentRect.height =
                    metrics.lineHeight(*c.family, c.fontSize, *c.weight);
                IFCItem brit{};
                brit.height = c.strut.lineHeight;
                brit.above = c.strut.above;
                brit.below = c.strut.below;
                brit.baseline = c.strut.ascent;
                brit.shift = c.shift;
                brit.node = child;
                brit.owner = parent;
                brit.forceBreak = true;
                items.push_back(std::move(brit));
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
};

// Where an item's box starts, in the block's content coordinates, once
// placed: text runs and markers carry it in placedX; elements in their box.
float itemLeft(const IFCItem& it) { return it.placedX; }

} // namespace

void layoutBlockInlineContent(LayoutNode* node, float childAvailable, float fontSize,
                              TextMetrics& metrics, float& cursorY) {
    const std::string& whiteSpace = styleVal(node, Prop::WhiteSpace);
    // CSS2 §10.8: every line box starts with the block's strut (see
    // computeStrut for the Blink-compatible half-leading model).
    InlineCtx blockCtx = makeCtx(node, fontSize, metrics);
    const StrutMetrics& strut = blockCtx.strut;
    const float strutAscent = strut.ascent;
    const float strutAbove = strut.above;
    const float strutBelow = strut.below;

    std::vector<IFCItem> items;
    std::vector<InlineBox> boxes;
    std::vector<ItemCollector::PendingStatic> pendingStatic;
    ItemCollector collect{node, childAvailable, metrics, items, boxes, pendingStatic};
    collect.children(node, blockCtx);

    auto isContent = [&](size_t i) { return items[i].edge == 0; };

    // Drop trailing collapsible whitespace synthetic items (markers after
    // them don't protect them: they collapse against the end all the same).
    // Dropped in place, so the markers keep their indices.
    for (size_t i = items.size(); i-- > 0;) {
        if (!isContent(i)) continue;
        if (isSpaceItem(items[i])) {
            items[i].dropped = true;
            items[i].width = 0.0f;
            continue;
        }
        break;
    }
    // Strip leading whitespace from the first text run (collapses against the
    // IFC's start, matching Chromium).
    for (size_t i = 0; i < items.size(); ++i) {
        if (!isContent(i)) continue;
        IFCItem& front = items[i];
        if (!front.isElement && !front.forceBreak && !front.text.empty() &&
            front.text.front() == ' ') {
            front.text.erase(front.text.begin());
            front.width = front.text.empty() ? 0.0f
                                             : std::max(0.0f, front.width - front.spaceW);
        }
        break;
    }

    // Resolve text-align for line positioning
    const std::string& textAlign = styleVal(node, Prop::TextAlign);
    const std::string& direction = styleVal(node, Prop::Direction);
    std::string resolvedAlign = textAlign;
    if (resolvedAlign == "start" || resolvedAlign.empty()) {
        resolvedAlign = (direction == "rtl") ? "right" : "left";
    } else if (resolvedAlign == "end") {
        resolvedAlign = (direction == "rtl") ? "left" : "right";
    }

    // Build line boxes first, then position with alignment.
    //
    // Wrapping is width-driven but must only land on a real break
    // opportunity: whitespace on one side of the boundary or a canBreak flag
    // from the word-boundary splitter. When a width overflow would cut an
    // atomic unit (e.g. "Citadel" + "." across an inline boundary with no
    // intervening space), retreat to the most recent break opportunity on
    // the line. Markers are transparent to that test — the content on either
    // side decides — except that a line never ends just after an element's
    // open marker nor starts just before its close marker, which would strand
    // the element's padding on a line of its own.
    auto itemEndsInSpace = [](const IFCItem& it) {
        return it.canBreakAfter ||
               (!it.text.empty() && std::isspace(static_cast<unsigned char>(it.text.back())));
    };
    auto itemStartsInSpace = [](const IFCItem& it) {
        return it.canBreakBefore ||
               (!it.text.empty() && std::isspace(static_cast<unsigned char>(it.text.front())));
    };
    auto canBreakBetween = [&](size_t prev, size_t next) {
        if (prev >= items.size() || next >= items.size()) return true;
        if (items[prev].edge == +1 || items[next].edge == -1) return false;
        size_t p = prev;
        while (p > 0 && items[p].edge != 0) --p;
        size_t n = next;
        while (n + 1 < items.size() && items[n].edge != 0) ++n;
        const bool pContent = items[p].edge == 0;
        const bool nContent = items[n].edge == 0;
        if (!pContent || !nContent) return true;   // only markers on that side
        return itemEndsInSpace(items[p]) || itemStartsInSpace(items[n]);
    };

    struct LineBounds {
        size_t start; size_t end; float totalWidth;
        float maxHeight = 0;   // final line box height
        float above = 0;       // distance from line top to the baseline
    };
    std::vector<LineBounds> lines;

    // Resolve text-indent up front: the first line's usable width is reduced
    // (or, for a negative indent, extended) by it, and the positioning pass
    // below offsets the first line's start by it. An inside list marker
    // occupies the start of the first line the same way.
    float textIndent = resolveLength(styleVal(node, Prop::TextIndent),
                                     childAvailable, fontSize) +
                       insideMarkerInlineSize(node, fontSize, metrics);
    {
        size_t lineStart = 0;
        float cursorX = 0;
        auto emitLine = [&](size_t endIdx) {
            float w = 0;
            for (size_t k = lineStart; k < endIdx; ++k) w += items[k].width;
            lines.push_back({lineStart, endIdx, w});
            lineStart = endIdx;
            cursorX = 0;
        };
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].forceBreak) {
                // <br>: terminate line after including the break marker so it
                // advances cursorY by a full line even if the line was empty.
                lines.push_back({lineStart, i + 1, cursorX});
                lineStart = i + 1;
                cursorX = 0;
                continue;
            }
            float lineAvail = lines.empty() ? childAvailable - textIndent
                                            : childAvailable;
            // A collapsible space never forces a wrap: trailing whitespace
            // "hangs" past the line edge (CSS Text white-space processing)
            // rather than moving to the next line. It is still added to
            // cursorX so an interior space counts toward the next word's fit
            // test; the trailing-space trim below zeroes it for the final
            // line width.
            bool curIsSpace = isSpaceItem(items[i]) || items[i].dropped;
            if (whiteSpace != "nowrap" && !curIsSpace && cursorX > 0 &&
                cursorX + items[i].width > lineAvail + kFitSlack) {
                // Find the latest in-range break point at or before i.
                size_t breakIdx = i;
                while (breakIdx > lineStart && !canBreakBetween(breakIdx - 1, breakIdx)) {
                    --breakIdx;
                }
                if (breakIdx == lineStart) {
                    // No valid break — let the line overflow rather than
                    // split an atomic unit. An open marker goes with its
                    // content; a close marker never starts a line.
                    breakIdx = i;
                    while (breakIdx > lineStart && items[breakIdx - 1].edge == +1)
                        --breakIdx;
                    if (items[i].edge == -1) breakIdx = lineStart;
                }
                if (breakIdx > lineStart) {
                    emitLine(breakIdx);
                    for (size_t k = breakIdx; k < i; ++k) cursorX += items[k].width;
                }
            }
            cursorX += items[i].width;
        }
        if (lineStart < items.size()) {
            lines.push_back({lineStart, items.size(), cursorX});
        }
    }

    // Collapsible whitespace at a soft-wrap boundary is removed: a run that
    // starts a wrapped line drops its leading space and a run that ends any
    // line drops its trailing space (CSS Text white-space processing).
    // Markers at the line edge are looked through. Adjust the line's width so
    // alignment math matches.
    if (whiteSpace != "pre" && whiteSpace != "pre-wrap") {
        for (auto& line : lines) {
            if (line.start >= line.end) continue;
            size_t f = line.start;
            while (f < line.end && items[f].edge != 0) ++f;
            if (f < line.end) {
                IFCItem& first = items[f];
                if (!first.isElement && !first.forceBreak) {
                    while (!first.text.empty() && first.text.front() == ' ') {
                        first.text.erase(first.text.begin());
                        float cut = first.text.empty() ? first.width
                                                       : std::min(first.width, first.spaceW);
                        first.width -= cut;
                        line.totalWidth = std::max(0.0f, line.totalWidth - cut);
                    }
                }
            }
            size_t l = line.end;
            while (l > line.start && (items[l - 1].edge != 0 || items[l - 1].dropped)) --l;
            if (l > line.start) {
                IFCItem& last = items[l - 1];
                if (!last.isElement && !last.forceBreak) {
                    while (!last.text.empty() && last.text.back() == ' ') {
                        last.text.pop_back();
                        float cut = last.text.empty() ? last.width
                                                      : std::min(last.width, last.spaceW);
                        last.width -= cut;
                        line.totalWidth = std::max(0.0f, line.totalWidth - cut);
                    }
                }
            }
        }
    }

    // Resolve each line's vertical geometry (CSS2 §10.8): baseline-align the
    // items, take the union of their [baseline-above, baseline+below] extents
    // together with the block's strut. vertical-align: middle centers a box on
    // baseline + xHeight/2 and its extent participates in the union; a
    // top/bottom-aligned item that is still taller than the line grows it
    // downward (the baseline does not move).
    const float strutXHeight = strut.xHeight;
    for (auto& line : lines) {
        float above = strutAbove;
        float below = strutBelow;
        for (size_t k = line.start; k < line.end; ++k) {
            const IFCItem& it = items[k];
            if (it.valign == 0) {
                above = std::max(above, it.above + it.shift);
                below = std::max(below, it.below - it.shift);
            } else if (it.valign == 2) {
                float a = it.height * 0.5f + strutXHeight * 0.5f;
                above = std::max(above, a);
                below = std::max(below, it.height - a);
            }
        }
        for (size_t k = line.start; k < line.end; ++k) {
            const IFCItem& it = items[k];
            if (it.valign != 1 && it.valign != 3) continue;
            if (it.height > above + below) below = it.height - above;
        }
        line.above = above;
        line.maxHeight = above + below;
    }

    // Position items per line with text-align offset.
    for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
        auto& line = lines[lineIdx];

        // Any out-of-flow child that sat before this line's last item starts
        // here as far as the flow is concerned: a block-level hypothetical box
        // would have broken the line, and an inline-level one keeps the line's
        // left edge.
        for (auto& ps : pendingStatic) {
            if (ps.node && ps.itemIndex < line.end) {
                ps.node->staticPosX = 0.0f;
                ps.node->staticPosY = cursorY;
                ps.node->staticPosPass = currentLayoutPass();
                ps.itemIndex = static_cast<size_t>(-1);   // placed
            }
        }
        bool isLastLine = (lineIdx == lines.size() - 1);
        float extraSpace = childAvailable - line.totalWidth;
        if (lineIdx == 0) extraSpace -= textIndent;
        float xOffset = 0;
        float spaceExtra = 0;
        if (extraSpace > 0) {
            if (resolvedAlign == "center") xOffset = extraSpace / 2.0f;
            else if (resolvedAlign == "right" || resolvedAlign == "end") xOffset = extraSpace;
            else if (resolvedAlign == "justify" && !isLastLine) {
                bool endsForced = line.end > line.start && items[line.end - 1].forceBreak;
                if (!endsForced) {
                    size_t nGaps = 0;
                    for (size_t k = line.start; k < line.end; ++k)
                        if (isSpaceItem(items[k])) ++nGaps;
                    if (nGaps > 0) spaceExtra = extraSpace / static_cast<float>(nGaps);
                }
            }
        }
        float cursorX = xOffset;
        if (lineIdx == 0) cursorX += textIndent;

        float lineBaseline = cursorY + line.above;

        // Bidi visual reordering (UAX #9). The line's items are in logical
        // order; reordering turns that into the order they are painted in.
        // Two things decide an item's embedding level: the characters it
        // contains, resolved over the whole line by the metrics consumer, and
        // its own `direction` when that opposes the base.
        const bool rtlBase = (direction == "rtl");
        std::vector<size_t> order;
        order.reserve(line.end - line.start);
        {
            // Only content takes part in reordering. An element's open and
            // close markers are not characters: handed to bidi as excluded
            // items they would pin logical slots and split one directional
            // run in two. They follow the content they are next to instead —
            // an open marker (and anything empty after it) goes before the
            // next content item, a close marker after the previous one.
            std::vector<size_t> content;
            std::vector<BidiItem> bidiItems;
            std::vector<std::vector<size_t>> before, after;
            std::vector<size_t> pending;   // markers waiting for content
            for (size_t i = line.start; i < line.end; i++) {
                const IFCItem& it = items[i];
                if (it.edge != 0 || it.dropped) {
                    if (it.edge == +1 || !pending.empty() || content.empty())
                        pending.push_back(i);
                    else
                        after.back().push_back(i);
                    continue;
                }
                content.push_back(i);
                before.push_back(std::move(pending));
                pending.clear();
                after.emplace_back();
                BidiItem bi;
                bi.excluded = it.forceBreak;
                if (!it.forceBreak && it.node) {
                    if (it.isElement) bi.subtree = it.node;
                    else              bi.text = it.text;
                    bi.opposesBase =
                        ((styleVal(it.node, Prop::Direction) == "rtl") != rtlBase);
                }
                bidiItems.push_back(bi);
            }
            if (content.empty()) {
                order = pending;
            } else {
                for (size_t p : pending) after.back().push_back(p);
                for (int v : visualOrderForLine(bidiItems, rtlBase, metrics)) {
                    const size_t c = static_cast<size_t>(v);
                    order.insert(order.end(), before[c].begin(), before[c].end());
                    order.push_back(content[c]);
                    order.insert(order.end(), after[c].begin(), after[c].end());
                }
            }
        }

        for (size_t oi = 0; oi < order.size(); oi++) {
            size_t i = order[oi];
            auto& item = items[i];
            item.placedBaseline = lineBaseline;
            item.placedX = cursorX;
            item.placed = true;
            if (item.forceBreak) {
                if (item.node) {
                    // Position the <br> at line-end x with its natural font
                    // box hung on the line's baseline so its rect matches
                    // Chromium's getBoundingClientRect.
                    float brY = lineBaseline - item.baseline - item.shift;
                    if (brY < cursorY) brY = cursorY;
                    item.node->box.contentRect.x = cursorX;
                    item.node->box.contentRect.y = brY;
                    item.node->box.contentRect.width = 0;
                }
                continue;
            }
            if (item.edge != 0) {
                cursorX += item.width;
                continue;
            }
            if (item.dropped) {
                item.placed = false;
                continue;
            }
            if (item.node) {
                if (item.isElement) {
                    // Vertical position: align baselines by default;
                    // vertical-align top/middle/bottom position the margin
                    // box against the line box instead.
                    float yTop;
                    switch (item.valign) {
                        case 1: yTop = cursorY; break;
                        case 2: yTop = lineBaseline -
                            (item.height * 0.5f + strutXHeight * 0.5f); break;
                        case 3: yTop = cursorY + line.maxHeight - item.height; break;
                        default: yTop = lineBaseline - item.baseline - item.shift; break;
                    }
                    item.node->box.contentRect.x = cursorX + item.node->box.margin.left +
                        item.node->box.padding.left + item.node->box.border.left;
                    if (item.baselineFromContent && item.valign == 0) {
                        // Non-replaced inline laid out whole: `baseline` is
                        // measured from the content top — padding/border sit
                        // outside the line geometry and don't shift it.
                        item.node->box.contentRect.y = yTop;
                    } else {
                        item.node->box.contentRect.y = yTop + item.node->box.margin.top +
                            item.node->box.padding.top + item.node->box.border.top;
                    }
                } else {
                    // Record the placed run so caret/selection geometry
                    // queries can map DOM offsets back to (x, y, w, h).
                    PlacedTextRun placed;
                    placed.x = cursorX;
                    placed.y = lineBaseline - item.baseline - item.shift;
                    placed.width = item.width;
                    placed.height = item.height;
                    placed.text = item.text;
                    placed.srcStart = item.srcStart;
                    placed.srcEnd = item.srcEnd;
                    auto& trs = item.node->box.textRuns;
                    auto& cr = item.node->box.contentRect;
                    if (trs.empty()) {
                        cr = {placed.x, placed.y, placed.width, placed.height};
                    } else {
                        float left = std::min(cr.x, placed.x);
                        float top  = std::min(cr.y, placed.y);
                        float right = std::max(cr.x + cr.width, placed.x + placed.width);
                        float bottom = std::max(cr.y + cr.height, placed.y + placed.height);
                        cr = {left, top, right - left, bottom - top};
                    }
                    trs.push_back(std::move(placed));
                }
            }
            cursorX += item.width;
            if (spaceExtra != 0 && isSpaceItem(item)) cursorX += spaceExtra;
        }
        {
            float lineLeft = lineIdx == 0 ? textIndent : 0.0f;
            node->box.lineBoxes.push_back(
                {cursorY, line.maxHeight, lineLeft, childAvailable - lineLeft});
        }
        cursorY += line.maxHeight;
    }

    // Trailing out-of-flow children — nothing followed them, so they start
    // below the last line.
    for (auto& ps : pendingStatic) {
        if (ps.itemIndex == static_cast<size_t>(-1)) continue;
        ps.node->staticPosX = 0.0f;
        ps.node->staticPosY = cursorY;
        ps.node->staticPosPass = currentLayoutPass();
    }

    // The block's own baseline: the last line box's baseline, measured from
    // the content top (used when this block is an inline-block child of
    // another IFC).
    if (!lines.empty()) {
        node->box.baselineOffset =
            (cursorY - lines.back().maxHeight) + lines.back().above;
    }

    if (boxes.empty()) return;

    // Each flattened inline element's box: the union of its fragments. Its
    // own font strip on every line it touches (baseline - ascent to baseline
    // + descent — Chromium's inline fragment geometry) spanning from where
    // its content starts to where it ends; content taller than the strip
    // (an inline-block, a bigger font) overflows the box without growing it.
    for (auto& b : boxes) {
        const IFCItem& open = items[b.open];
        const IFCItem& close = items[b.close];
        float left = open.placedX + open.width;
        float right = close.placedX;
        float top = open.placedBaseline - b.shift - b.ascent;
        float bottom = close.placedBaseline - b.shift + b.descent;
        for (size_t i = b.open + 1; i < b.close; ++i) {
            const IFCItem& it = items[i];
            if (!it.placed || it.forceBreak) continue;
            float l = itemLeft(it);
            float r = l + it.width;
            if (it.edge != 0) { l = r = it.placedX; }
            left = std::min(left, l);
            right = std::max(right, r);
            top = std::min(top, it.placedBaseline - b.shift - b.ascent);
            bottom = std::max(bottom, it.placedBaseline - b.shift + b.descent);
        }
        if (right < left) right = left;
        b.x = left;
        b.y = top;
        b.w = right - left;
        b.h = bottom - top;
    }

    // Rebase everything inside an inline element into that element's content
    // coordinates (and the element itself into its parent's). Positions were
    // all written in the block's content coordinates above.
    std::unordered_map<const LayoutNode*, const InlineBox*> boxOf;
    boxOf.reserve(boxes.size());
    for (const auto& b : boxes) boxOf[b.node] = &b;
    auto originOf = [&](const LayoutNode* owner, float& ox, float& oy) {
        ox = oy = 0.0f;
        if (owner == node) return;
        auto it = boxOf.find(owner);
        if (it == boxOf.end()) return;
        ox = it->second->x;
        oy = it->second->y;
    };
    for (auto& b : boxes) {
        float ox, oy;
        originOf(b.owner, ox, oy);
        b.node->box.contentRect = {b.x - ox, b.y - oy, b.w, b.h};
        // The strip hangs its ascent above the baseline by construction; a
        // parent line aligns this element by it.
        b.node->box.baselineOffset = b.ascent;
    }
    // Text nodes collect several runs; shift each node once.
    std::unordered_map<const LayoutNode*, bool> shifted;
    for (const auto& it : items) {
        if (!it.node || it.edge != 0 || it.owner == node || !it.placed) continue;
        if (shifted.count(it.node)) continue;
        shifted[it.node] = true;
        float ox, oy;
        originOf(it.owner, ox, oy);
        auto& box = it.node->box;
        box.contentRect.x -= ox;
        box.contentRect.y -= oy;
        for (auto& run : box.textRuns) { run.x -= ox; run.y -= oy; }
    }
    for (auto& ps : pendingStatic) {
        if (ps.owner == node) continue;
        float ox, oy;
        originOf(ps.owner, ox, oy);
        ps.node->staticPosX -= ox;
        ps.node->staticPosY -= oy;
    }
}

} // namespace htmlayout::layout
