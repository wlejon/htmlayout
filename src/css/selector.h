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

    // Shadow DOM: is this element a shadow host? (has an attached shadow root)
    // If so, returns a pointer to its shadow root (used as scope for inner elements).
    virtual void* shadowRoot() const { return nullptr; }

    // Slot distribution: which slot is this element assigned to?
    // Returns nullptr if not slotted.
    virtual ElementRef* assignedSlot() const { return nullptr; }

    // CSS Parts: the part name(s) exposed by this element (space-separated).
    virtual std::string partName() const { return ""; }

    // Custom elements: is this element defined (registered)?
    virtual bool isDefined() const { return true; }

    // Container queries: the container type of this element.
    // Valid values: "none", "inline-size", "size"
    virtual std::string containerType() const { return "none"; }

    // Container queries: the container name of this element (space-separated names).
    virtual std::string containerName() const { return ""; }

    // Container queries: the current inline size of this element's content box.
    virtual float containerInlineSize() const { return 0; }

    // Container queries: the current block size of this element's content box.
    virtual float containerBlockSize() const { return 0; }
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

struct CompoundSelector; // forward declaration for selectorListArg

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
    // Attribute case-insensitive flag [attr=val i]
    bool attrCaseInsensitive = false;
    // :not() argument (parsed sub-selectors)
    std::vector<SimpleSelector> notArg;
    // :host() / :host-context() argument (parsed sub-selectors)
    std::vector<SimpleSelector> hostArg;
    // :is()/:where()/:has() arguments (selector list, stored as compound selectors)
    std::vector<CompoundSelector> selectorListArg;
    // ::slotted() argument (parsed compound selector)
    std::vector<SimpleSelector> slottedArg;
    // ::part() argument (part name)
    std::string partArg;
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
