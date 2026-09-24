// block_inline.cpp — inline formatting contexts (CSS2 §9.4.2, §10.8): the
// line builder for a block container whose in-flow children are all
// inline-level, and for each anonymous inline run of a block container that
// mixes block-level and inline-level children (the run sits in an anonymous
// block box of its own, CSS2 §9.2.1.1).
//
// Inline elements are not laid out as boxes of their own here. A non-replaced
// inline element (<span>, <b>, <a>, ...) whose content is all inline-level is
// *flattened* into the one sequence of line items (block_inline_items.cpp),
// bracketed by open and close markers carrying its inline-start and
// inline-end margin + border + padding. Lines are broken over that single
// sequence, so an inline element's text wraps where it stands — mid-line,
// after whatever precedes it — instead of being one box that has to fit whole
// or move to the next line.
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
#include "layout/block_inline_items.h"
#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include "layout/text.h"
#include "layout/bidi_line.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace htmlayout::layout {

using namespace ifc;

void layoutBlockInlineContent(LayoutNode* node, float childAvailable, float fontSize,
                              TextMetrics& metrics, float& cursorY) {
    layoutInlineRun(node, childAvailable, fontSize, metrics, cursorY, InlineRunEnv{});
}

void layoutInlineRun(LayoutNode* node, float childAvailable, float fontSize,
                     TextMetrics& metrics, float& cursorY, const InlineRunEnv& env) {
    const std::string& whiteSpace = styleVal(node, Prop::WhiteSpace);
    // CSS2 §10.8: every line box starts with the block's strut (see
    // computeStrut for the Blink-compatible half-leading model).
    InlineCtx blockCtx = makeCtx(node, fontSize, metrics);
    const StrutMetrics& strut = blockCtx.strut;
    const float strutAbove = strut.above;
    const float strutBelow = strut.below;

    std::vector<IFCItem> items;
    std::vector<InlineBox> boxes;
    std::vector<PendingStatic> pendingStatic;
    {
        ItemCollector collect{node, childAvailable, metrics, items, boxes, pendingStatic};
        collect.acceptFloats = env.anonymous;
        if (env.children) collect.children(node, *env.children, blockCtx);
        else              collect.children(node, getLayoutChildren(node), blockCtx);
    }

    auto isContent = [&](size_t i) { return items[i].edge == 0 && !items[i].isFloat; };

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
        if (!front.isElement && !front.forceBreak && !front.preserved &&
            !front.text.empty() && front.text.front() == ' ') {
            front.text.erase(front.text.begin());
            front.width = front.text.empty() ? 0.0f
                                             : std::max(0.0f, front.width - front.spaceW);
        }
        break;
    }

    // A line box resolves the collapsed margin above it; a run that makes
    // none (white space between two blocks) leaves the margin alone.
    if (env.beforeLines) {
        bool anyLine = false;
        for (const auto& it : items) {
            if (it.isFloat || it.dropped) continue;
            if (it.forceBreak || it.isElement || (it.edge != 0 && it.width > 0) ||
                (it.edge == 0 && (it.preserved ? it.width > 0
                                               : it.text.find_first_not_of(' ') !=
                                                     std::string::npos))) {
                anyLine = true;
                break;
            }
        }
        if (anyLine) env.beforeLines();
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
    // the element's padding on a line of its own. A float is not line
    // content; a line may always break beside one.
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
        if (items[p].isFloat || items[n].isFloat) return true;
        return itemEndsInSpace(items[p]) || itemStartsInSpace(items[n]);
    };

    struct LineBounds {
        size_t start; size_t end; float totalWidth;
        float maxHeight = 0;   // final line box height
        float above = 0;       // distance from line top to the baseline
        float left = 0;        // the float-free band the line was built in
        float avail = 0;
        bool endsWithBreak = false;
    };
    std::vector<LineBounds> lines;

    // Resolve each line's vertical geometry (CSS2 §10.8): baseline-align the
    // items, take the union of their [baseline-above, baseline+below] extents
    // together with the block's strut. vertical-align: middle centers a box on
    // baseline + xHeight/2 and its extent participates in the union; a
    // top/bottom-aligned item that is still taller than the line grows it
    // downward (the baseline does not move).
    const float strutXHeight = strut.xHeight;
    auto lineExtents = [&](size_t s, size_t e) {
        float above = strutAbove;
        float below = strutBelow;
        for (size_t k = s; k < e; ++k) {
            const IFCItem& it = items[k];
            if (it.isFloat) continue;
            if (it.valign == 0) {
                above = std::max(above, it.above + it.shift);
                below = std::max(below, it.below - it.shift);
            } else if (it.valign == 2) {
                float a = it.height * 0.5f + strutXHeight * 0.5f;
                above = std::max(above, a);
                below = std::max(below, it.height - a);
            }
        }
        for (size_t k = s; k < e; ++k) {
            const IFCItem& it = items[k];
            if (it.valign != 1 && it.valign != 3) continue;
            if (it.height > above + below) below = it.height - above;
        }
        return std::pair<float, float>{above, below};
    };

    // Resolve text-indent up front: the first line's usable width is reduced
    // (or, for a negative indent, extended) by it, and the positioning pass
    // below offsets the first line's start by it. An inside list marker
    // occupies the start of the first line the same way.
    float textIndent = 0.0f;
    if (env.firstFormattedLine)
        textIndent = resolveLength(styleVal(node, Prop::TextIndent), childAvailable, fontSize) +
                     insideMarkerInlineSize(node, fontSize, metrics);

    const bool canWrap = whiteSpace != "nowrap" && whiteSpace != "pre";
    {
        // Each line is built in the band the floats leave free at its top.
        float lineY = cursorY;
        auto bandAt = [&](float y) {
            if (env.band) return env.band(y, strut.lineHeight);
            return std::pair<float, float>{0.0f, childAvailable};
        };
        auto [curL, curR] = bandAt(lineY);
        size_t lineStart = 0;
        float cursorX = 0;
        auto emitLine = [&](size_t endIdx, bool brk) {
            float w = 0;
            for (size_t k = lineStart; k < endIdx; ++k)
                if (!items[k].isFloat && !items[k].forceBreak) w += items[k].width;
            auto [a, b] = lineExtents(lineStart, endIdx);
            LineBounds lb{lineStart, endIdx, w};
            lb.maxHeight = a + b;
            lb.above = a;
            lb.left = curL;
            lb.avail = curR - curL;
            lb.endsWithBreak = brk;
            lines.push_back(lb);
            if (!env.anonymous || w > 0 || brk) lineY += lb.maxHeight;
            lineStart = endIdx;
            cursorX = 0;
            auto next = bandAt(lineY);
            curL = next.first;
            curR = next.second;
        };
        auto sumWidths = [&](size_t s, size_t e) {
            float w = 0;
            for (size_t k = s; k < e; ++k)
                if (!items[k].isFloat) w += items[k].width;
            return w;
        };
        auto split = [&](size_t i, float maxW, bool mustFit) {
            return splitTextItem(items, i, maxW, mustFit, boxes, pendingStatic, metrics);
        };
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].isFloat) {
                // A float breaking into the middle of a line is placed below
                // the line under construction (its top may not sit above the
                // current line box, CSS2 §9.5.1 rule 6); at a line start it
                // sits at the line's own top and narrows that line.
                float placeY = lineY;
                if (cursorX > 0) {
                    auto [a, b] = lineExtents(lineStart, i);
                    placeY = lineY + a + b;
                }
                if (env.placeFloat) env.placeFloat(items[i].node, placeY);
                if (cursorX == 0) {
                    auto b = bandAt(lineY);
                    curL = b.first;
                    curR = b.second;
                }
                continue;
            }
            if (items[i].forceBreak) {
                // <br>: terminate line after including the break marker so it
                // advances cursorY by a full line even if the line was empty.
                emitLine(i + 1, true);
                continue;
            }
            const float lineAvail = (curR - curL) - (lines.empty() ? textIndent : 0.0f);
            // A collapsible space never forces a wrap: trailing whitespace
            // "hangs" past the line edge (CSS Text white-space processing)
            // rather than moving to the next line. It is still added to
            // cursorX so an interior space counts toward the next word's fit
            // test; the trailing-space trim below zeroes it for the final
            // line width. Preserved white space at the end of a run hangs the
            // same way.
            const bool curIsSpace = isSpaceItem(items[i]) || items[i].dropped;
            const float fitW = items[i].width - items[i].hangW;
            if (canWrap && !curIsSpace && cursorX + fitW > lineAvail + kFitSlack) {
                const int mode = items[i].breakMode;
                if (cursorX <= 0) {
                    // Alone at the start of its line and still too wide: a run
                    // that may break inside a word is cut at the line edge.
                    if (mode > 0) split(i, lineAvail, /*mustFit=*/false);
                } else {
                    // word-break: break-all — every character boundary is an
                    // opportunity, so the latest one is inside this run.
                    if (mode == 2 && split(i, lineAvail - cursorX, /*mustFit=*/true)) {
                        cursorX += items[i].width;
                        continue;
                    }
                    // Find the latest in-range break point at or before i.
                    size_t breakIdx = i;
                    while (breakIdx > lineStart && !canBreakBetween(breakIdx - 1, breakIdx))
                        --breakIdx;
                    if (breakIdx == lineStart) {
                        // No opportunity on the line: overflow-wrap lets the
                        // run break at the edge.
                        if (mode == 1 && split(i, lineAvail - cursorX, /*mustFit=*/true)) {
                            cursorX += items[i].width;
                            continue;
                        }
                        // Otherwise break at this item anyway. An open marker
                        // goes with its content; a close marker never starts
                        // a line.
                        breakIdx = i;
                        while (breakIdx > lineStart && items[breakIdx - 1].edge == +1)
                            --breakIdx;
                        if (items[i].edge == -1) breakIdx = lineStart;
                    }
                    if (breakIdx > lineStart) {
                        emitLine(breakIdx, false);
                        cursorX = sumWidths(breakIdx, i);
                        // Test item i again on the new line: it may still not
                        // fit, and may be cut.
                        --i;
                        continue;
                    }
                }
            }
            cursorX += items[i].width;
        }
        if (lineStart < items.size()) emitLine(items.size(), false);
    }

    // Collapsible whitespace at a soft-wrap boundary is removed: a run that
    // starts a wrapped line drops its leading space and a run that ends any
    // line drops its trailing space (CSS Text white-space processing).
    // Markers and floats at the line edge are looked through, and so is the
    // forced break ending a line. Adjust the line's width so alignment math
    // matches.
    for (auto& line : lines) {
        if (line.start >= line.end) continue;
        size_t f = line.start;
        while (f < line.end && !isContent(f)) ++f;
        if (f < line.end) {
            IFCItem& first = items[f];
            if (!first.isElement && !first.forceBreak && !first.preserved) {
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
        while (l > line.start && (!isContent(l - 1) || items[l - 1].dropped ||
                                  items[l - 1].forceBreak))
            --l;
        if (l > line.start) {
            IFCItem& last = items[l - 1];
            if (!last.isElement && !last.preserved) {
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

    // Position items per line with text-align offset.
    const bool rtlBase = (direction == "rtl");
    for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
        auto& line = lines[lineIdx];
        const float indent = lineIdx == 0 ? textIndent : 0.0f;

        if (env.anonymous && line.totalWidth <= 0 && !line.endsWithBreak) {
            // Nothing on this line (collapsed white space, an empty inline
            // element): no line box. Markers still need a position for the
            // element boxes below.
            for (size_t i = line.start; i < line.end; ++i) {
                items[i].placedX = line.left + indent;
                items[i].placedBaseline = cursorY + line.above;
            }
            continue;
        }

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
        float extraSpace = line.avail - indent - line.totalWidth;
        float xOffset = 0;
        float spaceExtra = 0;
        if (extraSpace > 0) {
            if (resolvedAlign == "center") xOffset = extraSpace / 2.0f;
            else if (resolvedAlign == "right" || resolvedAlign == "end") xOffset = extraSpace;
            else if (resolvedAlign == "justify" && !isLastLine && !line.endsWithBreak) {
                size_t nGaps = 0;
                for (size_t k = line.start; k < line.end; ++k)
                    if (isSpaceItem(items[k])) ++nGaps;
                if (nGaps > 0) spaceExtra = extraSpace / static_cast<float>(nGaps);
            }
        }
        float cursorX = line.left + indent + xOffset;

        float lineBaseline = cursorY + line.above;

        // Bidi visual reordering (UAX #9). The line's items are in logical
        // order; reordering turns that into the order they are painted in.
        // Two things decide an item's embedding level: the characters it
        // contains, resolved over the whole line by the metrics consumer, and
        // its own `direction` when that opposes the base.
        std::vector<size_t> order;
        order.reserve(line.end - line.start);
        {
            // Only content takes part in reordering. An element's open and
            // close markers are not characters: handed to bidi as excluded
            // items they would pin logical slots and split one directional
            // run in two. They follow the content they are next to instead,
            // on the side their element's direction puts them: a
            // left-to-right element opens before (left of) its first content
            // and closes after its last; a right-to-left element opens to the
            // right of its first content and closes to the left of its last.
            // Nested markers on one side stack outward.
            std::vector<size_t> content;
            std::vector<BidiItem> bidiItems;
            std::vector<std::vector<size_t>> before, after;
            std::vector<size_t> pendingBefore;   // left of the next content
            std::vector<size_t> pendingAfter;    // right of the next content, outermost first
            for (size_t i = line.start; i < line.end; i++) {
                const IFCItem& it = items[i];
                if (it.isFloat) continue;
                if (it.edge != 0 || it.dropped) {
                    const bool waiting = content.empty() || !pendingBefore.empty() ||
                                         !pendingAfter.empty();
                    if (it.edge == +1 && it.rtlEdge) {
                        pendingAfter.push_back(i);
                    } else if (it.edge == +1) {
                        pendingBefore.push_back(i);
                    } else if (it.edge == -1 && it.rtlEdge && !pendingAfter.empty() &&
                               items[pendingAfter.back()].node == it.node) {
                        // An empty right-to-left element: its end edge left of
                        // its start edge.
                        size_t open = pendingAfter.back();
                        pendingAfter.pop_back();
                        pendingBefore.push_back(i);
                        pendingBefore.push_back(open);
                    } else if (waiting) {
                        pendingBefore.push_back(i);
                    } else if (it.edge == -1 && it.rtlEdge) {
                        before.back().insert(before.back().begin(), i);
                    } else {
                        after.back().push_back(i);
                    }
                    continue;
                }
                content.push_back(i);
                before.push_back(std::move(pendingBefore));
                pendingBefore.clear();
                after.emplace_back(pendingAfter.rbegin(), pendingAfter.rend());
                pendingAfter.clear();
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
                order = pendingBefore;
                order.insert(order.end(), pendingAfter.rbegin(), pendingAfter.rend());
            } else {
                for (size_t p : pendingBefore) after.back().push_back(p);
                after.back().insert(after.back().end(), pendingAfter.rbegin(),
                                    pendingAfter.rend());
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
        node->box.lineBoxes.push_back(
            {cursorY, line.maxHeight, line.left + indent, line.avail - indent});
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
    if (!env.anonymous && !lines.empty()) {
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
        // The marker on the element's left side ends where its content
        // starts; the one on its right side starts where its content ends.
        const IFCItem& leftMark = items[b.rtl ? b.close : b.open];
        const IFCItem& rightMark = items[b.rtl ? b.open : b.close];
        float left = leftMark.placedX + leftMark.width;
        float right = rightMark.placedX;
        float top = std::min(leftMark.placedBaseline, rightMark.placedBaseline) -
                    b.shift - b.ascent;
        float bottom = std::max(leftMark.placedBaseline, rightMark.placedBaseline) -
                       b.shift + b.descent;
        if (right < left) std::swap(left, right);
        for (size_t i = b.open + 1; i < b.close; ++i) {
            const IFCItem& it = items[i];
            if (!it.placed || it.forceBreak || it.isFloat) continue;
            float l = it.placedX;
            float r = l + it.width;
            if (it.edge != 0) { l = r = it.placedX; }
            left = std::min(left, l);
            right = std::max(right, r);
            top = std::min(top, it.placedBaseline - b.shift - b.ascent);
            bottom = std::max(bottom, it.placedBaseline - b.shift + b.descent);
        }
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
