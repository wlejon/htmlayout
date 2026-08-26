#include "css/cascade.h"
#include "css/properties.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_set>

namespace htmlayout::css {

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
        std::string_view cType = current->containerType();
        if (cType != "none") {
            // Check name match if required
            if (!containerName.empty()) {
                std::string cName(current->containerName());
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

            float inlineSize = current->containerInlineSize();
            float blockSize = current->containerBlockSize();

            // Build a MediaContext to reuse evaluateMediaFeature / range parsing
            MediaContext mctx;
            mctx.viewportWidth = inlineSize;
            mctx.viewportHeight = (cType == "size") ? blockSize : 0;

            // Evaluate potentially multiple conditions joined by and/or
            // Split into parenthesized features
            std::string fullCond = "(" + cond + ")";
            return evaluateMediaQuery(fullCond, mctx);
        }
        current = current->parent();
    }
    return false; // no container found
}

void Cascade::setImportResolver(ImportResolver resolver) {
    importResolver_ = std::move(resolver);
}

void Cascade::addStylesheet(const Stylesheet& sheet, void* scope,
                             const MediaContext* media, Origin origin) {
    // Process @import rules first (imported rules precede this sheet in source order)
    if (importResolver_) {
        for (auto& imp : sheet.imports) {
            if (loadedImports_.count(imp.url)) continue;
            loadedImports_.insert(imp.url);

            // Check media condition on the import
            if (!imp.mediaCondition.empty() && media) {
                if (!evaluateMediaQuery(imp.mediaCondition, *media)) continue;
            }

            std::string css = importResolver_(imp.url);
            if (css.empty()) continue;

            Stylesheet imported = parse(css);

            // If import specifies a layer, wrap all imported rules in that layer
            if (!imp.layer.empty()) {
                LayerBlock layerBlock;
                layerBlock.name = imp.layer;
                layerBlock.rules = std::move(imported.rules);
                layerBlock.mediaBlocks = std::move(imported.mediaBlocks);
                imported.rules.clear();
                imported.mediaBlocks.clear();
                imported.layerBlocks.push_back(std::move(layerBlock));
            }

            addStylesheet(imported, scope, media, origin);
        }
    }

    // Store @font-face rules
    for (auto& ff : sheet.fontFaces) {
        fontFaces_.push_back(ff);
    }

    // Store @keyframes rules
    for (auto& kf : sheet.keyframes) {
        // Later definitions override earlier ones with the same name
        bool found = false;
        for (auto& existing : keyframes_) {
            if (existing.name == kf.name) {
                existing = kf;
                found = true;
                break;
            }
        }
        if (!found) keyframes_.push_back(kf);
    }

    // Record pre-declared layer ordering
    for (auto& name : sheet.layerOrder) {
        getOrCreateLayerIndex(name);
    }

    // Add unconditional rules
    for (auto& rule : sheet.rules) {
        auto selectors = parseSelectorList(rule.selector);
        for (auto& sel : selectors) {
            rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, -1, origin, {}, {}});
            classifyLastRule();
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
                    rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, -1, origin, {}, {}});
                    classifyLastRule();
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
                rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, layerIdx, origin, {}, {}});
                classifyLastRule();
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
                        rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, layerIdx, origin, {}, {}});
                        classifyLastRule();
                    }
                }
            }
        }
    }

    // Add @container rules
    for (auto& containerBlock : sheet.containerBlocks) {
        if (!containerBlock.rules.empty()) usesContainers_ = true;
        for (auto& rule : containerBlock.rules) {
            auto selectors = parseSelectorList(rule.selector);
            for (auto& sel : selectors) {
                rules_.push_back({std::move(sel), rule.declarations, scope, nextOrder_++, -1, origin,
                                  containerBlock.name, containerBlock.condition});
                classifyLastRule();
            }
        }
    }
}

