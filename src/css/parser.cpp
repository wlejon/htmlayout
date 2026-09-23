#include "css/parser.h"
#include "css/properties.h"
#include "css/color.h"
#include "css/nesting.h"
#include "../from_chars_compat.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <unordered_set>

namespace htmlayout::css {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : m_tokens(tokens), m_pos(0) {}

    Stylesheet parseStylesheet() {
        Stylesheet sheet;
        m_sheet = &sheet;
        Scope top{&sheet.rules, &sheet.mediaBlocks, {}};
        skipWhitespace();
        while (!atEnd()) {
            if (peek().type == TokenType::AtKeyword) {
                const std::string& kw = peek().value;
                if (kw == "media" || kw == "supports" || kw == "layer" || kw == "container") {
                    // @media blocks land in sheet.mediaBlocks, @layer in
                    // sheet.layerBlocks, @container in sheet.containerBlocks;
                    // a true @supports contributes its rules straight to
                    // sheet.rules.
                    parseConditionalAtRule(top, nullptr, {});
                } else if (peek().value == "import") {
                    auto importRule = parseImportRule();
                    if (!importRule.url.empty()) {
                        sheet.imports.push_back(std::move(importRule));
                    }
                } else if (peek().value == "keyframes" || peek().value == "-webkit-keyframes") {
                    auto kf = parseKeyframesRule();
                    if (!kf.name.empty()) {
                        sheet.keyframes.push_back(std::move(kf));
                    }
                } else if (peek().value == "font-face") {
                    auto ff = parseFontFaceRule();
                    if (!ff.family.empty() && !ff.src.empty()) {
                        sheet.fontFaces.push_back(std::move(ff));
                    }
                } else {
                    // @charset, etc. — skip gracefully
                    consumeAtRule();
                }
                skipWhitespace();
                continue;
            }
            // Try to parse a qualified rule (selector { declarations })
            parseQualifiedRule(top, nullptr);
            skipWhitespace();
        }
        m_sheet = nullptr;
        return sheet;
    }

    std::vector<Declaration> parseDeclarationList() {
        std::vector<Declaration> decls;
        skipWhitespace();
        while (!atEnd()) {
            skipWhitespace();
            if (atEnd()) break;
            if (peek().type == TokenType::Semicolon) {
                advance();
                continue;
            }
            auto decl = parseDeclaration();
            if (!decl.property.empty()) {
                decls.push_back(std::move(decl));
            }
        }
        return decls;
    }

private:
    const std::vector<Token>& m_tokens;
    size_t m_pos;

    bool atEnd() const {
        return m_pos >= m_tokens.size() || m_tokens[m_pos].type == TokenType::EndOfFile;
    }

    const Token& peek() const {
        static const Token eof{TokenType::EndOfFile, "", 0.0, ""};
        return m_pos < m_tokens.size() ? m_tokens[m_pos] : eof;
    }

    const Token& advance() {
        const Token& tok = peek();
        if (m_pos < m_tokens.size()) m_pos++;
        return tok;
    }

    void skipWhitespace() {
        while (!atEnd() && peek().type == TokenType::Whitespace) advance();
    }

    // ------------------------------------------------------------------
    // Rule lists and style-rule bodies, including css-nesting-1.
    //
    // Nested rules are desugared as they are parsed: every style rule, or
    // run of declarations between nested rules, is emitted as a flat Rule
    // (selector resolved by css/nesting.cpp) into the current Scope, and a
    // nested @media becomes its own MediaBlock carrying the enclosing
    // conditions. Rule::sourcePos records the emission order so the cascade
    // can interleave plain and @media rules correctly.
    //
    // The conditional group rules nest in any order: @media / @supports
    // anywhere; @layer anywhere (a nested layer's name is qualified by its
    // parent's, `a.b`); @container anywhere, carrying the enclosing @media
    // conditions, container queries and layer (ContainerBlock::
    // mediaConditions / enclosing / layer). Inside a container query, an
    // @media, @layer or further @container becomes its own ContainerBlock
    // with the combined conditions.

    struct Scope {
        std::vector<Rule>* rules = nullptr;             // where style rules go
        std::vector<MediaBlock>* mediaOut = nullptr;    // nested @media; null = dropped
        std::vector<std::string> mediaConds;            // enclosing @media conditions
        bool inLayer = false;                           // inside an @layer block
        std::string layer;                              // its full name
        std::vector<ContainerQuery> containers;         // enclosing @container queries
        bool dead = false;                              // inside a false @supports
    };

