#include "layout/multicol.h"
#include "layout/block_internal.h"
#include "layout/formatting_context.h"
#include "layout/style_cache.h"
#include "layout/style_util.h"
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

namespace {

// Baseline geometry of an atomic inline-level box (inline-block, replaced
// element…) that has already been laid out. `height` is the margin-box
// height. Returns the distance from the margin-box top to the box's
// baseline plus the vertical-align keyword (0 = baseline, 1 = top,
// 2 = middle, 3 = bottom) — CSS2 §10.8.1.
struct AtomicInlineGeom {
    float baseline = 0;
    int valign = 0;
};

AtomicInlineGeom atomicInlineGeometry(LayoutNode* child, float height,
                                      float strutBelow, float availWidth,
                                      TextMetrics& metrics) {
    AtomicInlineGeom g;
    auto& styleRef = child->computedStyle();
    const std::string& va = styleVal(child, Prop::VerticalAlign);
    if (va == "top" || va == "text-top") g.valign = 1;
    else if (va == "middle") g.valign = 2;
    else if (va == "bottom" || va == "text-bottom") g.valign = 3;

    float iw = 0, ih = 0;
    if (child->intrinsicSize(iw, ih, availWidth)) {
        std::string_view tag = child->tagName();
        bool isTextarea = (tag == "textarea" || tag == "TEXTAREA");
        if (child->hasIntrinsicRatio() || isTextarea) {
            // Replaced media (img/canvas/video/svg) and the multi-line
            // <textarea> align by the bottom margin edge, so the line strut's
            // descent hangs below the control (Blink).
            g.baseline = height;
        } else {
            std::string_view ctype = child->attribute("type");
            bool isCheckRadio = (tag == "input" || tag == "INPUT") &&
                                (ctype == "checkbox" || ctype == "radio");
            if (isCheckRadio) {
                // Checkbox/radio: align by the bottom of the border box.
                g.baseline = height - child->box.margin.bottom;
            } else {
                // Single-line form controls (input, select, button-type
                // inputs): align by the control's internal text baseline —
                // top border + padding + the control font's ascent.
                float fs = resolveLength(styleVal(child, Prop::FontSize), 16.0f, 16.0f);
                if (fs <= 0.0f) fs = 16.0f;
                const std::string& fam = styleVal(child, Prop::FontFamily);
                const std::string& wt  = styleVal(child, Prop::FontWeight);
                float asc = fs > 0.0f ? metrics.ascent(fam, fs, wt) : 0.0f;
                if (asc <= 0.0f) asc = fs * 0.8f;
                g.baseline = child->box.margin.top + child->box.border.top +
                             child->box.padding.top + asc;
            }
            if (g.baseline > height) g.baseline = height;
            if (g.baseline < 0) g.baseline = 0;
            (void)strutBelow;
        }
    } else {
        // Atomic inline: baseline of the last line box when it has in-flow
        // content and visible overflow; otherwise the bottom margin edge.
        const std::string& ov = styleVal(child, Prop::Overflow);
        bool visibleOv = ov.empty() || ov == "visible";
        if (visibleOv && child->box.baselineOffset >= 0) {
            g.baseline = child->box.margin.top + child->box.border.top +
                child->box.padding.top + child->box.baselineOffset;
            if (g.baseline > height) g.baseline = height;
        } else {
            g.baseline = height;
        }
    }
    return g;
}

} // anonymous namespace

bool isMulticolContainer(const LayoutNode* node) {
    const std::string& colCount = styleVal(node, Prop::ColumnCount);
    const std::string& colWidth = styleVal(node, Prop::ColumnWidth);
    return (!colCount.empty() && colCount != "auto") ||
           (!colWidth.empty() && colWidth != "auto");
}

