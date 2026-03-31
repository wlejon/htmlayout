#include "css/cascade.h"
#include "css/properties.h"
#include <algorithm>

namespace htmlayout::css {

namespace {

// Check if a property name is a CSS custom property (starts with --)
bool isCustomProperty(const std::string& name) {
    return name.size() >= 2 && name[0] == '-' && name[1] == '-';
}

// Resolve var() references in a value string, using the current style and parent.
// Supports var(--name) and var(--name, fallback).
// Resolves nested var() in fallbacks.
std::string resolveVarReferences(const std::string& value,
                                  const ComputedStyle& style,
                                  const ComputedStyle* parentStyle,
                                  int depth = 0) {
    if (depth > 10) return value; // prevent infinite recursion

    std::string result;
    size_t i = 0;
    while (i < value.size()) {
        // Look for "var("
        if (i + 3 < value.size() && value.substr(i, 4) == "var(") {
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

            // Look up the variable
            std::string resolved;
            auto it = style.find(varName);
            if (it != style.end() && !it->second.empty()) {
                resolved = it->second;
            } else if (parentStyle) {
                auto pit = parentStyle->find(varName);
                if (pit != parentStyle->end() && !pit->second.empty()) {
                    resolved = pit->second;
                }
            }

            if (resolved.empty() && !fallback.empty()) {
                resolved = fallback;
            }

            // Recursively resolve any var() in the resolved value
            result += resolveVarReferences(resolved, style, parentStyle, depth + 1);
        } else {
            result += value[i++];
        }
    }
    return result;
}

} // anonymous namespace

void Cascade::addStylesheet(const Stylesheet& sheet, void* scope,
                             const MediaContext* media) {
    // Add unconditional rules
    for (auto& rule : sheet.rules) {
        auto selectors = parseSelectorList(rule.selector);
        for (auto& sel : selectors) {
            rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++});
        }
    }

    // Add @media rules whose conditions match
    for (auto& block : sheet.mediaBlocks) {
        bool matches = true;
        if (media) {
            matches = evaluateMediaQuery(block.condition, *media);
        }
        // If no media context provided, include all @media rules (permissive)
        if (matches) {
            for (auto& rule : block.rules) {
                auto selectors = parseSelectorList(rule.selector);
                for (auto& sel : selectors) {
                    rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++});
                }
            }
        }
    }
}