    // Parse a block body (the '{' consumed) into a new ContainerBlock whose
    // queries are `queries` (the last is the block's own) and whose media
    // conditions and layer are the given ones.
    void parseContainerBody(const Scope& s, std::vector<ContainerQuery> queries,
                            std::vector<std::string> mediaConds, bool inLayer,
                            const std::string& layer,
                            const std::vector<std::string>* parents,
                            const std::string& selText) {
        ContainerBlock cb;
        cb.name = queries.back().name;
        cb.condition = queries.back().condition;
        cb.enclosing.assign(queries.begin(), queries.end() - 1);
        cb.mediaConditions = mediaConds;
        cb.layered = inLayer;
        cb.layer = layer;
        Scope inner{&cb.rules, nullptr, std::move(mediaConds)};
        inner.inLayer = inLayer;
        inner.layer = layer;
        inner.containers = std::move(queries);
        inner.dead = s.dead;
        parseBody(inner, parents, selText);
        if (!cb.rules.empty()) m_sheet->containerBlocks.push_back(std::move(cb));
    }

    Stylesheet* m_sheet = nullptr;
    size_t m_nextSourcePos = 1;
    static constexpr int kMaxBodyDepth = 128;
    int m_bodyDepth = 0;

    void emit(Scope& s, Rule rule) {
        rule.sourcePos = m_nextSourcePos++;
        s.rules->push_back(std::move(rule));
    }

    // Collect an at-rule prelude up to '{' (consumed) — false if the rule
    // ends first (a ';' is consumed too).
    bool collectPrelude(std::string& out) {
        advance(); // @keyword
        skipWhitespace();
        std::string text;
        while (!atEnd() && peek().type != TokenType::LeftBrace &&
               peek().type != TokenType::Semicolon && peek().type != TokenType::RightBrace) {
            text += tokenToString(advance());
        }
        out = trim(text);
        if (!atEnd() && peek().type == TokenType::Semicolon) { advance(); return false; }
        if (atEnd() || peek().type != TokenType::LeftBrace) return false;
        advance(); // '{'
        return true;
    }

    // Skip the rest of a block whose '{' was already consumed.
    void skipBlockBody() {
        int depth = 1;
        while (!atEnd()) {
            auto t = advance().type;
            if (t == TokenType::LeftBrace) depth++;
            else if (t == TokenType::RightBrace && --depth == 0) return;
        }
    }

    // Does the item at the cursor start a nested rule rather than a
    // declaration? A '{' reached before any top-level ';' or '}' means a
    // rule (css-syntax-3 would try the declaration first and reparse; the
    // outcome is the same outside custom properties whose value holds a
    // {} block, which are always taken as declarations here).
    bool nextIsNestedRule() const {
        size_t p = m_pos;
        auto at = [&](size_t i) -> const Token& {
            static const Token eof{TokenType::EndOfFile, "", 0.0, ""};
            return i < m_tokens.size() ? m_tokens[i] : eof;
        };
        if (at(p).type == TokenType::Ident && at(p).value.rfind("--", 0) == 0) {
            size_t q = p + 1;
            while (at(q).type == TokenType::Whitespace) q++;
            if (at(q).type == TokenType::Colon) return false;
        }
        int depth = 0;
        for (; p < m_tokens.size(); p++) {
            auto t = m_tokens[p].type;
            if (t == TokenType::EndOfFile) return false;
            if (t == TokenType::Function || t == TokenType::LeftParen ||
                t == TokenType::LeftBracket) depth++;
            else if (t == TokenType::RightParen || t == TokenType::RightBracket) {
                if (depth > 0) depth--;
            } else if (depth == 0) {
                if (t == TokenType::Semicolon || t == TokenType::RightBrace) return false;
                if (t == TokenType::LeftBrace) return true;
            }
        }
        return false;
    }