bool layoutMulticolIfNeeded(LayoutNode* node, float contentWidth, float fontSize,
                            float specifiedHeight, TextMetrics& metrics) {
    // Multi-column layout: redistribute children into columns if column-count or column-width is set
    const std::string& colCountStr = styleVal(node, Prop::ColumnCount);
    const std::string& colWidthStr = styleVal(node, Prop::ColumnWidth);

    bool hasMulticol = (!colCountStr.empty() && colCountStr != "auto") ||
                       (!colWidthStr.empty() && colWidthStr != "auto");

    if (!hasMulticol) return false;

    int columnCount = 1;
    float columnWidth = 0.0f;

    if (!colCountStr.empty() && colCountStr != "auto") {
        try { columnCount = std::stoi(colCountStr); } catch (...) { columnCount = 1; }
        if (columnCount < 1) columnCount = 1;
    }

    if (!colWidthStr.empty() && colWidthStr != "auto") {
        columnWidth = resolveLength(colWidthStr, contentWidth, fontSize);
    }

    // Resolve column gap
    const std::string& colGapStr = styleVal(node, Prop::ColumnGap);
    float columnGap = 0.0f;
    if (!colGapStr.empty() && colGapStr != "normal") {
        columnGap = resolveLength(colGapStr, contentWidth, fontSize);
    }

    // Determine actual column count and width
    if (columnWidth > 0 && (colCountStr.empty() || colCountStr == "auto")) {
        // Only column-width specified: compute count from available width
        columnCount = std::max(1, static_cast<int>((contentWidth + columnGap) / (columnWidth + columnGap)));
    }
    // Compute actual column width from count
    float actualColWidth = (contentWidth - columnGap * (columnCount - 1)) / columnCount;
    if (actualColWidth < 0) actualColWidth = 0;

    // Compute total content height (already computed as cursorY or specifiedHeight)
    float totalHeight = node->box.contentRect.height;

    // Target column height: divide total evenly
    float targetColHeight = totalHeight / columnCount;
    if (targetColHeight < 1.0f) targetColHeight = 1.0f;

    // Redistribute in-flow children into columns
    // Collect in-flow children (not absolute, not display:none)
    std::vector<LayoutNode*> inFlowChildren;
    for (auto* child : getLayoutChildren(node)) {
        if (child->isTextNode()) continue;
        auto& cs = child->computedStyle();
        if (styleVal(child, Prop::Display) == "none") continue;
        const std::string& cp = styleVal(child, Prop::Position);
        if (cp == "absolute" || cp == "fixed") continue;
        inFlowChildren.push_back(child);
    }

    // Split children into segments separated by column-span:all elements.
    // Each segment is laid out in columns; spanners get full width between segments.
    struct Segment {
        std::vector<LayoutNode*> children;
        bool isSpanner = false; // true = column-span:all element
    };
    std::vector<Segment> segments;
    Segment current;

    for (auto* child : inFlowChildren) {
        auto& cs = child->computedStyle();
        if (styleVal(child, Prop::ColumnSpan) == "all") {
            if (!current.children.empty()) {
                segments.push_back(std::move(current));
                current = Segment{};
            }
            Segment spanner;
            spanner.isSpanner = true;
            spanner.children.push_back(child);
            segments.push_back(std::move(spanner));
        } else {
            current.children.push_back(child);
        }
    }
    if (!current.children.empty()) segments.push_back(std::move(current));

    float totalY = 0.0f;

    for (auto& seg : segments) {
        if (seg.isSpanner) {
            // Layout spanner at full container width
            auto* child = seg.children[0];
            layoutNode(child, contentWidth, metrics);
            child->box.contentRect.x = child->box.margin.left +
                child->box.padding.left + child->box.border.left;
            child->box.contentRect.y = totalY + child->box.margin.top +
                child->box.padding.top + child->box.border.top;
            float childFullH = child->box.margin.top + child->box.border.top +
                child->box.padding.top + child->box.contentRect.height +
                child->box.padding.bottom + child->box.border.bottom +
                child->box.margin.bottom;
            totalY += childFullH;
        } else {
            // Lay the segment out as a sequence of fragmentation-atomic
            // items — block children stay whole (htmlayout never splits
            // a box across columns, which also satisfies break-inside:
            // avoid), and runs of atomic inline-level children are
            // packed into line boxes at the column width so inline
            // content fragments by lines like Chromium.
            struct FragItem {
                std::vector<LayoutNode*> nodes; // 1 for blocks, n per line
                std::vector<float> nodeX;       // line: margin-box x in column
                std::vector<float> nodeY;       // line: margin-box y in item
                float height = 0;               // border-box / line height
                float marginTop = 0, marginBottom = 0; // blocks only
                bool isLine = false;
                bool breakBefore = false, breakAfter = false;
            };
            std::vector<FragItem> frag;
            StrutMetrics colStrut = computeStrut(node, fontSize, metrics);

            auto isInlineLevel = [](LayoutNode* c) {
                const std::string& d = styleVal(c, Prop::Display);
                return d == "inline" || d == "inline-block" ||
                       d == "inline-flex" || d == "inline-grid";
            };

            size_t ci = 0;
            while (ci < seg.children.size()) {
                LayoutNode* child = seg.children[ci];
                if (isInlineLevel(child)) {
                    size_t cj = ci;
                    while (cj < seg.children.size() &&
                           isInlineLevel(seg.children[cj])) ++cj;
                    struct LItem { LayoutNode* n; float w, h; AtomicInlineGeom g; };
                    std::vector<LItem> li;
                    for (size_t k = ci; k < cj; ++k) {
                        LayoutNode* c = seg.children[k];
                        layoutNode(c, actualColWidth, metrics);
                        float cw = c->box.fullWidth() + c->box.margin.left + c->box.margin.right;
                        float ch = c->box.fullHeight() + c->box.margin.top + c->box.margin.bottom;
                        li.push_back({c, cw, ch,
                            atomicInlineGeometry(c, ch, colStrut.below, actualColWidth, metrics)});
                    }
                    size_t ls = 0;
                    while (ls < li.size()) {
                        float cx = 0;
                        size_t le = ls;
                        while (le < li.size()) {
                            if (le > ls && cx + li[le].w > actualColWidth) break;
                            cx += li[le].w;
                            ++le;
                        }
                        // Line vertical geometry: union of the strut and
                        // the items' baseline-aligned extents (same model
                        // as the IFC path).
                        float a = colStrut.above, b = colStrut.below;
                        for (size_t k = ls; k < le; ++k) {
                            if (li[k].g.valign == 0) {
                                a = std::max(a, li[k].g.baseline);
                                b = std::max(b, li[k].h - li[k].g.baseline);
                            } else if (li[k].g.valign == 2) {
                                float ia = li[k].h * 0.5f + colStrut.xHeight * 0.5f;
                                a = std::max(a, ia);
                                b = std::max(b, li[k].h - ia);
                            }
                        }
                        for (size_t k = ls; k < le; ++k) {
                            if ((li[k].g.valign == 1 || li[k].g.valign == 3) &&
                                li[k].h > a + b) b = li[k].h - a;
                        }
                        FragItem fi;
                        fi.isLine = true;
                        fi.height = a + b;
                        float x = 0;
                        for (size_t k = ls; k < le; ++k) {
                            float yTop;
                            switch (li[k].g.valign) {
                                case 1: yTop = 0; break;
                                case 2: yTop = a - (li[k].h * 0.5f +
                                    colStrut.xHeight * 0.5f); break;
                                case 3: yTop = (a + b) - li[k].h; break;
                                default: yTop = a - li[k].g.baseline; break;
                            }
                            fi.nodes.push_back(li[k].n);
                            fi.nodeX.push_back(x);
                            fi.nodeY.push_back(yTop);
                            x += li[k].w;
                        }
                        frag.push_back(std::move(fi));
                        ls = le;
                    }
                    ci = cj;
                } else {
                    layoutNode(child, actualColWidth, metrics);
                    auto& cs = child->computedStyle();
                    FragItem fi;
                    fi.nodes.push_back(child);
                    fi.height = child->box.fullHeight();
                    fi.marginTop = child->box.margin.top;
                    fi.marginBottom = child->box.margin.bottom;
                    const std::string& bb = styleVal(child, Prop::BreakBefore);
                    const std::string& ba = styleVal(child, Prop::BreakAfter);
                    fi.breakBefore = (bb == "column" || bb == "always");
                    fi.breakAfter = (ba == "column" || ba == "always");
                    frag.push_back(std::move(fi));
                    ++ci;
                }
            }

            // Greedy fragmentation at column height H: items stack with
            // collapsed sibling margins; margins are truncated at a
            // column top (CSS Fragmentation §adjoining margins). Returns
            // {columns used, tallest column content height}.
            std::vector<int> itemCol(frag.size(), 0);
            std::vector<float> itemY(frag.size(), 0.0f);
            auto fill = [&](float H, bool commit) {
                int col = 0;
                float y = 0, maxBottom = 0, prevMB = 0;
                bool colEmpty = true;
                // Margins truncate only at UNFORCED breaks (css-break
                // §5.2). The first column's top and forced break-before/
                // after columns keep the leading margin — the multicol
                // container is a BFC, so it doesn't collapse away.
                bool keepTopMargin = true;
                for (size_t k = 0; k < frag.size(); ++k) {
                    auto& it = frag[k];
                    if (it.breakBefore && !colEmpty) {
                        ++col; y = 0; colEmpty = true; prevMB = 0;
                        keepTopMargin = true;
                    }
                    float spacing = colEmpty ?
                        (keepTopMargin ? it.marginTop : 0.0f) :
                        std::max({prevMB, it.marginTop, 0.0f}) +
                        std::min({prevMB, it.marginTop, 0.0f});
                    float top = y + spacing;
                    if (!colEmpty && top + it.height > H + 0.001f) {
                        ++col; top = 0; colEmpty = true;
                    }
                    if (commit) { itemCol[k] = col; itemY[k] = top; }
                    y = top + it.height;
                    maxBottom = std::max(maxBottom, y);
                    colEmpty = false;
                    prevMB = it.marginBottom;
                    if (it.breakAfter) {
                        ++col; y = 0; colEmpty = true; prevMB = 0;
                        keepTopMargin = true;
                    }
                }
                int used = frag.empty() ? 1 : (colEmpty ? col : col + 1);
                return std::pair<int, float>{used, maxBottom};
            };

            const float unbounded = std::numeric_limits<float>::max() * 0.25f;
            const bool fillAuto = (styleVal(node, Prop::ColumnFill) == "auto");
            float H;
            if (specifiedHeight >= 0.0f && fillAuto) {
                // column-fill: auto with a definite height: columns fill to
                // it sequentially and overflow into extra columns when the
                // content doesn't fit.
                H = std::max(1.0f, node->box.contentRect.height);
            } else if (columnCount <= 1) {
                H = fill(unbounded, false).second;
            } else {
                // column-fill: balance (the default) — find the minimal
                // column height that still fits within columnCount
                // columns. Feasibility is monotonic in H, so bisect,
                // then tighten H to the tallest column actually used.
                // The floor is the tallest unbreakable item: a smaller H
                // can look "feasible" by spreading one oversized item
                // per column, but the container gets no shorter and the
                // distribution stops matching Chromium's.
                float total = fill(unbounded, false).second;
                float tallest = 0.0f;
                for (auto& it : frag) tallest = std::max(tallest, it.height);
                float lo = tallest, hi = std::max({total, tallest, 1.0f});
                for (int iter = 0; iter < 60 && hi - lo > 0.0005f; ++iter) {
                    float mid = (lo + hi) * 0.5f;
                    if (fill(mid, false).first <= columnCount) hi = mid;
                    else lo = mid;
                }
                H = hi;
                // A definite height caps the balanced column height:
                // balancing may want a shorter column (honored), but never a
                // taller one — overflow spills into extra columns instead.
                if (specifiedHeight >= 0.0f)
                    H = std::min(H, std::max(1.0f, node->box.contentRect.height));
            }
            float maxBottom = fill(H, true).second;

            for (size_t k = 0; k < frag.size(); ++k) {
                auto& it = frag[k];
                float colX = itemCol[k] * (actualColWidth + columnGap);
                if (it.isLine) {
                    for (size_t m = 0; m < it.nodes.size(); ++m) {
                        LayoutNode* c = it.nodes[m];
                        c->box.contentRect.x = colX + it.nodeX[m] +
                            c->box.margin.left + c->box.padding.left +
                            c->box.border.left;
                        c->box.contentRect.y = totalY + itemY[k] + it.nodeY[m] +
                            c->box.margin.top + c->box.padding.top +
                            c->box.border.top;
                    }
                } else {
                    LayoutNode* c = it.nodes[0];
                    c->box.contentRect.x = colX + c->box.margin.left +
                        c->box.padding.left + c->box.border.left;
                    c->box.contentRect.y = totalY + itemY[k] +
                        c->box.padding.top + c->box.border.top;
                    if (c->box.contentRect.width > actualColWidth) {
                        c->box.contentRect.width = actualColWidth;
                    }
                }
            }
            totalY += maxBottom;
        }
    }
    if (specifiedHeight < 0.0f) node->box.contentRect.height = totalY;
    return true;
}

} // namespace htmlayout::layout
