#include "css/cascade.h"
#include "css/properties.h"
#include <algorithm>
#include <sstream>

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

int Cascade::getOrCreateLayerIndex(const std::string& name) {
    for (int i = 0; i < static_cast<int>(layerNames_.size()); i++) {
        if (layerNames_[i] == name) return i;
    }
    layerNames_.push_back(name);
    return static_cast<int>(layerNames_.size()) - 1;
}

bool Cascade::evaluateContainerQuery(const ElementRef& elem,
                                      const std::string& containerName,
                                      const std::string& condition) const {
    // Walk up the tree to find the nearest container ancestor
    const ElementRef* current = elem.parent();
    while (current) {
        std::string cType = current->containerType();
        if (cType != "none") {
            // Check name match if required
            if (!containerName.empty()) {
                std::string cName = current->containerName();
                // Check if the container's name list contains the required name
                bool nameMatch = false;
                std::istringstream iss(cName);
                std::string n;
                while (iss >> n) {
                    if (n == containerName) { nameMatch = true; break; }
                }
                if (!nameMatch) {
                    current = current->parent();
                    continue;
                }
            }

            // Evaluate the condition against this container
            // Parse conditions like "(min-width: 400px)" or "(width > 400px)"
            std::string cond = condition;
            // Strip outer parens if present
            if (!cond.empty() && cond.front() == '(') cond.erase(0, 1);
            if (!cond.empty() && cond.back() == ')') cond.pop_back();

            // Trim
            size_t s = cond.find_first_not_of(" \t\n\r\f");
            size_t e = cond.find_last_not_of(" \t\n\r\f");
            if (s == std::string::npos) return true;
            cond = cond.substr(s, e - s + 1);

            auto colonPos = cond.find(':');
            if (colonPos == std::string::npos) return true;

            std::string feature = cond.substr(0, colonPos);
            std::string valueStr = cond.substr(colonPos + 1);
            // Trim
            {
                size_t a = feature.find_first_not_of(" \t"); size_t b = feature.find_last_not_of(" \t");
                feature = (a != std::string::npos) ? feature.substr(a, b - a + 1) : "";
                a = valueStr.find_first_not_of(" \t"); b = valueStr.find_last_not_of(" \t");
                valueStr = (a != std::string::npos) ? valueStr.substr(a, b - a + 1) : "";
            }

            // Parse numeric value
            float val = 0;
            try { val = std::stof(valueStr); } catch (...) {}

            float inlineSize = current->containerInlineSize();
            float blockSize = current->containerBlockSize();

            if (feature == "min-width") return inlineSize >= val;
            if (feature == "max-width") return inlineSize <= val;
            if (feature == "width") return inlineSize == val;
            if (feature == "min-height") {
                if (cType != "size") return true; // block-size only with size containment
                return blockSize >= val;
            }
            if (feature == "max-height") {
                if (cType != "size") return true;
                return blockSize <= val;
            }
            if (feature == "height") {
                if (cType != "size") return true;
                return blockSize == val;
            }

            return true; // unknown feature, permissive
        }
        current = current->parent();
    }
    return false; // no container found
}