    // Parse the contents of a block whose '{' was consumed, up to and
    // including its '}'. `parents` non-null means a style rule's body (or a
    // conditional rule nested in one): declarations are allowed and apply
    // to `selText`. Null means a plain rule list.
    void parseBody(Scope& s, const std::vector<std::string>* parents,
                   const std::string& selText, bool emitEmptySelf = false) {
        // Blocks nest by recursion; past a depth no real sheet reaches, the
        // block is dropped whole rather than running the stack out.
        if (m_bodyDepth >= kMaxBodyDepth) { skipBlockBody(); return; }
        struct DepthGuard {
            int& d;
            explicit DepthGuard(int& x) : d(x) { ++d; }
            ~DepthGuard() { --d; }
        } guard(m_bodyDepth);
        Rule pending;
        pending.selector = selText;
        bool sawNested = false;
        auto flush = [&]() {
            if (pending.declarations.empty()) return;
            Rule r;
            r.selector = selText;
            r.declarations = std::move(pending.declarations);
            pending.declarations.clear();
            emit(s, std::move(r));
        };
        skipWhitespace();
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            if (peek().type == TokenType::Whitespace || peek().type == TokenType::Semicolon) {
                advance();
                continue;
            }
            if (peek().type == TokenType::AtKeyword) {
                sawNested = true;
                flush();
                parseConditionalAtRule(s, parents, selText);
                continue;
            }
            if (!parents || nextIsNestedRule()) {
                sawNested = true;
                flush();
                parseQualifiedRule(s, parents);
                continue;
            }
            auto decl = parseDeclaration();
            if (!decl.property.empty()) pending.declarations.push_back(std::move(decl));
        }
        if (!atEnd() && peek().type == TokenType::RightBrace) advance();
        if (!pending.declarations.empty()) flush();
        else if (emitEmptySelf && !sawNested) emit(s, std::move(pending));
    }

    // @media / @supports / @container (and anything else, skipped) at the
    // cursor, in a rule list or nested in a style rule.
    void parseConditionalAtRule(Scope& s, const std::vector<std::string>* parents,
                                const std::string& selText) {
        const std::string name = peek().value;
        if (name == "media") {
            std::string cond;
            if (!collectPrelude(cond)) return;
            if (!s.containers.empty() && !s.dead) {
                // @media inside @container: the same container queries, with
                // one more media condition.
                std::vector<std::string> conds = s.mediaConds;
                conds.push_back(cond);
                parseContainerBody(s, s.containers, std::move(conds), s.inLayer, s.layer,
                                   parents, selText);
                return;
            }
            if (!s.mediaOut) { skipBlockBody(); return; }
            MediaBlock mb;
            mb.condition = cond;
            mb.andConditions = s.mediaConds;
            Scope inner = s;
            inner.rules = &mb.rules;
            inner.mediaConds.push_back(cond);
            parseBody(inner, parents, selText);
            s.mediaOut->push_back(std::move(mb));
            return;
        }
        if (name == "supports") {
            std::string cond;
            if (!collectPrelude(cond)) return;
            if (evaluateSupportsCondition(cond)) {
                parseBody(s, parents, selText);
            } else {
                std::vector<Rule> dropped;
                std::vector<MediaBlock> droppedMedia;
                Scope dead{&dropped, &droppedMedia, {}};
                dead.dead = true;
                parseBody(dead, parents, selText);
            }
            return;
        }
        if (name == "container" && m_sheet && !s.dead) {
            std::string prelude;
            if (!collectPrelude(prelude)) return;
            ContainerQuery q;
            splitContainerPrelude(prelude, q);
            // A query with no condition matches nothing (css-contain-3
            // requires one); its rules are dropped.
            if (q.condition.empty()) { skipBlockBody(); return; }
            std::vector<ContainerQuery> queries = s.containers;
            queries.push_back(std::move(q));
            parseContainerBody(s, std::move(queries), s.mediaConds, s.inLayer, s.layer,
                               parents, selText);
            return;
        }
        if (name == "layer" && m_sheet && !s.dead) {
            parseLayerAtRule(s, parents, selText);
            return;
        }
        // @layer / @container inside a false @supports, @charset, unknown
        // at-rules: skipped.
        consumeAtRule();
    }

    // `@layer a, b;` (order declaration) or `@layer [name] { ... }` at the
    // cursor. Nested layers qualify their names by the parent's (`a.b`);
    // anonymous layers share the name "" as they always have here.
    void parseLayerAtRule(Scope& s, const std::vector<std::string>* parents,
                          const std::string& selText) {
        auto qualify = [&](const std::string& n) {
            if (!s.inLayer || s.layer.empty()) return n;
            return n.empty() ? s.layer : s.layer + "." + n;
        };
        std::string prelude;
        if (!collectPrelude(prelude)) {
            // Statement form: record the declared order.
            for (auto& n : nesting::splitSelectorList(prelude))
                m_sheet->layerOrder.push_back(qualify(n));
            return;
        }
        if (prelude.find(',') != std::string::npos) { skipBlockBody(); return; }
        if (!s.containers.empty()) {
            // @layer inside @container: the container block carries the layer.
            parseContainerBody(s, s.containers, s.mediaConds, /*inLayer=*/true,
                               qualify(prelude), parents, selText);
            return;
        }
        LayerBlock lb;
        lb.name = qualify(prelude);
        Scope inner{&lb.rules, &lb.mediaBlocks, s.mediaConds};
        inner.inLayer = true;
        inner.layer = lb.name;
        if (s.mediaConds.empty()) {
            parseBody(inner, parents, selText);
        } else {
            // Inside @media: the layer's rules keep the conditions.
            MediaBlock mb;
            mb.condition = s.mediaConds.back();
            mb.andConditions.assign(s.mediaConds.begin(), s.mediaConds.end() - 1);
            inner.rules = &mb.rules;
            parseBody(inner, parents, selText);
            lb.mediaBlocks.push_back(std::move(mb));
        }
        m_sheet->layerBlocks.push_back(std::move(lb));
    }

    // A qualified rule at the cursor. Top-level (`parents` null) keeps the
    // prelude text as the selector; nested ones resolve `&` against parents.
    void parseQualifiedRule(Scope& s, const std::vector<std::string>* parents) {
        std::string prelude;
        while (!atEnd() && peek().type != TokenType::LeftBrace) {
            prelude += tokenToString(advance());
        }
        prelude = trim(prelude);
        if (atEnd() || peek().type != TokenType::LeftBrace) {
            // Malformed trailing rule: kept, without declarations, as before.
            if (!prelude.empty() && !parents) {
                Rule r;
                r.selector = prelude;
                emit(s, std::move(r));
            }
            return;
        }
        advance(); // '{'
        std::vector<std::string> selectors;
        std::string selText;
        if (parents) {
            selectors = nesting::resolve(prelude, *parents);
            for (size_t i = 0; i < selectors.size(); i++) {
                if (i) selText += ", ";
                selText += selectors[i];
            }
        } else if (prelude.find('&') != std::string::npos) {
            // A top-level `&` is the scoping root (css-nesting-1 §2).
            selectors = nesting::resolveTopLevel(prelude);
            for (size_t i = 0; i < selectors.size(); i++) {
                if (i) selText += ", ";
                selText += selectors[i];
            }
        } else {
            selectors = nesting::splitSelectorList(prelude);
            selText = prelude;
        }
        if (selectors.empty()) { skipBlockBody(); return; }
        parseBody(s, &selectors, selText, /*emitEmptySelf=*/true);
    }

    static void splitContainerPrelude(const std::string& prelude, ContainerQuery& block) {
        // "sidebar (min-width: 400px)" or "(min-width: 400px)". The name is a
        // leading ident, so neither a function (`style(...)`) nor the `not`
        // keyword opening the condition counts as one.
        std::string p = trim(prelude);
        size_t end = 0;
        while (end < p.size() && p[end] != '(' &&
               !std::isspace(static_cast<unsigned char>(p[end])))
            ++end;
        std::string word = p.substr(0, end);
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        const bool isName = !word.empty() && end < p.size() && p[end] != '(' &&
                            lower != "not" && lower != "and" && lower != "or";
        if (isName) {
            block.name = word;
            block.condition = trim(p.substr(end));
        } else {
            block.condition = p;
        }
    }

    // @supports declaration probe: does this engine support `property: value`?
    // Mirrors browser behavior of "does the declaration parse" as closely as
    // the engine's string-valued style store allows: the property must be
    // known, and for the commonly probed enumerated/color properties the
    // value must be valid. Everything else is permissive.
    static bool isDeclarationSupported(std::string prop, std::string value) {
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

    static bool evaluateSupportsCondition(const std::string& condition) {
        if (trim(condition).empty()) return false;
        SupportsEvaluator ev(condition);
        bool ok = false;
        if (!ev.parseCondition(ok)) return false;
        return ok;
    }

    ImportRule parseImportRule() {
        ImportRule rule;
        advance(); // skip @import
        skipWhitespace();

        // Extract URL: either a bare string token or url("...")
        if (peek().type == TokenType::String) {
            rule.url = peek().value;
            advance();
        } else if (peek().type == TokenType::Function && peek().value == "url") {
            advance(); // skip url(
            skipWhitespace();
            if (peek().type == TokenType::String) {
                rule.url = peek().value;
                advance();
            } else {
                // Unquoted URL: collect tokens until ')'
                std::string url;
                while (!atEnd() && peek().type != TokenType::RightParen) {
                    url += peek().value;
                    advance();
                }
                rule.url = trim(url);
            }
            skipWhitespace();
            if (!atEnd() && peek().type == TokenType::RightParen) advance();
        } else {
            // Malformed @import — skip to semicolon
            while (!atEnd() && peek().type != TokenType::Semicolon) advance();
            if (!atEnd()) advance();
            return rule;
        }

        skipWhitespace();

        // Parse optional layer and/or media qualifiers before ';'
        // Possible forms: layer, layer(name), supports(...), media condition
        if (!atEnd() && peek().type != TokenType::Semicolon) {
            if (peek().type == TokenType::Ident && peek().value == "layer") {
                advance(); // skip "layer"
                rule.layered = true;
                skipWhitespace();
                if (!atEnd() && peek().type == TokenType::Function && peek().value == "layer") {
                    // Shouldn't happen after ident, but handle gracefully
                    advance();
                } else if (!atEnd() && peek().type == TokenType::LeftParen) {
                    // layer(name) — but tokenizer would emit Function token for "layer("
                    // This branch handles if layer and ( are separate tokens
                    advance(); // skip (
                    skipWhitespace();
                    std::string layerName;
                    while (!atEnd() && peek().type != TokenType::RightParen) {
                        layerName += peek().value;
                        advance();
                    }
                    if (!atEnd()) advance(); // skip )
                    rule.layer = trim(layerName);
                } else {
                    rule.layer = "";  // anonymous layer import
                }
                skipWhitespace();
            } else if (peek().type == TokenType::Function && peek().value == "layer") {
                advance(); // skip layer(
                rule.layered = true;
                skipWhitespace();
                std::string layerName;
                while (!atEnd() && peek().type != TokenType::RightParen) {
                    layerName += peek().value;
                    advance();
                }
                if (!atEnd()) advance(); // skip )
                rule.layer = trim(layerName);
                skipWhitespace();
            }
        }

        // Remaining tokens before ';' are the media condition
        if (!atEnd() && peek().type != TokenType::Semicolon) {
            std::string media;
            while (!atEnd() && peek().type != TokenType::Semicolon) {
                media += tokenToString(advance());
            }
            rule.mediaCondition = trim(media);
        }

        // Consume the semicolon
        if (!atEnd() && peek().type == TokenType::Semicolon) advance();

        return rule;
    }

    // Parse @font-face { font-family: ...; src: url(...); ... }
    FontFaceRule parseFontFaceRule() {
        FontFaceRule rule;
        advance(); // skip @font-face
        skipWhitespace();

        if (atEnd() || peek().type != TokenType::LeftBrace) {
            consumeAtRule();
            return rule;
        }
        advance(); // skip '{'

        // Collect tokens until matching '}'
        std::vector<Token> declTokens;
        int depth = 1;
        while (!atEnd() && depth > 0) {
            if (peek().type == TokenType::LeftBrace) ++depth;
            else if (peek().type == TokenType::RightBrace) {
                --depth;
                if (depth <= 0) { advance(); break; }
            }
            declTokens.push_back(peek());
            advance();
        }
        declTokens.push_back(Token{TokenType::EndOfFile, "", 0.0, ""});
        Parser declParser(declTokens);
        auto decls = declParser.parseDeclarationList();

        for (auto& d : decls) {
            if (d.property == "font-family") {
                // Strip quotes
                rule.family = d.value;
                if (!rule.family.empty() &&
                    (rule.family.front() == '"' || rule.family.front() == '\''))
                    rule.family = rule.family.substr(1, rule.family.size() - 2);
            } else if (d.property == "src") {
                // Extract url(...) from src value
                auto pos = d.value.find("url(");
                if (pos != std::string::npos) {
                    auto start = pos + 4;
                    auto end = d.value.find(')', start);
                    if (end != std::string::npos) {
                        rule.src = d.value.substr(start, end - start);
                        // Strip quotes
                        if (!rule.src.empty() &&
                            (rule.src.front() == '"' || rule.src.front() == '\''))
                            rule.src = rule.src.substr(1, rule.src.size() - 2);
                    }
                }
            } else if (d.property == "font-weight") {
                if (d.value == "bold") rule.weight = 700;
                else if (d.value == "normal") rule.weight = 400;
                else {
                    char* end = nullptr;
                    int v = static_cast<int>(std::strtof(d.value.c_str(), &end));
                    if (end != d.value.c_str() && v > 0) rule.weight = v;
                }
            } else if (d.property == "font-style") {
                rule.italic = (d.value == "italic" || d.value == "oblique");
            }
        }
        return rule;
    }

    // Parse @keyframes name { from/to/% { declarations } ... }
    KeyframeBlock parseKeyframesRule() {
        KeyframeBlock block;
        advance(); // skip @keyframes / @-webkit-keyframes
        skipWhitespace();

        // Parse animation name (ident or string)
        if (!atEnd()) {
            if (peek().type == TokenType::String) {
                block.name = peek().value;
                advance();
            } else if (peek().type == TokenType::Ident) {
                block.name = peek().value;
                advance();
            }
        }
        skipWhitespace();

        // Expect opening brace
        if (atEnd() || peek().type != TokenType::LeftBrace) {
            consumeAtRule();
            return block;
        }
        advance(); // skip '{'
        skipWhitespace();

        // Parse keyframe stops
        while (!atEnd() && peek().type != TokenType::RightBrace) {
            skipWhitespace();
            if (atEnd() || peek().type == TokenType::RightBrace) break;

            // Parse offset(s): "from", "to", or "50%", or comma-separated list
            std::vector<float> offsets;
            while (!atEnd() && peek().type != TokenType::LeftBrace) {
                skipWhitespace();
                if (peek().type == TokenType::Ident) {
                    if (peek().value == "from") offsets.push_back(0.0f);
                    else if (peek().value == "to") offsets.push_back(1.0f);
                    advance();
                } else if (peek().type == TokenType::Percentage) {
                    offsets.push_back(static_cast<float>(peek().numeric / 100.0));
                    advance();
                } else if (peek().type == TokenType::Number) {
                    // Handle "0" without %
                    offsets.push_back(static_cast<float>(peek().numeric / 100.0));
                    advance();
                } else if (peek().type == TokenType::Comma) {
                    advance();
                } else {
                    advance(); // skip unexpected
                }
                skipWhitespace();
            }

            // Parse declarations block
            if (!atEnd() && peek().type == TokenType::LeftBrace) {
                advance(); // skip '{'
                // Collect tokens until matching '}'
                std::vector<Token> declTokens;
                int depth = 1;
                while (!atEnd() && depth > 0) {
                    if (peek().type == TokenType::LeftBrace) ++depth;
                    else if (peek().type == TokenType::RightBrace) {
                        --depth;
                        if (depth <= 0) { advance(); break; }
                    }
                    declTokens.push_back(peek());
                    advance();
                }
                // Parse declarations from collected tokens
                declTokens.push_back(Token{TokenType::EndOfFile, "", 0.0, ""});
                Parser declParser(declTokens);
                auto decls = declParser.parseDeclarationList();

                // Create a stop for each offset
                for (float offset : offsets) {
                    block.stops.push_back({offset, decls});
                }
            }
            skipWhitespace();
        }

        if (!atEnd() && peek().type == TokenType::RightBrace) advance();

        // Sort stops by offset
        std::sort(block.stops.begin(), block.stops.end(),
            [](const KeyframeStop& a, const KeyframeStop& b) {
                return a.offset < b.offset;
            });

        return block;
    }

    void consumeAtRule() {
        advance(); // skip @keyword
        int braceDepth = 0;
        while (!atEnd()) {
            auto type = peek().type;
            if (type == TokenType::Semicolon && braceDepth == 0) {
                advance();
                return;
            }
            if (type == TokenType::LeftBrace) {
                braceDepth++;
            } else if (type == TokenType::RightBrace) {
                braceDepth--;
                if (braceDepth <= 0) {
                    advance();
                    return;
                }
            }
            advance();
        }
    }

    Declaration parseDeclaration() {
        Declaration decl;
        skipWhitespace();

        // Property name (must be an ident)
        if (peek().type != TokenType::Ident) {
            // Skip to next semicolon or brace
            skipToRecovery();
            return decl;
        }
        decl.property = advance().value;

        skipWhitespace();

        // Expect ':'
        if (peek().type != TokenType::Colon) {
            skipToRecovery();
            return Declaration{};
        }
        advance(); // skip ':'

        skipWhitespace();

        // Collect value tokens until ';' or '}' or EOF
        std::string value;
        while (!atEnd() && peek().type != TokenType::Semicolon && peek().type != TokenType::RightBrace) {
            value += tokenToString(advance());
        }

        // Consume the semicolon if present
        if (!atEnd() && peek().type == TokenType::Semicolon) {
            advance();
        }

        value = trim(value);

        // Check for !important
        decl.important = checkAndStripImportant(value);
        decl.value = value;

        return decl;
    }

    void skipToRecovery() {
        // Enhanced error recovery: skip to next semicolon or closing brace,
        // respecting nested blocks so we don't consume too much.
        int braceDepth = 0;
        while (!atEnd()) {
            auto type = peek().type;
            if (type == TokenType::LeftBrace) {
                braceDepth++;
                advance();
                continue;
            }
            if (type == TokenType::RightBrace) {
                if (braceDepth > 0) {
                    braceDepth--;
                    advance();
                    continue;
                }
                // Don't consume the closing brace of our containing block
                return;
            }
            if (type == TokenType::Semicolon && braceDepth == 0) {
                advance(); // consume the semicolon
                return;
            }
            advance();
        }
    }

    static bool checkAndStripImportant(std::string& value) {
        // Look for "!important" at the end
        const std::string marker = "!important";
        // Find last non-whitespace
        size_t end = value.find_last_not_of(" \t\n\r\f");
        if (end == std::string::npos) return false;

        size_t len = end + 1;
        if (len >= marker.size()) {
            std::string tail = value.substr(len - marker.size(), marker.size());
            // Case-insensitive compare
            for (auto& ch : tail) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (tail == marker) {
                value = trim(value.substr(0, len - marker.size()));
                return true;
            }
        }

        // Also handle "! important" (with space after !)
        // Find last occurrence of '!'
        auto excl = value.rfind('!');
        if (excl != std::string::npos) {
            std::string after = value.substr(excl + 1);
            // trim and lowercase
            after = trim(after);
            for (auto& ch : after) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (after == "important") {
                value = trim(value.substr(0, excl));
                return true;
            }
        }
        return false;
    }

    static std::string tokenToString(const Token& tok) {
        switch (tok.type) {
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
            case TokenType::Hash: return "#" + tok.value;
            case TokenType::AtKeyword: return "@" + tok.value;
            case TokenType::Delim: return tok.value;
            case TokenType::String: return "\"" + tok.value + "\"";
            case TokenType::Function: return tok.value + "(";
            case TokenType::Dimension: return tok.value + tok.unit;
            case TokenType::Percentage: return tok.value + "%";
            case TokenType::Number: return tok.value;
            case TokenType::CDO: return "<!--";
            case TokenType::CDC: return "-->";
            default: return tok.value;
        }
    }

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r\f");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r\f");
        return s.substr(start, end - start + 1);
    }
};

} // anonymous namespace

