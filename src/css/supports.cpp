#include "css/parser.h"
#include "css/properties.h"
#include "css/color.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>

namespace htmlayout::css {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r\f");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r\f");
    return s.substr(start, end - start + 1);
}

// @supports declaration probe: does this engine support `property: value`?
// Mirrors browser behavior of "does the declaration parse" as closely as
// the engine's string-valued style store allows: the property must be
// known, and for the commonly probed enumerated/color properties the
// value must be valid. Everything else is permissive.
bool isDeclarationSupported(std::string prop, std::string value) {
    auto toLower = [](std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
    };
    prop = trim(prop);
    value = trim(value);
    toLower(prop);
    if (prop.empty() || value.empty()) return false;
    if (prop.rfind("--", 0) == 0) return true; // custom property declaration

    static const std::unordered_set<std::string> knownSet = [] {
        std::unordered_set<std::string> s;
        for (const auto& def : knownProperties()) s.insert(def.name);
        return s;
    }();
    // Shorthands expand to known longhands; accept a property when either
    // it's known directly or its shorthand expansion produced longhands
    // with different names.
    if (!knownSet.count(prop)) {
        auto expanded = expandShorthand(prop, value);
        bool isShorthand = false;
        for (const auto& d : expanded)
            if (d.property != prop) { isShorthand = true; break; }
        if (!isShorthand) return false;
    }

    std::string lowerVal = value;
    toLower(lowerVal);
    // Can't validate substituted or global values — treat as supported.
    if (lowerVal.find("var(") != std::string::npos) return true;
    if (lowerVal == "inherit" || lowerVal == "initial" ||
        lowerVal == "unset" || lowerVal == "revert" ||
        lowerVal == "revert-layer") return true;

    static const std::unordered_set<std::string> displayValues = {
        "none", "block", "inline", "inline-block", "flex", "inline-flex",
        "grid", "inline-grid", "contents", "list-item", "flow-root",
        "table", "inline-table", "table-row", "table-cell", "table-caption",
        "table-row-group", "table-header-group", "table-footer-group",
        "table-column", "table-column-group", "-webkit-box", "-webkit-inline-box",
    };
    static const std::unordered_set<std::string> positionValues = {
        "static", "relative", "absolute", "fixed", "sticky",
    };
    static const std::unordered_set<std::string> floatValues = {
        "none", "left", "right", "inline-start", "inline-end",
    };
    if (prop == "display") return displayValues.count(lowerVal) > 0;
    if (prop == "position") return positionValues.count(lowerVal) > 0;
    if (prop == "float") return floatValues.count(lowerVal) > 0;
    if (prop == "color" || (prop.size() > 6 &&
            prop.compare(prop.size() - 6, 6, "-color") == 0)) {
        if (lowerVal == "transparent" || lowerVal == "currentcolor" ||
            lowerVal == "inherit") return true;
        Color parsed;
        return tryParseColor(value, parsed);
    }
    return true;
}

// @supports condition evaluator (CSS Conditional Rules Level 3):
//   cond          := 'not' in-parens | in-parens (('and'|'or') in-parens)*
//   in-parens     := '(' cond ')' | '(' declaration ')' | func '(' any ')'
// Unknown/unparseable constructs evaluate to false, per spec.
struct SupportsEvaluator {
    const std::string& s;
    size_t pos = 0;
    explicit SupportsEvaluator(const std::string& str) : s(str) {}

    void ws() {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) pos++;
    }
    bool keyword(const char* w) {
        size_t len = std::strlen(w);
        if (pos + len > s.size()) return false;
        for (size_t i = 0; i < len; i++)
            if (std::tolower((unsigned char)s[pos + i]) != w[i]) return false;
        if (pos + len < s.size()) {
            char c = s[pos + len];
            if (std::isalnum((unsigned char)c) || c == '-') return false;
        }
        pos += len;
        return true;
    }

    bool parseCondition(bool& ok) {
        ws();
        if (keyword("not")) {
            bool inner;
            if (!parseInParens(inner)) return false;
            ok = !inner;
            return true;
        }
        bool acc;
        if (!parseInParens(acc)) return false;
        ws();
        while (pos < s.size()) {
            if (keyword("and")) {
                bool r;
                if (!parseInParens(r)) return false;
                acc = acc && r;
            } else if (keyword("or")) {
                bool r;
                if (!parseInParens(r)) return false;
                acc = acc || r;
            } else {
                break;
            }
            ws();
        }
        ok = acc;
        return true;
    }

    bool parseInParens(bool& ok) {
        ws();
        if (pos < s.size() && s[pos] == '(') {
            pos++; // '('
            ws();
            // Nested condition? ('(' or 'not' at cursor)
            size_t save = pos;
            if ((pos < s.size() && s[pos] == '(') || peekKeyword("not")) {
                if (parseCondition(ok)) {
                    ws();
                    if (pos < s.size() && s[pos] == ')') { pos++; return true; }
                }
                pos = save;
            }
            // Declaration: prop : value (up to the matching ')')
            int depth = 1;
            size_t start = pos;
            while (pos < s.size() && depth > 0) {
                if (s[pos] == '(') depth++;
                else if (s[pos] == ')') depth--;
                if (depth > 0) pos++;
            }
            if (pos >= s.size()) return false;
            std::string inner = s.substr(start, pos - start);
            pos++; // ')'
            auto colon = inner.find(':');
            if (colon == std::string::npos) { ok = false; return true; }
            ok = isDeclarationSupported(inner.substr(0, colon),
                                        inner.substr(colon + 1));
            return true;
        }
        // Function form, e.g. selector(...) — skip the balanced body.
        size_t start = pos;
        while (pos < s.size() &&
               (std::isalnum((unsigned char)s[pos]) || s[pos] == '-')) pos++;
        if (pos > start && pos < s.size() && s[pos] == '(') {
            std::string func = s.substr(start, pos - start);
            std::transform(func.begin(), func.end(), func.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            int depth = 0;
            while (pos < s.size()) {
                if (s[pos] == '(') depth++;
                else if (s[pos] == ')' && --depth == 0) { pos++; break; }
                pos++;
            }
            // selector(...) probes pass (we support the selector engine);
            // other functions are unknown → false.
            ok = (func == "selector");
            return true;
        }
        return false;
    }

    bool peekKeyword(const char* w) {
        size_t save = pos;
        bool r = keyword(w);
        pos = save;
        return r;
    }
};

} // anonymous namespace

bool evaluateSupportsCondition(const std::string& condition) {
    if (trim(condition).empty()) return false;
    SupportsEvaluator ev(condition);
    bool ok = false;
    if (!ev.parseCondition(ok)) return false;
    return ok;
}

bool supportsDeclaration(const std::string& property, const std::string& value) {
    return isDeclarationSupported(property, value);
}

bool supportsCondition(const std::string& condition) {
    if (evaluateSupportsCondition(condition)) return true;
    // CSS.supports(conditionText) also accepts a bare declaration.
    std::string t = condition;
    while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
    if (!t.empty() && t.front() != '(')
        return evaluateSupportsCondition("(" + t + ")");
    return false;
}

} // namespace htmlayout::css