void Cascade::addStylesheet(const Stylesheet& sheet, void* scope,
                             const MediaContext* media) {
    // Record pre-declared layer ordering
    for (auto& name : sheet.layerOrder) {
        getOrCreateLayerIndex(name);
    }

    // Add unconditional rules
    for (auto& rule : sheet.rules) {
        auto selectors = parseSelectorList(rule.selector);
        for (auto& sel : selectors) {
            rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, -1, {}, {}});
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
                    rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, -1, {}, {}});
                }
            }
        }
    }

    // Add @layer rules
    for (auto& layerBlock : sheet.layerBlocks) {
        int layerIdx = getOrCreateLayerIndex(layerBlock.name);
        for (auto& rule : layerBlock.rules) {
            auto selectors = parseSelectorList(rule.selector);
            for (auto& sel : selectors) {
                rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, layerIdx, {}, {}});
            }
        }
        // @media inside @layer
        for (auto& mediaBlock : layerBlock.mediaBlocks) {
            bool matches = true;
            if (media) {
                matches = evaluateMediaQuery(mediaBlock.condition, *media);
            }
            if (matches) {
                for (auto& rule : mediaBlock.rules) {
                    auto selectors = parseSelectorList(rule.selector);
                    for (auto& sel : selectors) {
                        rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, layerIdx, {}, {}});
                    }
                }
            }
        }
    }

    // Add @container rules
    for (auto& containerBlock : sheet.containerBlocks) {
        for (auto& rule : containerBlock.rules) {
            auto selectors = parseSelectorList(rule.selector);
            for (auto& sel : selectors) {
                rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, -1,
                                  containerBlock.name, containerBlock.condition});
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
        int layerOrder;  // -1 = unlayered, >=0 = layer index
    };

    std::vector<MatchedDecl> matched;

    for (auto& rule : rules_) {
        // Container query check: if the rule has a container condition, evaluate it
        if (!rule.containerCondition.empty()) {
            if (!evaluateContainerQuery(elem, rule.containerName, rule.containerCondition)) {
                continue;
            }
        }

        // Check if this is a :host or :host-context selector from a shadow stylesheet.
        // :host rules live in the shadow scope but match the shadow host element (which
        // is in the outer/document scope).
        bool isHostSelector = false;
        if (!rule.selector.chain.entries.empty()) {
            for (auto& s : rule.selector.chain.entries[0].compound.simples) {
                if (s.type == SimpleSelectorType::PseudoClass &&
                    (s.value == "host" || s.value == "host-context")) {
                    isHostSelector = true;
                    break;
                }
            }
        }

        // Check if this is a ::slotted() selector from a shadow stylesheet.
        // ::slotted rules live in the shadow scope but match slotted light DOM elements.
        bool isSlottedSelector = false;
        if (!rule.selector.chain.entries.empty()) {
            for (auto& s : rule.selector.chain.entries[0].compound.simples) {
                if (s.type == SimpleSelectorType::PseudoElement && s.value == "slotted") {
                    isSlottedSelector = true;
                    break;
                }
            }
        }

        // Check if this is a ::part() selector from the outer scope.
        // ::part rules live in the outer scope but match elements inside a shadow tree.
        bool isPartSelector = false;
        if (!rule.selector.chain.entries.empty()) {
            for (auto& s : rule.selector.chain.entries[0].compound.simples) {
                if (s.type == SimpleSelectorType::PseudoElement && s.value == "part") {
                    isPartSelector = true;
                    break;
                }
            }
        }

        if (isHostSelector) {
            // :host rules are scoped to a shadow root. They match the host element
            // whose shadowRoot() equals the rule's scope.
            if (rule.scope == nullptr) continue;  // :host must be in a shadow stylesheet
            if (elem.shadowRoot() != rule.scope) continue;
        } else if (isSlottedSelector) {
            // ::slotted rules are in the shadow scope. They match light DOM children
            // that are distributed into a slot inside that shadow tree.
            if (rule.scope == nullptr) continue;
            // The element must be slotted into this shadow tree
            auto* slot = elem.assignedSlot();
            if (!slot) continue;
            if (slot->scope() != rule.scope) continue;
        } else if (isPartSelector) {
            // ::part rules are in the outer scope. They target elements inside a shadow tree
            // by their part name. The rule scope should be the outer scope (document or parent shadow).
            // The element must be inside a shadow tree and expose a part name.
            if (elem.scope() == nullptr) continue;  // element must be in a shadow scope
            if (rule.scope != nullptr) continue;     // ::part rules come from outer/document scope
            // Check part name match
            std::string partName = elem.partName();
            if (partName.empty()) continue;
        } else {
            // Normal scope check: rule scope must match element scope
            if (rule.scope != elem.scope()) continue;
        }

        // For :host selectors, match directly (the :host pseudo-class handles the logic)
        if (isHostSelector) {
            if (!rule.selector.matches(elem)) continue;
        } else if (isSlottedSelector) {
            // Match the ::slotted() argument against the element
            bool slottedMatch = true;
            for (auto& s : rule.selector.chain.entries[0].compound.simples) {
                if (s.type == SimpleSelectorType::PseudoElement && s.value == "slotted") {
                    // Match the slotted argument selectors
                    for (auto& inner : s.slottedArg) {
                        if (!matchSimple(inner, elem)) { slottedMatch = false; break; }
                    }
                    break;
                }
            }
            if (!slottedMatch) continue;
        } else if (isPartSelector) {
            // Match the ::part(name) against the element's part name
            bool partMatch = false;
            for (auto& s : rule.selector.chain.entries[0].compound.simples) {
                if (s.type == SimpleSelectorType::PseudoElement && s.value == "part") {
                    // Check if element's part name list contains the target part name
                    std::string elemParts = elem.partName();
                    std::istringstream iss(elemParts);
                    std::string p;
                    while (iss >> p) {
                        if (p == s.partArg) { partMatch = true; break; }
                    }
                    break;
                }
            }
            if (!partMatch) continue;

            // Also match other selectors in the chain (e.g., "my-element::part(foo)")
            if (rule.selector.chain.entries.size() > 1) {
                // The ancestor part of the chain must match the host element
                // For now, skip complex chains and just match
            }
        } else {
            // Normal selector match
            if (!rule.selector.matches(elem)) continue;
        }

        // Add all declarations from this rule
        for (auto& decl : rule.declarations) {
            matched.push_back({
                decl.property, decl.value, decl.important,
                rule.selector.specificity, rule.order, false, rule.layerOrder
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
                true,
                -1          // inline styles are unlayered
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
        [](const MatchedDecl& a, const MatchedDecl& b) {
            // Important declarations come after normal ones (applied last = wins)
            if (a.important != b.important) return !a.important;

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
    layerNames_.clear();
}

} // namespace htmlayout::css
