#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

namespace htmlayout::css {

// Heterogeneous lookup for the string-keyed maps below (and ComputedStyle, and
// Cascade's rule buckets). Without `is_transparent` every probe of a
// std::string-keyed map has to *construct a std::string* from whatever the
// caller had — a literal, a string_view into the DOM — and any property name
// past the 15-char small-string limit ("border-bottom-width",
// "grid-template-columns") means a malloc and a free per lookup. Layout does
// hundreds of thousands of these per pass, so the key must be probed, not built.
struct SvHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const {
        return std::hash<std::string_view>{}(s);
    }
};
struct SvEq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const { return a == b; }
};

// CSS property metadata
struct PropertyDef {
    std::string name;
    std::string initialValue;
    bool inherited;         // does this property inherit from parent?
};

// Registry of known CSS properties and their defaults
const std::vector<PropertyDef>& knownProperties();

// Check if a property is inherited
bool isInherited(std::string_view property);

// Get the initial (default) value for a property
std::string initialValue(std::string_view property);

// Get the initial value by const reference (no allocation). Returns "" for unknown properties.
const std::string& initialValueRef(std::string_view property);

// Expand CSS shorthand properties into longhands.
// e.g. "margin: 10px" -> [margin-top:10px, margin-right:10px, ...]
struct ExpandedDecl {
    std::string property;
    std::string value;
};
std::vector<ExpandedDecl> expandShorthand(const std::string& property,
                                           const std::string& value);

} // namespace htmlayout::css