Stylesheet parse(const std::string& css) {
    auto tokens = tokenize(css);
    Parser parser(tokens);
    return parser.parseStylesheet();
}

std::vector<Declaration> parseInlineStyle(const std::string& style) {
    auto tokens = tokenize(style);
    Parser parser(tokens);
    return parser.parseDeclarationList();
}

namespace {

// Parse a length value like "768px" to pixels
float parseMediaLength(const std::string& s) {
    float num = 0;
    const char* begin = s.data();
    const char* end = begin + s.size();
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
    if (ec != std::errc()) return 0;
    // unit is ignored for now (assume px)
    return num;
}

// Trim whitespace helper
void trimInPlace(std::string& str) {
    size_t a = str.find_first_not_of(" \t\n\r\f");
    size_t b = str.find_last_not_of(" \t\n\r\f");
    str = (a == std::string::npos) ? "" : str.substr(a, b - a + 1);
}

// Compare helper: apply a comparison operator
bool applyComparison(float lhs, const std::string& op, float rhs) {
    if (op == ">") return lhs > rhs;
    if (op == ">=") return lhs >= rhs;
    if (op == "<") return lhs < rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == "=") return lhs == rhs;
    return false;
}

// Resolve a media feature name to its value from the context
float resolveMediaFeatureValue(const std::string& name, const MediaContext& ctx) {
    if (name == "width") return ctx.viewportWidth;
    if (name == "height") return ctx.viewportHeight;
    return -1;
}

