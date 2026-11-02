#include "layout/inline.h"
#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/line_clamp.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include "layout/text.h"
#include "layout/bidi_line.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

bool tryLayoutInlineBlockOrReplaced(LayoutNode* node, float availableWidth,
                                    float fontSize, TextMetrics& metrics) {
    const std::string& fontFamily = styleVal(node, Prop::FontFamily);
    const std::string& fontWeight = styleVal(node, Prop::FontWeight);
    const std::string& whiteSpace = styleVal(node, Prop::WhiteSpace);
    const std::string& lineHeightVal = styleVal(node, Prop::LineHeight);
    const std::string& direction = styleVal(node, Prop::Direction);
    bool isRtl = (direction == "rtl");
    const std::string& display = styleVal(node, Prop::Display);

    // An inline-block with block-level children is a full block formatting
    // context: floats, clear/clearance, and margin collapsing all apply
    // inside it. Delegate to the block engine (which shrink-to-fits an
    // auto-width inline-block) instead of the simple vertical stacker below.
    if (display == "inline-block") {
        float iw = 0, ih = 0;
        bool intrinsic = node->intrinsicSize(iw, ih, availableWidth);
        bool hasBlockChild = false;
        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            const std::string& d = styleVal(child, Prop::Display);
            if (d == "none") continue;
            const std::string& cp = styleVal(child, Prop::Position);
            if (cp == "absolute" || cp == "fixed") continue;
            if (d != "inline" && d != "inline-block" &&
                d != "inline-flex" && d != "inline-grid")
                hasBlockChild = true;
        }
        // Line clamping lives in the block engine, which counts the line
        // boxes it builds; a clamped inline-block (-webkit-inline-box
        // included) goes there too, as does one clamped last pass, whose
        // clamp has to be undone. The text-overflow ellipsis works on the
        // same line boxes, so an ellipsizing inline-block goes there too.
        if (!intrinsic && (hasBlockChild || lineClampApplies(node) ||
                           textOverflowApplies(node))) {
            layoutBlock(node, availableWidth, metrics);
            return true;
        }
    }

    // Fresh layout pass: forget any baseline recorded by a previous layout.
    node->box.baselineOffset = -1.0f;
    node->box.inlineExtentAbove = -1.0f;
    node->box.inlineExtentBelow = -1.0f;

    // Resolve margin, padding, border for the node itself
    node->box.margin = resolveEdges(node, kMarginProps, availableWidth, fontSize);
    node->box.padding = resolveEdges(node, kPaddingProps, availableWidth, fontSize);
    node->box.border = resolveBorders(node, availableWidth, fontSize);

    float paddingH = node->box.padding.left + node->box.padding.right;
    float borderH = node->box.border.left + node->box.border.right;

    // min-width / max-width as content-box limits (max wins over nothing,
    // min wins over max, CSS2 §10.4).
    auto clampInlineBlockWidth = [&](float w) {
        const bool borderBox = styleVal(node, Prop::BoxSizing) == "border-box";
        const std::string& maxV = styleVal(node, Prop::MaxWidth);
        if (!maxV.empty() && maxV != "none" && maxV != "auto") {
            float maxW = resolveLength(maxV, availableWidth, fontSize);
            if (borderBox) maxW -= paddingH + borderH;
            if (w > maxW) w = maxW;
        }
        const std::string& minV = styleVal(node, Prop::MinWidth);
        if (!minV.empty() && minV != "auto" && minV != "none") {
            float minW = resolveLength(minV, availableWidth, fontSize);
            if (borderBox) minW -= paddingH + borderH;
            if (w < minW) w = minW;
        }
        return w < 0.0f ? 0.0f : w;
    };

    // Check for intrinsic size (replaced elements like <input>)
    float intrW = 0, intrH = 0;
    bool hasIntrinsic = node->intrinsicSize(intrW, intrH, availableWidth - paddingH - borderH);

    // For inline-block or inline replaced elements: resolve explicit width/height
    if (display != "inline-block" && !hasIntrinsic) {
        return false;
    }

    float specW = resolveLength(styleVal(node, Prop::Width), availableWidth, fontSize);
    const std::string& widthVal = styleVal(node, Prop::Width);
    const std::string& heightVal = styleVal(node, Prop::Height);
    // A percentage height on an inline-block / inline replaced element
    // resolves against the containing block's *definite* height, propagated
    // here as node->availableHeight (the same value the block path uses).
    // When that height is indefinite (<= 0), CSS treats a percentage height
    // as auto (CSS2 §10.5) — fall back to the intrinsic height below rather
    // than collapsing the box to 0. Previously the percentage basis was
    // hard-coded to 0, so e.g. an inline <iframe height:100%> vanished.
    float heightRef = node->availableHeight;
    bool heightPctIndefinite = !heightVal.empty() && heightVal.back() == '%' &&
                               heightRef <= 0.0f;
    float specH = resolveLength(heightVal, heightRef, fontSize);

    float contentAvail = availableWidth - paddingH - borderH;

    if (widthVal != "auto" && !widthVal.empty()) {
        const std::string& boxSizing = styleVal(node, Prop::BoxSizing);
        if (boxSizing == "border-box") {
            node->box.contentRect.width = specW - paddingH - borderH;
            if (node->box.contentRect.width < 0) node->box.contentRect.width = 0;
        } else {
            node->box.contentRect.width = specW;
        }
        contentAvail = node->box.contentRect.width;
    } else if (hasIntrinsic) {
        node->box.contentRect.width = intrW;
        contentAvail = intrW;
    } else if (display == "inline-block") {
        // Shrink-to-fit (CSS2 §10.3.9): min(max-content,
        // max(min-content, available)). Without this, block-level
        // children (divs) laid out at full `contentAvail` would expand
        // the inline-block to parent width, breaking horizontal flow of
        // sibling inline-blocks.
        float maxContent = computeMaxContentWidth(node, metrics);
        float minContent = computeMinContentWidth(node, metrics);
        float fitAvail = std::min(maxContent, std::max(minContent, contentAvail));
        if (fitAvail < 0) fitAvail = 0;
        contentAvail = fitAvail;
        node->box.contentRect.width = fitAvail;
    }
    // min-width / max-width clamp the used content width of an
    // inline-block whatever sized it (CSS2 §10.4); the lines inside are
    // then broken against the clamped width.
    if (display == "inline-block" && !hasIntrinsic) {
        float clamped = clampInlineBlockWidth(node->box.contentRect.width);
        node->box.contentRect.width = clamped;
        contentAvail = clamped;
    }

    if (heightVal != "auto" && !heightVal.empty() && !heightPctIndefinite) {
        const std::string& boxSizing = styleVal(node, Prop::BoxSizing);
        float paddingV = node->box.padding.top + node->box.padding.bottom;
        float borderV = node->box.border.top + node->box.border.bottom;
        if (boxSizing == "border-box") {
            node->box.contentRect.height = specH - paddingV - borderV;
            if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
        } else {
            node->box.contentRect.height = specH;
        }
    } else if (hasIntrinsic) {
        node->box.contentRect.height = intrH;
    }

    // CSS 2.1 §10.6.2: on a replaced element with an intrinsic ratio, an
    // `auto` dimension is derived from the *used* value of the other one —
    // not from the raw intrinsic size. So `img { width: 16px }` on a
    // 512×512 icon is 16×16, where taking intrinsic height directly makes
    // it 16×512: a sliver of the artwork stretched half a page down.
    //
    // The block path already does this (block.cpp, the auto-height branch);
    // an atomic inline is the same element in a different formatting
    // context and has to agree. Gated on hasIntrinsicRatio() so only
    // ratio-locked media qualify — a form control reports an intrinsic size
    // without a locked ratio, and must keep its own height when given a
    // width.
    const bool widthIsAuto  = (widthVal == "auto" || widthVal.empty());
    const bool heightIsAuto = (heightVal == "auto" || heightVal.empty() ||
                               heightPctIndefinite);
    const float intrinsicRatio =
        (intrW > 0.0f && intrH > 0.0f) ? (intrW / intrH) : 0.0f;
    if (hasIntrinsic && intrinsicRatio > 0.0f && node->hasIntrinsicRatio()) {
        if (!widthIsAuto && heightIsAuto) {
            node->box.contentRect.height =
                node->box.contentRect.width / intrinsicRatio;
        } else if (widthIsAuto && !heightIsAuto) {
            node->box.contentRect.width =
                node->box.contentRect.height * intrinsicRatio;
        }
    }

    // Replaced elements with intrinsic size don't need child layout
    if (hasIntrinsic) {
        return true;
    }

    // Layout children inside inline-block
    float cursorY = 0;
    float maxContentW = 0;

    // Check if children are all inline-level
    bool ibAllInline = true;
    bool ibHasContent = false;
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) {
            std::string_view t = child->textContent();
            for (char c : t) {
                if (!std::isspace(static_cast<unsigned char>(c))) { ibHasContent = true; break; }
            }
        } else {
            auto& cs = child->computedStyle();
            const std::string& d = styleVal(child, Prop::Display);
            if (d == "none") continue;
            ibHasContent = true;
            if (d != "inline" && d != "inline-block" && d != "inline-flex" && d != "inline-grid") ibAllInline = false;
        }
    }

    if (ibHasContent && ibAllInline) {
        // Inline content in inline-block: measure text and inline children
        float ibLineHeight = resolveLineHeight(lineHeightVal, fontSize,
            fontFamily, fontWeight, metrics);
        // Font ascent for baseline bookkeeping: text runs are hung on a
        // baseline that sits half-leading + ascent below the line top.
        float ibAscent = metrics.ascent(fontFamily, fontSize, fontWeight);
        float cursorX = 0, lineMaxH = 0;
        // Pure-whitespace text nodes between inline siblings collapse to
        // one pending space advance, flushed when the next item stays on
        // the line and dropped at wraps / the line end (CSS Text §4.1).
        float ibSpaceW = metrics.measureWidth(" ", fontFamily, fontSize, fontWeight);
        float ibPending = 0.0f;
        // Per-line placement records so text-align can shift completed
        // lines afterwards (buttons center their labels via the UA
        // sheet). runIdx >= 0 indexes the child's textRuns; -1 is the
        // child's own box.
        // `advance` and `gapBefore` are what bidi reordering needs to
        // re-run the pen: this stacker folds collapsed inter-item spaces
        // into the cursor rather than emitting them as items, so a gap is
        // not recoverable from the entries' positions after the fact.
        struct IbPlaced {
            LayoutNode* child;
            int runIdx;
            float advance = 0.0f;
            float gapBefore = 0.0f;
        };
        std::vector<IbPlaced> ibLine;
        std::vector<std::pair<std::vector<IbPlaced>, float>> ibLines;
        auto ibEndLine = [&](float lineW) {
            if (!ibLine.empty()) {
                ibLines.emplace_back(std::move(ibLine), lineW);
                ibLine.clear();
            }
        };
        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) {
                float ls = resolveLength(styleVal(node, Prop::LetterSpacing), 0, fontSize);
                float ws = resolveLength(styleVal(node, Prop::WordSpacing), 0, fontSize);
                auto runs = breakTextIntoRuns(std::string(child->textContent()), contentAvail,
                    fontFamily, fontSize, fontWeight, whiteSpace, metrics,
                    "normal", "normal", ls, ws, styleVal(node, Prop::TextTransform));
                bool firstRun = true;
                int ibPrevSrcEnd = 0;
                child->box.textRuns.clear();
                if (runs.empty() && cursorX > 0 &&
                    (whiteSpace == "normal" || whiteSpace.empty())) {
                    for (char c : child->textContent()) {
                        if (std::isspace(static_cast<unsigned char>(c))) {
                            ibPending = ibSpaceW;
                            break;
                        }
                    }
                }
                for (auto& run : runs) {
                    if (run.text.empty() && run.width == 0) continue;
                    // Consecutive runs of one text node are separated by
                    // collapsed whitespace in the source; when they end up
                    // on the SAME line (the splitter packed against a
                    // fractionally different width) the space must be
                    // re-inserted, not silently dropped.
                    if (!firstRun && run.canBreakBefore)
                        ibPending = ibSpaceW + ls + ws;
                    // The run's line contribution is its LEADED box — the
                    // element's line-height — not the natural glyph
                    // height. A line-height below the font's natural
                    // height shrinks the line box and the glyphs overflow
                    // it (CSS2 §10.8); taking max() here made the natural
                    // height win and inflated the box.
                    float h = ibLineHeight;
                    float gapUsed = 0.0f;
                    if (cursorX > 0 &&
                        cursorX + ibPending + run.width > contentAvail + kFitSlack) {
                        ibEndLine(cursorX);
                        maxContentW = std::max(maxContentW, cursorX);
                        cursorY += lineMaxH;
                        cursorX = 0;
                        lineMaxH = 0;
                    } else {
                        cursorX += ibPending;
                        gapUsed = ibPending;
                    }
                    ibPending = 0;
                    // Center the font's natural box in the line via
                    // half-leading; the glyph baseline then sits at
                    // placed.y + ascent, and the box's reported baseline
                    // (for the parent line box to align against) tracks
                    // the LAST placed run — CSS2 §10.8.1 gives an
                    // inline-block the baseline of its last line box.
                    // Half-leading centers the natural box in the leaded
                    // one; negative when line-height < natural (glyphs
                    // hang out both sides). Blink floors the ascent side.
                    float halfLead = std::floor((h - run.height) * 0.5f);

                    // The collapsed whitespace between two words of one
                    // text node becomes a real run, not just cursor
                    // advance. Caret and selection geometry map DOM
                    // offsets through textRuns, so a space that exists
                    // only as a gap belongs to no run and is invisible to
                    // them: every Range spanning it lost the space's
                    // width, and offsets past it mapped into the wrong
                    // run. It is also its own line entry, so the bidi
                    // reorder and text-align passes below move it with
                    // everything else instead of leaving it behind.
                    if (gapUsed > 0.0f && run.srcStart > ibPrevSrcEnd) {
                        PlacedTextRun sp;
                        sp.x = cursorX - gapUsed;
                        sp.y = cursorY + halfLead;
                        sp.width = gapUsed;
                        sp.height = run.height;
                        sp.text = " ";
                        sp.srcStart = ibPrevSrcEnd;
                        sp.srcEnd = run.srcStart;
                        child->box.textRuns.push_back(std::move(sp));
                        ibLine.push_back({child,
                            static_cast<int>(child->box.textRuns.size()) - 1,
                            gapUsed, 0.0f});
                        gapUsed = 0.0f;   // now carried by the space item
                    }

                    PlacedTextRun placed;
                    placed.x = cursorX;
                    placed.y = cursorY + halfLead;
                    placed.width = run.width;
                    placed.height = run.height;
                    placed.text = run.text;
                    placed.srcStart = run.srcStart;
                    placed.srcEnd   = run.srcEnd;
                    float runAscent = (ibAscent > 0 && ibAscent < run.height)
                        ? ibAscent : run.height * 0.8f;
                    node->box.baselineOffset = placed.y + runAscent;
                    // Record first run's position on contentRect so the
                    // draw traversal knows the text node's origin.
                    if (firstRun) {
                        child->box.contentRect.x = placed.x;
                        child->box.contentRect.y = placed.y;
                        child->box.contentRect.width = placed.width;
                        child->box.contentRect.height = placed.height;
                        firstRun = false;
                    } else {
                        // Extend to union of placed runs.
                        float right = std::max(
                            child->box.contentRect.x + child->box.contentRect.width,
                            placed.x + placed.width);
                        float bottom = std::max(
                            child->box.contentRect.y + child->box.contentRect.height,
                            placed.y + placed.height);
                        child->box.contentRect.width  = right  - child->box.contentRect.x;
                        child->box.contentRect.height = bottom - child->box.contentRect.y;
                    }
                    child->box.textRuns.push_back(std::move(placed));
                    ibLine.push_back({child,
                        static_cast<int>(child->box.textRuns.size()) - 1,
                        run.width, gapUsed});
                    ibPrevSrcEnd = run.srcEnd;
                    cursorX += run.width;
                    lineMaxH = std::max(lineMaxH, h);
                    // Hard line break preserved by white-space: pre /
                    // pre-wrap / pre-line — advance to the next line.
                    if (run.forceBreakAfter) {
                        ibEndLine(cursorX);
                        maxContentW = std::max(maxContentW, cursorX);
                        cursorY += lineMaxH;
                        cursorX = 0;
                        lineMaxH = 0;
                    }
                }
            } else if ((child->tagName() == "br" || child->tagName() == "BR")) {
                // Forced line break: end current line, start a new one.
                float brH = std::max(ibLineHeight,
                    metrics.lineHeight(fontFamily, fontSize, fontWeight));
                if (cursorX == 0 && lineMaxH == 0) lineMaxH = brH;
                ibEndLine(cursorX);
                maxContentW = std::max(maxContentW, cursorX);
                cursorY += lineMaxH;
                cursorX = 0;
                lineMaxH = 0;
                ibPending = 0;
                child->box.contentRect = {};
            } else {
                auto& cs = child->computedStyle();
                if (styleVal(child, Prop::Display) == "none") { clearHiddenBox(child); continue; }
                const std::string& childPos = styleVal(child, Prop::Position);
                if (childPos == "absolute" || childPos == "fixed") {
                    // Out of flow: record the static position it would have
                    // had (LayoutNode::staticPosX). A block-level box breaks
                    // the line, so it starts below whatever is on this one.
                    child->staticPosX = 0.0f;
                    child->staticPosY = cursorY + (cursorX > 0 ? lineMaxH : 0.0f);
                    child->staticPosPass = currentLayoutPass();
                    continue;
                }
                layoutNode(child, contentAvail, metrics);
                float cw = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
                float ch = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;
                float gapUsed = 0.0f;
                if (cursorX > 0 && cursorX + ibPending + cw > contentAvail + kFitSlack) {
                    ibEndLine(cursorX);
                    maxContentW = std::max(maxContentW, cursorX);
                    cursorY += lineMaxH;
                    cursorX = 0;
                    lineMaxH = 0;
                } else {
                    cursorX += ibPending;
                    gapUsed = ibPending;
                }
                ibPending = 0;
                child->box.contentRect.x = cursorX + child->box.margin.left +
                    child->box.padding.left + child->box.border.left;
                child->box.contentRect.y = cursorY + child->box.margin.top +
                    child->box.padding.top + child->box.border.top;
                ibLine.push_back({child, -1, cw, gapUsed});
                cursorX += cw;
                // The line still includes the block's strut (CSS2 §10.8):
                // a line of only atomic inlines shorter than the
                // line-height must not shrink the line box below it.
                lineMaxH = std::max(lineMaxH, std::max(ch, ibLineHeight));
                // CSS2 §10.8.1: this box's own baseline is its LAST line
                // box's baseline. This simplified stacker keeps lines on
                // the strut baseline — half-leading + ascent below the
                // line top — so the parent line box aligns us there
                // rather than at the bottom margin edge.
                {
                    float ibNatural = metrics.lineHeight(fontFamily, fontSize, fontWeight);
                    if (ibNatural <= 0) ibNatural = fontSize * 1.2f;
                    float asc = (ibAscent > 0 && ibAscent < ibNatural)
                        ? ibAscent : ibNatural * 0.8f;
                    float half = std::floor((ibLineHeight - ibNatural) * 0.5f);
                    node->box.baselineOffset = cursorY + asc + half;
                }
            }
        }
        if (cursorX > 0) {
            ibEndLine(cursorX);
            maxContentW = std::max(maxContentW, cursorX);
            cursorY += lineMaxH;
        }

        // Bidi visual reordering, for the same reason the block IFCs and
        // the inline path above do it — and it was missing here, so
        // `<button><span>שלום</span> <span>עולם</span></button>` kept its
        // labels in logical order while the identical markup in a <div>
        // did not.
        //
        // This stacker places items as it walks them instead of building a
        // line and then positioning it, so there was no line to reorder.
        // There is one now: `ibLines` already records each line's entries
        // for the text-align pass below. Reordering re-runs the pen over
        // those entries in visual order, which is the same edit the align
        // pass makes — it rewrites x on a placed run or box — just with a
        // per-item offset rather than one shift for the whole line.
        //
        // Collapsed spaces between items were folded into the cursor and
        // never became items, so each entry carries the gap that preceded
        // it and the gap travels with it. For the reversal this actually
        // performs that is the right answer: the space between two words
        // stays between them whichever order they end up in.
        for (auto& [entries, lineW] : ibLines) {
            if (entries.size() < 2) continue;
            std::vector<BidiItem> bidiItems;
            bidiItems.reserve(entries.size());
            for (auto& e : entries) {
                BidiItem bi;
                if (e.runIdx >= 0) {
                    bi.text = e.child->box.textRuns[
                        static_cast<size_t>(e.runIdx)].text;
                } else {
                    bi.subtree = e.child;
                    bi.opposesBase =
                        ((styleVal(e.child, Prop::Direction) == "rtl") != isRtl);
                }
                bidiItems.push_back(bi);
            }
            std::vector<int> visual = visualOrderForLine(bidiItems, isRtl, metrics);
            if (visual.size() != entries.size()) continue;

            // Identity order is the overwhelmingly common case; skip it so
            // ordinary Latin content is not rewritten at all.
            bool reordered = false;
            for (size_t k = 0; k < visual.size(); ++k) {
                if (static_cast<size_t>(visual[k]) != k) { reordered = true; break; }
            }
            if (!reordered) continue;

            float pen = 0.0f;
            std::vector<IbPlaced> ordered;
            ordered.reserve(entries.size());
            for (int slot : visual) {
                IbPlaced& e = entries[static_cast<size_t>(slot)];
                pen += e.gapBefore;
                if (e.runIdx >= 0) {
                    e.child->box.textRuns[static_cast<size_t>(e.runIdx)].x = pen;
                } else {
                    // Element boxes report the CONTENT origin, so the pen
                    // (a margin-box position) has to be walked in past the
                    // left margin, border and padding — exactly as the
                    // original placement did.
                    e.child->box.contentRect.x = pen + e.child->box.margin.left +
                        e.child->box.padding.left + e.child->box.border.left;
                }
                pen += e.advance;
                ordered.push_back(e);
            }
            entries = std::move(ordered);

            // A text node's contentRect spans its runs, which have just
            // moved; re-derive it or the node's reported rect still points
            // at where the text used to be.
            for (auto& e : entries) {
                if (e.runIdx < 0) continue;
                auto& rs = e.child->box.textRuns;
                if (rs.empty()) continue;
                float minX = rs[0].x, maxX = rs[0].x + rs[0].width;
                for (auto& r : rs) {
                    minX = std::min(minX, r.x);
                    maxX = std::max(maxX, r.x + r.width);
                }
                e.child->box.contentRect.x = minX;
                e.child->box.contentRect.width = maxX - minX;
            }
        }

        // text-align inside the inline-block: shift each completed line
        // within the final content width (buttons center their labels;
        // contentAvail is the used content width for both explicit and
        // shrink-to-fit boxes, so single-line shrink-wrapped content
        // shifts by 0).
        const std::string& ibAlign = styleVal(node, Prop::TextAlign);
        if (ibAlign == "center" || ibAlign == "right" || ibAlign == "end") {
            std::vector<LayoutNode*> shiftedText;
            for (auto& [entries, lineW] : ibLines) {
                float shift = contentAvail - lineW;
                if (ibAlign == "center") shift *= 0.5f;
                if (shift <= 0.01f) continue;
                for (auto& e : entries) {
                    if (e.runIdx < 0) {
                        e.child->box.contentRect.x += shift;
                    } else {
                        e.child->box.textRuns[static_cast<size_t>(e.runIdx)].x += shift;
                        if (std::find(shiftedText.begin(), shiftedText.end(),
                                      e.child) == shiftedText.end())
                            shiftedText.push_back(e.child);
                    }
                }
            }
            // Re-derive shifted text nodes' contentRect from their runs.
            for (auto* tc : shiftedText) {
                auto& runs2 = tc->box.textRuns;
                if (runs2.empty()) continue;
                float minX = runs2[0].x, maxX = runs2[0].x + runs2[0].width;
                for (auto& r : runs2) {
                    minX = std::min(minX, r.x);
                    maxX = std::max(maxX, r.x + r.width);
                }
                tc->box.contentRect.x = minX;
                tc->box.contentRect.width = maxX - minX;
            }
        }

    } else {
        // Block children inside inline-block
        for (auto* child : getLayoutChildren(node)) {
            if (child->isTextNode()) continue;
            auto& cs = child->computedStyle();
            if (styleVal(child, Prop::Display) == "none") { clearHiddenBox(child); continue; }
            const std::string& childPos = styleVal(child, Prop::Position);
            if (childPos == "absolute" || childPos == "fixed") {
                // Out of flow: keep the position it would have stacked at.
                child->staticPosX = 0.0f;
                child->staticPosY = cursorY;
                child->staticPosPass = currentLayoutPass();
                continue;
            }
            layoutNode(child, contentAvail, metrics);
            child->box.contentRect.x = child->box.margin.left + child->box.padding.left + child->box.border.left;
            child->box.contentRect.y = cursorY + child->box.padding.top + child->box.border.top;
            float childFullW = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
            maxContentW = std::max(maxContentW, childFullW);
            cursorY += child->box.margin.top + child->box.border.top + child->box.padding.top +
                       child->box.contentRect.height +
                       child->box.padding.bottom + child->box.border.bottom + child->box.margin.bottom;
        }
    }

    if (heightVal != "auto" && !heightVal.empty()) {
        const std::string& boxSizing = styleVal(node, Prop::BoxSizing);
        float paddingV = node->box.padding.top + node->box.padding.bottom;
        float borderV = node->box.border.top + node->box.border.bottom;
        if (boxSizing == "border-box") {
            node->box.contentRect.height = specH - paddingV - borderV;
            if (node->box.contentRect.height < 0) node->box.contentRect.height = 0;
        } else {
            node->box.contentRect.height = specH;
        }
    } else {
        node->box.contentRect.height = cursorY;
    }
    centerButtonContent(node, cursorY);

    if (widthVal == "auto" || widthVal.empty()) {
        // The shrink-to-fit width decided above is the box's width: the
        // lines broken inside it do not shrink it further (a max-width
        // that wraps the text leaves the box at max-width, not at its
        // widest line). A line that still overflows (an unbreakable
        // word) widens it, as min-content would have.
        node->box.contentRect.width = clampInlineBlockWidth(
            std::max(contentAvail, maxContentW > contentAvail + kFitSlack
                                       ? maxContentW : 0.0f));
    }

    return true;
}

} // namespace htmlayout::layout
