#include "css/length.h"
#include "../from_chars_compat.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <vector>

namespace htmlayout::css {

// Current viewport for vw/vh/vmin/vmax resolution, set by layoutTree() before each
// pass. Thread-local so concurrent layout passes on separate documents/threads do
// not race on viewport dimensions. 0 means "unknown".
static thread_local float g_viewportWidth = 0.0f;
static thread_local float g_viewportHeight = 0.0f;

// Root element (<html>) font-size, used to resolve rem units. Threaded the same
// thread-local way as the viewport since rem, like vw/vh, needs a document-global
// reference the per-site resolver has no node to reach. Defaults to 16 (the
// initial font-size) until layoutTree() sets the real root size.
static thread_local float g_rootFontSize = 16.0f;

// ch and ex are font-metric units: 1ch is the advance of "0" in the element's
// font, 1ex its x-height. Both depend on the font family AND size, which vary
// per element, so they can't be a once-per-pass global like the viewport. Layout
// sets this thread-local context (absolute px for the element's current font) right
// before it resolves that element's own lengths. 0 means "unset" — the resolver then
// falls back to the CSS-defined 0.5em approximation so callers without a font in hand
// (intrinsic-width probes, standalone resolveLength) keep working.
static thread_local float g_chPx = 0.0f;
static thread_local float g_exPx = 0.0f;

void setLengthViewport(float width, float height) {
    g_viewportWidth = width;
    g_viewportHeight = height;
}

void setLengthRootFontSize(float px) {
    g_rootFontSize = (px > 0.0f) ? px : 16.0f;
}

void setLengthFontMetrics(float chPx, float exPx) {
    g_chPx = chPx;
    g_exPx = exPx;
}

// Callers screen out the keywords before calling this, and from_chars rejects
// them anyway (falling through to the thin/medium/thick ladder, which returns 0
// for them) — so re-testing them here would just be a second round of
// strlen-per-literal on the hottest path in layout.
float resolveSingleLength(const std::string& value, float referenceSize, float fontSize) {
    if (value.empty()) return 0.0f;

    const char* begin = value.data();
    const char* end = begin + value.size();
    float num = 0.0f;

    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
    if (ec != std::errc()) {
        if (value == "thin") return 1.0f;
        if (value == "medium") return 3.0f;
        if (value == "thick") return 5.0f;
        return 0.0f;
    }

    // The unit is a view of the tail, not a copy of it: this runs on every length
    // of every node of every pass, and building a std::string here to compare it
    // against a ladder of literals was most of what the ladder cost.
    //
    // Dispatched on length first, so `px` — far and away the common case — is
    // reached in one compare instead of walking past thirteen others.
    std::string_view unit(ptr, static_cast<size_t>(end - ptr));
    // Viewport units resolve against the real viewport (set by layoutTree). When a
    // viewport dimension is unknown (0), fall back to the percentage reference.
    const auto vwRef = [&] { return g_viewportWidth  > 0.0f ? g_viewportWidth  : referenceSize; };
    const auto vhRef = [&] { return g_viewportHeight > 0.0f ? g_viewportHeight : referenceSize; };

    switch (unit.size()) {
    case 0:
        return num;
    case 1:
        if (unit[0] == '%') return num * referenceSize / 100.0f;
        break;
    case 2:
        if (unit == "px") return num;
        if (unit == "em") return num * fontSize;
        if (unit == "vw") return num * vwRef() / 100.0f;
        if (unit == "vh") return num * vhRef() / 100.0f;
        if (unit == "pt") return num * 96.0f / 72.0f;
        if (unit == "ch") return num * (g_chPx > 0.0f ? g_chPx : fontSize * 0.5f);
        if (unit == "ex") return num * (g_exPx > 0.0f ? g_exPx : fontSize * 0.5f);
        if (unit == "cm") return num * 96.0f / 2.54f;
        if (unit == "mm") return num * 96.0f / 25.4f;
        if (unit == "in") return num * 96.0f;
        if (unit == "pc") return num * 96.0f / 6.0f;
        break;
    case 3:
        if (unit == "rem") return num * g_rootFontSize;
        break;
    case 4:
        if (unit == "vmin") return num * std::min(vwRef(), vhRef()) / 100.0f;
        if (unit == "vmax") return num * std::max(vwRef(), vhRef()) / 100.0f;
        break;
    default:
        break;
    }

    return num;
}

// A recursive-descent parser over a calc() expression: number+unit, +, -, *, /,
// parentheses, and nested calc/min/max/clamp.
float evalLengthCalc(const std::string& expr, float referenceSize, float fontSize) {
    struct CalcParser {
        const std::string& s;
        size_t pos;
        float refSize, fontSz;

        void skipSpaces() {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        }

        // Parse a comma-separated argument list up to the closing ')'.
        // Consumes the ')'. Each argument is a full additive expression.
        std::vector<float> parseArgs() {
            std::vector<float> args;
            while (true) {
                args.push_back(parseExpr());
                skipSpaces();
                if (pos < s.size() && s[pos] == ',') { pos++; continue; }
                break;
            }
            skipSpaces();
            if (pos < s.size() && s[pos] == ')') pos++;
            return args;
        }

        // Case-insensitive match of a function name + '(' at the cursor.
        bool matchFunc(const char* name) {
            size_t n = std::strlen(name);
            if (pos + n + 1 > s.size()) return false;
            for (size_t i = 0; i < n; i++)
                if (std::tolower(static_cast<unsigned char>(s[pos + i])) != name[i])
                    return false;
            return s[pos + n] == '(';
        }

        // Parse a primary: math function, number+unit, (expr), or calc(expr)
        float parsePrimary() {
            skipSpaces();
            if (pos >= s.size()) return 0.0f;

            // CSS math functions: calc/min/max/clamp (nestable, case-insensitive).
            if (matchFunc("calc")) {
                pos += 5; // "calc("
                float val = parseExpr();
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') pos++;
                return val;
            }
            if (matchFunc("min")) {
                pos += 4; // "min("
                auto args = parseArgs();
                if (args.empty()) return 0.0f;
                float v = args[0];
                for (float a : args) v = std::min(v, a);
                return v;
            }
            if (matchFunc("max")) {
                pos += 4; // "max("
                auto args = parseArgs();
                if (args.empty()) return 0.0f;
                float v = args[0];
                for (float a : args) v = std::max(v, a);
                return v;
            }
            if (matchFunc("clamp")) {
                pos += 6; // "clamp("
                auto args = parseArgs();
                // clamp(MIN, VAL, MAX) == max(MIN, min(VAL, MAX)).
                if (args.size() < 3) return args.empty() ? 0.0f : args[0];
                float lo = args[0], val = args[1], hi = args[2];
                return std::max(lo, std::min(val, hi));
            }

            // Handle parenthesized sub-expression
            if (s[pos] == '(') {
                pos++;
                float val = parseExpr();
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') pos++;
                return val;
            }

            // Parse number + optional unit
            size_t numStart = pos;
            // Handle leading sign
            if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
            // Digits and decimal
            while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) pos++;

            // Parse unit
            while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) pos++;
            // Handle %
            if (pos < s.size() && s[pos] == '%') pos++;

            std::string token = s.substr(numStart, pos - numStart);
            return resolveSingleLength(token, refSize, fontSz);
        }

