#include "css/cascade.h"
#include "css/properties.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

namespace htmlayout::css {

void collectPresentationAttrs(const ElementRef& elem,
                              std::vector<std::pair<std::string_view, std::string_view>>& out);

namespace {

// Check if a property name is a CSS custom property (starts with --)
bool isCustomProperty(const std::string& name) {
    return name.size() >= 2 && name[0] == '-' && name[1] == '-';
}

// Resolve var() references in a value string, using the current style and parent.
// Supports var(--name) and var(--name, fallback).
// Resolves nested var() in fallbacks.
// Uses a visited set to detect cycles per CSS Variables L1 spec —
// a cyclic reference produces the guaranteed-invalid value (empty string).
std::string resolveVarReferences(const std::string& value,
                                  const ComputedStyle& style,
                                  const ComputedStyle* parentStyle,
                                  std::unordered_set<std::string>& visiting) {
    std::string result;
    size_t i = 0;
    while (i < value.size()) {
        // Look for "var("
        if (i + 3 < value.size() && value.compare(i, 4, "var(") == 0) {
            i += 4; // skip "var("
            // Find the matching closing paren, respecting nesting
            int parenDepth = 1;
            size_t argStart = i;
            while (i < value.size() && parenDepth > 0) {
                if (value[i] == '(') parenDepth++;
                else if (value[i] == ')') parenDepth--;
                if (parenDepth > 0) i++;
            }
            std::string args = value.substr(argStart, i - argStart);
            if (i < value.size()) i++; // skip ')'

            // Split args into variable name and optional fallback
            // Find first comma not inside parens
            std::string varName, fallback;
            int pd = 0;
            size_t commaPos = std::string::npos;
            for (size_t j = 0; j < args.size(); j++) {
                if (args[j] == '(') pd++;
                else if (args[j] == ')') pd--;
                else if (args[j] == ',' && pd == 0) {
                    commaPos = j;
                    break;
                }
            }
            if (commaPos != std::string::npos) {
                varName = args.substr(0, commaPos);
                fallback = args.substr(commaPos + 1);
                // Trim whitespace
                while (!varName.empty() && varName.back() == ' ') varName.pop_back();
                while (!fallback.empty() && fallback.front() == ' ') fallback.erase(fallback.begin());
            } else {
                varName = args;
                while (!varName.empty() && varName.back() == ' ') varName.pop_back();
            }
            while (!varName.empty() && varName.front() == ' ') varName.erase(varName.begin());

            // Cycle detection: if we're already resolving this variable, it's a cycle
            if (visiting.count(varName)) {
                // Guaranteed-invalid value per CSS Variables L1 spec
                // Try fallback if available, otherwise empty
                if (!fallback.empty()) {
                    result += resolveVarReferences(fallback, style, parentStyle, visiting);
                }
                // else: empty string (guaranteed-invalid)
                continue;
            }

            // Look up the variable: the element's own declaration, then the
            // inherited set it was handed (see ComputedStyle::customProperty),
            // then — for a caller that supplied a parent style without wiring
            // the inherited set — the parent's view of it.
            std::string resolved;
            if (const std::string* own = style.customProperty(varName); own && !own->empty()) {
                resolved = *own;
            } else if (parentStyle) {
                if (const std::string* pv = parentStyle->customProperty(varName); pv && !pv->empty())
                    resolved = *pv;
            }

            if (resolved.empty() && !fallback.empty()) {
                resolved = fallback;
            }

            // Recursively resolve any var() in the resolved value, tracking this variable
            visiting.insert(varName);
            result += resolveVarReferences(resolved, style, parentStyle, visiting);
            visiting.erase(varName);
        } else {
            result += value[i++];
        }
    }
    return result;
}

// Convenience overload that creates a fresh visited set
std::string resolveVarReferences(const std::string& value,
                                  const ComputedStyle& style,
                                  const ComputedStyle* parentStyle) {
    std::unordered_set<std::string> visiting;
    return resolveVarReferences(value, style, parentStyle, visiting);
}

// Blockification (CSS Display §2.7): an inline-level display computes to its
// block-level equivalent when the box must be block-level: a flex or grid
// item (parentStyle displays flex/grid), an absolutely positioned box, or a
// float. Shared by elements and ::before/::after, whose parent is their
// originating element. Absent keys are initial values.
void blockify(ComputedStyle& style, const ComputedStyle* parentStyle) {
    auto own = [&](const char* k, const char* dflt) -> std::string {
        auto it = style.find(k);
        return (it != style.end()) ? it->second : std::string(dflt);
    };

    bool must = false;

    if (parentStyle) {
        auto pd = parentStyle->find("display");
        if (pd != parentStyle->end() &&
            (pd->second == "flex" || pd->second == "inline-flex" ||
             pd->second == "grid" || pd->second == "inline-grid"))
            must = true;  // (a)
    }
    if (!must) {
        const std::string pos = own("position", "static");
        if (pos == "absolute" || pos == "fixed") must = true;  // (b)
    }
    if (!must) {
        const std::string fl = own("float", "none");
        if (fl == "left" || fl == "right" || fl == "inline-start" ||
            fl == "inline-end") must = true;  // (c)
    }
    if (must) {
        const std::string d = own("display", "inline");
        if (d == "inline" || d == "inline-block") style["display"] = "block";
        else if (d == "inline-table") style["display"] = "table";
        else if (d == "inline-flex") style["display"] = "flex";
        else if (d == "inline-grid") style["display"] = "grid";
        else if (d == "-webkit-inline-box") style["display"] = "-webkit-box";
    }
}

// CSS Logical Properties L1 — resolve inline-axis logical properties (margin/
// padding/border/inset -inline-start/end) to physical sides using the element's
// own computed `direction`. Block-axis logical properties are already physical
// by shorthand-expansion time (writing-mode is always horizontal-tb). Presence
// of a logical key in the map means it was explicitly set (non-inherited
// defaults are never stored), so it wins over the physical initial value.
void resolveInlineLogical(ComputedStyle& style) {
    auto dirIt = style.find("direction");
    const bool rtl = (dirIt != style.end() && dirIt->second == "rtl");
    const char* startSide = rtl ? "right" : "left";
    const char* endSide   = rtl ? "left"  : "right";
    auto move = [&](const char* logical, const char* physicalPrefix,
                    const char* side, const char* physicalSuffix) {
        auto it = style.find(logical);
        if (it == style.end()) return;
        std::string v = std::move(it->second);
        style.erase(it);
        style[std::string(physicalPrefix) + side + physicalSuffix] = std::move(v);
    };
    move("margin-inline-start", "margin-", startSide, "");
    move("margin-inline-end",   "margin-", endSide,   "");
    move("padding-inline-start", "padding-", startSide, "");
    move("padding-inline-end",   "padding-", endSide,   "");
    move("inset-inline-start", "", startSide, "");
    move("inset-inline-end",   "", endSide,   "");
    move("border-inline-start-width", "border-", startSide, "-width");
    move("border-inline-end-width",   "border-", endSide,   "-width");
    move("border-inline-start-style", "border-", startSide, "-style");
    move("border-inline-end-style",   "border-", endSide,   "-style");
    move("border-inline-start-color", "border-", startSide, "-color");
    move("border-inline-end-color",   "border-", endSide,   "-color");
}

// Cascade precedence for a matched declaration (anything with important,
// origin, layerOrder, specificity and order): true when `a` applies before
// `b`, so `b` wins.
template <typename M>
bool cascadesBefore(const M& a, const M& b) {
    // Important declarations come after normal ones (applied last = wins)
    if (a.important != b.important) return !a.important;

    // Origin ordering (CSS Cascade L5):
    //   Normal:    UA < Author  (author wins)
    //   Important: Author !important < UA !important  (UA wins)
    if (a.origin != b.origin) {
        if (a.important) {
            // For !important: UA beats Author — UA comes later (wins)
            return a.origin == Origin::Author;
        } else {
            // For normal: Author beats UA — UA comes earlier (loses)
            return a.origin == Origin::UserAgent;
        }
    }

    // Layer ordering
    if (a.layerOrder != b.layerOrder) {
        if (a.important) {
            // For !important: layered beats unlayered, earlier layers beat later
            if (a.layerOrder == -1) return true;   // unlayered !important loses
            if (b.layerOrder == -1) return false;   // unlayered !important loses
            return a.layerOrder > b.layerOrder;     // earlier layer wins (comes later in sort)
        } else {
            // For normal: unlayered beats layered, later layers beat earlier
            if (a.layerOrder == -1) return false;   // unlayered wins (comes later)
            if (b.layerOrder == -1) return true;    // unlayered wins (comes later)
            return a.layerOrder < b.layerOrder;     // later layer wins (comes later)
        }
    }

    // Higher specificity wins (comes later)
    if (a.specificity != b.specificity) return a.specificity < b.specificity;
    // Later source order wins (comes later)
    return a.order < b.order;
}

} // anonymous namespace

ComputedStyle Cascade::resolveMatched(const ElementRef& elem, const std::vector<uint32_t>& rules,
                                      const std::string& inlineStyle,
                                      const ComputedStyle* parentStyle) const {
    // 1. The matched declarations, by pointer: nothing is copied from the rules.
    struct MatchedDecl {
        const std::string* property;
        const std::string* value;
        bool important;
        uint32_t specificity;
        size_t order;
        bool isInline;  // inline style has highest author specificity
        int layerOrder;  // -1 = unlayered, >=0 = layer index
        Origin origin;
    };
    std::vector<MatchedDecl> matched;
    {
        size_t n = 0;
        for (uint32_t ruleIdx : rules) n += rules_[ruleIdx].declarations.size();
        matched.reserve(n + 8);
    }

    // 0. SVG presentation attributes (fill="red", stroke-width="2", ...) are the
    //    lowest-priority style source: they beat inheritance (they enter the
    //    cascade as a real declaration) but lose to any author rule or inline
    //    style. Seed them at specificity 0 and push them FIRST so stable_sort
    //    keeps them earliest among equal-specificity declarations (a universal
    //    author rule still wins). Attribute names match their CSS property names
    //    1:1. presDecls must outlive the apply pass — MatchedDecl holds pointers.
    std::vector<Declaration> presDecls;
    {
        std::vector<std::pair<std::string_view, std::string_view>> attrs;
        collectPresentationAttrs(elem, attrs);
        presDecls.reserve(attrs.size());
        for (auto& [name, v] : attrs) {
            std::string value(v);
            // SVG lengths are unitless user units (= px). font-size is resolved
            // as a CSS <length>, where a bare number is invalid-at-computed-
            // value-time and would reset to `medium`; append px so e.g.
            // font-size="34" computes to 34px like Chromium.
            if (name == "font-size") {
                const char* b = value.c_str();
                char* e = nullptr;
                std::strtod(b, &e);
                if (e != b && *e == '\0') value += "px";
            }
            presDecls.push_back({std::string(name), std::move(value), false});
        }
        for (auto& decl : presDecls) {
            matched.push_back({
                &decl.property, &decl.value, /*important=*/false,
                /*specificity=*/0, /*order=*/0, /*isInline=*/false,
                /*layerOrder=*/-1, Origin::Author
            });
        }
    }

    // 1a. The matched rules' declarations, in source order.
    for (uint32_t ruleIdx : rules) {
        const auto& rule = rules_[ruleIdx];
        for (auto& decl : rule.declarations) {
            matched.push_back({
                &decl.property, &decl.value, decl.important,
                rule.selector.specificity, rule.order, false,
                rule.layerOrder < 0 ? -1 : layerRanks_[static_cast<size_t>(rule.layerOrder)],
                rule.origin
            });
        }
    }

    // 2. Parse and add inline style declarations (highest author specificity)
    //    Keep inlineDecls alive since MatchedDecl holds pointers into it.
    std::vector<Declaration> inlineDecls;
    if (!inlineStyle.empty()) {
        inlineDecls = parseInlineStyle(inlineStyle);
        for (auto& decl : inlineDecls) {
            matched.push_back({
                &decl.property, &decl.value, decl.important,
                0xFFFFFFFF, // inline style beats all selector specificities
                SIZE_MAX,   // and all source orders
                true,
                -1,         // inline styles are unlayered
                Origin::Author
            });
        }
    }

    // 3. Sort by cascade precedence:
    //    - !important declarations beat normal declarations
    //    - Layer ordering (between importance and specificity):
    //      Normal:    unlayered(-1) wins over layered; among layered, later layers win
    //      Important: layered wins over unlayered; among layered, earlier layers win (reversed)
    //    - Among same importance+layer: inline > higher specificity > later source order
    std::stable_sort(matched.begin(), matched.end(),
        [](const MatchedDecl& a, const MatchedDecl& b) { return cascadesBefore(a, b); });

    // 4. Apply declarations in sorted order (last wins per property)
    //    Expand shorthands into longhands before applying.
    //    Build uaStyle in the same pass to avoid double expandShorthand.
    ComputedStyle style;
    ComputedStyle uaStyle;
    // The custom properties this element inherits are the set its parent hands
    // down, by pointer (see ComputedStyle). Wired before any declaration is
    // applied because shorthand substitution in 4b already resolves var().
    if (parentStyle) style.inheritedVars = parentStyle->varsForChildren();
    // 4a. Custom properties first — they never expand, and shorthand
    //     substitution in 4b needs their final values.
    for (auto& m : matched) {
        if (!isCustomProperty(*m.property)) continue;
        style[*m.property] = *m.value;
        if (m.origin == Origin::UserAgent) {
            uaStyle[*m.property] = *m.value;
        }
    }
    // 4b. Normal declarations. A var() inside a shorthand must be substituted
    //     BEFORE expansion (CSS Variables: the declaration holds a pending-
    //     substitution value; the shorthand splits into longhands only after
    //     substitution) — expanding "border: var(--bw, 6px) solid" raw would
    //     mis-tokenize and drop the width.
    //     The UA-origin map exists only for `revert`, so it is built only when
    //     some matched value says so; a longhand is written straight from the
    //     declaration rather than round-tripped through the expander's vector.
    //     substitutedProps holds views of the map's own keys — node-stable in an
    //     unordered_map — so noting a substitution allocates nothing.
    bool anyRevert = false;
    for (auto& m : matched) if (*m.value == "revert") { anyRevert = true; break; }
    std::vector<std::string_view> substitutedProps;
    auto noteSubstituted = [&](const std::string& key, bool was) {
        auto it = std::find(substitutedProps.begin(), substitutedProps.end(),
                            std::string_view(key));
        if (was) { if (it == substitutedProps.end()) substitutedProps.push_back(key); }
        else if (it != substitutedProps.end()) substitutedProps.erase(it);
    };
    auto put = [&](const std::string& prop, const std::string& value, bool was,
                   Origin origin) {
        auto [it, inserted] = style.try_emplace(prop, value);
        if (!inserted) it->second = value;
        noteSubstituted(it->first, was);
        if (anyRevert && origin == Origin::UserAgent) uaStyle[prop] = value;
    };
    for (auto& m : matched) {
        if (isCustomProperty(*m.property)) continue;
        const std::string* valPtr = m.value;
        std::string substituted;
        bool wasSubstituted = false;
        if (valPtr->find("var(") != std::string::npos) {
            substituted = resolveVarReferences(*valPtr, style, parentStyle);
            valPtr = &substituted;
            wasSubstituted = true;
        }
        if (!isShorthandProperty(*m.property)) {
            put(*m.property, *valPtr, wasSubstituted, m.origin);
            continue;
        }
        for (auto& e : expandShorthand(*m.property, *valPtr))
            put(e.property, e.value, wasSubstituted, m.origin);
    }

    // 5. Resolve inherit/initial/unset/revert keywords
    for (auto& [prop, val] : style) {
        if (val == "inherit") {
            // Force inheritance regardless of whether property normally inherits
            if (parentStyle) {
                auto it = parentStyle->find(prop);
                if (it != parentStyle->end()) {
                    val = it->second;
                } else {
                    val = initialValue(prop);
                }
            } else {
                val = initialValue(prop);
            }
        } else if (val == "initial") {
            val = initialValue(prop);
        } else if (val == "revert") {
            // revert: roll back to UA stylesheet value for this property
            auto it = uaStyle.find(prop);
            if (it != uaStyle.end()) {
                val = it->second;
            } else if (isInherited(prop) && parentStyle) {
                auto pit = parentStyle->find(prop);
                val = (pit != parentStyle->end()) ? pit->second : initialValue(prop);
            } else {
                val = initialValue(prop);
            }
        } else if (val == "unset") {
            // unset: if inherited property -> inherit, else -> initial
            if (isInherited(prop)) {
                if (parentStyle) {
                    auto it = parentStyle->find(prop);
                    if (it != parentStyle->end()) {
                        val = it->second;
                    } else {
                        val = initialValue(prop);
                    }
                } else {
                    val = initialValue(prop);
                }
            } else {
                val = initialValue(prop);
            }
        }
    }

    // 6. Custom properties (--*) are NOT copied down from the parent. The
    //    element sees them through `inheritedVars`, wired above; only its own
    //    declarations live in the map.

    // 7. Resolve var() references in all property values. A value substituted
    //    from a custom property that is invalid for its property is
    //    "invalid at computed-value time" (IACVT, CSS Variables §3.2): the
    //    property computes to its inherited value (if inherited) or initial
    //    value (otherwise). The common trigger is a length property receiving
    //    a unitless non-zero number (e.g. --x:20 → width:var(--x)); "20" is not
    //    a valid <length>, so width falls back to its initial value (auto).
    for (auto& [prop, val] : style) {
        bool hadVar = val.find("var(") != std::string::npos;
        if (hadVar) val = resolveVarReferences(val, style, parentStyle);
        // Values substituted during shorthand application (4b) already lost
        // their var() marker but still need IACVT validation below.
        else if (std::find(substitutedProps.begin(), substitutedProps.end(),
                           std::string_view(prop)) == substitutedProps.end()) continue;

        if (isCustomProperty(prop)) continue;
        // Is the substituted value a bare non-zero number (no unit / percent)?
        // Only length-valued properties treat that as invalid — properties that
        // legitimately take unitless numbers (line-height, z-index, opacity,
        // flex-grow, order, …) must be left alone.
        static const std::unordered_set<std::string> kLengthProps = {
            "width", "height", "min-width", "min-height", "max-width", "max-height",
            "top", "right", "bottom", "left",
            "margin-top", "margin-right", "margin-bottom", "margin-left",
            "padding-top", "padding-right", "padding-bottom", "padding-left",
            "border-top-width", "border-right-width", "border-bottom-width",
            "border-left-width", "font-size", "text-indent",
            "letter-spacing", "word-spacing", "flex-basis",
            "column-width", "column-gap", "row-gap", "gap",
        };
        if (kLengthProps.count(prop) == 0) continue;
        const char* b = val.c_str();
        char* e = nullptr;
        double n = std::strtod(b, &e);
        // Fully consumed (no trailing unit) and non-zero → invalid <length>.
        if (e != b && *e == '\0' && n != 0.0) {
            val = initialValue(prop); // IACVT → initial (width/height → auto, etc.)
        }
    }

    // Whether this element itself carries a font-size declaration (as opposed
    // to inheriting one in step 8) — the monospace default-size quirk below
    // needs the distinction.
    const bool fontSizeSpecifiedHere = style.find("font-size") != style.end();

    // 8. Inherit inherited properties from parent.
    //    Only iterate the small set of inherited property names (~25) rather
    //    than all ~317 known properties.  Look each one up in the parent and,
    //    if present and not already set on this element, copy it over.
    static const auto& inheritedProps = [] {
        static std::vector<std::string> names;
        for (auto& p : knownProperties()) {
            if (p.inherited) names.push_back(p.name);
        }
        return names;
    }();

    if (parentStyle) {
        for (auto& name : inheritedProps) {
            if (style.find(name) == style.end()) {
                auto it = parentStyle->find(name);
                if (it != parentStyle->end()) {
                    style[name] = it->second;
                }
            }
        }
    }

    // 8b. Monospace default-size quirk (Blink behavior): the font-size
    //     keyword `medium` — also font-size's initial value — resolves
    //     against the font family's default size: 16px normally, 13px when
    //     the computed family's FIRST entry is the generic `monospace`.
    //     The KEYWORD inherits, not the px value (in Chromium a sans-serif
    //     child of a <pre> is back at 16px), so the active keyword is
    //     tracked in an internal, manually-inherited key and materialized
    //     as a px font-size per element.
    {
        static constexpr const char* kFsKeyword = "-hl-font-size-keyword";
        std::string keyword;
        if (fontSizeSpecifiedHere) {
            auto it = style.find("font-size");
            if (it != style.end() && it->second == "medium")
                keyword = it->second;
        } else if (parentStyle) {
            auto pk = parentStyle->find(kFsKeyword);
            if (pk != parentStyle->end())
                keyword = pk->second;
        } else {
            keyword = "medium"; // initial value of font-size
        }
        if (!keyword.empty()) {
            style[kFsKeyword] = keyword;
            // First family of the computed list, trimmed of space/quotes,
            // lowercased. The quirk keys off the generic keyword alone:
            // `Menlo, monospace` does NOT trigger it (matches Blink).
            std::string fam;
            auto ff = style.find("font-family");
            if (ff != style.end()) {
                fam = ff->second.substr(0, ff->second.find(','));
                size_t b = fam.find_first_not_of(" \t\"'");
                size_t e = fam.find_last_not_of(" \t\"'");
                fam = (b == std::string::npos) ? std::string()
                                               : fam.substr(b, e - b + 1);
                for (char& c : fam)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            style["font-size"] = (fam == "monospace") ? "13px" : "16px";
        }
    }

    // 8c. Percentage line-height computes to an absolute length at
    //     computed-value time (CSS2 §10.8.1): the percentage resolves against
    //     the element's own font-size and the RESOLVED value inherits (a
    //     child with a different font-size keeps the parent's px, unlike a
    //     unitless number). Only convertible when the computed font-size is
    //     already in px — otherwise layout's resolver handles it.
    {
        auto lh = style.find("line-height");
        if (lh != style.end() && !lh->second.empty() &&
            lh->second.back() == '%') {
            auto fs = style.find("font-size");
            if (fs != style.end() && fs->second.size() > 2 &&
                fs->second.compare(fs->second.size() - 2, 2, "px") == 0) {
                char* endp = nullptr;
                double pct = std::strtod(lh->second.c_str(), &endp);
                if (endp && *endp == '%') {
                    double fpx = std::strtod(fs->second.c_str(), nullptr);
                    double px = pct / 100.0 * fpx;
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%g", px);
                    lh->second = std::string(buf) + "px";
                }
            }
        }
    }

    // 9. Blockification (CSS Display §2.7). Inline-level display values compute
    //    to their block-level equivalents (inline → block, inline-flex → flex,
    //    ...) whenever the box is required to be block-level. display:none and
    //    display:contents are never transformed. This happens at computed-value
    //    time, so getComputedStyle reports the blockified value (matches
    //    Chromium), and layout routes the box through block/flex/grid/table
    //    layout — which is what lets e.g. a percentage height on a <span> flex
    //    item resolve instead of being ignored by inline layout.
    //
    //    Four triggers, per spec:
    //      a. flex / grid items,
    //      b. absolutely positioned boxes (position: absolute | fixed),
    //      c. floats (float: left | right | inline-start | inline-end).
    //    The root element is the fourth trigger in the spec; it is left to the
    //    UA stylesheet (`html { display: block }`) because this function cannot
    //    identify the root - a null parentStyle also means "resolve this
    //    element standalone", which is how callers style detached subtrees.
    //    (b) and (c) matter as much as (a): an out-of-flow <span> that stays
    //    `display: inline` gets laid out by the inline path, where width/height
    //    do not apply — it collapses to a zero-size box instead of honouring
    //    its inset/size properties.
    blockify(style, parentStyle);

    // 9d. CSS Logical Properties L1 — resolve inline-axis logical properties to
    //     physical sides now that `direction` is known. Block-axis logical
    //     properties were already mapped to physical at shorthand-expansion time
    //     (writing-mode is always horizontal-tb), but inline-start/end depend on
    //     the element's own `direction`: for ltr inline-start=left, inline-end=
    //     right; for rtl they swap. Presence of a logical key in the map means it
    //     was explicitly set (non-inherited defaults are never stored), so mapping
    //     it onto the physical side reproduces the correct computed value.
    resolveInlineLogical(style);

    // 10. Non-inherited defaults are NOT stored in the map.  Layout's styleVal()
    //    falls back to initialValueRef() for missing keys, so the map only
    //    contains properties that were explicitly set or inherited — typically
    //    ~10-30 entries instead of ~317.

    // 11. HTML table structure attributes: colspan/rowspan on cells and span
    //     on <col>/<colgroup> are structural inputs to table layout rather
    //     than CSS properties. Surface them as computed-style keys so table
    //     layout still sees spans when the consumer's LayoutNode doesn't
    //     bridge HTML attributes (layout prefers LayoutNode::attribute() and
    //     falls back to these keys). Values are normalized positive integers;
    //     they are non-inherited by construction (only set from the element's
    //     own attributes).
    {
        auto tagIs = [&](std::string_view want) {
            std::string_view tag = elem.tagName();
            if (tag.size() != want.size()) return false;
            for (size_t i = 0; i < tag.size(); i++) {
                if (std::tolower(static_cast<unsigned char>(tag[i])) != want[i])
                    return false;
            }
            return true;
        };
        auto surfaceSpan = [&](const char* attr) {
            if (style.find(attr) != style.end()) return;
            std::string_view v = elem.getAttribute(attr);
            if (v.empty()) return;
            int n = std::atoi(std::string(v).c_str());
            if (n >= 1) style[attr] = std::to_string(n);
        };
        if (tagIs("td") || tagIs("th")) {
            surfaceSpan("colspan");
            surfaceSpan("rowspan");
        } else if (tagIs("col") || tagIs("colgroup")) {
            surfaceSpan("span");
        }
    }

    return style;
}

ComputedStyle Cascade::resolvePseudo(const ElementRef& elem,
                                      const std::string& pseudoName,
                                      const ComputedStyle& elemStyle) const {
    // Collect rules whose selector targets ::pseudoName on this element.
    // Pointers into the rules: nothing here is copied per element.
    struct MatchedDecl {
        const std::string* property;
        const std::string* value;
        bool important;
        uint32_t specificity;
        size_t order;
        int layerOrder;  // layer rank, -1 = unlayered
        Origin origin;
    };

    // Only rules whose subject targets ::pseudoName can contribute. They were
    // bucketed at insertion time, so a page with no ::before rule pays nothing
    // here — the alternative, rescanning every rule in the sheet for every
    // element on every restyle, is what made a hover over a long list crawl.
    auto bucket = pseudoRules_.find(pseudoName);
    if (bucket == pseudoRules_.end()) return {};

    auto applies = [&](const ScopedRule& rule) {
        // Pseudo-elements have no starting style here.
        if (rule.startingStyle) return false;
        if (rule.scope != nullptr && rule.scope != elem.scope()) return false;
        // The selector with the pseudo-element already stripped from its
        // subject (classifyLastRule); an empty subject matches everything.
        if (!rule.pseudoSelector.matches(elem)) return false;
        // A pseudo-element's query container is the nearest container among
        // its originating element and that element's ancestors.
        return containerQueriesHold(elem, rule.containerQueries, /*fromSelf=*/true, &elemStyle);
    };

    // ::before and ::after generate a box only if some rule gives them
    // `content`. Ask that first, of the few rules that set it: a reset sheet's
    // `*, *::before, *::after { box-sizing: border-box }` matches every element
    // in the document twice, and without this gate every one of them computed
    // a full pseudo style — several hundred properties — for a box that does
    // not exist. This is where half of a large document's restyle went. The
    // other pseudo-elements (::placeholder, ::selection, ::marker, …) style a
    // box that exists regardless, so they are not gated.
    if (pseudoName == "before" || pseudoName == "after") {
        bool generatesBox = false;
        for (size_t ruleIdx : bucket->second) {
            const auto& rule = rules_[ruleIdx];
            if (!rule.declaresContent) continue;
            if (applies(rule)) { generatesBox = true; break; }
        }
        if (!generatesBox) return {};
    }

    std::vector<MatchedDecl> matched;
    for (size_t ruleIdx : bucket->second) {
        const auto& rule = rules_[ruleIdx];
        if (!applies(rule)) continue;
        for (auto& decl : rule.declarations) {
            matched.push_back({
                &decl.property, &decl.value, decl.important,
                rule.selector.specificity, rule.order,
                rule.layerOrder < 0 ? -1 : layerRanks_[static_cast<size_t>(rule.layerOrder)],
                rule.origin
            });
        }
    }

    if (matched.empty()) return {};

    // Sort by cascade precedence: the same origin / layer / specificity /
    // order rules as an element's own declarations.
    std::stable_sort(matched.begin(), matched.end(),
        [](const MatchedDecl& a, const MatchedDecl& b) { return cascadesBefore(a, b); });

    // Apply declarations. The pseudo inherits the originating element's custom
    // properties the way a child would: by sharing its set.
    // Custom properties first, then the rest with var() substituted before a
    // shorthand expands, as for an element (resolve() steps 4a/4b): a
    // `background: var(--rim)` on a ::after otherwise reached paint unparsed.
    ComputedStyle style;
    style.inheritedVars = elemStyle.varsForChildren();
    for (auto& m : matched) {
        if (isCustomProperty(*m.property)) style[*m.property] = *m.value;
    }
    for (auto& m : matched) {
        if (isCustomProperty(*m.property)) continue;
        if (m.value->find("var(") == std::string::npos) {
            for (auto& e : expandShorthand(*m.property, *m.value)) style[e.property] = e.value;
            continue;
        }
        std::string substituted = resolveVarReferences(*m.value, style, &elemStyle);
        for (auto& e : expandShorthand(*m.property, substituted)) style[e.property] = e.value;
    }

    // Resolve inline-axis logical properties to physical sides before the
    // defaults-fill loop below stores every non-inherited property (which would
    // otherwise make "explicitly set" indistinguishable from default). Direction
    // is inherited from the originating element when not set on the pseudo.
    if (style.find("direction") == style.end()) {
        auto d = elemStyle.find("direction");
        if (d != elemStyle.end()) style["direction"] = d->second;
    }
    resolveInlineLogical(style);

    // Inherit from the originating element's style for inherited properties
    for (auto& prop : knownProperties()) {
        if (style.find(prop.name) == style.end()) {
            if (prop.inherited) {
                auto it = elemStyle.find(prop.name);
                if (it != elemStyle.end()) {
                    style[prop.name] = it->second;
                } else {
                    style[prop.name] = prop.initialValue;
                }
            } else {
                style[prop.name] = prop.initialValue;
            }
        }
    }

    // A positioned ::after (a divider rule, a badge dot) or one inside a flex
    // or grid element is block-level, so its width and height apply.
    blockify(style, &elemStyle);

    return style;
}

} // namespace htmlayout::css
