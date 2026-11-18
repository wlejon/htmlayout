#include "css/selector.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace htmlayout::css {

namespace {

std::string toLowerMatch(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::vector<std::string> splitWhitespace(std::string_view s) {
    std::vector<std::string> parts;
    std::istringstream iss((std::string(s)));
    std::string part;
    while (iss >> part) parts.push_back(part);
    return parts;
}

// Scans the class attribute in place. This runs for every class simple of
// every candidate rule, for every ancestor a descendant combinator walks —
// splitting the attribute into a vector of strings per call was most of the
// cost of matching.
bool hasClass(const ElementRef& elem, const std::string& cls) {
    if (cls.empty()) return false;
    std::string_view classes = elem.className();
    size_t pos = 0;
    while (pos < classes.size()) {
        size_t start = classes.find_first_not_of(" \t\n\r\f", pos);
        if (start == std::string_view::npos) return false;
        size_t end = classes.find_first_of(" \t\n\r\f", start);
        if (end == std::string_view::npos) end = classes.size();
        if (end - start == cls.size() &&
            classes.compare(start, cls.size(), cls) == 0)
            return true;
        pos = end;
    }
    return false;
}

// ASCII case-insensitive equality against an already-lowercase name, without
// building a lowercased copy of the tag per comparison.
bool equalsLowered(std::string_view tag, const std::string& lowered) {
    if (tag.size() != lowered.size()) return false;
    for (size_t i = 0; i < tag.size(); i++) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i]))) != lowered[i])
            return false;
    }
    return true;
}

bool matchNth(int a, int b, int index) {
    if (a == 0) return index == b;
    int n = index - b;
    if (a < 0) return n <= 0 && (n % a == 0);
    return n >= 0 && (n % a == 0);
}

bool isDescendantOf(const ElementRef* e, const ElementRef* scope) {
    for (ElementRef* p = e->parent(); p; p = p->parent())
        if (p == scope) return true;
    return false;
}

template <typename F>
bool anyDescendant(const ElementRef& root, F&& f) {
    for (ElementRef* c : root.children()) {
        if (f(*c)) return true;
        if (anyDescendant(*c, f)) return true;
    }
    return false;
}