        // Parse multiplicative: primary (* or / primary)*
        float parseMul() {
            float left = parsePrimary();
            while (true) {
                skipSpaces();
                if (pos >= s.size()) break;
                if (s[pos] == '*') {
                    pos++; skipSpaces();
                    left *= parsePrimary();
                } else if (s[pos] == '/') {
                    pos++; skipSpaces();
                    float right = parsePrimary();
                    if (right != 0.0f) left /= right;
                } else {
                    break;
                }
            }
            return left;
        }

        // Parse additive: mul (+ or - mul)*. Every nesting level (parens,
        // calc/min/max/clamp) comes back through here, so the depth bound
        // keeps a hostile `((((…` from running the stack out.
        int depth = 0;
        float parseExpr() {
            if (depth >= 64) { pos = s.size(); return 0.0f; }
            ++depth;
            float left = parseExprBody();
            --depth;
            return left;
        }
        float parseExprBody() {
            float left = parseMul();
            while (true) {
                skipSpaces();
                if (pos >= s.size()) break;
                if (s[pos] == '+' && (pos + 1 < s.size() && s[pos + 1] == ' ')) {
                    pos++; skipSpaces();
                    left += parseMul();
                } else if (s[pos] == '-' && (pos + 1 < s.size() && s[pos + 1] == ' ')) {
                    pos++; skipSpaces();
                    left -= parseMul();
                } else if (s[pos] == '+' || s[pos] == '-') {
                    // Could be a signed number (no spaces around operator)
                    // In valid calc(), + and - require spaces, so try as operator
                    char op = s[pos];
                    pos++; skipSpaces();
                    float right = parseMul();
                    if (op == '+') left += right;
                    else left -= right;
                } else {
                    break;
                }
            }
            return left;
        }
    };

    CalcParser parser{expr, 0, referenceSize, fontSize};
    return parser.parseExpr();
}

bool isLengthMathFunction(std::string_view v) {
    // Every length resolution asks this, and almost every one of them is a plain
    // `16px` that has to walk all four names to say no. A math function is
    // `name(...)`, so a value that doesn't end in ')' cannot be one — check that
    // first and the common case leaves after a single byte compare.
    if (v.size() < 5 || v.back() != ')') return false;
    auto starts = [&](const char* f) {
        size_t n = std::strlen(f);
        if (v.size() < n + 1) return false;
        for (size_t i = 0; i < n; i++) {
            // ASCII-lower by hand: std::tolower is locale-aware, which means a
            // function call and a table lookup per character, on a path this hot.
            char c = v[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != f[i]) return false;
        }
        return v[n] == '(';
    };
    return starts("calc") || starts("min") || starts("max") || starts("clamp");
}

} // namespace htmlayout::css
