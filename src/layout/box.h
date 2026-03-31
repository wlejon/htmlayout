#pragma once
#include "css/cascade.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace htmlayout::layout {

// A positioned rectangle
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

// Edge values (margin, padding, border)
struct Edges {
    float top = 0, right = 0, bottom = 0, left = 0;
};

// The output of layout: a positioned box with resolved geometry
struct LayoutBox {
    Rect contentRect;       // content area position and size
    Edges margin;
    Edges padding;
    Edges border;

    // Full box dimensions including padding + border
    float fullWidth() const { return contentRect.width + padding.left + padding.right + border.left + border.right; }
    float fullHeight() const { return contentRect.height + padding.top + padding.bottom + border.top + border.bottom; }

    // Outer box including margin
    Rect marginBox() const {
        return {
            contentRect.x - padding.left - border.left - margin.left,
            contentRect.y - padding.top - border.top - margin.top,
            fullWidth() + margin.left + margin.right,
            fullHeight() + margin.top + margin.bottom
        };
    }
};

// Abstract interface for a node in the layout tree.
// Consumers implement this to bridge their DOM.
struct LayoutNode {
    virtual ~LayoutNode() = default;

    // Identity
    virtual std::string tagName() const = 0;
    virtual bool isTextNode() const = 0;
    virtual std::string textContent() const = 0;

    // Tree
    virtual LayoutNode* parent() const = 0;
    virtual std::vector<LayoutNode*> children() const = 0;

    // Computed style (from the CSS cascade)
    virtual const css::ComputedStyle& computedStyle() const = 0;

    // Output: layout writes the positioned box here
    LayoutBox box;
};

// Text measurement callback — consumers provide this (e.g. via Skia)
struct TextMetrics {
    virtual ~TextMetrics() = default;
    virtual float measureWidth(const std::string& text,
                                const std::string& fontFamily,
                                float fontSize,
                                const std::string& fontWeight) = 0;
    virtual float lineHeight(const std::string& fontFamily,
                              float fontSize,
                              const std::string& fontWeight) = 0;
};

// Perform layout on a tree, computing LayoutBox for every node.
// viewportWidth is the available width for the root element.
void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics);

// Apply overflow clipping to a laid-out tree.
// Children of nodes with overflow:hidden/scroll/auto are clipped to the parent's
// content+padding box. Call after layoutTree().
void applyOverflowClipping(LayoutNode* root);

// Hit test: find the deepest LayoutNode at a given point
LayoutNode* hitTest(LayoutNode* root, float x, float y);

} // namespace htmlayout::layout
