#include "layout/formatting_context.h"
#include "layout/block.h"
#include "layout/inline.h"
#include "layout/flex.h"
#include <cctype>
#include <charconv>
#include <cmath>

namespace htmlayout::layout {

namespace {

// Get a style value with fallback
const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

} // anonymous namespace

float resolveLength(const std::string& value, float referenceSize, float fontSize) {
    if (value.empty() || value == "auto" || value == "none" || value == "normal") {
        return 0.0f;
    }

    // Try to parse a number followed by an optional unit
    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;

    auto [ptr, ec] = std::from_chars(begin, end, num);
    if (ec != std::errc()) {
        // Not a valid number — try named border widths
        if (value == "thin") return 1.0f;
        if (value == "medium") return 3.0f;
        if (value == "thick") return 5.0f;
        return 0.0f;
    }

    // Extract the unit from whatever remains
    std::string unit(ptr, end);

    if (unit.empty() || unit == "px") return num;
    if (unit == "em") return num * fontSize;
    if (unit == "rem") return num * 16.0f; // root em, assume 16px default
    if (unit == "%") return num * referenceSize / 100.0f;
    if (unit == "vw") return num * referenceSize / 100.0f; // approximate
    if (unit == "pt") return num * 96.0f / 72.0f;

    return num; // unknown unit, treat as px
}

Edges resolveEdges(const css::ComputedStyle& style,
                   const std::string& prefix,
                   float referenceWidth,
                   float fontSize) {
    Edges e;
    e.top = resolveLength(styleVal(style, prefix + "-top"), referenceWidth, fontSize);
    e.right = resolveLength(styleVal(style, prefix + "-right"), referenceWidth, fontSize);
    e.bottom = resolveLength(styleVal(style, prefix + "-bottom"), referenceWidth, fontSize);
    e.left = resolveLength(styleVal(style, prefix + "-left"), referenceWidth, fontSize);
    return e;
}

void layoutNode(LayoutNode* node, float availableWidth, TextMetrics& metrics) {
    if (!node) return;

    auto& style = node->computedStyle();
    const std::string& display = styleVal(style, "display");

    if (display == "none") {
        // Hidden — zero-size box, skip children
        node->box = LayoutBox{};
        return;
    }

    if (display == "flex" || display == "inline-flex") {
        layoutFlex(node, availableWidth, metrics);
    } else if (display == "inline" || display == "inline-block") {
        layoutInline(node, availableWidth, metrics);
    } else {
        // block, list-item, or anything else defaults to block layout
        layoutBlock(node, availableWidth, metrics);
    }
}

} // namespace htmlayout::layout
