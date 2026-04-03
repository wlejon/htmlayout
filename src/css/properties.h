#pragma once
#include <string>
#include <vector>
#include <unordered_set>

namespace htmlayout::css {

// CSS property metadata
struct PropertyDef {
    std::string name;
    std::string initialValue;
    bool inherited;         // does this property inherit from parent?
};

// Registry of known CSS properties and their defaults
const std::vector<PropertyDef>& knownProperties();

// Check if a property is inherited
bool isInherited(const std::string& property);

// Get the initial (default) value for a property
std::string initialValue(const std::string& property);

// Get the initial value by const reference (no allocation). Returns "" for unknown properties.
const std::string& initialValueRef(const std::string& property);

// Expand CSS shorthand properties into longhands.
// e.g. "margin: 10px" -> [margin-top:10px, margin-right:10px, ...]
struct ExpandedDecl {
    std::string property;
    std::string value;
};
std::vector<ExpandedDecl> expandShorthand(const std::string& property,
                                           const std::string& value);

} // namespace htmlayout::css
