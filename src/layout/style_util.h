#pragma once
#include "css/cascade.h"
#include "css/properties.h"
#include <string>

namespace htmlayout::layout {

// Look up a CSS property in a computed style, falling back to its initial value
// if not present. Returns a const reference (no allocation).
inline const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    auto it = style.find(prop);
    if (it != style.end()) return it->second;
    return css::initialValueRef(prop);
}

} // namespace htmlayout::layout