ComputedStyle Cascade::resolve(const ElementRef& elem,
                                const std::string& inlineStyle,
                                const ComputedStyle* parentStyle) const {
    // 1. Collect all matching rules whose scope matches the element's scope
    struct MatchedDecl {
        std::string property;
        std::string value;
        bool important;
        uint32_t specificity;
        size_t order;
        bool isInline;  // inline style has highest author specificity
    };

    std::vector<MatchedDecl> matched;

    for (auto& rule : rules_) {
        // Scope check: rule scope must match element scope
        if (rule.scope != elem.scope()) continue;

        // Selector match
        if (!rule.selector.matches(elem)) continue;

        // Add all declarations from this rule
        for (auto& decl : rule.declarations) {
            matched.push_back({
                decl.property, decl.value, decl.important,
                rule.selector.specificity, rule.order, false
            });
        }
    }

    // 2. Parse and add inline style declarations (highest author specificity)
    if (!inlineStyle.empty()) {
        auto inlineDecls = parseInlineStyle(inlineStyle);
        for (auto& decl : inlineDecls) {
            matched.push_back({
                decl.property, decl.value, decl.important,
                0xFFFFFFFF, // inline style beats all selector specificities
                SIZE_MAX,   // and all source orders
                true
            });
        }
    }

    // 3. Sort by cascade precedence:
    //    - !important declarations beat normal declarations
    //    - Among same importance: inline > higher specificity > later source order
    std::stable_sort(matched.begin(), matched.end(),
        [](const MatchedDecl& a, const MatchedDecl& b) {
            // Important declarations come after normal ones (applied last = wins)
            if (a.important != b.important) return !a.important;
            // Higher specificity wins (comes later)
            if (a.specificity != b.specificity) return a.specificity < b.specificity;
            // Later source order wins (comes later)
            return a.order < b.order;
        });

    // 4. Apply declarations in sorted order (last wins per property)
    //    Expand shorthands into longhands before applying.
    ComputedStyle style;
    for (auto& m : matched) {
        auto expanded = expandShorthand(m.property, m.value);
        for (auto& e : expanded) {
            style[e.property] = e.value;
        }
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
        } else if (val == "unset" || val == "revert") {
            // unset: if inherited property -> inherit, else -> initial
            // revert: ideally rolls back to UA, but we treat as unset for now
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

    // 6. Inherit custom properties (--*) from parent if not explicitly set
    if (parentStyle) {
        for (auto& [prop, val] : *parentStyle) {
            if (isCustomProperty(prop) && style.find(prop) == style.end()) {
                style[prop] = val;
            }
        }
    }

    // 7. Resolve var() references in all property values
    for (auto& [prop, val] : style) {
        if (val.find("var(") != std::string::npos) {
            val = resolveVarReferences(val, style, parentStyle);
        }
    }

    // 8. For inherited properties not explicitly set, inherit from parent
    // 9. For non-inherited properties not explicitly set, use initial value
    for (auto& prop : knownProperties()) {
        if (style.find(prop.name) == style.end()) {
            if (prop.inherited && parentStyle) {
                auto it = parentStyle->find(prop.name);
                if (it != parentStyle->end()) {
                    style[prop.name] = it->second;
                } else {
                    style[prop.name] = prop.initialValue;
                }
            } else {
                style[prop.name] = prop.initialValue;
            }
        }
    }

    return style;
}

ComputedStyle Cascade::resolvePseudo(const ElementRef& elem,
                                      const std::string& pseudoName,
                                      const ComputedStyle& elemStyle) const {
    // Collect rules whose selector targets ::pseudoName on this element
    struct MatchedDecl {
        std::string property;
        std::string value;
        bool important;
        uint32_t specificity;
        size_t order;
    };

    std::vector<MatchedDecl> matched;

    for (auto& rule : rules_) {
        if (rule.scope != elem.scope()) continue;

        // Check if the subject compound has a pseudo-element matching pseudoName
        auto& chain = rule.selector.chain;
        if (chain.entries.empty()) continue;

        auto& subject = chain.entries[0].compound;
        bool hasPseudo = false;
        for (auto& s : subject.simples) {
            if (s.type == SimpleSelectorType::PseudoElement && s.value == pseudoName) {
                hasPseudo = true;
                break;
            }
        }
        if (!hasPseudo) continue;

        // Now match the selector against the element, ignoring the pseudo-element part.
        // Build a temporary compound without the pseudo-element.
        CompoundSelector filtered;
        for (auto& s : subject.simples) {
            if (s.type != SimpleSelectorType::PseudoElement) {
                filtered.simples.push_back(s);
            }
        }

        // If the filtered compound is empty (just ::before), treat as universal
        bool subjectMatches = true;
        if (!filtered.simples.empty()) {
            // Match the filtered compound against elem
            for (auto& s : filtered.simples) {
                if (!matchSimple(s, elem)) {
                    subjectMatches = false;
                    break;
                }
            }
        }

        // Also match ancestor/sibling parts of the chain
        if (subjectMatches && chain.entries.size() > 1) {
            // Build a chain without the pseudo-element for matching
            SelectorChain testChain;
            SelectorChain::Entry subjectEntry;
            subjectEntry.compound = filtered;
            subjectEntry.combinator = Combinator::None;
            testChain.entries.push_back(subjectEntry);
            for (size_t i = 1; i < chain.entries.size(); i++) {
                testChain.entries.push_back(chain.entries[i]);
            }
            Selector testSel;
            testSel.chain = testChain;
            subjectMatches = testSel.matches(elem);
        }

        if (!subjectMatches) continue;

        for (auto& decl : rule.declarations) {
            matched.push_back({
                decl.property, decl.value, decl.important,
                rule.selector.specificity, rule.order
            });
        }
    }

    if (matched.empty()) return {};

    // Sort by cascade precedence
    std::stable_sort(matched.begin(), matched.end(),
        [](const MatchedDecl& a, const MatchedDecl& b) {
            if (a.important != b.important) return !a.important;
            if (a.specificity != b.specificity) return a.specificity < b.specificity;
            return a.order < b.order;
        });

    // Apply declarations
    ComputedStyle style;
    for (auto& m : matched) {
        auto expanded = expandShorthand(m.property, m.value);
        for (auto& e : expanded) {
            style[e.property] = e.value;
        }
    }

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

    return style;
}

void Cascade::clear() {
    rules_.clear();
    nextOrder_ = 0;
}

} // namespace htmlayout::css
