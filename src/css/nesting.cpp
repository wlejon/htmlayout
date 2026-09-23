// css-nesting-1 desugaring: nested style rules become flat rules whose
// selector has the parent substituted for `&`.
//
// The spec defines `&` as `:is(<parent list>)`. The selector engine's :is()
// only takes compound selectors, so instead each nested selector is expanded
// against every parent alternative (a cross product), textually:
//   - `&` as a whole compound is replaced by the parent selector;
//   - `&` inside a compound (`&.x`, `.x&:hover`) merges the parent's last
//     compound into it, keeping the parent's leading compounds in front;
//   - a selector with no top-level `&` is relative: `.c` is `& .c`,
//     `> .c` is `& > .c`;
//   - `&` inside a functional pseudo-class (`:not(&)`) is replaced by the whole
//     parent list, since those arguments are lists.
// Deviations from the spec, all inherent in flattening without complex :is():
//   - Specificity: each expansion carries its own parent alternative's
//     specificity, where the spec gives every expansion the most specific
//     parent's (`#a, .b { & .x {} }` makes `.b .x` (0,2,0), not (1,1,0)).
//   - `.a &` with a complex parent `.p .q` becomes `.a .p .q`, which requires
//     .a above .p; `:is(.p .q)` would also match with .a between them.
//   - A parent selector list containing pseudo-elements is substituted as
//     text rather than matching nothing.
#include "css/nesting.h"

namespace htmlayout::css::nesting {

namespace {

std::string trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\n\r\f");
    if (b == std::string_view::npos) return {};
    size_t e = s.find_last_not_of(" \t\n\r\f");
    return std::string(s.substr(b, e - b + 1));
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool isCombinatorChar(char c) { return c == '>' || c == '+' || c == '~'; }
bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || (unsigned char)c >= 0x80;
}

// Walks `s`, calling f(index, depth) for every character that is not inside a
// string and not escaped. Depth counts () and [] nesting.
template <typename F>
void walk(std::string_view s, F&& f) {
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (quote) {
            if (c == '\\') i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\\') { i++; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(' || c == '[') { f(i, depth); depth++; continue; }
        if (c == ')' || c == ']') { if (depth > 0) depth--; f(i, depth); continue; }
        f(i, depth);
    }
}

struct Item {
    std::string text;  // compound text, or a normalised combinator (" ", " > ", …)
    bool combinator = false;
};

std::vector<Item> segment(std::string_view s) {
    std::vector<Item> items;
    std::string cur;
    std::string comb;  // pending combinator run
    bool inComb = false;
    size_t copied = 0;  // characters of s already consumed into cur/comb
    auto flushCompound = [&]() {
        if (!cur.empty()) items.push_back({cur, false});
        cur.clear();
    };
    auto flushComb = [&]() {
        if (!inComb) return;
        char op = 0;
        for (char c : comb) if (isCombinatorChar(c)) op = c;
        std::string text = op ? std::string(" ") + op + " " : " ";
        items.push_back({text, true});
        comb.clear();
        inComb = false;
    };
    walk(s, [&](size_t i, int depth) {
        // Copy anything skipped by walk() (strings, escapes) into the compound.
        if (copied < i) {
            if (inComb) flushComb();
            cur.append(s.substr(copied, i - copied));
        }
        copied = i + 1;
        char c = s[i];
        if (depth == 0 && (isSpace(c) || isCombinatorChar(c))) {
            if (!inComb) { flushCompound(); inComb = true; }
            comb += c;
            return;
        }
        if (inComb) flushComb();
        cur += c;
    });
    if (copied < s.size()) {
        if (inComb) flushComb();
        cur.append(s.substr(copied));
    }
    // A trailing combinator run is only whitespace after trimming; drop it.
    if (!inComb) flushCompound();
    return items;
}

std::string join(const std::vector<Item>& items) {
    std::string out;
    for (auto& it : items) out += it.text;
    return out;
}

bool hasTopLevelAmp(std::string_view s) {
    bool found = false;
    walk(s, [&](size_t i, int depth) { if (depth == 0 && s[i] == '&') found = true; });
    return found;
}

// Replace `&` inside (), e.g. `:not(&)`, with the whole parent list.
std::string replaceInnerAmps(std::string_view s, const std::string& parentList) {
    std::string out;
    size_t last = 0;
    walk(s, [&](size_t i, int depth) {
        if (depth > 0 && s[i] == '&') {
            out.append(s.substr(last, i - last));
            out += parentList;
            last = i + 1;
        }
    });
    out.append(s.substr(last));
    return out;
}

