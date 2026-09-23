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

// A top-level rule's selector list. An alternative using `&` outside any
// style rule resolves it to the scoping root, `:scope` (the root element
// outside @scope); alternatives without `&` are returned as they are — at the
// top level they are not relative.
std::vector<std::string> resolveTopLevel(std::string_view list);

} // namespace htmlayout::css::nesting
