#pragma once
#include "css/cascade.h"
#include "css/properties.h"
#include "layout/box.h"
#include <string>
#include <string_view>

namespace htmlayout::layout {

// Look up a CSS property in a computed style, falling back to its initial value
// if not present. Returns a const reference (no allocation).
//
// The key is a string_view and both maps behind this are heterogeneous, so a
// literal at the call site is hashed where it sits. Taking a const std::string&
// here — the old signature — silently constructed one per call, which for the
// property names past the 15-char small-string limit meant a malloc and a free
// on every read of every node, every pass.
inline const std::string& styleVal(const css::ComputedStyle& style, std::string_view prop) {
    layoutStatsMut().styleLookups++;
    auto it = style.find(prop);
    if (it != style.end()) return it->second;
    return css::initialValueRef(prop);
}

} // namespace htmlayout::layout
