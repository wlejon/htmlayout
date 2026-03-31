#include "css/cascade.h"

namespace htmlayout::css {

void Cascade::addStylesheet(const Stylesheet& sheet, void* scope) {
    for (auto& rule : sheet.rules) {
        auto selectors = parseSelectorList(rule.selector);
        for (auto& sel : selectors) {
            rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++});
        }
    }
}

ComputedStyle Cascade::resolve(const ElementRef& elem,
                                const std::string& inlineStyle) const {
    // TODO: Implement cascade resolution
    // 1. Collect all matching rules where rule.scope == elem.scope()
    // 2. Sort by specificity, then by order
    // 3. Apply declarations in order (later wins)
    // 4. Apply inline style (highest specificity)
    // 5. Inherit inheritable properties from parent
    return {};
}

void Cascade::clear() {
    rules_.clear();
    nextOrder_ = 0;
}

} // namespace htmlayout::css