std::string removeTopLevelAmps(std::string_view s) {
    std::string out;
    size_t last = 0;
    walk(s, [&](size_t i, int depth) {
        if (depth == 0 && s[i] == '&') {
            out.append(s.substr(last, i - last));
            last = i + 1;
        }
    });
    out.append(s.substr(last));
    return out;
}

// Length of a leading type selector (`div`, `*`, `ns|div`), 0 if none.
size_t typePrefixLength(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (isIdentChar(c) || c == '*' || c == '|') { i++; continue; }
        if (c == '\\' && i + 1 < s.size()) { i += 2; continue; }
        break;
    }
    // A leading digit or lone '-' can't start a type selector, but the
    // tokenizer never hands us one at a compound start, so accept.
    return i;
}

bool hasAnyAmp(std::string_view s) {
    bool found = false;
    walk(s, [&](size_t i, int) { if (s[i] == '&') found = true; });
    return found;
}

std::string expandOne(std::string_view nested, const std::string& parent,
                      const std::string& parentList) {
    // A selector containing `&` anywhere (even only as `:not(&)`) is not
    // relative, so it gets no implicit `& ` prefix.
    bool anyAmp = hasAnyAmp(nested);
    std::string s = replaceInnerAmps(nested, parentList);
    auto items = segment(s);
    if (items.empty()) return {};

    if (!anyAmp) {
        // Relative selector: implicitly `& <nested>`.
        std::string body = join(items);
        return items[0].combinator ? parent + body : parent + " " + body;
    }

    auto pItems = segment(parent);
    std::string pLast, pPrefix;
    if (!pItems.empty() && !pItems.back().combinator) {
        pLast = pItems.back().text;
        pItems.pop_back();
        pPrefix = join(pItems);
    } else {
        pLast = parent;
    }

    for (auto& it : items) {
        if (it.combinator || !hasTopLevelAmp(it.text)) continue;
        if (it.text == "&") { it.text = parent; continue; }
        bool ampFirst = it.text[0] == '&';
        std::string rest = removeTopLevelAmps(it.text);
        std::string type;
        if (!ampFirst) {
            size_t n = typePrefixLength(rest);
            type = rest.substr(0, n);
            rest = rest.substr(n);
        }
        std::string last = pLast;
        // `div&` with parent `span`: two type selectors can't share a
        // compound textually, so keep the parent's behind :is().
        if (!type.empty() && typePrefixLength(pLast) > 0) last = ":is(" + pLast + ")";
        it.text = pPrefix + type + last + rest;
    }
    return join(items);
}

} // namespace

std::vector<std::string> splitSelectorList(std::string_view list) {
    std::vector<std::string> parts;
    size_t start = 0;
    walk(list, [&](size_t i, int depth) {
        if (depth == 0 && list[i] == ',') {
            std::string p = trim(list.substr(start, i - start));
            if (!p.empty()) parts.push_back(std::move(p));
            start = i + 1;
        }
    });
    std::string p = trim(list.substr(start));
    if (!p.empty()) parts.push_back(std::move(p));
    return parts;
}

std::vector<std::string> resolveTopLevel(std::string_view list) {
    auto alts = splitSelectorList(list);
    static const std::string kScope = ":scope";
    for (auto& alt : alts)
        if (hasAnyAmp(alt)) alt = expandOne(alt, kScope, kScope);
    return alts;
}

std::vector<std::string> resolve(std::string_view nestedList,
                                 const std::vector<std::string>& parents) {
    auto alts = splitSelectorList(nestedList);
    if (parents.empty()) return alts;
    std::string parentList;
    for (size_t i = 0; i < parents.size(); i++) {
        if (i) parentList += ", ";
        parentList += parents[i];
    }
    // The cross product (and `& &`, which doubles the text) grows
    // geometrically with depth; a rule past these bounds is dropped, as
    // nothing but a hostile sheet reaches them.
    constexpr size_t kMaxSelectors = 4096;
    constexpr size_t kMaxBytes = 1 << 20;
    if (parentList.size() > kMaxBytes) return {};
    std::vector<std::string> out;
    size_t bytes = 0;
    for (auto& alt : alts) {
        for (auto& p : parents) {
            std::string r = expandOne(alt, p, parentList);
            if (r.empty()) continue;
            bytes += r.size();
            if (out.size() >= kMaxSelectors || bytes > kMaxBytes) return {};
            out.push_back(std::move(r));
        }
    }
    return out;
}

} // namespace htmlayout::css::nesting
