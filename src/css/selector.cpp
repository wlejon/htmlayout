#include "css/selector.h"
#include "css/tokenizer.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace htmlayout::css {

namespace {

std::string toLower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r\f");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n\r\f");
    return s.substr(a, b - a + 1);
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool hasBalancedDelimiters(const std::string& text) {
    int parenDepth = 0;
    int bracketDepth = 0;
    char quote = 0;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\\') {
            if (i + 1 >= text.size()) return false;
            i++;
            continue;
        }
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[') {
            bracketDepth++;
        } else if (c == ']') {
            if (bracketDepth == 0) return false;
            bracketDepth--;
        } else if (c == '(') {
            parenDepth++;
        } else if (c == ')') {
            if (parenDepth == 0) return false;
            parenDepth--;
        }
    }
    return quote == 0 && bracketDepth == 0 && parenDepth == 0;
}

// Split a selector list at its top-level commas: not inside parentheses,
// attribute brackets or quoted strings, and not an escaped comma, so
// `[data-x="1,2"]` and `:is(a, b)` stay whole. Parts come back untrimmed.
std::vector<std::string> splitTopLevelCommas(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    int bracketDepth = 0;
    char quote = 0;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            current += c;
            current += text[++i];
            continue;
        }
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') quote = c;
        else if (c == '[') bracketDepth++;
        else if (c == ']') { if (bracketDepth > 0) bracketDepth--; }
        else if (c == '(') parenDepth++;
        else if (c == ')') parenDepth--;
        else if (c == ',' && parenDepth == 0 && bracketDepth == 0) {
            parts.push_back(std::move(current));
            current.clear();
            continue;
        }
        current += c;
    }
    parts.push_back(std::move(current));
    return parts;
}

// ---- Selector Parser ----

// Functional pseudo-classes (:not/:is/:where/:has, ::slotted) parse their
// arguments with a nested parser. Past this nesting depth an argument parses
// as empty, so a hostile `:not(:not(:not(…` neither runs the stack out nor
// re-scans its tail once per level; every later recursion over the parsed
// structure (specificity, matching) is bounded with it.
constexpr int kMaxSelectorNesting = 32;
thread_local int t_selectorNesting = 0;

class SelectorParser {
public:
    explicit SelectorParser(const std::string& text)
        : m_text(text), m_pos(0) { ++t_selectorNesting; }
    ~SelectorParser() { --t_selectorNesting; }
    SelectorParser(const SelectorParser&) = delete;
    SelectorParser& operator=(const SelectorParser&) = delete;

    static bool tooDeep() { return t_selectorNesting > kMaxSelectorNesting; }
    static bool atDepthLimit() { return t_selectorNesting >= kMaxSelectorNesting; }

    SelectorChain parseChain() {
        SelectorChain chain;
        if (tooDeep()) { m_pos = m_text.size(); return chain; }
        skipWS();
        if (m_pos >= m_text.size()) return chain;

        // Parse leftmost compound selector (read left-to-right, reversed below)
        auto compound = parseCompound();
        if (m_invalid || compound.simples.empty()) return SelectorChain{};
        chain.entries.push_back({std::move(compound), Combinator::None});

        // Parse combinator + compound pairs
        while (m_pos < m_text.size()) {
            skipWS();
            if (m_pos >= m_text.size()) break;

            Combinator comb = Combinator::Descendant;
            bool explicitComb = false;
            char c = peek();
            if (c == '>') {
                comb = Combinator::Child;
                explicitComb = true;
                advance();
                skipWS();
            } else if (c == '+') {
                comb = Combinator::AdjacentSibling;
                explicitComb = true;
                advance();
                skipWS();
            } else if (c == '~') {
                comb = Combinator::GeneralSibling;
                explicitComb = true;
                advance();
                skipWS();
            }

            if (m_pos >= m_text.size()) {
                if (explicitComb) return SelectorChain{};
                break;
            }

            auto next = parseCompound();
            if (m_invalid || next.simples.empty()) return SelectorChain{};
            chain.entries.push_back({std::move(next), comb});
        }

        skipWS();
        if (m_invalid || m_pos < m_text.size()) return SelectorChain{};

        // Reverse so entries[0] = subject (rightmost compound).
        if (chain.entries.size() > 1) {
            size_t n = chain.entries.size();
            SelectorChain reversed;
            reversed.entries.resize(n);
            for (size_t i = 0; i < n; i++) {
                reversed.entries[i].compound = std::move(chain.entries[n - 1 - i].compound);
            }
            reversed.entries[0].combinator = Combinator::None;
            for (size_t i = 1; i < n; i++) {
                reversed.entries[i].combinator = chain.entries[n - i].combinator;
            }
            chain = std::move(reversed);
        }

        return chain;
    }

private:
    const std::string& m_text;
    size_t m_pos;
    bool m_invalid = false;

