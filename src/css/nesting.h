#pragma once
// Internal: css-nesting-1 selector desugaring for the parser.
#include <string>
#include <string_view>
#include <vector>

namespace htmlayout::css::nesting {

// Split a selector list on top-level commas (outside (), [], and strings);
// each part is trimmed and empty parts are dropped.
std::vector<std::string> splitSelectorList(std::string_view list);

// Resolve a nested rule's selector list against its parent's (already flat)
// selector alternatives, returning flat selectors with `&` substituted and
// relative selectors (`> .c`, `.c`) anchored to the parent.
std::vector<std::string> resolve(std::string_view nestedList,
                                 const std::vector<std::string>& parents);

} // namespace htmlayout::css::nesting
