#pragma once
#include "css/parser.h"
#include "css/selector.h"
#include "css/properties.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace htmlayout::css {

// Computed style: the final resolved CSS properties for one element
using ComputedStyle = std::unordered_map<std::string, std::string>;

// Stylesheet origin for the cascade
enum class Origin { UserAgent, Author };

// A cascade context resolves which CSS rules apply to which elements.
// It supports scoping for shadow DOM: rules in a shadow scope only match
// elements in that same scope.
class Cascade {
public:
    // Add a stylesheet to a given scope.
    // scope = nullptr means document-level (global).
    // scope = shadow_root_ptr means shadow-scoped styles.
    // If a MediaContext is provided, @media blocks are conditionally included.
    // origin distinguishes UA from author styles (used by the `revert` keyword).
    void addStylesheet(const Stylesheet& sheet, void* scope = nullptr,
                       const MediaContext* media = nullptr,
                       Origin origin = Origin::Author);

    // Resolve computed style for an element.
    // Considers: author styles, inline styles, inheritance, initial values.
    // Only matches rules whose scope matches the element's scope.
    // parentStyle: the computed style of the parent element (for inheritance).
    //   Pass nullptr for root elements.
    ComputedStyle resolve(const ElementRef& elem,
                          const std::string& inlineStyle = {},
                          const ComputedStyle* parentStyle = nullptr) const;

    // Resolve computed style for a pseudo-element (::before or ::after).
    // Returns an empty style if no rules target this pseudo-element.
    // The "content" property determines what text to generate.
    // elemStyle is the computed style of the originating element (for inheritance).
    ComputedStyle resolvePseudo(const ElementRef& elem,
                                const std::string& pseudoName,
                                const ComputedStyle& elemStyle) const;

    // Clear all stylesheets
    void clear();

private:
    struct ScopedRule {
        Selector selector;
        std::vector<Declaration> declarations;
        void* scope = nullptr;  // which shadow root, or nullptr for global
        size_t order = 0;       // insertion order for stable sort
        int layerOrder = -1;    // -1 = unlayered (highest priority), >=0 = layer index
        Origin origin = Origin::Author;
        // Container query: if non-empty, this rule only applies when the container condition is met
        std::string containerName;     // required container name (empty = any)
        std::string containerCondition; // e.g. "(min-width: 400px)"
    };
    std::vector<ScopedRule> rules_;
    size_t nextOrder_ = 0;

    // Layer ordering: maps layer name -> index. Lower index = lower priority.
    std::vector<std::string> layerNames_;
    int getOrCreateLayerIndex(const std::string& name);

    // Evaluate a container query condition against an element's container ancestors
    bool evaluateContainerQuery(const ElementRef& elem,
                                const std::string& containerName,
                                const std::string& condition) const;
};

} // namespace htmlayout::css
