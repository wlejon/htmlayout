#include "layout/block.h"
#include "layout/block_internal.h"
#include "layout/formatting_context.h"
#include "../from_chars_compat.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace htmlayout::layout {

using layout::styleVal;

// Does this box establish a new block formatting context (CSS2 §9.4.1)?
// BFC roots contain their floats, don't collapse margins with their
// children, and — when sitting beside a float — narrow to the space
// between the float edges instead of overlapping them.
bool nodeEstablishesBFC(LayoutNode* node) {
    if (!node->parent()) return true; // root element: the initial BFC
    auto& style = node->computedStyle();
    const std::string& ov = styleVal(node, Prop::Overflow);
    const std::string& ovx = styleVal(node, Prop::OverflowX);
    const std::string& ovy = styleVal(node, Prop::OverflowY);
    if ((ov != "visible" && !ov.empty()) ||
        (ovx != "visible" && !ovx.empty()) ||
        (ovy != "visible" && !ovy.empty()))
        return true;
    const std::string& disp = styleVal(node, Prop::Display);
    if (disp == "inline-block" || disp == "flex" || disp == "inline-flex" ||
        disp == "grid" || disp == "inline-grid" || disp == "flow-root" ||
        disp == "table-cell" || disp == "table-caption" ||
        disp == "-webkit-box")  // (-webkit-inline-box reads as inline-block)
        return true;
    const std::string& position = styleVal(node, Prop::Position);
    if (position == "absolute" || position == "fixed")
        return true;
    const std::string& flt = styleVal(node, Prop::Float);
    if (flt == "left" || flt == "right")
        return true;
    // A flex or grid item establishes an independent formatting context
    // (Flexbox §4, Grid §6.1).
    if (LayoutNode* p = node->parent()) {
        const std::string& pd = styleVal(p, Prop::Display);
        if (pd == "flex" || pd == "inline-flex" ||
            pd == "grid" || pd == "inline-grid")
            return true;
    }
    // Multi-column containers are formatting-context roots too.
    const std::string& mcCount = styleVal(node, Prop::ColumnCount);
    const std::string& mcWidth = styleVal(node, Prop::ColumnWidth);
    if ((!mcCount.empty() && mcCount != "auto") ||
        (!mcWidth.empty() && mcWidth != "auto"))
        return true;
    // Layout containment makes the box an independent formatting context
    // (css-contain §2). container-type: size / inline-size implies it.
    const std::string& ctype = styleVal(node, Prop::ContainerType);
    if (ctype.find("size") != std::string::npos)
        return true;
    const std::string& contain = styleVal(node, Prop::Contain);
    if (contain.find("layout") != std::string::npos ||
        contain.find("paint") != std::string::npos ||
        contain == "strict" || contain == "content")
        return true;
    return false;
}

// Get available width at a given Y position accounting for floats.
// A float with a circle shape (shape-outside) only excludes the chord
// of the circle covered by the queried band — outside the circle's
// vertical range content flows past the float box entirely.
std::pair<float, float> blockAvailableAtY(const std::vector<BlockFloatRect>& floats,
                                          float childAvailable, float y, float h) {
    float leftEdge = 0;
    float rightEdge = childAvailable;
    float effectiveH = h > 0 ? h : 1.0f; // treat zero-height as point query
    for (auto& f : floats) {
        if (!(y + effectiveH > f.y && y < f.y + f.height)) continue;
        if (f.shapeR >= 0) {
            // Vertical distance from the circle's center to the nearest
            // edge of the band (0 when the band covers the center).
            float dy = 0;
            if (f.shapeCy < y) dy = y - f.shapeCy;
            else if (f.shapeCy > y + effectiveH) dy = f.shapeCy - (y + effectiveH);
            if (dy >= f.shapeR) continue; // band misses the circle
            float half = std::sqrt(std::max(0.0f, f.shapeR * f.shapeR - dy * dy));
            if (f.isLeft) {
                leftEdge = std::max(leftEdge,
                    std::min(f.x + f.width, f.shapeCx + half));
            } else {
                rightEdge = std::min(rightEdge,
                    std::max(f.x, f.shapeCx - half));
            }
        } else if (f.isLeft) {
            leftEdge = std::max(leftEdge, f.x + f.width);
        } else {
            rightEdge = std::min(rightEdge, f.x);
        }
    }
    return {leftEdge, rightEdge};
}

