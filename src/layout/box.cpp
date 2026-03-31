#include "layout/box.h"
#include "layout/formatting_context.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace htmlayout::layout {

void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics) {
    if (!root) return;
    layoutNode(root, viewportWidth, metrics);
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;
}

void layoutTree(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics) {
    if (!root) return;
    layoutNode(root, viewport.width, metrics);
    root->box.contentRect.x = root->box.margin.left + root->box.padding.left + root->box.border.left;
    root->box.contentRect.y = root->box.margin.top + root->box.padding.top + root->box.border.top;
}

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

bool pointInBox(const LayoutBox& box, float x, float y) {
    // Border box = content + padding + border
    float bx = box.contentRect.x - box.padding.left - box.border.left;
    float by = box.contentRect.y - box.padding.top - box.border.top;
    float bw = box.contentRect.width + box.padding.left + box.padding.right + box.border.left + box.border.right;
    float bh = box.contentRect.height + box.padding.top + box.padding.bottom + box.border.top + box.border.bottom;
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// Get z-index as integer (auto treated as 0 for ordering purposes)
int getZIndex(const css::ComputedStyle& style) {
    const std::string& z = styleVal(style, "z-index");
    if (z.empty() || z == "auto") return 0;
    try { return std::stoi(z); } catch (...) { return 0; }
}

// Check if an element creates a stacking context
bool createsStackingContext(const css::ComputedStyle& style) {
    const std::string& pos = styleVal(style, "position");
    const std::string& z = styleVal(style, "z-index");
    // Positioned element with z-index other than auto
    if ((pos == "absolute" || pos == "relative" || pos == "fixed" || pos == "sticky") &&
        !z.empty() && z != "auto") {
        return true;
    }
    // opacity < 1 creates a stacking context
    const std::string& op = styleVal(style, "opacity");
    if (!op.empty() && op != "1") {
        try {
            float opVal = std::stof(op);
            if (opVal < 1.0f) return true;
        } catch (...) {}
    }
    // transform other than none
    const std::string& tr = styleVal(style, "transform");
    if (!tr.empty() && tr != "none") return true;
    // filter other than none
    const std::string& ft = styleVal(style, "filter");
    if (!ft.empty() && ft != "none") return true;
    // isolation: isolate
    if (styleVal(style, "isolation") == "isolate") return true;
    return false;
}

// Parse translate(x, y) or translate(x) from a transform string.
// Returns {tx, ty}. Only handles translate(), translateX(), translateY() with px values.
std::pair<float, float> parseTranslate(const std::string& transform) {
    float tx = 0.0f, ty = 0.0f;
    size_t pos = 0;

    while (pos < transform.size()) {
        // Find translate functions
        size_t tpos = transform.find("translate", pos);
        if (tpos == std::string::npos) break;

        size_t nameEnd = tpos + 9; // past "translate"
        if (nameEnd >= transform.size()) break;

        bool isX = false, isY = false;
        if (transform[nameEnd] == 'X') { isX = true; nameEnd++; }
        else if (transform[nameEnd] == 'Y') { isY = true; nameEnd++; }

        // Find '('
        if (nameEnd >= transform.size() || transform[nameEnd] != '(') {
            pos = nameEnd;
            continue;
        }
        nameEnd++; // skip '('

        // Parse first value
        size_t closeParen = transform.find(')', nameEnd);
        if (closeParen == std::string::npos) break;

        std::string args = transform.substr(nameEnd, closeParen - nameEnd);

        // Split by comma
        auto parseVal = [](const std::string& s) -> float {
            float val = 0.0f;
            const char* begin = s.data();
            const char* end = begin + s.size();
            // Skip leading whitespace
            while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
            auto [ptr, ec] = std::from_chars(begin, end, val);
            // Assume px if no unit or explicit px
            return val;
        };

        size_t comma = args.find(',');
        if (isX) {
            tx += parseVal(args);
        } else if (isY) {
            ty += parseVal(args);
        } else if (comma != std::string::npos) {
            tx += parseVal(args.substr(0, comma));
            ty += parseVal(args.substr(comma + 1));
        } else {
            tx += parseVal(args);
        }

        pos = closeParen + 1;
    }

    return {tx, ty};
}

LayoutNode* hitTestRecursive(LayoutNode* node, float x, float y) {
    if (!node) return nullptr;

    auto& style = node->computedStyle();

    // Skip display:none and pointer-events:none
    if (styleVal(style, "display") == "none") return nullptr;
    if (styleVal(style, "pointer-events") == "none") return nullptr;
    // visibility:hidden still occupies space but is not hit-testable
    if (styleVal(style, "visibility") == "hidden") return nullptr;

    // Apply transform: adjust the test point by inverse translation
    float testX = x, testY = y;
    const std::string& transform = styleVal(style, "transform");
    if (!transform.empty() && transform != "none") {
        auto [tx, ty] = parseTranslate(transform);
        testX = x - tx;
        testY = y - ty;
    }

    // Check if point is within this node's border box (using transform-adjusted coords)
    if (!pointInBox(node->box, testX, testY)) return nullptr;

    // Overflow clipping: if overflow is hidden/scroll/auto, clip to border box
    const std::string& overflow = styleVal(style, "overflow");
    if (overflow == "hidden" || overflow == "scroll" || overflow == "auto") {
        // Point is already confirmed inside; children outside are clipped.
    }

    // Sort children by z-index for proper stacking context hit testing
    auto children = node->children();

    // Build z-index sorted list (higher z-index tested first = on top)
    std::vector<std::pair<int, LayoutNode*>> zChildren;
    zChildren.reserve(children.size());
    for (auto* child : children) {
        int z = 0;
        if (child) {
            z = getZIndex(child->computedStyle());
        }
        zChildren.push_back({z, child});
    }

    // Sort by z-index descending, preserving source order for equal z-index
    // (later source order = painted on top = tested first)
    std::stable_sort(zChildren.begin(), zChildren.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    for (auto& [z, child] : zChildren) {
        LayoutNode* hit = hitTestRecursive(child, testX, testY);
        if (hit) return hit;
    }

    // No child hit — this node is the deepest match
    return node;
}

// Clip a child's content rect to a parent's padding box (content + padding).
void clipToParentPaddingBox(LayoutBox& childBox, const LayoutBox& parentBox) {
    // Parent's padding box boundaries
    float px = parentBox.contentRect.x - parentBox.padding.left;
    float py = parentBox.contentRect.y - parentBox.padding.top;
    float pw = parentBox.contentRect.width + parentBox.padding.left + parentBox.padding.right;
    float ph = parentBox.contentRect.height + parentBox.padding.top + parentBox.padding.bottom;

    float cx = childBox.contentRect.x;
    float cy = childBox.contentRect.y;
    float cw = childBox.contentRect.width;
    float ch = childBox.contentRect.height;

    // Clip left
    if (cx < px) {
        float diff = px - cx;
        cw -= diff;
        cx = px;
    }
    // Clip top
    if (cy < py) {
        float diff = py - cy;
        ch -= diff;
        cy = py;
    }
    // Clip right
    if (cx + cw > px + pw) {
        cw = px + pw - cx;
    }
    // Clip bottom
    if (cy + ch > py + ph) {
        ch = py + ph - cy;
    }

    // Clamp to non-negative
    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;

    childBox.contentRect.x = cx;
    childBox.contentRect.y = cy;
    childBox.contentRect.width = cw;
    childBox.contentRect.height = ch;
}

void applyOverflowClippingRecursive(LayoutNode* node, bool parentClips, const LayoutBox* clipBox) {
    if (!node) return;

    auto& style = node->computedStyle();
    if (styleVal(style, "display") == "none") return;

    // If parent clips and this node extends outside, clip it
    if (parentClips && clipBox) {
        clipToParentPaddingBox(node->box, *clipBox);
    }

    // Check if this node clips its children
    const std::string& overflow = styleVal(style, "overflow");
    bool thisClips = (overflow == "hidden" || overflow == "scroll" || overflow == "auto");

    for (auto* child : node->children()) {
        applyOverflowClippingRecursive(child, thisClips, thisClips ? &node->box : nullptr);
    }
}

} // anonymous namespace

void applyOverflowClipping(LayoutNode* root) {
    applyOverflowClippingRecursive(root, false, nullptr);
}

LayoutNode* hitTest(LayoutNode* root, float x, float y) {
    return hitTestRecursive(root, x, y);
}

} // namespace htmlayout::layout
