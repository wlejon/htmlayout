#include "css/selector.h"
#include "css/tokenizer.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>

namespace htmlayout::css {

namespace {

// ---- Helpers ----

int safeStoi(const std::string& s, int defaultVal = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return defaultVal;
    }
}

std::string toLower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r\f");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n\r\f");
    return s.substr(a, b - a + 1);
}

// Split a string by a delimiter (for class matching)
std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string part;
    while (iss >> part) parts.push_back(part);
    return parts;
}

bool hasClass(const ElementRef& elem, const std::string& cls) {
    auto classes = splitWhitespace(elem.className());
    return std::find(classes.begin(), classes.end(), cls) != classes.end();
}

// ---- Selector Parser ----

class SelectorParser {
public:
    explicit SelectorParser(const std::string& text)
        : m_text(text), m_pos(0) {}

    SelectorChain parseChain() {
        SelectorChain chain;
        skipWS();
        if (m_pos >= m_text.size()) return chain;

        // Parse rightmost compound selector (the subject)
        auto compound = parseCompound();
        chain.entries.push_back({std::move(compound), Combinator::None});

        // Parse combinator + compound pairs going left
        while (m_pos < m_text.size()) {
            skipWS();
            if (m_pos >= m_text.size()) break;

            Combinator comb = Combinator::Descendant; // default
            char c = peek();
            if (c == '>') {
                comb = Combinator::Child;
                advance();
                skipWS();
            } else if (c == '+') {
                comb = Combinator::AdjacentSibling;
                advance();
                skipWS();
            } else if (c == '~') {
                comb = Combinator::GeneralSibling;
                advance();
                skipWS();
            }
            // else descendant combinator (whitespace already consumed)

            if (m_pos >= m_text.size()) break;

            // Check for comma or end
            if (peek() == ',') break;

            auto next = parseCompound();
            if (next.simples.empty()) break;
            chain.entries.push_back({std::move(next), comb});
        }

        // Reverse so entries[0] = subject (rightmost compound).
        // Original: [A,None] [B,comb1] [C,comb2]
        //   means A -comb1-> B -comb2-> C
        // Want:     [C,None] [B,comb2] [A,comb1]
        //   entries[i>0].combinator = how entries[i-1] relates to entries[i]
        if (chain.entries.size() > 1) {
            size_t n = chain.entries.size();
            // Build reversed chain
            SelectorChain reversed;
            reversed.entries.resize(n);
            for (size_t i = 0; i < n; i++) {
                reversed.entries[i].compound = std::move(chain.entries[n - 1 - i].compound);
            }
            reversed.entries[0].combinator = Combinator::None;
            for (size_t i = 1; i < n; i++) {
                // The combinator between original[n-1-i] and original[n-i]
                // is stored on original[n-i].combinator
                reversed.entries[i].combinator = chain.entries[n - i].combinator;
            }
            chain = std::move(reversed);
        }

        return chain;
    }

private:
    const std::string& m_text;
    size_t m_pos;

    char peek(size_t off = 0) const {
        size_t i = m_pos + off;
        return i < m_text.size() ? m_text[i] : '\0';
    }
    char advance() { return m_pos < m_text.size() ? m_text[m_pos++] : '\0'; }
    void skipWS() { while (m_pos < m_text.size() && isspace((unsigned char)m_text[m_pos])) m_pos++; }

    bool isNameStart(char c) const {
        return std::isalpha((unsigned char)c) || c == '_' || c == '-' || (unsigned char)c >= 0x80;
    }
    bool isNameChar(char c) const {
        return isNameStart(c) || std::isdigit((unsigned char)c);
    }

    std::string consumeName() {
        std::string name;
        while (m_pos < m_text.size() && isNameChar(peek())) {
            name += advance();
        }
        return name;
    }

    int consumeInt() {
        std::string s;
        if (peek() == '+' || peek() == '-') s += advance();
        while (m_pos < m_text.size() && std::isdigit((unsigned char)peek())) s += advance();
        if (s.empty() || s == "+" || s == "-") return 0;
        return safeStoi(s);
    }

