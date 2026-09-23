#include "css/color_calc.h"
#include "../from_chars_compat.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace htmlayout::css::colorcalc {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Tok {
    enum Kind { Num, Ident, Func, Op, LParen, RParen, Comma } kind;
    Value val;            // Num
    std::string_view s;   // Ident / Func name
    char op = 0;          // Op: + - * /
};

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || c == '-' || c == '_' || (unsigned char)c >= 0x80;
}
bool isIdentChar(char c) { return isIdentStart(c) || isDigit(c); }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool startsNumber(std::string_view s, size_t p) {
    if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
    if (p < s.size() && isDigit(s[p])) return true;
    return p + 1 < s.size() && s[p] == '.' && isDigit(s[p + 1]);
}

std::optional<double> angleToDeg(double v, std::string_view unit) {
    if (unit == "deg") return v;
    if (unit == "rad") return v * 180.0 / kPi;
    if (unit == "grad") return v * 0.9;
    if (unit == "turn") return v * 360.0;
    return std::nullopt;
}

std::optional<std::vector<Tok>> lex(std::string_view s) {
    std::vector<Tok> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (isSpace(c)) { i++; continue; }
        if (c == '(') { out.push_back({Tok::LParen}); i++; continue; }
        if (c == ')') { out.push_back({Tok::RParen}); i++; continue; }
        if (c == ',') { out.push_back({Tok::Comma}); i++; continue; }
        if (c == '*' || c == '/') {
            Tok t{Tok::Op};
            t.op = c;
            out.push_back(t);
            i++;
            continue;
        }
        if (startsNumber(s, i)) {
            size_t p = i;
            if (s[p] == '+' || s[p] == '-') p++;
            while (p < n && isDigit(s[p])) p++;
            if (p + 1 < n && s[p] == '.' && isDigit(s[p + 1])) {
                p++;
                while (p < n && isDigit(s[p])) p++;
            }
            if (p < n && s[p] == 'e') {
                size_t q = p + 1;
                if (q < n && (s[q] == '+' || s[q] == '-')) q++;
                if (q < n && isDigit(s[q])) {
                    p = q;
                    while (p < n && isDigit(s[p])) p++;
                }
            }
            const char* b = s.data() + i;
            if (*b == '+') b++;
            double v = 0;
            auto [ptr, ec] = htmlayout::from_chars_fp(b, s.data() + p, v);
            if (ec != std::errc() || ptr != s.data() + p) return std::nullopt;
            Tok t{Tok::Num};
            t.val = {v, Type::Number};
            i = p;
            if (i < n && s[i] == '%') {
                t.val.type = Type::Percent;
                i++;
            } else if (i < n && isIdentStart(s[i])) {
                size_t u = i;
                while (i < n && isIdentChar(s[i])) i++;
                auto deg = angleToDeg(v, s.substr(u, i - u));
                if (!deg) return std::nullopt;  // no lengths or other units in a colour
                t.val = {*deg, Type::Angle};
            }
            out.push_back(t);
            continue;
        }
        if (c == '+' || c == '-') {
            // An operator: CSS wants whitespace on both sides, and a '-' that
            // starts an identifier (`-h`) is not an operator at all.
            bool spaced = i + 1 >= n || isSpace(s[i + 1]) || s[i + 1] == '(';
            if (!spaced && c == '-' && i + 1 < n && isIdentStart(s[i + 1])) {
                // fall through to the identifier branch
            } else {
                Tok t{Tok::Op};
                t.op = c;
                out.push_back(t);
                i++;
                continue;
            }
        }
        if (isIdentStart(c)) {
            size_t start = i;
            while (i < n && isIdentChar(s[i])) i++;
            Tok t{Tok::Ident};
            t.s = s.substr(start, i - start);
            if (i < n && s[i] == '(') {
                t.kind = Tok::Func;
                out.push_back(t);
                out.push_back({Tok::LParen});
                i++;
                continue;
            }
            out.push_back(t);
            continue;
        }
        return std::nullopt;
    }
    return out;
}

struct Parser {
    const std::vector<Tok>& t;
    std::span<const Channel> channels;
    size_t p = 0;
    int depth = 0;

    bool at(Tok::Kind k) const { return p < t.size() && t[p].kind == k; }
    bool atOp(char op) const { return at(Tok::Op) && t[p].op == op; }