// CSS Logical Properties L1 — resolve inline-axis logical properties (margin/
// padding/border/inset -inline-start/end) to physical sides using the element's
// own computed `direction`. Block-axis logical properties are already physical
// by shorthand-expansion time (writing-mode is always horizontal-tb). Presence
// of a logical key in the map means it was explicitly set (non-inherited
// defaults are never stored), so it wins over the physical initial value.
static void resolveInlineLogical(ComputedStyle& style) {
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

ComputedStyle Cascade::resolve(const ElementRef& elem,
                                const std::string& inlineStyle,
                                const ComputedStyle* parentStyle) const {
    // 1. Collect all matching rules whose scope matches the element's scope.
    //    Use pointers to avoid copying property/value strings from Declaration objects.
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

    // 0. SVG presentation attributes (fill="red", stroke-width="2", ...) are the
    //    lowest-priority style source: they beat inheritance (they enter the
    //    cascade as a real declaration) but lose to any author rule or inline
    //    style. Seed them at specificity 0 and push them FIRST so stable_sort
    //    keeps them earliest among equal-specificity declarations (a universal
    //    author rule still wins). Attribute names match their CSS property names
    //    1:1. presDecls must outlive the apply pass — MatchedDecl holds pointers.
    std::vector<Declaration> presDecls;
    {
        static const char* const kSvgPresAttrs[] = {
            "fill", "fill-opacity", "fill-rule",
            "stroke", "stroke-opacity", "stroke-width",
            "stroke-linecap", "stroke-linejoin", "stroke-miterlimit",
            "stroke-dasharray", "stroke-dashoffset",
            "clip-rule", "clip-path", "paint-order", "color", "opacity",
            "stop-color", "stop-opacity",
            "font-family", "font-size", "font-weight", "font-style",
            "text-anchor", "dominant-baseline", "alignment-baseline",
            "baseline-shift",
            "marker-start", "marker-mid", "marker-end",
        };
        static const char* const kSvgTextPresAttrs[] = {
            "direction", "unicode-bidi",
        };
        // `direction` and `unicode-bidi` are presentation attributes only on
        // SVG text content elements. Unlike the names above they collide with
        // nothing in HTML *because* HTML spells the same idea `dir` — so
        // seeding them unconditionally would invent a `<div direction="rtl">`
        // that no browser honours. Gate them on the tags that can carry them.
        static const char* const kSvgTextTags[] = {
            "text", "tspan", "textpath", "tref", "altglyph",
        };
        bool isSvgTextTag = false;
        {
            std::string_view tag = elem.tagName();
            for (const char* t : kSvgTextTags) {
                if (tag.size() == std::string_view(t).size() &&
                    std::equal(tag.begin(), tag.end(), t,
                               [](char a, char b) {
                                   return std::tolower(static_cast<unsigned char>(a)) ==
                                          std::tolower(static_cast<unsigned char>(b));
                               })) {
                    isSvgTextTag = true;
                    break;
                }
            }
        }

        for (const char* attr : kSvgPresAttrs) {
            std::string_view v = elem.getAttribute(attr);
            if (v.empty()) continue;
            std::string value(v);
            // SVG lengths are unitless user units (= px). font-size is resolved
            // as a CSS <length>, where a bare number is invalid-at-computed-
            // value-time and would reset to `medium`; append px so e.g.
            // font-size="34" computes to 34px like Chromium.
            if (std::string_view(attr) == "font-size") {
                const char* b = value.c_str();
                char* e = nullptr;
                std::strtod(b, &e);
                if (e != b && *e == '\0') value += "px";
            }
            presDecls.push_back({std::string(attr), std::move(value), false});
        }
        if (isSvgTextTag) {
            for (const char* attr : kSvgTextPresAttrs) {
                std::string_view v = elem.getAttribute(attr);
                if (v.empty()) continue;
                presDecls.push_back({std::string(attr), std::string(v), false});
            }
        }
        for (auto& decl : presDecls) {
            matched.push_back({
                &decl.property, &decl.value, /*important=*/false,
                /*specificity=*/0, /*order=*/0, /*isInline=*/false,
                /*layerOrder=*/-1, Origin::Author
            });
        }
    }

    // 1a. Candidate rules: everything in the buckets this element's id, classes
    //     and tag name can reach, plus the rules that require no name at all.
    //     Sorted back into source order so `matched` is built exactly as a scan
    //     of every rule would have built it — the stable_sort below leans on it.
    std::vector<size_t> candidates = universalRules_;
    auto addBucket = [&](const RuleBuckets& buckets, std::string_view key) {
        if (key.empty() || buckets.empty()) return;
        auto it = buckets.find(key);
        if (it == buckets.end()) return;
        candidates.insert(candidates.end(), it->second.begin(), it->second.end());
    };
    addBucket(idRules_, elem.id());
    if (!classRules_.empty()) {
        std::string_view classes = elem.className();
        size_t pos = 0;
        while (pos < classes.size()) {
            size_t start = classes.find_first_not_of(" \t\n\r\f", pos);
            if (start == std::string_view::npos) break;
            size_t end = classes.find_first_of(" \t\n\r\f", start);
            if (end == std::string_view::npos) end = classes.size();
            addBucket(classRules_, classes.substr(start, end - start));
            pos = end;
        }
    }
    addBucket(tagRules_, toLowerKey(elem.tagName()));
    std::sort(candidates.begin(), candidates.end());

    for (size_t ruleIdx : candidates) {
        const auto& rule = rules_[ruleIdx];
        // Container query check: if the rule has a container condition, evaluate it
        if (!rule.containerCondition.empty()) {
            if (!evaluateContainerQuery(elem, rule.containerName, rule.containerCondition)) {
                continue;
            }
        }

        // Use pre-classified selector type flags (set at insertion time)
        bool isHostSelector = rule.isHostSelector;
        bool isSlottedSelector = rule.isSlottedSelector;
        bool isPartSelector = rule.isPartSelector;

        if (isHostSelector) {
            // :host rules are scoped to a shadow root. They match the host element
            // whose shadowRoot() equals the rule's scope.
            if (rule.scope == nullptr) continue;  // :host must be in a shadow stylesheet
            // Simple :host (no descendants): elem IS the host
            // :host with descendants (e.g. :host([attr]) .child): elem is inside the shadow tree
            bool hasDescendant = rule.selector.chain.entries.size() > 1;
            if (hasDescendant) {
                if (elem.scope() != rule.scope) continue;
            } else {
                if (elem.shadowRoot() != rule.scope) continue;
            }
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
            std::string_view partName = elem.partName();
            if (partName.empty()) continue;
        } else {
            // Scope check: null-scope rules (UA defaults) apply everywhere;
            // non-null-scope rules only match elements in their shadow root
            if (rule.scope != nullptr && rule.scope != elem.scope()) continue;
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
                    std::string elemParts(elem.partName());
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

        // Add all declarations from this rule (by pointer, no string copies)
        for (auto& decl : rule.declarations) {
            matched.push_back({
                &decl.property, &decl.value, decl.important,
                rule.selector.specificity, rule.order, false, rule.layerOrder,
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
        [](const MatchedDecl& a, const MatchedDecl& b) {
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
        });

    // 4. Apply declarations in sorted order (last wins per property)
    //    Expand shorthands into longhands before applying.
    //    Build uaStyle in the same pass to avoid double expandShorthand.
    ComputedStyle style;
    ComputedStyle uaStyle;
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
    std::unordered_set<std::string> substitutedProps;
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
        auto expanded = expandShorthand(*m.property, *valPtr);
        for (auto& e : expanded) {
            style[e.property] = e.value;
            if (wasSubstituted) substitutedProps.insert(e.property);
            else substitutedProps.erase(e.property);
            if (m.origin == Origin::UserAgent) {
                uaStyle[e.property] = e.value;
            }
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

    // 6. Inherit custom properties (--*) from parent if not explicitly set
    if (parentStyle) {
        for (auto& [prop, val] : *parentStyle) {
            if (isCustomProperty(prop) && style.find(prop) == style.end()) {
                style[prop] = val;
            }
        }
    }

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
        else if (substitutedProps.count(prop) == 0) continue;

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
    {
        // Absent key = initial value (non-inherited defaults are not stored in
        // the map — see the note below).
        auto own = [&](const char* k, const char* dflt) -> std::string {
            auto it = style.find(k);
            return (it != style.end()) ? it->second : std::string(dflt);
        };

        bool blockify = false;

        if (parentStyle) {
            auto pd = parentStyle->find("display");
            if (pd != parentStyle->end() &&
                (pd->second == "flex" || pd->second == "inline-flex" ||
                 pd->second == "grid" || pd->second == "inline-grid"))
                blockify = true;  // (a)
        }
        if (!blockify) {
            const std::string pos = own("position", "static");
            if (pos == "absolute" || pos == "fixed") blockify = true;  // (b)
        }
        if (!blockify) {
            const std::string fl = own("float", "none");
            if (fl == "left" || fl == "right" || fl == "inline-start" ||
                fl == "inline-end") blockify = true;  // (c)
        }
        if (blockify) {
            const std::string d = own("display", "inline");
            if (d == "inline" || d == "inline-block") style["display"] = "block";
            else if (d == "inline-table") style["display"] = "table";
            else if (d == "inline-flex") style["display"] = "flex";
            else if (d == "inline-grid") style["display"] = "grid";
        }
    }

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
    // Collect rules whose selector targets ::pseudoName on this element
    struct MatchedDecl {
        std::string property;
        std::string value;
        bool important;
        uint32_t specificity;
        size_t order;
    };

    std::vector<MatchedDecl> matched;

    // Only rules whose subject targets ::pseudoName can contribute. They were
    // bucketed at insertion time, so a page with no ::before rule pays nothing
    // here — the alternative, rescanning every rule in the sheet for every
    // element on every restyle, is what made a hover over a long list crawl.
    auto bucket = pseudoRules_.find(pseudoName);
    if (bucket == pseudoRules_.end()) return {};

    for (size_t ruleIdx : bucket->second) {
        auto& rule = rules_[ruleIdx];
        if (rule.scope != nullptr && rule.scope != elem.scope()) continue;

        auto& chain = rule.selector.chain;
        auto& subject = chain.entries[0].compound;

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

    return style;
}

void Cascade::clear() {
    rules_.clear();
    keyframes_.clear();
    fontFaces_.clear();
    pseudoRules_.clear();
    idRules_.clear();
    classRules_.clear();
    tagRules_.clear();
    universalRules_.clear();
    hoverSrcIds_.clear();
    hoverSrcClasses_.clear();
    hoverSrcTags_.clear();
    hoverDescIds_.clear();
    hoverDescClasses_.clear();
    hoverDescTags_.clear();
    hoverSrcUniversal_ = false;
    hoverDescUniversal_ = false;
    hoverSiblings_ = false;
    nextOrder_ = 0;
    usesHover_ = false;
    usesContainers_ = false;
    usesForcedInherit_ = false;
    usesHas_ = false;
    ancestorClasses_.clear();
    layerNames_.clear();
    loadedImports_.clear();
}

} // namespace htmlayout::css