    CompoundSelector parseCompound() {
        CompoundSelector compound;
        while (m_pos < m_text.size()) {
            char c = peek();
            if (c == '*') {
                advance();
                compound.simples.push_back({SimpleSelectorType::Universal, "*"});
            } else if (c == '.') {
                advance();
                std::string cls = consumeName();
                compound.simples.push_back({SimpleSelectorType::Class, cls});
            } else if (c == '#') {
                advance();
                std::string id = consumeName();
                compound.simples.push_back({SimpleSelectorType::Id, id});
            } else if (c == '[') {
                compound.simples.push_back(parseAttribute());
            } else if (c == ':') {
                compound.simples.push_back(parsePseudo());
            } else if (isNameStart(c) && c != '-') {
                // Tag name (but not starting with - which could be confusing)
                std::string tag = consumeName();
                compound.simples.push_back({SimpleSelectorType::Tag, toLower(tag)});
            } else if (c == '-' && isNameStart(peek(1))) {
                // Custom element name like my-element
                std::string tag = consumeName();
                compound.simples.push_back({SimpleSelectorType::Tag, toLower(tag)});
            } else {
                break; // not part of compound selector
            }
        }
        return compound;
    }

    SimpleSelector parseAttribute() {
        SimpleSelector ss;
        ss.type = SimpleSelectorType::Attribute;
        advance(); // skip '['
        skipWS();
        ss.attrName = consumeName();
        skipWS();

        if (peek() == ']') {
            ss.attrOp = AttrMatchOp::Exists;
            advance();
            return ss;
        }

        // Determine operator
        char c = peek();
        if (c == '=') {
            ss.attrOp = AttrMatchOp::Equals;
            advance();
        } else if (c == '~' && peek(1) == '=') {
            ss.attrOp = AttrMatchOp::Includes;
            advance(); advance();
        } else if (c == '|' && peek(1) == '=') {
            ss.attrOp = AttrMatchOp::DashMatch;
            advance(); advance();
        } else if (c == '^' && peek(1) == '=') {
            ss.attrOp = AttrMatchOp::Prefix;
            advance(); advance();
        } else if (c == '$' && peek(1) == '=') {
            ss.attrOp = AttrMatchOp::Suffix;
            advance(); advance();
        } else if (c == '*' && peek(1) == '=') {
            ss.attrOp = AttrMatchOp::Substring;
            advance(); advance();
        }

        skipWS();
        // Value: quoted or unquoted
        if (peek() == '"' || peek() == '\'') {
            char quote = advance();
            while (m_pos < m_text.size() && peek() != quote) {
                ss.attrValue += advance();
            }
            if (peek() == quote) advance();
        } else {
            ss.attrValue = consumeName();
        }
        skipWS();
        // Check for case-insensitive flag: 'i' or 'I' before ']'
        if ((peek() == 'i' || peek() == 'I') && m_pos + 1 < m_text.size()) {
            // Look ahead to see if next non-space is ']'
            size_t saved = m_pos;
            advance(); // skip 'i'
            skipWS();
            if (peek() == ']') {
                ss.attrCaseInsensitive = true;
            } else {
                m_pos = saved; // not the flag, restore
            }
        }
        if (peek() == ']') advance();
        return ss;
    }

