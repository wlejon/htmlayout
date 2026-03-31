#pragma once
#include "css/parser.h"
#include "css/selector.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace htmlayout::css {

// Computed style: the final resolved CSS properties for one element
using ComputedStyle = std::unordered_map<std::string, std::string>;

// A cascade context resolves which CSS rules apply to which elements.
// It supports scoping for shadow DOM: rules in a shadow scope only match
// elements in that same scope.
class Cascade {
public:
    // Add a stylesheet to a given scope.
    // scope = nullptr means document-level (global).
    // scope = shadow_root_ptr means shadow-scoped styles.
    void addStylesheet(const Stylesheet& sheet, void* scope = nullptr);

    // Resolve computed style for an element.
    // Considers: author styles, inline styles, inheritance, initial values.
    // Only matches rules whose scope matches the element's scope.
    // parentStyle: the computed style of the parent element (for inheritance).
    //   Pass nullptr for root elements.
    ComputedStyle resolve(const ElementRef& elem,
                          const std::string& inlineStyle = {},
                          const ComputedStyle* parentStyle = nullptr) const;

    // Clear all stylesheets
    void clear();

private:
    struct ScopedRule {
        Selector selector;
        std::vector<Declaration> declarations;
        void* scope = nullptr;  // which shadow root, or nullptr for global
        size_t order = 0;       // insertion order for stable sort
    };
    std::vector<ScopedRule> rules_;
    size_t nextOrder_ = 0;
};

} // namespace htmlayout::css