bool matchChainTop(const SelectorChain& chain, const ElementRef& elem,
                   const ElementRef** topOut) {
    if (chain.entries.empty()) return false;

    // entries[0] is the subject
    if (!matchCompound(chain.entries[0].compound, elem)) return false;

    // Now match the rest (entries[1..n]) walking the tree
    const ElementRef* current = &elem;
    for (size_t i = 1; i < chain.entries.size(); i++) {
        auto& entry = chain.entries[i];
        bool found = false;

        switch (entry.combinator) {
            case Combinator::Child: {
                ElementRef* p = current->parent();
                if (!p || !matchCompound(entry.compound, *p)) return false;
                current = p;
                found = true;
                break;
            }
            case Combinator::Descendant: {
                ElementRef* p = current->parent();
                while (p) {
                    if (matchCompound(entry.compound, *p)) {
                        current = p;
                        found = true;
                        break;
                    }
                    p = p->parent();
                }
                if (!found) return false;
                break;
            }
            case Combinator::AdjacentSibling: {
                ElementRef* p = current->parent();
                if (!p) return false;
                int idx = current->childIndex();
                if (idx <= 0) return false;
                auto siblings = p->children();
                if (idx < static_cast<int>(siblings.size()) &&
                    matchCompound(entry.compound, *siblings[idx - 1])) {
                    current = siblings[idx - 1];
                    found = true;
                } else {
                    return false;
                }
                break;
            }
            case Combinator::GeneralSibling: {
                ElementRef* p = current->parent();
                if (!p) return false;
                int idx = current->childIndex();
                auto siblings = p->children();
                for (int j = idx - 1; j >= 0; j--) {
                    if (matchCompound(entry.compound, *siblings[j])) {
                        current = siblings[j];
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
                break;
            }
            default:
                return false;
        }
    }
    if (topOut) *topOut = current;
    return true;
}

// :has(rel) — does any element match the relative selector with the :has()
// element as scope? The chain matches bottom-up from a candidate subject; the
// topmost matched element must then relate to the scope by the lead combinator.
bool matchHas(const SimpleSelector& ss, const ElementRef& scope) {
    for (const auto& rel : ss.relativeArgs) {
        auto tryMatch = [&](const ElementRef& cand) -> bool {
            const ElementRef* top = nullptr;
            if (!matchChainTop(rel.chain, cand, &top)) return false;
            switch (rel.lead) {
                case Combinator::Child:
                    return top->parent() == &scope;
                case Combinator::None:
                case Combinator::Descendant:
                    return isDescendantOf(top, &scope);
                case Combinator::AdjacentSibling:
                    return top->parent() == scope.parent() && scope.parent() &&
                           top->childIndex() == scope.childIndex() + 1;
                case Combinator::GeneralSibling:
                    return top->parent() == scope.parent() && scope.parent() &&
                           top->childIndex() > scope.childIndex();
            }
            return false;
        };

        bool matched = false;
        if (rel.lead == Combinator::AdjacentSibling ||
            rel.lead == Combinator::GeneralSibling) {
            if (ElementRef* p = scope.parent()) {
                auto sibs = p->children();
                for (int j = scope.childIndex() + 1;
                     j < static_cast<int>(sibs.size()) && !matched; j++) {
                    if (tryMatch(*sibs[j]) || anyDescendant(*sibs[j], tryMatch))
                        matched = true;
                }
            }
        } else {
            matched = anyDescendant(scope, tryMatch);
        }
        if (matched) return true;
    }
    return false;
}

} // namespace

bool matchSimple(const SimpleSelector& ss, const ElementRef& elem) {
    switch (ss.type) {
        case SimpleSelectorType::Universal:
            return true;
        case SimpleSelectorType::Tag:
            return equalsLowered(elem.tagName(), ss.value);
        case SimpleSelectorType::Class:
            return hasClass(elem, ss.value);
        case SimpleSelectorType::Id:
            return elem.id() == ss.value;
        case SimpleSelectorType::Attribute: {
            if (!elem.hasAttribute(ss.attrName)) return false;
            if (ss.attrOp == AttrMatchOp::Exists) return true;
            std::string val(elem.getAttribute(ss.attrName));
            std::string cmpVal = ss.attrValue;
            if (ss.attrCaseInsensitive) {
                val = toLowerMatch(val);
                cmpVal = toLowerMatch(cmpVal);
            }
            switch (ss.attrOp) {
                case AttrMatchOp::Equals:
                    return val == cmpVal;
                case AttrMatchOp::Includes: {
                    auto parts = splitWhitespace(val);
                    return std::find(parts.begin(), parts.end(), cmpVal) != parts.end();
                }
                case AttrMatchOp::DashMatch:
                    return val == cmpVal ||
                           (val.size() > cmpVal.size() &&
                            val.substr(0, cmpVal.size()) == cmpVal &&
                            val[cmpVal.size()] == '-');
                case AttrMatchOp::Prefix:
                    return !cmpVal.empty() && val.size() >= cmpVal.size() &&
                           val.substr(0, cmpVal.size()) == cmpVal;
                case AttrMatchOp::Suffix:
                    return !cmpVal.empty() && val.size() >= cmpVal.size() &&
                           val.substr(val.size() - cmpVal.size()) == cmpVal;
                case AttrMatchOp::Substring:
                    return !cmpVal.empty() && val.find(cmpVal) != std::string::npos;
                default: return false;
            }
        }
        case SimpleSelectorType::PseudoClass: {
            const std::string& name = ss.value;
            if (name == "first-child") return elem.childIndex() == 0;
            if (name == "last-child") return elem.childIndex() == elem.siblingCount() - 1;
            if (name == "only-child") return elem.siblingCount() == 1;
            if (name == "first-of-type") return elem.childIndexOfType() == 0;
            if (name == "last-of-type") return elem.childIndexOfType() == elem.siblingCountOfType() - 1;
            if (name == "only-of-type") return elem.siblingCountOfType() == 1;
            if (name == "hover") return elem.isHovered();
            if (name == "focus") return elem.isFocused();
            if (name == "active") return elem.isActive();
            if (name == "root") return elem.parent() == nullptr;
            // The scoping root: the root element in a stylesheet (outside
            // @scope, which is not implemented), the context element when a
            // consumer answers querySelector() and friends (ElementRef).
            if (name == "scope") return elem.isScopingRoot();
            if (name == "empty")
                return elem.children().empty() && !elem.hasTextChildren();
            if (name == "host") {
                if (!elem.shadowRoot()) return false;
                if (!ss.hostArg.empty()) {
                    for (auto& inner : ss.hostArg) {
                        if (!matchSimple(inner, elem)) return false;
                    }
                }
                return true;
            }
            if (name == "host-context") {
                if (!elem.shadowRoot()) return false;
                if (!ss.hostArg.empty()) {
                    bool hostMatches = true;
                    for (auto& inner : ss.hostArg) {
                        if (!matchSimple(inner, elem)) { hostMatches = false; break; }
                    }
                    if (hostMatches) return true;
                    ElementRef* ancestor = elem.parent();
                    while (ancestor) {
                        bool ancestorMatches = true;
                        for (auto& inner : ss.hostArg) {
                            if (!matchSimple(inner, *ancestor)) { ancestorMatches = false; break; }
                        }
                        if (ancestorMatches) return true;
                        ancestor = ancestor->parent();
                    }
                }
                return false;
            }
            if (name == "defined") return elem.isDefined();
            if (name == "any-link" || name == "link") return elem.isLink();
            if (name == "visited") return elem.isVisited();
            if (name == "focus-within") return elem.isFocusWithin();
            if (name == "focus-visible") return elem.isFocusVisible();
            if (name == "checked") return elem.isChecked();
            if (name == "disabled") return elem.isDisabled();
            if (name == "enabled") return elem.isEnabled();
            if (name == "required") return elem.isRequired();
            if (name == "optional") return elem.isOptional();
            if (name == "read-only") return elem.isReadOnly();
            if (name == "read-write") return elem.isReadWrite();
            if (name == "placeholder-shown") return elem.isPlaceholderShown();
            if (name == "indeterminate") return elem.isIndeterminate();
            if (name == "target") return elem.isTarget();
            if (name == "modal") return elem.isModal();
            if (name == "not") {
                for (auto& compound : ss.selectorListArg) {
                    if (matchCompound(compound, elem)) return false;
                }
                for (auto& ch : ss.chainListArg) {
                    if (matchChain(ch, elem)) return false;
                }
                return true;
            }
            if (name == "is" || name == "where") {
                for (auto& compound : ss.selectorListArg) {
                    if (matchCompound(compound, elem)) return true;
                }
                for (auto& ch : ss.chainListArg) {
                    if (matchChain(ch, elem)) return true;
                }
                return false;
            }
            if (name == "has") return matchHas(ss, elem);
            if (name == "nth-child") return matchNth(ss.nthA, ss.nthB, elem.childIndex() + 1);
            if (name == "nth-last-child")
                return matchNth(ss.nthA, ss.nthB, elem.siblingCount() - elem.childIndex());
            if (name == "nth-of-type")
                return matchNth(ss.nthA, ss.nthB, elem.childIndexOfType() + 1);
            if (name == "nth-last-of-type")
                return matchNth(ss.nthA, ss.nthB, elem.siblingCountOfType() - elem.childIndexOfType());
            return false;
        }
        case SimpleSelectorType::PseudoElement:
            return false;
        default:
            return false;
    }
}

bool matchCompound(const CompoundSelector& compound, const ElementRef& elem) {
    // An empty compound never matches by vacuous truth.
    if (compound.simples.empty()) return false;
    for (auto& s : compound.simples) {
        if (!matchSimple(s, elem)) return false;
    }
    return true;
}

bool matchChain(const SelectorChain& chain, const ElementRef& elem) {
    return matchChainTop(chain, elem, nullptr);
}

bool Selector::matches(const ElementRef& elem) const {
    return matchChain(chain, elem);
}

} // namespace htmlayout::css
