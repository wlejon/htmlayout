#include "layout/formatting_context.h"
#include "layout/style_util.h"
#include "layout/style_cache.h"
#include <optional>
#include <string>
#include <vector>

namespace htmlayout::layout {

using layout::styleVal;

// ============================================================
// Post-layout pass: position absolute/fixed elements
// ============================================================

namespace {

// Resolve a dimension: nullopt means auto/none/empty (unspecified).
// Specified-ness must be kept separate from the value — offsets like
// top:-40px are valid negative lengths and must not read as "auto".
std::optional<float> resolveDimAbs(const std::string& value, float available, float fontSize) {
    if (value.empty() || value == "auto" || value == "none") return std::nullopt;
    return resolveLength(value, available, fontSize);
}

// Check if a node establishes a containing block for absolute descendants.
// Per CSS spec: position != static, or has transform/filter/perspective,
// or has contain: layout/paint.
bool isContainingBlock(LayoutNode* node) {
    const std::string& pos = styleVal(node, Prop::Position);
    if (pos == "relative" || pos == "absolute" || pos == "fixed" || pos == "sticky")
        return true;
    // transform, filter, and perspective also create containing blocks
    const std::string& transform = styleVal(node, Prop::Transform);
    if (!transform.empty() && transform != "none") return true;
    const std::string& filter = styleVal(node, Prop::Filter);
    if (!filter.empty() && filter != "none") return true;
    // CSS Containment L2: contain: layout or contain: paint creates a containing block
    if (hasContainment(node, "layout") || hasContainment(node, "paint"))
        return true;
    return false;
}

// Find the containing block for an absolute element: nearest positioned ancestor.
// Returns nullptr if none found (meaning use the initial containing block / viewport).
LayoutNode* findContainingBlock(LayoutNode* node) {
    for (LayoutNode* p = node->parent(); p != nullptr; p = p->parent()) {
        if (isContainingBlock(p)) return p;
    }
    return nullptr; // initial containing block
}

// Compute accumulated offset from a node's parent up to (but not including) an ancestor.
// This is the sum of contentRect.x/y of all intermediate nodes between
// the node's DOM parent and the containing block.
struct Offset { float x, y; };

// Layout and position a single absolute/fixed child.
void layoutAbsoluteChild(LayoutNode* child, float cbWidth, float cbHeight,
                         float cbOriginOffsetX, float cbOriginOffsetY,
                         float domParentOffsetX, float domParentOffsetY,
                         float /*viewportHeight*/, TextMetrics& metrics) {
    float fontSize = resolveLength(styleVal(child, Prop::FontSize), 16.0f, 16.0f);
    if (fontSize <= 0.0f) fontSize = 16.0f;

    // Set available height for the child's own percentage resolution
    child->availableHeight = cbHeight;

    // Resolve offsets and explicit dimensions
    std::optional<float> left = resolveDimAbs(styleVal(child, Prop::Left), cbWidth, fontSize);
    std::optional<float> right = resolveDimAbs(styleVal(child, Prop::Right), cbWidth, fontSize);
    std::optional<float> top = resolveDimAbs(styleVal(child, Prop::Top), cbHeight, fontSize);
    std::optional<float> bottom = resolveDimAbs(styleVal(child, Prop::Bottom), cbHeight, fontSize);
    // An intrinsic size keyword (fit-content / min-content / max-content) is
    // content-sized: never a length, and never stretched between two pinned
    // offsets. Block layout sizes the width itself against the space the
    // offsets leave; the height is the content height.
    const std::string& wVal = styleVal(child, Prop::Width);
    const std::string& hVal = styleVal(child, Prop::Height);
    const bool intrinsicW = isIntrinsicSizingKeyword(wVal);
    const bool intrinsicH = isIntrinsicSizingKeyword(hVal);
    std::optional<float> specW =
        intrinsicW ? std::nullopt : resolveDimAbs(wVal, cbWidth, fontSize);
    std::optional<float> specH =
        intrinsicH ? std::nullopt : resolveDimAbs(hVal, cbHeight, fontSize);

    // Determine available width for layout
    // Shrink-wrap if width:auto and not both left+right set
    bool shrinkWrap = (!specW && !intrinsicW && !(left && right));
    bool stretchW = (!specW && !intrinsicW && left && right);

    bool stretchH = (!specH && !intrinsicH && top && bottom);

    // The width the inner layout runs at. Margins, padding and border all
    // resolve from the child's own style here rather than from child->box: the
    // in-flow pass skips absolute/fixed children, so their box holds no resolved
    // edges at this point (and reading it as zero would make the inner layout
    // subtract padding+border from a too-small available width and shrink the
    // children).
    //
    // Stretched width (left+right pinned, width:auto) has to reach the inner
    // layout so flex/grid children resolve their cross size against the real
    // containing-block width, not the raw cbWidth.
    float layoutW = cbWidth;
    if (stretchW) {
        float mh = resolveLength(styleVal(child, Prop::MarginLeft), cbWidth, fontSize) +
                   resolveLength(styleVal(child, Prop::MarginRight), cbWidth, fontSize);
        // layoutNode treats layoutW as the parent content width including
        // padding+border for this child; subtract only the margins here.
        float w = cbWidth - *left - *right - mh;
        if (w > 0) layoutW = w;
    } else if (shrinkWrap) {
        float maxCW = computeMaxContentWidth(child, metrics);
        if (maxCW > cbWidth) maxCW = cbWidth;
        float ph = resolveLength(styleVal(child, Prop::PaddingLeft), cbWidth, fontSize) +
                   resolveLength(styleVal(child, Prop::PaddingRight), cbWidth, fontSize);
        Edges bEdges = resolveBorders(child, cbWidth, fontSize);
        float bh = bEdges.left + bEdges.right;
        float mh = resolveLength(styleVal(child, Prop::MarginLeft), cbWidth, fontSize) +
                   resolveLength(styleVal(child, Prop::MarginRight), cbWidth, fontSize);
        layoutW = maxCW + ph + bh + mh;
    } else if (intrinsicW) {
        // fit-content's available space: the containing block less the
        // offsets pinned on this axis.
        float w = cbWidth - (left ? *left : 0.0f) - (right ? *right : 0.0f);
        if (w > 0) layoutW = w;
    }

    // Claim the node for this layout pass — which clears the geometry the
    // previous pass left in its box — BEFORE pre-writing the stretched height
    // below. layoutNode() would otherwise clear the box on its own first visit
    // of the pass and throw that height away, and the height is an *input*: it
    // is what lets a top+bottom-pinned flex or grid container size its children
    // against a definite cross axis instead of collapsing to content height.
    //
    // beginLayoutNode returns false when the child's cached subtree is still
    // valid for these inputs. Nothing is laid out then — the box already holds
    // the right geometry — and we fall through to reposition it.
    if (beginLayoutNode(child, layoutW)) {
        // Pre-computed stretched height: see above.
        if (stretchH) {
            float marginTop = resolveLength(styleVal(child, Prop::MarginTop), cbHeight, fontSize);
            float marginBottom = resolveLength(styleVal(child, Prop::MarginBottom), cbHeight, fontSize);
            float padTop = resolveLength(styleVal(child, Prop::PaddingTop), cbHeight, fontSize);
            float padBottom = resolveLength(styleVal(child, Prop::PaddingBottom), cbHeight, fontSize);
            float borTop = (styleVal(child, Prop::BorderTopStyle) != "none")
                ? resolveLength(styleVal(child, Prop::BorderTopWidth), cbHeight, fontSize) : 0.0f;
            float borBottom = (styleVal(child, Prop::BorderBottomStyle) != "none")
                ? resolveLength(styleVal(child, Prop::BorderBottomWidth), cbHeight, fontSize) : 0.0f;
            float h = cbHeight - *top - *bottom - marginTop - marginBottom -
                      padTop - padBottom - borTop - borBottom;
            if (h > 0) child->box.contentRect.height = h;
        }
        layoutNode(child, layoutW, metrics);
    }

    // Stretch width if both left and right are set and width is auto
    if (stretchW) {
        float w = cbWidth - *left - *right -
                  child->box.margin.left - child->box.margin.right -
                  child->box.padding.left - child->box.padding.right -
                  child->box.border.left - child->box.border.right;
        if (w > 0) child->box.contentRect.width = w;
    }

    // Re-apply the stretched height in case layoutNode overwrote it from
    // content size (block layout typically does); the absolute-position
    // contract is that top+bottom together pin the box.
    if (stretchH) {
        float h = cbHeight - *top - *bottom -
                  child->box.margin.top - child->box.margin.bottom -
                  child->box.padding.top - child->box.padding.bottom -
                  child->box.border.top - child->box.border.bottom;
        if (h > 0) child->box.contentRect.height = h;
    }

    // Auto margins with both offsets pinned and the size not stretched
    // (CSS 2.1 §10.3.7 / §10.6.4): the space left over goes to the auto
    // margins — split evenly when both are auto, which is what centres
    // `inset: 0; margin: auto` boxes such as a modal dialog. When the box
    // overflows, the start margin stays at zero and the end one goes negative.
    auto resolveAutoMargins = [&](float cbSize, float startOff, float endOff,
                                  Prop startProp, Prop endProp,
                                  float& mStart, float& mEnd, float boxSize) {
        const bool aStart = styleVal(child, startProp) == "auto";
        const bool aEnd = styleVal(child, endProp) == "auto";
        if (!aStart && !aEnd) return;
        const float used = boxSize + (aStart ? 0.0f : mStart) + (aEnd ? 0.0f : mEnd);
        const float free = cbSize - startOff - endOff - used;
        if (aStart && aEnd) {
            if (free < 0) { mStart = 0.0f; mEnd = free; }
            else { mStart = free / 2; mEnd = free / 2; }
        } else if (aStart) {
            mStart = free;
        } else {
            mEnd = free;
        }
    };
    if (left && right && !stretchW) {
        const float bw = child->box.contentRect.width + child->box.padding.left +
                         child->box.padding.right + child->box.border.left + child->box.border.right;
        resolveAutoMargins(cbWidth, *left, *right, Prop::MarginLeft, Prop::MarginRight,
                           child->box.margin.left, child->box.margin.right, bw);
    }
    if (top && bottom && !stretchH) {
        const float bh = child->box.contentRect.height + child->box.padding.top +
                         child->box.padding.bottom + child->box.border.top + child->box.border.bottom;
        resolveAutoMargins(cbHeight, *top, *bottom, Prop::MarginTop, Prop::MarginBottom,
                           child->box.margin.top, child->box.margin.bottom, bh);
    }

    // Compute position in containing-block-relative space.
    //
    // With both offsets on an axis `auto` the box goes at its static position
    // (CSS 2.1 §10.3.7): where it would have started in the flow, which the
    // in-flow pass recorded in the DOM parent's coordinates. Falling back to
    // the containing block's origin — as this did before there was a static
    // position to read — is what dropped every offset-less dropdown into the
    // corner of its containing block instead of under the control that owns it.
    // A pass that skipped the parent's flow wrote no fresh static position;
    // the one from the pass that last laid that flow out still describes where
    // the box belongs, and it is expressed in the parent's coordinates, which
    // are re-derived below. Only a box the flow has never reached has none.
    const bool haveStatic = child->staticPosPass != 0;
    const float staticInCBX = child->staticPosX + domParentOffsetX - cbOriginOffsetX;
    const float staticInCBY = child->staticPosY + domParentOffsetY - cbOriginOffsetY;

    float xInCB = (haveStatic ? staticInCBX : 0.0f) +
                  child->box.margin.left + child->box.padding.left + child->box.border.left;
    float yInCB = (haveStatic ? staticInCBY : 0.0f) +
                  child->box.margin.top + child->box.padding.top + child->box.border.top;

    if (left) {
        xInCB = *left + child->box.margin.left + child->box.padding.left + child->box.border.left;
    } else if (right) {
        xInCB = cbWidth - *right - child->box.margin.right -
                child->box.padding.right - child->box.border.right - child->box.contentRect.width;
    }

    if (top) {
        yInCB = *top + child->box.margin.top + child->box.padding.top + child->box.border.top;
    } else if (bottom) {
        yInCB = cbHeight - *bottom - child->box.margin.bottom -
                child->box.padding.bottom - child->box.border.bottom - child->box.contentRect.height;
    }

    // Transform from CB space to DOM-parent-relative space.
    // cbOriginOffset: the CB's padding-box origin in absolute coordinates
    // domParentOffset: the DOM parent's content-area origin in absolute coordinates
    // contentRect must be relative to the DOM parent's content area
    child->box.contentRect.x = xInCB + cbOriginOffsetX - domParentOffsetX;
    child->box.contentRect.y = yInCB + cbOriginOffsetY - domParentOffsetY;
}

// Compute the absolute position of a node's content area origin
// by walking from root and accumulating contentRect offsets.
Offset computeAbsolutePosition(LayoutNode* node) {
    // Build path from root to node
    std::vector<LayoutNode*> path;
    for (LayoutNode* p = node; p != nullptr; p = p->parent()) {
        path.push_back(p);
    }
    // Walk from root (end of vector) toward node, accumulating offsets
    Offset off{0, 0};
    // The root's contentRect is in viewport space, so start from it
    // Each node's contentRect.x/y is relative to its parent's content area
    for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
        off.x += path[i]->box.contentRect.x;
        off.y += path[i]->box.contentRect.y;
    }
    return off;
}

// Recursive tree walk to find and position all absolute/fixed elements.
// Processes in depth-first pre-order so ancestor absolutes are positioned
// before their descendant absolutes.
//
// Returns whether any positioned element lives strictly below `node`, and
// caches it in node->subtreeHasPositioned so later passes skip branches with
// none. Branches that do contain one are walked fully every pass — a
// positioned box must re-resolve against its containing block's current
// geometry, which can move without anything in this branch being dirty.
bool layoutAbsoluteElementsRecursive(LayoutNode* node, const Viewport& viewport,
                                      TextMetrics& metrics) {
    if (node->subtreeHasPositioned == 0) return false;
    bool any = false;
    // ::before / ::after hang off the node outside children(); a positioned
    // one (a divider rule, a badge dot) is laid out here like any child.
    std::vector<LayoutNode*> kids;
    if (auto* p = node->pseudoBefore()) kids.push_back(p);
    for (auto* child : node->children()) kids.push_back(child);
    if (auto* p = node->pseudoAfter()) kids.push_back(p);
    for (auto* child : kids) {
        if (!child || child->isTextNode()) continue;

        const std::string& display = styleVal(child, Prop::Display);
        if (display == "none") continue;

        const std::string& pos = styleVal(child, Prop::Position);

        if (pos == "fixed") {
            // Fixed: containing block is the viewport
            float cbWidth = viewport.width;
            float cbHeight = viewport.height;

            // CB origin is (0, 0) in absolute space
            float cbOriginX = 0, cbOriginY = 0;

            // DOM parent's absolute content position
            Offset parentPos = computeAbsolutePosition(node);

            child->viewportHeight = viewport.height;
            layoutAbsoluteChild(child, cbWidth, cbHeight,
                                cbOriginX, cbOriginY,
                                parentPos.x, parentPos.y,
                                viewport.height, metrics);
            any = true;
        } else if (pos == "absolute") {
            // Find the containing block
            LayoutNode* cb = findContainingBlock(child);

            float cbWidth, cbHeight;
            float cbOriginX, cbOriginY;

            if (cb) {
                // Containing block is the padding box of the positioned ancestor
                cbWidth = cb->box.contentRect.width + cb->box.padding.left + cb->box.padding.right;
                cbHeight = cb->box.contentRect.height + cb->box.padding.top + cb->box.padding.bottom;

                // CB's padding-box origin in absolute coordinates
                Offset cbPos = computeAbsolutePosition(cb);
                cbOriginX = cbPos.x - cb->box.padding.left;
                cbOriginY = cbPos.y - cb->box.padding.top;
            } else {
                // No positioned ancestor: use viewport (initial containing block)
                cbWidth = viewport.width;
                cbHeight = viewport.height;
                cbOriginX = 0;
                cbOriginY = 0;
            }

            // DOM parent's absolute content position
            Offset parentPos = computeAbsolutePosition(node);

            child->viewportHeight = viewport.height;
            layoutAbsoluteChild(child, cbWidth, cbHeight,
                                cbOriginX, cbOriginY,
                                parentPos.x, parentPos.y,
                                viewport.height, metrics);
            any = true;
        }

        // Recurse into children (including into absolute elements, which can
        // contain further absolute descendants)
        if (layoutAbsoluteElementsRecursive(child, viewport, metrics)) any = true;
    }
    node->subtreeHasPositioned = any ? 1 : 0;
    return any;
}

} // anonymous namespace

void layoutAbsoluteElements(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics) {
    if (!root) return;
    layoutAbsoluteElementsRecursive(root, viewport, metrics);
}

} // namespace htmlayout::layout