    SimpleSelector parsePseudo() {
        SimpleSelector ss;
        advance(); // skip first ':'
        if (peek() == ':') {
            // Pseudo-element ::before, ::after, ::slotted(), ::part()
            advance();
            ss.type = SimpleSelectorType::PseudoElement;
            ss.value = consumeName();

            // Handle functional pseudo-elements
            if (peek() == '(') {
                advance(); // skip '('
                skipWS();
                if (ss.value == "slotted") {
                    // ::slotted(selector) — parse compound selector argument
                    std::string arg;
                    int depth = 1;
                    size_t argStart = m_pos;
                    while (m_pos < m_text.size() && depth > 0) {
                        if (peek() == '(') depth++;
                        else if (peek() == ')') { depth--; if (depth == 0) break; }
                        advance();
                    }
                    arg = m_text.substr(argStart, m_pos - argStart);
                    if (peek() == ')') advance();

                    SelectorParser subParser(arg);
                    auto compound = subParser.parseCompound();
                    ss.slottedArg = std::move(compound.simples);
                } else if (ss.value == "part") {
                    // ::part(name) — consume the part name
                    std::string arg;
                    while (m_pos < m_text.size() && peek() != ')') {
                        arg += advance();
                    }
                    if (peek() == ')') advance();
                    // Trim whitespace
                    size_t a = arg.find_first_not_of(" \t");
                    size_t b = arg.find_last_not_of(" \t");
                    ss.partArg = (a != std::string::npos) ? arg.substr(a, b - a + 1) : "";
                } else {
                    // Unknown functional pseudo-element — skip to closing paren
                    int depth = 1;
                    while (m_pos < m_text.size() && depth > 0) {
                        if (peek() == '(') depth++;
                        else if (peek() == ')') depth--;
                        advance();
                    }
                }
            }
            return ss;
        }
        ss.type = SimpleSelectorType::PseudoClass;
        ss.value = consumeName();

        // Handle functional pseudo-classes
        if (peek() == '(') {
            advance(); // skip '('
            skipWS();
            if (ss.value == "not") {
                // Parse the argument as simple selectors
                std::string arg;
                int depth = 1;
                size_t argStart = m_pos;
                while (m_pos < m_text.size() && depth > 0) {
                    if (peek() == '(') depth++;
                    else if (peek() == ')') { depth--; if (depth == 0) break; }
                    advance();
                }
                arg = m_text.substr(argStart, m_pos - argStart);
                if (peek() == ')') advance();

                // Parse the :not() argument as a compound selector
                SelectorParser subParser(arg);
                auto compound = subParser.parseCompound();
                ss.notArg = std::move(compound.simples);
            } else if (ss.value == "nth-child" || ss.value == "nth-last-child" ||
                       ss.value == "nth-of-type" || ss.value == "nth-last-of-type") {
                parseNth(ss);
                if (peek() == ')') advance();
            } else if (ss.value == "is" || ss.value == "where" || ss.value == "has") {
                // :is()/:where()/:has() — parse comma-separated selector list
                std::string arg;
                int depth = 1;
                size_t argStart = m_pos;
                while (m_pos < m_text.size() && depth > 0) {
                    if (peek() == '(') depth++;
                    else if (peek() == ')') { depth--; if (depth == 0) break; }
                    advance();
                }
                arg = m_text.substr(argStart, m_pos - argStart);
                if (peek() == ')') advance();

                // Split by comma and parse each as a compound selector
                std::string current;
                int pd = 0;
                for (size_t j = 0; j < arg.size(); j++) {
                    if (arg[j] == '(') pd++;
                    else if (arg[j] == ')') pd--;
                    else if (arg[j] == ',' && pd == 0) {
                        std::string part = trim(current);
                        if (!part.empty()) {
                            SelectorParser subParser(part);
                            ss.selectorListArg.push_back(subParser.parseCompound());
                        }
                        current.clear();
                        continue;
                    }
                    current += arg[j];
                }
                std::string part = trim(current);
                if (!part.empty()) {
                    SelectorParser subParser(part);
                    ss.selectorListArg.push_back(subParser.parseCompound());
                }
            } else if (ss.value == "host" || ss.value == "host-context") {
                // :host(selector) / :host-context(selector)
                std::string arg;
                int depth = 1;
                size_t argStart = m_pos;
                while (m_pos < m_text.size() && depth > 0) {
                    if (peek() == '(') depth++;
                    else if (peek() == ')') { depth--; if (depth == 0) break; }
                    advance();
                }
                arg = m_text.substr(argStart, m_pos - argStart);
                if (peek() == ')') advance();

                SelectorParser subParser(arg);
                auto compound = subParser.parseCompound();
                ss.hostArg = std::move(compound.simples);
            } else {
                // Unknown functional pseudo - skip to closing paren
                int depth = 1;
                while (m_pos < m_text.size() && depth > 0) {
                    if (peek() == '(') depth++;
                    else if (peek() == ')') depth--;
                    advance();
                }
            }
        }
        return ss;
    }

