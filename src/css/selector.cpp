#include "css/selector.h"

namespace htmlayout::css {

bool Selector::matches(const ElementRef& elem) const {
    // TODO: Implement selector matching
    return false;
}

Selector parseSelector(const std::string& text) {
    // TODO: Implement selector parsing
    return {text, calculateSpecificity(text)};
}

std::vector<Selector> parseSelectorList(const std::string& text) {
    // TODO: Implement comma-separated selector list parsing
    return {};
}

uint32_t calculateSpecificity(const std::string& selector) {
    // TODO: Implement specificity calculation
    return 0;
}

} // namespace htmlayout::css