// Place a floated child with its margin-box top at `atY`, register its
// exclusion (including any shape-outside: circle geometry), and return.
void placeBlockFloat(LayoutNode* child, float atY, float childAvailable, float fontSize,
                     TextMetrics& metrics, std::vector<BlockFloatRect>& floats) {
    auto& childStyle = child->computedStyle();
    const std::string& childFloat = styleVal(child, Prop::Float);
    layoutNode(child, childAvailable, metrics);

    float floatWidth = child->box.fullWidth() + child->box.margin.left + child->box.margin.right;
    float floatHeight = child->box.fullHeight() + child->box.margin.top + child->box.margin.bottom;

    // CSS2 §9.5.1 rule 5: a float's outer top may not be higher than the
    // outer top of any earlier float in the same formatting context.
    for (auto& f : floats) atY = std::max(atY, f.y);

    // Rules 2/3/7: if the float doesn't fit between the exclusions at
    // atY, move it down to the next Y where the set of intersecting
    // floats changes (a float bottom) until it fits — or until no float
    // intersects its band (a float wider than the containing block
    // itself just overflows).
    float bandH = floatHeight > 0 ? floatHeight : 1.0f;
    for (;;) {
        auto [le, re] = blockAvailableAtY(floats, childAvailable, atY, floatHeight);
        if (floatWidth <= re - le) break;
        float nextY = std::numeric_limits<float>::max();
        for (auto& f : floats) {
            if (atY + bandH > f.y && atY < f.y + f.height)
                nextY = std::min(nextY, f.y + f.height);
        }
        if (nextY == std::numeric_limits<float>::max()) break;
        atY = nextY;
    }

    auto [leftEdge, rightEdge] = blockAvailableAtY(floats, childAvailable, atY, floatHeight);

    BlockFloatRect fr{0, atY, floatWidth, floatHeight, childFloat == "left"};
    if (childFloat == "left") {
        child->box.contentRect.x = leftEdge + child->box.margin.left +
            child->box.padding.left + child->box.border.left;
        fr.x = leftEdge;
    } else {
        child->box.contentRect.x = rightEdge - floatWidth + child->box.margin.left +
            child->box.padding.left + child->box.border.left;
        fr.x = rightEdge - floatWidth;
    }
    child->box.contentRect.y = atY + child->box.margin.top +
        child->box.padding.top + child->box.border.top;

    // shape-outside: circle(<r>) — content wraps the circle's chord
    // instead of the margin box. The reference box defaults to the
    // margin box; a percentage radius resolves against
    // sqrt(w² + h²)/√2 (CSS Shapes §basic-shape).
    const std::string& shapeOutside = styleVal(child, Prop::ShapeOutside);
    if (shapeOutside.rfind("circle(", 0) == 0) {
        std::string arg = shapeOutside.substr(7);
        size_t stop = arg.find_first_of(")");
        size_t at = arg.find(" at ");
        if (at != std::string::npos && at < stop) stop = at;
        arg = arg.substr(0, stop);
        while (!arg.empty() && std::isspace((unsigned char)arg.front())) arg.erase(arg.begin());
        while (!arg.empty() && std::isspace((unsigned char)arg.back())) arg.pop_back();
        float r = -1.0f;
        if (arg.empty() || arg == "closest-side") {
            r = std::min(floatWidth, floatHeight) * 0.5f;
        } else if (arg == "farthest-side") {
            r = std::max(floatWidth, floatHeight) * 0.5f;
        } else if (!arg.empty() && arg.back() == '%') {
            float pct = 0;
            htmlayout::from_chars_fp(arg.data(), arg.data() + arg.size(), pct);
            r = pct * 0.01f * std::sqrt(
                (floatWidth * floatWidth + floatHeight * floatHeight) * 0.5f);
        } else {
            r = resolveLength(arg, floatWidth, fontSize);
        }
        if (r >= 0) {
            fr.shapeCx = fr.x + floatWidth * 0.5f;
            fr.shapeCy = fr.y + floatHeight * 0.5f;
            fr.shapeR = r;
        }
    }
    floats.push_back(fr);
}

// Fieldset with a rendered legend (CSS rendering §fieldset): the first
// in-flow legend child straddles the block-start border — shrink-to-fit,
// positioned at the fieldset's border-box top — and the fieldset content
// is pushed down below it (the legend's margin box replaces the block-
// start border in the flow). Laid out here, then skipped in the loop.
LayoutNode* layoutFieldsetLegend(LayoutNode* node, float childAvailable,
                                 TextMetrics& metrics, float& cursorY) {
    LayoutNode* fieldsetLegend = nullptr;
    std::string_view ntag = node->tagName();
    if (ntag == "fieldset" || ntag == "FIELDSET") {
        for (auto* c : getLayoutChildren(node)) {
            if (c->isTextNode()) continue;
            if (styleVal(c, Prop::Display) == "none") continue;
            std::string_view ctag = c->tagName();
            if (ctag == "legend" || ctag == "LEGEND") fieldsetLegend = c;
            break; // only the first in-flow child can be the legend
        }
    }
    if (fieldsetLegend) {
        layoutNode(fieldsetLegend, childAvailable, metrics);
        auto& lb = fieldsetLegend->box;
        float padBorderH = lb.padding.left + lb.padding.right +
                           lb.border.left + lb.border.right;
        float legMax = computeMaxContentWidth(fieldsetLegend, metrics);
        float legContent = std::min(legMax,
            std::max(0.0f, childAvailable - padBorderH));
        if (legContent < 0) legContent = 0;
        lb.contentRect.width = legContent;
        // Inset horizontally to the fieldset's content-left; pull the
        // border-box top up to the fieldset border-box top.
        lb.contentRect.x = lb.margin.left + lb.border.left + lb.padding.left;
        lb.contentRect.y = -(node->box.border.top + node->box.padding.top) +
                           lb.margin.top + lb.border.top + lb.padding.top;
        float legMarginH = lb.fullHeight() + lb.margin.top + lb.margin.bottom;
        float push = legMarginH - node->box.border.top;
        if (push > 0.0f) cursorY += push;
    }
    return fieldsetLegend;
}

} // namespace htmlayout::layout