    char peek(size_t off = 0) const {
        size_t i = m_pos + off;
        return i < m_text.size() ? m_text[i] : '\0';
    }
    char advance() { return m_pos < m_text.size() ? m_text[m_pos++] : '\0'; }
    void skipWS() { while (m_pos < m_text.size() && std::isspace((unsigned char)m_text[m_pos])) m_pos++; }

    static bool isNameBase(char c) {
        return std::isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
    }

    bool validEscapeAt(size_t pos) const {
        if (pos + 1 >= m_text.size() || m_text[pos] != '\\') return false;
        char n = m_text[pos + 1];
        return n != '\n' && n != '\r' && n != '\f';
    }

    bool canStartIdent() const {
        if (m_pos >= m_text.size()) return false;
        char c = m_text[m_pos];
        if (isNameBase(c)) return true;
        if (c == '\\') return validEscapeAt(m_pos);
        if (c == '-') {
            if (m_pos + 1 >= m_text.size()) return false;
            char n = m_text[m_pos + 1];
            return isNameBase(n) || n == '-' || validEscapeAt(m_pos + 1);
        }
        return false;
    }

    void consumeEscape(std::string& out) {
        if (!validEscapeAt(m_pos)) {
            m_invalid = true;
            if (m_pos < m_text.size()) m_pos++;
            return;
        }
        m_pos++; // skip '\'
        char first = m_text[m_pos];
        if (std::isxdigit((unsigned char)first)) {
            uint32_t cp = 0;
            int count = 0;
            while (m_pos < m_text.size() && count < 6 && std::isxdigit((unsigned char)m_text[m_pos])) {
                char h = m_text[m_pos++];
                uint32_t v = (h <= '9') ? (h - '0')
                           : (h <= 'F') ? (10 + h - 'A')
                                        : (10 + h - 'a');
                cp = (cp << 4) | v;
                count++;
            }
            if (m_pos < m_text.size() && std::isspace((unsigned char)m_text[m_pos])) {
                if (m_text[m_pos] == '\r' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '\n')
                    m_pos += 2;
                else
                    m_pos++;
            }
            appendUtf8(out, cp);
        } else {
            out += m_text[m_pos++];
        }
    }