    void parseNth(SimpleSelector& ss) {
        skipWS();
        std::string arg;
        while (m_pos < m_text.size() && peek() != ')') arg += advance();
        arg = trim(arg);
        std::string lower = toLower(arg);

        if (lower == "odd") {
            ss.nthA = 2; ss.nthB = 1;
        } else if (lower == "even") {
            ss.nthA = 2; ss.nthB = 0;
        } else {
            // Parse An+B
            size_t nPos = lower.find('n');
            if (nPos != std::string::npos) {
                std::string aPart = lower.substr(0, nPos);
                if (aPart.empty() || aPart == "+") ss.nthA = 1;
                else if (aPart == "-") ss.nthA = -1;
                else ss.nthA = safeStoi(aPart);

                std::string bPart = trim(lower.substr(nPos + 1));
                if (bPart.empty()) ss.nthB = 0;
                else {
                    // Remove spaces around +/-
                    std::string cleaned;
                    for (char ch : bPart) if (!isspace((unsigned char)ch)) cleaned += ch;
                    if (!cleaned.empty()) ss.nthB = safeStoi(cleaned);
                }
            } else {
                ss.nthA = 0;
                ss.nthB = safeStoi(lower.empty() ? "0" : lower);
            }
        }
    }
};

// ---- Specificity Calculator ----

uint32_t computeSpecificity(const SelectorChain& chain) {
    int ids = 0, classes = 0, types = 0;
    for (auto& entry : chain.entries) {
        for (auto& s : entry.compound.simples) {
            switch (s.type) {
                case SimpleSelectorType::Id:
                    ids++;
                    break;
                case SimpleSelectorType::Class:
                case SimpleSelectorType::Attribute:
                case SimpleSelectorType::PseudoClass:
                    if (s.value == "not") {
                        // :not() specificity comes from its argument
                        for (auto& inner : s.notArg) {
                            switch (inner.type) {
                                case SimpleSelectorType::Id: ids++; break;
                                case SimpleSelectorType::Class:
                                case SimpleSelectorType::Attribute:
                                case SimpleSelectorType::PseudoClass: classes++; break;
                                case SimpleSelectorType::Tag: types++; break;
                                default: break;
                            }
                        }
                    } else if (s.value == "is" || s.value == "has") {
                        // :is()/:has() specificity = most specific argument
                        int maxIds = 0, maxClasses = 0, maxTypes = 0;
                        for (auto& compound : s.selectorListArg) {
                            int ci = 0, cc = 0, ct = 0;
                            for (auto& inner : compound.simples) {
                                switch (inner.type) {
                                    case SimpleSelectorType::Id: ci++; break;
                                    case SimpleSelectorType::Class:
                                    case SimpleSelectorType::Attribute:
                                    case SimpleSelectorType::PseudoClass: cc++; break;
                                    case SimpleSelectorType::Tag: ct++; break;
                                    default: break;
                                }
                            }
                            uint32_t spec = (ci << 16) | (cc << 8) | ct;
                            uint32_t maxSpec = (maxIds << 16) | (maxClasses << 8) | maxTypes;
                            if (spec > maxSpec) { maxIds = ci; maxClasses = cc; maxTypes = ct; }
                        }
                        ids += maxIds;
                        classes += maxClasses;
                        types += maxTypes;
                    } else if (s.value == "where") {
                        // :where() contributes zero specificity
                    } else if (s.value == "host" || s.value == "host-context") {
                        // :host/:host-context itself = pseudo-class specificity
                        classes++;
                        // Plus the specificity of the argument selectors
                        for (auto& inner : s.hostArg) {
                            switch (inner.type) {
                                case SimpleSelectorType::Id: ids++; break;
                                case SimpleSelectorType::Class:
                                case SimpleSelectorType::Attribute:
                                case SimpleSelectorType::PseudoClass: classes++; break;
                                case SimpleSelectorType::Tag: types++; break;
                                default: break;
                            }
                        }
                    } else {
                        classes++;
                    }
                    break;
                case SimpleSelectorType::Tag:
                    types++;
                    break;
                case SimpleSelectorType::PseudoElement:
                    types++;
                    // ::slotted(selector) specificity includes its argument
                    if (s.value == "slotted") {
                        for (auto& inner : s.slottedArg) {
                            switch (inner.type) {
                                case SimpleSelectorType::Id: ids++; break;
                                case SimpleSelectorType::Class:
                                case SimpleSelectorType::Attribute:
                                case SimpleSelectorType::PseudoClass: classes++; break;
                                case SimpleSelectorType::Tag: types++; break;
                                default: break;
                            }
                        }
                    }
                    // ::part(name) has specificity of a pseudo-element (already counted above)
                    break;
                case SimpleSelectorType::Universal:
                    break; // no specificity
            }
        }
    }
    // Pack as (ids << 16) | (classes << 8) | types, clamped to 255 each
    if (ids > 255) ids = 255;
    if (classes > 255) classes = 255;
    if (types > 255) types = 255;
    return (static_cast<uint32_t>(ids) << 16) |
           (static_cast<uint32_t>(classes) << 8) |
           static_cast<uint32_t>(types);
}

