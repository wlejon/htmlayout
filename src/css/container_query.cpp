#include "css/container_query.h"
#include "css/tokenizer.h"
#include "from_chars_compat.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

namespace htmlayout::css {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string tokenText(const Token& t) {
    switch (t.type) {
        case TokenType::Whitespace: return " ";
        case TokenType::Colon: return ":";
        case TokenType::Semicolon: return ";";
        case TokenType::Comma: return ",";
        case TokenType::LeftBrace: return "{";
        case TokenType::RightBrace: return "}";
        case TokenType::LeftBracket: return "[";
        case TokenType::RightBracket: return "]";
        case TokenType::LeftParen: return "(";
        case TokenType::RightParen: return ")";
        case TokenType::Hash: return "#" + t.value;
        case TokenType::AtKeyword: return "@" + t.value;
        case TokenType::String: return "\"" + t.value + "\"";
        case TokenType::Function: return t.value + "(";
        case TokenType::Dimension: return t.value + t.unit;
        case TokenType::Percentage: return t.value + "%";
        case TokenType::CDO: return "<!--";
        case TokenType::CDC: return "-->";
        case TokenType::EndOfFile: return "";
        default: return t.value;
    }
}

std::vector<Token> tokensOf(const std::string& text) {
    std::vector<Token> toks = tokenize(text);
    while (!toks.empty() && toks.back().type == TokenType::EndOfFile) toks.pop_back();
    return toks;
}

// Tokens [b, e) joined, whitespace runs as one space, trimmed.
std::string joinTokens(const std::vector<Token>& toks, size_t b, size_t e) {
    std::string out;
    bool pendingSpace = false;
    for (size_t i = b; i < e; i++) {
        if (toks[i].type == TokenType::Whitespace) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out += ' ';
        pendingSpace = false;
        out += tokenText(toks[i]);
    }
    return out;
}

enum class Tri { False, True, Unknown };

Tri triNot(Tri t) {
    if (t == Tri::Unknown) return t;
    return t == Tri::True ? Tri::False : Tri::True;
}

bool isDelim(const Token& t, char c) {
    return t.type == TokenType::Delim && t.value.size() == 1 && t.value[0] == c;
}

} // namespace