    std::string consumeIdent() {
        std::string name;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c == '\\') {
                consumeEscape(name);
                if (m_invalid) break;
            } else if (isNameBase(c) || c == '-' || std::isdigit((unsigned char)c)) {
                name += m_text[m_pos++];
            } else {
                break;
            }
        }
        return name;
    }

    bool readParenArg(std::string& out) {
        int depth = 1;
        char quote = 0;
        size_t argStart = m_pos;
        while (m_pos < m_text.size() && depth > 0) {
            char c = m_text[m_pos];
            if (c == '\\') {
                if (m_pos + 1 >= m_text.size()) { m_invalid = true; return false; }
                m_pos += 2;
                continue;
            }
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth--;
                if (depth == 0) break;
            }
            m_pos++;
        }
        if (depth != 0 || quote != 0) {
            m_invalid = true;
            return false;
        }
        out = m_text.substr(argStart, m_pos - argStart);
        if (peek() == ')') advance();
        return true;
    }

    CompoundSelector parseCompound() {
        CompoundSelector compound;
        if (tooDeep()) { m_pos = m_text.size(); return compound; }
        while (m_pos < m_text.size()) {
            char c = peek();
            if (c == '*') {
                if (!compound.simples.empty()) { m_invalid = true; break; }
                advance();
                compound.simples.push_back({SimpleSelectorType::Universal, "*"});
            } else if (c == '.') {
                advance();
                if (!canStartIdent()) { m_invalid = true; break; }
                compound.simples.push_back({SimpleSelectorType::Class, consumeIdent()});
            } else if (c == '#') {
                advance();
                if (!canStartIdent()) { m_invalid = true; break; }
                compound.simples.push_back({SimpleSelectorType::Id, consumeIdent()});
            } else if (c == '[') {
                compound.simples.push_back(parseAttribute());
                if (m_invalid) break;
            } else if (c == ':') {
                compound.simples.push_back(parsePseudo());
                if (m_invalid) break;
            } else if (canStartIdent()) {
                if (!compound.simples.empty()) { m_invalid = true; break; }
                compound.simples.push_back({SimpleSelectorType::Tag, toLower(consumeIdent())});
            } else {
                break;
            }
        }
        return compound;
    }

    SimpleSelector parseAttribute() {
        SimpleSelector ss;
        ss.type = SimpleSelectorType::Attribute;
        advance(); // skip '['
        skipWS();
        if (!canStartIdent()) { m_invalid = true; return ss; }
        ss.attrName = consumeIdent();
        skipWS();

        if (peek() == ']') {
            ss.attrOp = AttrMatchOp::Exists;
            advance();
            return ss;
        }

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
        } else {
            m_invalid = true;
            return ss;
        }

        skipWS();
        if (peek() == '"' || peek() == '\'') {
            char quote = advance();
            while (m_pos < m_text.size() && peek() != quote) {
                if (peek() == '\\') consumeEscape(ss.attrValue);
                else ss.attrValue += advance();
                if (m_invalid) return ss;
            }
            if (peek() != quote) { m_invalid = true; return ss; }
            advance();
        } else {
            if (!canStartIdent()) { m_invalid = true; return ss; }
            ss.attrValue = consumeIdent();
        }
        skipWS();
        if (peek() == 'i' || peek() == 'I' || peek() == 's' || peek() == 'S') {
            bool ci = (peek() == 'i' || peek() == 'I');
            size_t saved = m_pos;
            advance();
            skipWS();
            if (peek() == ']') ss.attrCaseInsensitive = ci;
            else m_pos = saved;
        }
        if (peek() != ']') { m_invalid = true; return ss; }
        advance();
        return ss;
    }

    SimpleSelector parsePseudo() {
        SimpleSelector ss;
        advance(); // skip first ':'
        if (peek() == ':') {
            advance();
            if (!canStartIdent()) { m_invalid = true; return ss; }
            ss.type = SimpleSelectorType::PseudoElement;
            ss.value = toLower(consumeIdent());

            if (peek() == '(') {
                advance();
                std::string arg;
                if (!readParenArg(arg)) return ss;
                arg = trim(arg);
                if (ss.value == "slotted") {
                    if (!atDepthLimit()) {
                        if (arg.empty()) { m_invalid = true; return ss; }
                        SelectorParser sub(arg);
                        auto comp = sub.parseCompound();
                        sub.skipWS();
                        if (sub.m_invalid || sub.m_pos < arg.size() || comp.simples.empty()) {
                            m_invalid = true;
                            return ss;
                        }
                        ss.slottedArg = std::move(comp.simples);
                    }
                } else if (ss.value == "part") {
                    if (arg.empty()) { m_invalid = true; return ss; }
                    ss.partArg = std::move(arg);
                }
            }
            return ss;
        }

        if (!canStartIdent()) { m_invalid = true; return ss; }
        ss.type = SimpleSelectorType::PseudoClass;
        ss.value = toLower(consumeIdent());

        // Accept CSS 2.1 single-colon pseudo-elements (Selectors L3 §7).
        if (ss.value == "before" || ss.value == "after" ||
            ss.value == "first-line" || ss.value == "first-letter") {
            ss.type = SimpleSelectorType::PseudoElement;
            if (peek() == '(') m_invalid = true;
            return ss;
        }

        if (peek() == '(') {
            advance();
            if (ss.value == "nth-child" || ss.value == "nth-last-child" ||
                ss.value == "nth-of-type" || ss.value == "nth-last-of-type") {
                if (!parseNth(ss) || peek() != ')') {
                    m_invalid = true;
                    return ss;
                }
                advance();
                return ss;
            }
            std::string arg;
            if (!readParenArg(arg)) return ss;
            std::string trimmedArg = trim(arg);

            if (ss.value == "not") {
                if (atDepthLimit()) return ss;
                if (trimmedArg.empty()) { m_invalid = true; return ss; }
                for (const std::string& raw : splitTopLevelCommas(arg)) {
                    std::string part = trim(raw);
                    if (part.empty()) { m_invalid = true; return ss; }
                    Selector sub = parseSelector(part);
                    if (sub.chain.entries.empty()) { m_invalid = true; return ss; }
                    if (sub.chain.entries.size() == 1)
                        ss.selectorListArg.push_back(std::move(sub.chain.entries[0].compound));
                    else
                        ss.chainListArg.push_back(std::move(sub.chain));
                }
            } else if (ss.value == "is" || ss.value == "where") {
                if (atDepthLimit()) return ss;
                // :is() / :where() take a forgiving selector list: skip invalid items,
                // never push empty compounds.
                for (const std::string& raw : splitTopLevelCommas(arg)) {
                    std::string part = trim(raw);
                    if (part.empty()) continue;
                    Selector sub = parseSelector(part);
                    if (sub.chain.entries.size() == 1)
                        ss.selectorListArg.push_back(std::move(sub.chain.entries[0].compound));
                    else if (sub.chain.entries.size() > 1)
                        ss.chainListArg.push_back(std::move(sub.chain));
                }
            } else if (ss.value == "has") {
                if (atDepthLimit()) return ss;
                if (trimmedArg.empty()) { m_invalid = true; return ss; }
                for (const std::string& raw : splitTopLevelCommas(arg)) {
                    std::string part = trim(raw);
                    if (part.empty()) { m_invalid = true; return ss; }
                    RelativeSelector rel;
                    char c0 = part[0];
                    if (c0 == '>') rel.lead = Combinator::Child;
                    else if (c0 == '+') rel.lead = Combinator::AdjacentSibling;
                    else if (c0 == '~') rel.lead = Combinator::GeneralSibling;
                    if (c0 == '>' || c0 == '+' || c0 == '~') part = trim(part.substr(1));
                    if (part.empty()) { m_invalid = true; return ss; }
                    rel.chain = parseSelector(part).chain;
                    if (rel.chain.entries.empty()) { m_invalid = true; return ss; }
                    ss.relativeArgs.push_back(std::move(rel));
                }
            } else if (ss.value == "host" || ss.value == "host-context") {
                if (atDepthLimit()) return ss;
                if (trimmedArg.empty()) { m_invalid = true; return ss; }
                SelectorParser sub(trimmedArg);
                auto comp = sub.parseCompound();
                sub.skipWS();
                if (sub.m_invalid || sub.m_pos < trimmedArg.size() || comp.simples.empty()) {
                    m_invalid = true;
                    return ss;
                }
                ss.hostArg = std::move(comp.simples);
            }
        }
        return ss;
    }

    bool parseNth(SimpleSelector& ss) {
        skipWS();
        std::string arg;
        while (m_pos < m_text.size() && peek() != ')') arg += advance();
        arg = trim(arg);
        if (arg.empty()) return false;
        std::string lower = toLower(arg);
        if (lower == "odd") { ss.nthA = 2; ss.nthB = 1; return true; }
        if (lower == "even") { ss.nthA = 2; ss.nthB = 0; return true; }

        auto parseSignedInt = [](std::string_view sv, int& out) -> bool {
            if (sv.empty()) return false;
            size_t i = (sv[0] == '+' || sv[0] == '-') ? 1 : 0;
            if (i >= sv.size()) return false;
            for (; i < sv.size(); ++i)
                if (!std::isdigit((unsigned char)sv[i])) return false;
            try { out = std::stoi(std::string(sv)); return true; }
            catch (...) { return false; }
        };

        size_t nPos = lower.find('n');
        if (nPos != std::string::npos) {
            std::string_view aPart(lower.data(), nPos);
            if (aPart.empty() || aPart == "+") ss.nthA = 1;
            else if (aPart == "-") ss.nthA = -1;
            else if (!parseSignedInt(aPart, ss.nthA)) return false;

            std::string bPart = trim(lower.substr(nPos + 1));
            if (bPart.empty()) { ss.nthB = 0; return true; }
            if (bPart[0] != '+' && bPart[0] != '-') return false;
            char sign = bPart[0];
            std::string rest = trim(bPart.substr(1));
            if (rest.empty()) return false;
            for (char ch : rest)
                if (!std::isdigit((unsigned char)ch)) return false;
            return parseSignedInt(std::string(1, sign) + rest, ss.nthB);
        }

        ss.nthA = 0;
        return parseSignedInt(lower, ss.nthB);
    }
};