// ---- Matching ----

bool matchNth(int a, int b, int index) {
    if (a == 0) return index == b;
    int n = index - b;
    if (a < 0) {
        return n <= 0 && (n % a == 0);
    }
    return n >= 0 && (n % a == 0);
}

} // anonymous namespace

// Forward declaration for use by matchSimple (:is/:where/:has)
bool matchCompound(const CompoundSelector& compound, const ElementRef& elem);

bool matchSimple(const SimpleSelector& ss, const ElementRef& elem) {
    switch (ss.type) {
        case SimpleSelectorType::Universal:
            return true;
        case SimpleSelectorType::Tag:
            return toLower(elem.tagName()) == ss.value;
        case SimpleSelectorType::Class:
            return hasClass(elem, ss.value);
        case SimpleSelectorType::Id:
            return elem.id() == ss.value;
        case SimpleSelectorType::Attribute: {
            if (!elem.hasAttribute(ss.attrName)) return false;
            if (ss.attrOp == AttrMatchOp::Exists) return true;
            std::string val = elem.getAttribute(ss.attrName);
            std::string cmpVal = ss.attrValue;
            // Apply case-insensitive flag
            if (ss.attrCaseInsensitive) {
                val = toLower(val);
                cmpVal = toLower(cmpVal);
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
                    return val.size() >= cmpVal.size() &&
                           val.substr(0, cmpVal.size()) == cmpVal;
                case AttrMatchOp::Suffix:
                    return val.size() >= cmpVal.size() &&
                           val.substr(val.size() - cmpVal.size()) == cmpVal;
                case AttrMatchOp::Substring:
                    return val.find(cmpVal) != std::string::npos;
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
            if (name == "empty") return elem.children().empty();
            if (name == "host") {
                // :host matches the shadow host element.
                // An element is the shadow host if it has a shadow root.
                if (!elem.shadowRoot()) return false;
                // If :host has arguments, the host must also match those selectors
                if (!ss.hostArg.empty()) {
                    for (auto& inner : ss.hostArg) {
                        if (!matchSimple(inner, elem)) return false;
                    }
                }
                return true;
            }
            if (name == "host-context") {
                // :host-context(selector) matches if the host or any of its ancestors
                // matches the argument selector.
                if (!elem.shadowRoot()) return false;
                // Check the host itself
                if (!ss.hostArg.empty()) {
                    bool hostMatches = true;
                    for (auto& inner : ss.hostArg) {
                        if (!matchSimple(inner, elem)) { hostMatches = false; break; }
                    }
                    if (hostMatches) return true;
                    // Walk ancestors
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
            if (name == "defined") {
                return elem.isDefined();
            }
            // Selectors L4: link pseudo-classes
            if (name == "any-link" || name == "link") {
                return elem.isLink();
            }
            if (name == "visited") {
                return elem.isVisited();
            }
            // Selectors L4: focus pseudo-classes
            if (name == "focus-within") {
                return elem.isFocusWithin();
            }
            if (name == "focus-visible") {
                return elem.isFocusVisible();
            }
            // Selectors L4: form state pseudo-classes
            if (name == "checked") {
                return elem.isChecked();
            }
            if (name == "disabled") {
                return elem.isDisabled();
            }
            if (name == "enabled") {
                return elem.isEnabled();
            }
            if (name == "required") {
                return elem.isRequired();
            }
            if (name == "optional") {
                return elem.isOptional();
            }
            if (name == "read-only") {
                return elem.isReadOnly();
            }
            if (name == "read-write") {
                return elem.isReadWrite();
            }
            if (name == "placeholder-shown") {
                return elem.isPlaceholderShown();
            }
            if (name == "indeterminate") {
                return elem.isIndeterminate();
            }
            if (name == "target") {
                return elem.isTarget();
            }
            if (name == "not") {
                // :not() matches if NONE of the argument simple selectors match
                for (auto& inner : ss.notArg) {
                    if (matchSimple(inner, elem)) return false;
                }
                return true;
            }
            if (name == "is" || name == "where") {
                // :is()/:where() match if ANY compound in the list matches
                for (auto& compound : ss.selectorListArg) {
                    if (matchCompound(compound, elem)) return true;
                }
                return false;
            }
            if (name == "has") {
                // :has() matches if any descendant/child matches any compound
                // For simplicity, check all descendants
                auto checkDescendants = [&](auto& self, const ElementRef& parent) -> bool {
                    for (auto* child : parent.children()) {
                        for (auto& compound : ss.selectorListArg) {
                            if (matchCompound(compound, *child)) return true;
                        }
                        if (self(self, *child)) return true;
                    }
                    return false;
                };
                return checkDescendants(checkDescendants, elem);
            }
            if (name == "nth-child") {
                int index = elem.childIndex() + 1; // 1-based
                return matchNth(ss.nthA, ss.nthB, index);
            }
            if (name == "nth-last-child") {
                int index = elem.siblingCount() - elem.childIndex(); // 1-based from end
                return matchNth(ss.nthA, ss.nthB, index);
            }
            if (name == "nth-of-type") {
                int index = elem.childIndexOfType() + 1;
                return matchNth(ss.nthA, ss.nthB, index);
            }
            if (name == "nth-last-of-type") {
                int index = elem.siblingCountOfType() - elem.childIndexOfType();
                return matchNth(ss.nthA, ss.nthB, index);
            }
            return false;
        }
        case SimpleSelectorType::PseudoElement:
            // Pseudo-elements don't participate in normal matching
            return false;
        default:
            return false;
    }
}

bool matchCompound(const CompoundSelector& compound, const ElementRef& elem) {
    for (auto& s : compound.simples) {
        if (!matchSimple(s, elem)) return false;
    }
    return true;
}

// Match a selector chain against an element.
// entries[0] must match elem (subject). Then walk ancestors/siblings per combinators.
bool matchChain(const SelectorChain& chain, const ElementRef& elem) {
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
                // Parent must match
                ElementRef* p = current->parent();
                if (!p || !matchCompound(entry.compound, *p)) return false;
                current = p;
                found = true;
                break;
            }
            case Combinator::Descendant: {
                // Any ancestor must match
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
                // Immediately preceding sibling must match
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
                // Any preceding sibling must match
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
    return true;
}

// ---- Public API ----

bool Selector::matches(const ElementRef& elem) const {
    return matchChain(chain, elem);
}

Selector parseSelector(const std::string& text) {
    std::string trimmed = trim(text);
    SelectorParser parser(trimmed);
    auto chain = parser.parseChain();
    uint32_t spec = computeSpecificity(chain);
    return {text, spec, std::move(chain)};
}

std::vector<Selector> parseSelectorList(const std::string& text) {
    std::vector<Selector> selectors;
    // Split by comma (respecting parentheses)
    std::string current;
    int parenDepth = 0;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '(') parenDepth++;
        else if (c == ')') parenDepth--;
        else if (c == ',' && parenDepth == 0) {
            std::string part = trim(current);
            if (!part.empty()) {
                selectors.push_back(parseSelector(part));
            }
            current.clear();
            continue;
        }
        current += c;
    }
    std::string part = trim(current);
    if (!part.empty()) {
        selectors.push_back(parseSelector(part));
    }
    return selectors;
}

uint32_t calculateSpecificity(const std::string& selector) {
    auto sel = parseSelector(selector);
    return sel.specificity;
}

} // namespace htmlayout::css
