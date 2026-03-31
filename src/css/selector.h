#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace htmlayout::css {

// Abstract interface for an element that selectors can match against.
// Consumers (like bro) implement this to bridge their DOM.
struct ElementRef {
    virtual ~ElementRef() = default;
    virtual std::string tagName() const = 0;
    virtual std::string id() const = 0;
    virtual std::string className() const = 0;
    virtual std::string getAttribute(const std::string& name) const = 0;
    virtual bool hasAttribute(const std::string& name) const = 0;
    virtual ElementRef* parent() const = 0;
    virtual std::vector<ElementRef*> children() const = 0;
    virtual int childIndex() const = 0;          // index among siblings
    virtual int childIndexOfType() const = 0;    // index among same-tag siblings

    // Shadow DOM: which scope does this element belong to?
    // nullptr = document scope. Non-null = shadow root scope.
    virtual void* scope() const { return nullptr; }
};

// Parsed selector (compiled for fast matching)
struct Selector {
    std::string raw;            // original text
    uint32_t specificity = 0;   // CSS specificity as single comparable value

    // Match this selector against an element
    bool matches(const ElementRef& elem) const;
};

// Parse a selector string into a Selector
Selector parseSelector(const std::string& text);

// Parse a comma-separated selector list
std::vector<Selector> parseSelectorList(const std::string& text);

// Calculate specificity for a selector
uint32_t calculateSpecificity(const std::string& selector);

} // namespace htmlayout::css