// ---- Specificity Calculator ----

uint32_t computeSpecificity(const SelectorChain& chain);

void addSimpleSpecificity(const SimpleSelector& s, int& ids, int& classes, int& types) {
    switch (s.type) {
        case SimpleSelectorType::Id:
            ids++;
            break;
        case SimpleSelectorType::Class:
        case SimpleSelectorType::Attribute:
            classes++;
            break;
        case SimpleSelectorType::PseudoClass:
            if (s.value == "not" || s.value == "is" || s.value == "has") {
                uint32_t maxSpec = 0;
                for (auto& compound : s.selectorListArg) {
                    int ci = 0, cc = 0, ct = 0;
                    for (auto& inner : compound.simples)
                        addSimpleSpecificity(inner, ci, cc, ct);
                    uint32_t spec = (std::min(ci, 255) << 16) |
                                    (std::min(cc, 255) << 8) |
                                    std::min(ct, 255);
                    if (spec > maxSpec) maxSpec = spec;
                }
                for (auto& ch : s.chainListArg) {
                    uint32_t spec = computeSpecificity(ch);
                    if (spec > maxSpec) maxSpec = spec;
                }
                for (auto& rel : s.relativeArgs) {
                    uint32_t spec = computeSpecificity(rel.chain);
                    if (spec > maxSpec) maxSpec = spec;
                }
                ids += (maxSpec >> 16) & 0xFF;
                classes += (maxSpec >> 8) & 0xFF;
                types += maxSpec & 0xFF;
            } else if (s.value == "where") {
                // :where() contributes zero specificity
            } else if (s.value == "host" || s.value == "host-context") {
                classes++;
                for (auto& inner : s.hostArg)
                    addSimpleSpecificity(inner, ids, classes, types);
            } else {
                classes++;
            }
            break;
        case SimpleSelectorType::Tag:
            types++;
            break;
        case SimpleSelectorType::PseudoElement:
            types++;
            if (s.value == "slotted") {
                for (auto& inner : s.slottedArg)
                    addSimpleSpecificity(inner, ids, classes, types);
            }
            break;
        case SimpleSelectorType::Universal:
            break;
    }
}