std::string normalizeStyleValue(const std::string& value) {
    auto toks = tokensOf(value);
    return joinTokens(toks, 0, toks.size());
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

class ContainerConditionParser {
public:
    using Node = ContainerCondition::Node;
    using Kind = ContainerCondition::Kind;
    using Cmp = ContainerCondition::Cmp;
    using Feature = ContainerCondition::Feature;
    using Value = ContainerCondition::Value;

    ContainerConditionParser(ContainerCondition& out, std::vector<Token> toks)
        : c_(out), t_(std::move(toks)) {}

    bool run() {
        c_.root_ = parseCondition(0, t_.size(), /*style=*/false, 0);
        if (c_.root_ < 0) return false;
        markUses(c_.root_);
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;
    ContainerCondition& c_;
    std::vector<Token> t_;

    int add(Node n) {
        c_.nodes_.push_back(std::move(n));
        return static_cast<int>(c_.nodes_.size()) - 1;
    }
    int addKind(Kind k) {
        Node n;
        n.kind = k;
        return add(std::move(n));
    }

    void markUses(int i) {
        const Node& n = c_.nodes_[static_cast<size_t>(i)];
        if (n.kind == Kind::Size) c_.usesSize_ = true;
        if (n.kind == Kind::Style) c_.usesStyle_ = true;
        for (int k : n.kids) markUses(k);
    }

    void skipWs(size_t& p, size_t e) const {
        while (p < e && t_[p].type == TokenType::Whitespace) p++;
    }

    // Index of the ')' closing the '(' or function token at `open`, or npos.
    size_t matchClose(size_t open, size_t e) const {
        int depth = 0;
        for (size_t i = open; i < e; i++) {
            auto ty = t_[i].type;
            if (ty == TokenType::LeftParen || ty == TokenType::Function) depth++;
            else if (ty == TokenType::RightParen && --depth == 0) return i;
        }
        return std::string::npos;
    }

    // A whole <container-query> / <style-query> boolean shape over [b, e);
    // -1 unless it consumes the range exactly.
    int parseCondition(size_t b, size_t e, bool style, int depth) {
        if (depth > kMaxDepth) return -1;
        size_t p = b;
        skipWs(p, e);
        if (p < e && t_[p].type == TokenType::Ident && lower(t_[p].value) == "not") {
            ++p;
            skipWs(p, e);
            int kid = parseInParens(p, e, style, depth);
            if (kid < 0) return -1;
            skipWs(p, e);
            if (p != e) return -1;
            Node n;
            n.kind = Kind::Not;
            n.kids.push_back(kid);
            return add(std::move(n));
        }
        int first = parseInParens(p, e, style, depth);
        if (first < 0) return -1;
        std::vector<int> kids{first};
        std::string op;
        for (;;) {
            skipWs(p, e);
            if (p == e) break;
            if (t_[p].type != TokenType::Ident) return -1;
            std::string word = lower(t_[p].value);
            if (word != "and" && word != "or") return -1;
            // `and` and `or` never mix at one level without parentheses.
            if (!op.empty() && op != word) return -1;
            op = word;
            ++p;
            skipWs(p, e);
            int kid = parseInParens(p, e, style, depth);
            if (kid < 0) return -1;
            kids.push_back(kid);
        }
        if (kids.size() == 1) return first;
        Node n;
        n.kind = op == "and" ? Kind::And : Kind::Or;
        n.kids = std::move(kids);
        return add(std::move(n));
    }

    // <query-in-parens> / <style-in-parens> at p; advances past it.
    int parseInParens(size_t& p, size_t e, bool style, int depth) {
        if (p >= e) return -1;
        const Token& t = t_[p];
        if (t.type == TokenType::LeftParen) {
            size_t close = matchClose(p, e);
            if (close == std::string::npos) return -1;
            size_t b = p + 1;
            p = close + 1;
            int n = parseCondition(b, close, style, depth + 1);
            if (n >= 0) return n;
            n = style ? parseStyleFeature(b, close) : parseSizeFeature(b, close);
            if (n >= 0) return n;
            return addKind(Kind::Unknown);  // <general-enclosed>
        }
        if (t.type == TokenType::Function) {
            size_t close = matchClose(p, e);
            if (close == std::string::npos) return -1;
            size_t b = p + 1;
            p = close + 1;
            if (!style && lower(t.value) == "style") {
                int n = parseCondition(b, close, /*style=*/true, depth + 1);
                if (n >= 0) return n;
                n = parseStyleFeature(b, close);
                if (n >= 0) return n;
            }
            return addKind(Kind::Unknown);  // <general-enclosed>
        }
        return -1;
    }

    std::vector<size_t> significant(size_t b, size_t e) const {
        std::vector<size_t> out;
        for (size_t i = b; i < e; i++)
            if (t_[i].type != TokenType::Whitespace) out.push_back(i);
        return out;
    }

    // `--name` or `--name: value`. A standard property is valid syntax but
    // not something this engine answers (nor do browsers): unknown.
    int parseStyleFeature(size_t b, size_t e) {
        auto L = significant(b, e);
        if (L.empty() || t_[L[0]].type != TokenType::Ident) return -1;
        const std::string& name = t_[L[0]].value;
        const bool custom = name.size() > 2 && name[0] == '-' && name[1] == '-';
        Node n;
        n.kind = Kind::Style;
        n.property = name;
        if (L.size() == 1) {
            if (!custom) return -1;
            return add(std::move(n));
        }
        if (t_[L[1]].type != TokenType::Colon) return -1;
        if (!custom) return addKind(Kind::Unknown);
        n.hasValue = true;
        n.value = joinTokens(t_, L[1] + 1, e);
        return add(std::move(n));
    }

    static bool featureOf(const std::string& name, Feature& f) {
        if (name == "width") f = Feature::Width;
        else if (name == "height") f = Feature::Height;
        else if (name == "inline-size") f = Feature::InlineSize;
        else if (name == "block-size") f = Feature::BlockSize;
        else if (name == "aspect-ratio") f = Feature::AspectRatio;
        else if (name == "orientation") f = Feature::Orientation;
        else return false;
        return true;
    }

    // A value for `f` from the significant tokens L[i, j).
    bool parseValue(const std::vector<size_t>& L, size_t i, size_t j, Feature f,
                    Value& v) const {
        if (i >= j) return false;
        const Token& a = t_[L[i]];
        if (f == Feature::Orientation) {
            if (j - i != 1 || a.type != TokenType::Ident) return false;
            v.keyword = lower(a.value);
            return v.keyword == "portrait" || v.keyword == "landscape";
        }
        if (f == Feature::AspectRatio) {
            if (a.type != TokenType::Number || a.numeric < 0) return false;
            v.isRatio = true;
            v.num = a.numeric;
            v.den = 1;
            if (j - i == 1) return true;
            if (j - i != 3 || !isDelim(t_[L[i + 1]], '/')) return false;
            const Token& d = t_[L[i + 2]];
            if (d.type != TokenType::Number || d.numeric < 0) return false;
            v.den = d.numeric;
            return true;
        }
        if (j - i != 1) return false;
        if (a.type == TokenType::Dimension) {
            v.num = a.numeric;
            v.unit = lower(a.unit);
            return true;
        }
        if (a.type == TokenType::Number && a.numeric == 0) {
            v.num = 0;
            return true;
        }
        return false;
    }

    int parseSizeFeature(size_t b, size_t e) {
        auto L = significant(b, e);
        if (L.empty()) return -1;
        Node n;
        n.kind = Kind::Size;

        // `name: value` / `min-name: value` / `max-name: value`.
        if (L.size() >= 2 && t_[L[0]].type == TokenType::Ident &&
            t_[L[1]].type == TokenType::Colon) {
            std::string name = lower(t_[L[0]].value);
            Cmp cmp = Cmp::Eq;
            if (name.rfind("min-", 0) == 0) { cmp = Cmp::Ge; name = name.substr(4); }
            else if (name.rfind("max-", 0) == 0) { cmp = Cmp::Le; name = name.substr(4); }
            if (!featureOf(name, n.feature)) return -1;
            Value v;
            if (!parseValue(L, 2, L.size(), n.feature, v)) return -1;
            if (n.feature == Feature::Orientation && cmp != Cmp::Eq) n.featureValid = false;
            n.tests.push_back({cmp, v, /*valueFirst=*/false});
            return add(std::move(n));
        }

        // Boolean context: `(width)`.
        if (L.size() == 1 && t_[L[0]].type == TokenType::Ident) {
            if (!featureOf(lower(t_[L[0]].value), n.feature)) return -1;
            return add(std::move(n));
        }

        // Range forms: split at the comparison operators.
        struct Op { Cmp cmp; size_t at; size_t len; };
        std::vector<Op> ops;
        for (size_t k = 0; k < L.size(); k++) {
            const Token& t = t_[L[k]];
            if (isDelim(t, '<') || isDelim(t, '>')) {
                const bool lt = t.value[0] == '<';
                // `<=` / `>=`: the '=' must follow with no space between.
                const bool eq = k + 1 < L.size() && L[k + 1] == L[k] + 1 &&
                                isDelim(t_[L[k + 1]], '=');
                ops.push_back({lt ? (eq ? Cmp::Le : Cmp::Lt) : (eq ? Cmp::Ge : Cmp::Gt), k,
                               eq ? 2u : 1u});
                if (eq) k++;
            } else if (isDelim(t, '=')) {
                ops.push_back({Cmp::Eq, k, 1});
            }
        }
        if (ops.empty() || ops.size() > 2) return -1;
        auto isName = [&](size_t i, size_t j, Feature& f) {
            return j - i == 1 && t_[L[i]].type == TokenType::Ident &&
                   featureOf(lower(t_[L[i]].value), f);
        };
        if (ops.size() == 1) {
            const Op& o = ops[0];
            Value v;
            if (isName(0, o.at, n.feature)) {
                if (!parseValue(L, o.at + o.len, L.size(), n.feature, v)) return -1;
                n.tests.push_back({o.cmp, v, false});
            } else if (isName(o.at + o.len, L.size(), n.feature)) {
                if (!parseValue(L, 0, o.at, n.feature, v)) return -1;
                n.tests.push_back({o.cmp, v, true});
            } else {
                return -1;
            }
        } else {
            // `v1 < name < v2` or `v1 > name > v2`, never mixed, never `=`.
            const Op& a = ops[0];
            const Op& c = ops[1];
            auto lessish = [](Cmp x) { return x == Cmp::Lt || x == Cmp::Le; };
            auto greaterish = [](Cmp x) { return x == Cmp::Gt || x == Cmp::Ge; };
            if (!((lessish(a.cmp) && lessish(c.cmp)) || (greaterish(a.cmp) && greaterish(c.cmp))))
                return -1;
            if (!isName(a.at + a.len, c.at, n.feature)) return -1;
            Value v1, v2;
            if (!parseValue(L, 0, a.at, n.feature, v1)) return -1;
            if (!parseValue(L, c.at + c.len, L.size(), n.feature, v2)) return -1;
            n.tests.push_back({a.cmp, v1, true});
            n.tests.push_back({c.cmp, v2, false});
        }
        // Orientation has no range form.
        if (n.feature == Feature::Orientation) return -1;
        return add(std::move(n));
    }
};

std::shared_ptr<const ContainerCondition> ContainerCondition::parse(const std::string& text) {
    auto c = std::make_shared<ContainerCondition>();
    ContainerConditionParser p(*c, tokensOf(text));
    if (!p.run()) return nullptr;
    return c;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

namespace {

bool compare(double lhs, ContainerCondition::Cmp cmp, double rhs) {
    using Cmp = ContainerCondition::Cmp;
    switch (cmp) {
        case Cmp::Lt: return lhs < rhs;
        case Cmp::Le: return lhs <= rhs;
        case Cmp::Gt: return lhs > rhs;
        case Cmp::Ge: return lhs >= rhs;
        case Cmp::Eq: return lhs == rhs;
    }
    return false;
}

// The container's font-size in px, for em-based lengths; 16 when unknown.
double containerFontSize(const ContainerQueryEnv& env) {
    std::string fs;
    if (!env.styleValue || !env.styleValue("font-size", fs)) return 16.0;
    double v = 0;
    const char* b = fs.data();
    const char* e = b + fs.size();
    while (b < e && std::isspace(static_cast<unsigned char>(*b))) b++;
    auto [ptr, ec] = htmlayout::from_chars_fp(b, e, v);
    if (ec != std::errc() || std::string_view(ptr, static_cast<size_t>(e - ptr)) != "px" ||
        !(v > 0))
        return 16.0;
    return v;
}

// A length in px, or false when its unit has no meaning here (viewport and
// container-relative units, percentages).
bool lengthPx(const ContainerCondition::Value& v, const ContainerQueryEnv& env, double& out) {
    const std::string& u = v.unit;
    double k;
    if (u.empty() || u == "px") k = 1;
    else if (u == "em") k = containerFontSize(env);
    else if (u == "ex" || u == "ch") k = containerFontSize(env) * 0.5;
    else if (u == "rem") k = 16;
    else if (u == "in") k = 96;
    else if (u == "cm") k = 96.0 / 2.54;
    else if (u == "mm") k = 96.0 / 25.4;
    else if (u == "q") k = 96.0 / 101.6;
    else if (u == "pt") k = 96.0 / 72.0;
    else if (u == "pc") k = 16;
    else return false;
    out = v.num * k;
    return true;
}

Tri evalSize(const ContainerCondition::Node& n, const ContainerQueryEnv& env) {
    using Feature = ContainerCondition::Feature;
    if (!n.featureValid) return Tri::Unknown;
    const double w = env.inlineSize, h = env.blockSize;
    double val = 0;
    switch (n.feature) {
        case Feature::Width:
        case Feature::InlineSize:
            if (!env.hasInlineAxis) return Tri::Unknown;
            val = w;
            break;
        case Feature::Height:
        case Feature::BlockSize:
            if (!env.hasBlockAxis) return Tri::Unknown;
            val = h;
            break;
        case Feature::AspectRatio:
        case Feature::Orientation:
            if (!env.hasInlineAxis || !env.hasBlockAxis) return Tri::Unknown;
            break;
    }
    if (n.tests.empty()) {
        // Boolean context: true unless the value is zero.
        if (n.feature == Feature::Orientation) return Tri::True;
        if (n.feature == Feature::AspectRatio) return (w != 0 && h != 0) ? Tri::True : Tri::False;
        return val != 0 ? Tri::True : Tri::False;
    }
    for (const auto& t : n.tests) {
        bool ok;
        if (n.feature == Feature::Orientation) {
            ok = t.value.keyword == "portrait" ? h >= w : w > h;
        } else if (n.feature == Feature::AspectRatio) {
            // w/h against num/den, cross-multiplied so a zero side is fine.
            const double lhs = w * t.value.den, rhs = h * t.value.num;
            ok = t.valueFirst ? compare(rhs, t.cmp, lhs) : compare(lhs, t.cmp, rhs);
        } else {
            double px;
            if (!lengthPx(t.value, env, px)) return Tri::Unknown;
            ok = t.valueFirst ? compare(px, t.cmp, val) : compare(val, t.cmp, px);
        }
        if (!ok) return Tri::False;
    }
    return Tri::True;
}

Tri evalStyle(const ContainerCondition::Node& n, const ContainerQueryEnv& env) {
    std::string cv;
    if (!env.styleValue || !env.styleValue(n.property, cv)) return Tri::Unknown;
    const std::string norm = normalizeStyleValue(cv);
    // An unset custom property holds the guaranteed-invalid value (empty).
    if (!n.hasValue) return norm.empty() ? Tri::False : Tri::True;
    return norm == n.value ? Tri::True : Tri::False;
}

Tri evalNode(const std::vector<ContainerCondition::Node>& nodes, int i,
             const ContainerQueryEnv& env) {
    using Kind = ContainerCondition::Kind;
    const auto& n = nodes[static_cast<size_t>(i)];
    switch (n.kind) {
        case Kind::Not: return triNot(evalNode(nodes, n.kids[0], env));
        case Kind::And: {
            Tri r = Tri::True;
            for (int k : n.kids) {
                Tri v = evalNode(nodes, k, env);
                if (v == Tri::False) return Tri::False;
                if (v == Tri::Unknown) r = Tri::Unknown;
            }
            return r;
        }
        case Kind::Or: {
            Tri r = Tri::False;
            for (int k : n.kids) {
                Tri v = evalNode(nodes, k, env);
                if (v == Tri::True) return Tri::True;
                if (v == Tri::Unknown) r = Tri::Unknown;
            }
            return r;
        }
        case Kind::Size: return evalSize(n, env);
        case Kind::Style: return evalStyle(n, env);
        case Kind::Unknown: return Tri::Unknown;
    }
    return Tri::Unknown;
}

} // namespace

bool ContainerCondition::matches(const ContainerQueryEnv& env) const {
    if (root_ < 0) return false;
    return evalNode(nodes_, root_, env) == Tri::True;
}

} // namespace htmlayout::css
