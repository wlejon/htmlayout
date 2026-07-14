#pragma once
#include "css/cascade.h"
#include "css/properties.h"
#include "layout/box.h"
#include <string>
#include <string_view>
#ifdef HTMLAYOUT_STYLE_PROFILE
#include <source_location>
#endif

namespace htmlayout::layout {

// Look up a CSS property in a computed style, falling back to its initial value
// if not present. Returns a const reference (no allocation).
//
// The key is a string_view and both maps behind this are heterogeneous, so a
// literal at the call site is hashed where it sits. Taking a const std::string&
// here — the old signature — silently constructed one per call, which for the
// property names past the 15-char small-string limit meant a malloc and a free
// on every read of every node, every pass.
//
// Under -DHTMLAYOUT_STYLE_PROFILE every call also records where it came from, so
// the pass's 671,647 lookups can be attributed to a property and to a line. The
// answer that profile gives is that there is nothing here to patch: the busiest
// single line is 6.5% of the total, and behind it sit ~200 sites at ~1% each,
// every one of them reading a property the node's style has held unchanged since
// the last time it was asked. See computed_style.h for what replaces them.
#ifdef HTMLAYOUT_STYLE_PROFILE
inline const std::string& styleVal(const css::ComputedStyle& style, std::string_view prop,
                                   std::source_location loc = std::source_location::current()) {
    layoutStatsMut().styleLookups++;
    recordStyleLookup(prop, loc.file_name(), loc.line());
#else
inline const std::string& styleVal(const css::ComputedStyle& style, std::string_view prop) {
    layoutStatsMut().styleLookups++;
#endif
    auto it = style.find(prop);
    if (it != style.end()) return it->second;
    return css::initialValueRef(prop);
}

} // namespace htmlayout::layout