// Try to parse range syntax: "width > 500px", "width >= 500px",
// "500px < width", "500px <= width <= 800px"
// Returns true if parsed as range, false if not range syntax.
bool tryEvaluateRange(const std::string& f, const MediaContext& ctx, bool& result) {
    // Look for comparison operators: >=, <=, >, <, =
    // Possible forms:
    //   feature > value
    //   feature >= value
    //   value < feature
    //   value <= feature <= value2

    // Find comparison operators
    struct CompPart { std::string token; bool isOp; };
    std::vector<CompPart> parts;
    size_t i = 0;
    std::string current;
    while (i < f.size()) {
        if ((f[i] == '>' || f[i] == '<' || f[i] == '=') && i > 0) {
            if (!current.empty()) {
                std::string t = current; trimInPlace(t);
                if (!t.empty()) parts.push_back({t, false});
                current.clear();
            }
            std::string op(1, f[i]);
            if (i + 1 < f.size() && f[i + 1] == '=') { op += '='; i++; }
            parts.push_back({op, true});
        } else {
            current += f[i];
        }
        i++;
    }
    if (!current.empty()) {
        std::string t = current; trimInPlace(t);
        if (!t.empty()) parts.push_back({t, false});
    }

    // Need at least: value op value (3 parts) with at least one operator
    if (parts.size() < 3) return false;

    // Check if this looks like range syntax (has comparison operators)
    bool hasOp = false;
    for (auto& p : parts) if (p.isOp) { hasOp = true; break; }
    if (!hasOp) return false;

    // Simple range: feature op value  or  value op feature
    if (parts.size() == 3 && parts[1].isOp) {
        float featureVal = resolveMediaFeatureValue(parts[0].token, ctx);
        if (featureVal >= 0) {
            // feature op value
            float rhs = parseMediaLength(parts[2].token);
            result = applyComparison(featureVal, parts[1].token, rhs);
            return true;
        }
        featureVal = resolveMediaFeatureValue(parts[2].token, ctx);
        if (featureVal >= 0) {
            // value op feature
            float lhs = parseMediaLength(parts[0].token);
            result = applyComparison(lhs, parts[1].token, featureVal);
            return true;
        }
    }

    // Chained range: value op feature op value2 (5 parts)
    if (parts.size() == 5 && parts[1].isOp && parts[3].isOp) {
        float featureVal = resolveMediaFeatureValue(parts[2].token, ctx);
        if (featureVal >= 0) {
            float lhs = parseMediaLength(parts[0].token);
            float rhs = parseMediaLength(parts[4].token);
            result = applyComparison(lhs, parts[1].token, featureVal) &&
                     applyComparison(featureVal, parts[3].token, rhs);
            return true;
        }
    }

    return false;
}

