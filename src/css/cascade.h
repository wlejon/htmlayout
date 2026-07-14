#pragma once
#include "css/parser.h"
#include "css/selector.h"
#include "css/properties.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace htmlayout::css {

// Computed style: the final resolved CSS properties for one element
using ComputedStyle = std::unordered_map<std::string, std::string>;

// Stylesheet origin for the cascade
enum class Origin { UserAgent, Author };

// Callback to resolve @import URLs. Returns the CSS text for the given URL.
// The consumer is responsible for file I/O, URL resolution, etc.
using ImportResolver = std::function<std::string(const std::string& url)>;

// A cascade context resolves which CSS rules apply to which elements.
// It supports scoping for shadow DOM: rules in a shadow scope only match
// elements in that same scope.
class Cascade {
public:
    // Set a callback to resolve @import URLs into CSS text.
    // When set, addStylesheet() will automatically resolve and inline imports.
    // Each URL is resolved at most once (cached by URL string).
    void setImportResolver(ImportResolver resolver);

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

    // Access stored @keyframes rules (populated by addStylesheet)
    const std::vector<KeyframeBlock>& keyframes() const { return keyframes_; }

    // Access stored @font-face rules (populated by addStylesheet)
    const std::vector<FontFaceRule>& fontFaces() const { return fontFaces_; }

    // True if any added rule uses the :hover pseudo-class anywhere in its
    // selector (including nested inside :not()/:is()/:where()/:has()).
    // Lets the consumer skip all hover-driven restyle work — a hover-target
    // change cannot alter computed style on a page with no :hover rules.
    bool usesHoverPseudo() const { return usesHover_; }

    // True if any @container rule was added. Container queries read the
    // container's laid-out size, which trails style resolution by one layout
    // pass — consumers should re-resolve styles once after layout when this
    // is set.
    bool usesContainerQueries() const { return usesContainers_; }

    // True if any rule targets ::<name> (e.g. "before", "after"). A document
    // with no such rule can generate no content, so the consumer can skip the
    // whole generated-content pass instead of asking every element.
    bool hasPseudoElementRules(const std::string& name) const {
        auto it = pseudoRules_.find(name);
        return it != pseudoRules_.end() && !it->second.empty();
    }

    // Does a `content` value depend on state that accumulates across the whole
    // document — a counter scope, or the quote-nesting depth — rather than only
    // on its originating element? These are the only generated-content features
    // that force a document-order pass over every element; anything else (a
    // literal, attr(), a url()) a consumer can resolve for one element alone.
    //
    // Test the value a pseudo-element actually RESOLVED, not the stylesheet: a
    // UA sheet carries `q::before { content: open-quote }`, so "does any rule
    // use quotes" is true for every document ever and gates nothing.
    static bool contentIsStateful(const std::string& v) {
        return v.find("counter(") != std::string::npos ||
               v.find("counters(") != std::string::npos ||
               v.find("open-quote") != std::string::npos ||   // also no-open-quote
               v.find("close-quote") != std::string::npos;    // also no-close-quote
    }

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
        // Pre-classified selector type (avoids scanning simples per-element)
        bool isHostSelector = false;
        bool isSlottedSelector = false;
        bool isPartSelector = false;
    };
    // Does this simple selector (or anything nested in its :not()/:is()/
    // :where()/:has()/:host() args) use the :hover pseudo-class?
    static bool simpleMentionsHover(const SimpleSelector& s) {
        if (s.type == SimpleSelectorType::PseudoClass && s.value == "hover")
            return true;
        for (auto& n : s.notArg)     if (simpleMentionsHover(n)) return true;
        for (auto& n : s.hostArg)    if (simpleMentionsHover(n)) return true;
        for (auto& n : s.slottedArg) if (simpleMentionsHover(n)) return true;
        for (auto& c : s.selectorListArg)
            for (auto& sub : c.simples) if (simpleMentionsHover(sub)) return true;
        for (auto& rel : s.relativeArgs)
            for (auto& entry : rel.chain.entries)
                for (auto& sub : entry.compound.simples)
                    if (simpleMentionsHover(sub)) return true;
        return false;
    }

    // Classify :host/:slotted/::part flags after inserting a rule
    void classifyLastRule() {
        auto& rule = rules_.back();
        // Track whether any rule anywhere uses :hover (across every compound in
        // the chain, not just the subject) so the consumer can skip hover
        // restyle work on pages with no :hover rules.
        if (!usesHover_) {
            for (auto& entry : rule.selector.chain.entries) {
                for (auto& s : entry.compound.simples) {
                    if (simpleMentionsHover(s)) { usesHover_ = true; break; }
                }
                if (usesHover_) break;
            }
        }
        if (rule.selector.chain.entries.empty()) return;
        size_t idx = rules_.size() - 1;
        for (auto& s : rule.selector.chain.entries[0].compound.simples) {
            if (s.type == SimpleSelectorType::PseudoClass &&
                (s.value == "host" || s.value == "host-context")) {
                rule.isHostSelector = true;
            } else if (s.type == SimpleSelectorType::PseudoElement && s.value == "slotted") {
                rule.isSlottedSelector = true;
            } else if (s.type == SimpleSelectorType::PseudoElement && s.value == "part") {
                rule.isPartSelector = true;
            } else if (s.type == SimpleSelectorType::PseudoElement) {
                // Bucket by pseudo-element name so resolvePseudo() considers only
                // the handful of rules that could target it, rather than rescanning
                // every rule in the sheet (UA sheet included) once per element.
                pseudoRules_[s.value].push_back(idx);
            }
        }
    }

    std::vector<ScopedRule> rules_;
    std::vector<KeyframeBlock> keyframes_;
    std::vector<FontFaceRule> fontFaces_;
    // Pseudo-element name -> indices into rules_ whose subject targets it.
    // Indices stay valid: rules_ is only appended to, and clear() drops both.
    std::unordered_map<std::string, std::vector<size_t>> pseudoRules_;
    size_t nextOrder_ = 0;
    bool usesHover_ = false;  // any rule uses :hover (set in classifyLastRule)
    bool usesContainers_ = false; // any @container rule added

    // @import resolution
    ImportResolver importResolver_;
    std::unordered_set<std::string> loadedImports_;

    // Layer ordering: maps layer name -> index. Lower index = lower priority.
    std::vector<std::string> layerNames_;
    int getOrCreateLayerIndex(const std::string& name);

    // Evaluate a container query condition against an element's container ancestors
    bool evaluateContainerQuery(const ElementRef& elem,
                                const std::string& containerName,
                                const std::string& condition) const;
};

} // namespace htmlayout::css