uint32_t computeSpecificity(const SelectorChain& chain) {
    int ids = 0, classes = 0, types = 0;
    for (auto& entry : chain.entries) {
        for (auto& s : entry.compound.simples)
            addSimpleSpecificity(s, ids, classes, types);
    }
    if (ids > 255) ids = 255;
    if (classes > 255) classes = 255;
    if (types > 255) types = 255;
    return (static_cast<uint32_t>(ids) << 16) |
           (static_cast<uint32_t>(classes) << 8) |
           (static_cast<uint32_t>(types));
}

} // anonymous namespace

// ---- Public API ----

Selector parseSelector(const std::string& text) {
    std::string trimmed = trim(text);
    if (trimmed.empty() || !hasBalancedDelimiters(trimmed))
        return {text, 0, {}};
    SelectorParser parser(trimmed);
    auto chain = parser.parseChain();
    uint32_t spec = computeSpecificity(chain);
    return {text, spec, std::move(chain)};
}

std::vector<Selector> parseSelectorList(const std::string& text) {
    std::vector<Selector> selectors;
    if (trim(text).empty() || !hasBalancedDelimiters(text)) return selectors;
    for (const std::string& raw : splitTopLevelCommas(text)) {
        std::string part = trim(raw);
        if (part.empty()) continue;
        Selector sel = parseSelector(part);
        if (!sel.chain.entries.empty()) selectors.push_back(std::move(sel));
    }
    return selectors;
}

bool parseSelectorListChecked(const std::string& text, std::vector<Selector>& out) {
    out.clear();
    if (trim(text).empty() || !hasBalancedDelimiters(text)) return false;
    auto parts = splitTopLevelCommas(text);
    if (parts.empty()) return false;
    for (const std::string& raw : parts) {
        std::string part = trim(raw);
        if (part.empty()) {
            out.clear();
            return false;
        }
        Selector sel = parseSelector(part);
        if (sel.chain.entries.empty()) {
            out.clear();
            return false;
        }
        out.push_back(std::move(sel));
    }
    return !out.empty();
}

uint32_t calculateSpecificity(const std::string& selector) {
    auto sel = parseSelector(selector);
    return sel.specificity;
}

} // namespace htmlayout::css