// Evaluate a single media feature like "(min-width: 768px)" or "(width > 500px)"
bool evaluateMediaFeature(const std::string& feature, const MediaContext& ctx) {
    // Strip parens and trim
    std::string f = feature;
    if (!f.empty() && f.front() == '(') f.erase(0, 1);
    if (!f.empty() && f.back() == ')') f.pop_back();
    trimInPlace(f);

    if (f.empty()) return false;

    // Try range syntax first
    bool rangeResult = false;
    if (tryEvaluateRange(f, ctx, rangeResult)) {
        return rangeResult;
    }

    auto colonPos = f.find(':');
    if (colonPos == std::string::npos) {
        // Boolean feature, e.g. (color)
        return true;
    }

    std::string name = f.substr(0, colonPos);
    std::string value = f.substr(colonPos + 1);
    trimInPlace(name); trimInPlace(value);

    // Discrete (non-length) features first.
    if (name == "prefers-color-scheme") {
        for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value == ctx.colorScheme;
    }

    float val = parseMediaLength(value);

    if (name == "min-width") return ctx.viewportWidth >= val;
    if (name == "max-width") return ctx.viewportWidth <= val;
    if (name == "min-height") return ctx.viewportHeight >= val;
    if (name == "max-height") return ctx.viewportHeight <= val;
    if (name == "width") return ctx.viewportWidth == val;
    if (name == "height") return ctx.viewportHeight == val;
    if (name == "orientation") {
        if (value == "portrait") return ctx.viewportHeight >= ctx.viewportWidth;
        if (value == "landscape") return ctx.viewportWidth > ctx.viewportHeight;
    }

    return false;
}

} // anonymous namespace

