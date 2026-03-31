#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

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
    virtual int childIndex() const = 0;          // 0-based index among siblings
    virtual int childIndexOfType() const = 0;    // 0-based index among same-tag siblings
    virtual int siblingCount() const = 0;         // total sibling count
    virtual int siblingCountOfType() const = 0;   // same-tag sibling count

    // Dynamic pseudo-class state
    virtual bool isHovered() const { return false; }
    virtual bool isFocused() const { return false; }
    virtual bool isActive() const { return false; }

    // Shadow DOM: which scope does this element belong to?
    // nullptr = document scope. Non-null = shadow root scope.
    virtual void* scope() const { return nullptr; }
};

// ---- Internal selector representation ----

enum class SimpleSelectorType {
    Universal,      // *
    Tag,            // div, p, span
    Class,          // .classname
    Id,             // #id
    Attribute,      // [attr], [attr=val], etc.
    PseudoClass,    // :first-child, :hover, etc.
    PseudoElement,  // ::before, ::after
};

enum class AttrMatchOp {
    Exists,         // [attr]
    Equals,         // [attr=val]
    Includes,       // [attr~=val]  (whitespace-separated list contains)
    DashMatch,      // [attr|=val]  (equals or starts with val-)
    Prefix,         // [attr^=val]
    Suffix,         // [attr$=val]
    Substring,      // [attr*=val]
};

struct SimpleSelector {
    SimpleSelectorType type;
    std::string value;          // tag name, class, id, pseudo name
    // Attribute selector fields
    std::string attrName;
    std::string attrValue;
    AttrMatchOp attrOp = AttrMatchOp::Exists;
    // :nth-child(an+b) coefficients
    int nthA = 0;
    int nthB = 0;
    // :not() argument (parsed sub-selectors)
    std::vector<SimpleSelector> notArg;
    // :host() argument (parsed sub-selectors)
    std::vector<SimpleSelector> hostArg;
};

struct CompoundSelector {
    std::vector<SimpleSelector> simples;
};

enum class Combinator {
    None,               // start / rightmost compound
    Descendant,         // space
    Child,              // >
    AdjacentSibling,    // +
    GeneralSibling,     // ~
};

// A complete selector is a chain of compound selectors connected by combinators.
// Stored right-to-left: entries[0] is the subject (rightmost), entries[1..n]
// progress toward ancestors/siblings with their connecting combinator.
struct SelectorChain {
    struct Entry {
        CompoundSelector compound;
        Combinator combinator = Combinator::None;
    };
    std::vector<Entry> entries;
};

// Parsed selector (compiled for fast matching)
struct Selector {
    std::string raw;            // original text
    uint32_t specificity = 0;   // CSS specificity as single comparable value
    SelectorChain chain;        // compiled representation

    // Match this selector against an element
    bool matches(const ElementRef& elem) const;
};

// Parse a selector string into a Selector
Selector parseSelector(const std::string& text);

// Parse a comma-separated selector list
std::vector<Selector> parseSelectorList(const std::string& text);

// Calculate specificity for a selector
uint32_t calculateSpecificity(const std::string& selector);

// Match a single simple selector against an element (used by cascade for pseudo-elements)
bool matchSimple(const SimpleSelector& ss, const ElementRef& elem);

} // namespace htmlayout::css