    std::optional<Value> sum() {
        auto a = product();
        if (!a) return std::nullopt;
        while (atOp('+') || atOp('-')) {
            char op = t[p++].op;
            auto b = product();
            if (!b || b->type != a->type) return std::nullopt;
            a->v = op == '+' ? a->v + b->v : a->v - b->v;
        }
        return a;
    }

    std::optional<Value> product() {
        auto a = term();
        if (!a) return std::nullopt;
        while (atOp('*') || atOp('/')) {
            char op = t[p++].op;
            auto b = term();
            if (!b) return std::nullopt;
            if (op == '*') {
                if (a->type != Type::Number && b->type != Type::Number) return std::nullopt;
                Type ty = a->type == Type::Number ? b->type : a->type;
                a = Value{a->v * b->v, ty};
            } else {
                if (b->type != Type::Number) return std::nullopt;
                a->v = a->v / b->v;
            }
        }
        return a;
    }

    // Comma-separated sums up to the closing parenthesis (consumed).
    std::optional<std::vector<Value>> args() {
        std::vector<Value> out;
        for (;;) {
            auto v = sum();
            if (!v) return std::nullopt;
            out.push_back(*v);
            if (at(Tok::Comma)) { p++; continue; }
            if (!at(Tok::RParen)) return std::nullopt;
            p++;
            return out;
        }
    }

    std::optional<Value> function(std::string_view name) {
        if (++depth > 32) return std::nullopt;
        auto a = args();
        --depth;
        if (!a || a->empty()) return std::nullopt;
        auto& v = *a;
        for (const Value& x : v)
            if (x.type != v[0].type) return std::nullopt;
        if (name == "calc") {
            if (v.size() != 1) return std::nullopt;
            return v[0];
        }
        if (name == "min" || name == "max") {
            Value r = v[0];
            for (const Value& x : v)
                r.v = name == "min" ? std::fmin(r.v, x.v) : std::fmax(r.v, x.v);
            return r;
        }
        if (name == "clamp") {
            if (v.size() != 3) return std::nullopt;
            return Value{std::fmax(v[0].v, std::fmin(v[1].v, v[2].v)), v[0].type};
        }
        if (name == "abs") {
            if (v.size() != 1) return std::nullopt;
            return Value{std::fabs(v[0].v), v[0].type};
        }
        if (name == "sign") {
            if (v.size() != 1) return std::nullopt;
            double x = v[0].v;
            return Value{x > 0 ? 1.0 : x < 0 ? -1.0 : x, Type::Number};
        }
        return std::nullopt;
    }

    std::optional<Value> term() {
        if (p >= t.size()) return std::nullopt;
        const Tok& k = t[p++];
        switch (k.kind) {
            case Tok::Num: return k.val;
            case Tok::LParen: {
                auto v = sum();
                if (!v || !at(Tok::RParen)) return std::nullopt;
                p++;
                return v;
            }
            case Tok::Func:
                if (!isMathFunction(k.s) || !at(Tok::LParen)) return std::nullopt;
                p++;
                return function(k.s);
            case Tok::Ident: {
                for (const Channel& c : channels)
                    if (c.name == k.s) return Value{c.value, Type::Number};
                if (k.s == "e") return Value{2.718281828459045, Type::Number};
                if (k.s == "pi") return Value{kPi, Type::Number};
                if (k.s == "infinity")
                    return Value{std::numeric_limits<double>::infinity(), Type::Number};
                if (k.s == "-infinity")
                    return Value{-std::numeric_limits<double>::infinity(), Type::Number};
                if (k.s == "nan")
                    return Value{std::numeric_limits<double>::quiet_NaN(), Type::Number};
                return std::nullopt;
            }
            default: return std::nullopt;
        }
    }
};

} // namespace

bool isMathFunction(std::string_view name) {
    return name == "calc" || name == "min" || name == "max" || name == "clamp" ||
           name == "abs" || name == "sign";
}

std::optional<Value> evaluate(std::string_view name, std::string_view inner,
                              std::span<const Channel> channels) {
    if (!isMathFunction(name)) return std::nullopt;
    auto toks = lex(inner);
    if (!toks) return std::nullopt;
    // Re-use the function path: `name(` `inner` `)`.
    toks->push_back({Tok::RParen});
    Parser ps{*toks, channels};
    auto v = ps.function(name);
    if (!v || ps.p != toks->size()) return std::nullopt;
    if (std::isnan(v->v)) v->v = 0;
    return v;
}

} // namespace htmlayout::css::colorcalc