bool evaluateMediaQuery(const std::string& condition, const MediaContext& ctx) {
    if (condition.empty() || condition == "all") return true;

    // Media query LIST: top-level commas separate independent queries;
    // the list matches when ANY query matches (Media Queries §2.1).
    {
        int depth = 0;
        size_t start = 0;
        std::vector<std::string> parts;
        for (size_t i = 0; i < condition.size(); i++) {
            char c = condition[i];
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (c == ',' && depth == 0) {
                parts.push_back(condition.substr(start, i - start));
                start = i + 1;
            }
        }
        if (!parts.empty()) {
            parts.push_back(condition.substr(start));
            for (auto& p : parts) {
                std::string q = p;
                trimInPlace(q);
                if (!q.empty() && evaluateMediaQuery(q, ctx)) return true;
            }
            return false;
        }
    }

    // Handle media type prefixes: "screen and (...)", "print", etc.
    std::string cond = condition;

    // Simple "not" prefix
    bool negate = false;
    if (cond.size() > 4 && cond.substr(0, 4) == "not ") {
        negate = true;
        cond = cond.substr(4);
    }

    // Check for media type
    std::string mediaType;
    auto andPos = cond.find(" and ");
    if (andPos != std::string::npos && cond[0] != '(') {
        mediaType = cond.substr(0, andPos);
        cond = cond.substr(andPos + 5);
    } else if (cond[0] != '(') {
        mediaType = cond;
        cond.clear();
    }

    // Trim media type
    if (!mediaType.empty()) {
        for (auto& c : mediaType) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t s = mediaType.find_first_not_of(" \t");
        size_t e = mediaType.find_last_not_of(" \t");
        if (s != std::string::npos) mediaType = mediaType.substr(s, e - s + 1);

        if (mediaType != "all" && mediaType != ctx.mediaType) {
            return negate;
        }
    }

    if (cond.empty()) return !negate;

    // Extract parenthesized features and connecting keywords (and/or)
    std::vector<std::string> features;
    std::vector<std::string> connectors; // "and" or "or" between features

    size_t i = 0;
    while (i < cond.size()) {
        auto paren = cond.find('(', i);
        if (paren == std::string::npos) break;

        // Check for connector keyword between previous feature and this one
        if (!features.empty()) {
            std::string between = cond.substr(i, paren - i);
            trimInPlace(between);
            if (between == "or") connectors.push_back("or");
            else connectors.push_back("and"); // default is "and"
        }

        int depth = 0;
        size_t j = paren;
        while (j < cond.size()) {
            if (cond[j] == '(') depth++;
            else if (cond[j] == ')') { depth--; if (depth == 0) break; }
            j++;
        }
        features.push_back(cond.substr(paren, j - paren + 1));
        i = j + 1;
    }

    // Evaluate with and/or logic
    bool result = true;
    if (!features.empty()) {
        result = evaluateMediaFeature(features[0], ctx);
        for (size_t fi = 1; fi < features.size(); fi++) {
            bool val = evaluateMediaFeature(features[fi], ctx);
            if (fi - 1 < connectors.size() && connectors[fi - 1] == "or") {
                result = result || val;
            } else {
                result = result && val;
            }
        }
    }

    return negate ? !result : result;
}

} // namespace htmlayout::css
