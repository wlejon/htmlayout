#include "css/cascade.h"
#include "css/container_query.h"
#include "css/properties.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace htmlayout::css {

int Cascade::getOrCreateLayerIndex(const std::string& name) {
    for (int i = 0; i < static_cast<int>(layerNames_.size()); i++) {
        if (layerNames_[i] == name) return i;
    }
    // A sublayer's parent exists first: `@layer a.b` declares `a` too.
    auto dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) getOrCreateLayerIndex(name.substr(0, dot));
    layerNames_.push_back(name);
    rankLayers();
    return static_cast<int>(layerNames_.size()) - 1;
}

void Cascade::rankLayers() {
    const int n = static_cast<int>(layerNames_.size());
    auto parentOf = [&](int i) -> int {
        auto dot = layerNames_[i].rfind('.');
        if (dot == std::string::npos || dot == 0) return -1;
        const std::string parent = layerNames_[i].substr(0, dot);
        for (int j = 0; j < n; j++)
            if (layerNames_[j] == parent) return j;
        return -1;
    };
    std::vector<std::vector<int>> children(static_cast<size_t>(n) + 1);  // [n] = the root
    for (int i = 0; i < n; i++) {
        int p = parentOf(i);
        children[p < 0 ? static_cast<size_t>(n) : static_cast<size_t>(p)].push_back(i);
    }
    layerRanks_.assign(static_cast<size_t>(n), 0);
    int next = 0;
    // Post-order, iteratively: {node, next child to visit}.
    std::vector<std::pair<int, size_t>> stack{{n, 0}};
    while (!stack.empty()) {
        auto& [node, child] = stack.back();
        auto& kids = children[static_cast<size_t>(node)];
        if (child < kids.size()) {
            int c = kids[child++];
            stack.push_back({c, 0});
            continue;
        }
        if (node < n) layerRanks_[static_cast<size_t>(node)] = next++;
        stack.pop_back();
    }
}

bool Cascade::evaluateContainerQuery(const ElementRef& elem, const ContainerQuery& query,
                                      bool fromSelf, const ComputedStyle* firstStyle) const {
    // An invalid condition never matches.
    const ContainerCondition* cond = query.parsed.get();
    if (!cond) return false;

    // The query container is the nearest ancestor (the element itself for a
    // pseudo-element's originating element) that carries the query's name, if
    // it names one, and can answer every feature it tests: size features need
    // a size container (container-type size / inline-size); style features
    // any element, every element being a style container (css-contain-3 §2).
    const ElementRef* current = fromSelf ? &elem : elem.parent();
    bool first = true;
    for (; current; current = current->parent(), first = false) {
        std::string_view cType = current->containerType();
        const bool sizeContainer = cType == "size" || cType == "inline-size";
        if (cond->usesSize() && !sizeContainer) continue;
        if (!query.name.empty()) {
            std::istringstream iss{std::string(current->containerName())};
            bool nameMatch = false;
            std::string n;
            while (iss >> n) {
                if (n == query.name) { nameMatch = true; break; }
            }
            if (!nameMatch) continue;
        }

        ContainerQueryEnv env;
        env.hasInlineAxis = sizeContainer;
        env.hasBlockAxis = cType == "size";
        env.inlineSize = current->containerInlineSize();
        env.blockSize = current->containerBlockSize();
        const ComputedStyle* style = first ? firstStyle : nullptr;
        env.styleValue = [current, style](std::string_view prop, std::string& out) {
            if (style) {
                // A custom property as the container sees it: its own or
                // inherited (the map alone holds only its own).
                const std::string* v = nullptr;
                if (ComputedStyle::isCustom(prop)) {
                    v = style->customProperty(prop);
                } else if (auto it = style->find(prop); it != style->end()) {
                    v = &it->second;
                }
                out = v ? *v : std::string();
                return true;
            }
            out.clear();
            return current->computedStyleValue(prop, out);
        };
        return cond->matches(env);
    }
    return false;  // no eligible container: the query is unknown
}

void Cascade::setImportResolver(ImportResolver resolver) {
    if (!resolver) { importResolver_ = nullptr; return; }
    importResolver_ = [r = std::move(resolver)](const std::string& url, const std::string&,
                                                std::string&) { return r(url); };
}

void Cascade::setImportResolver(BasedImportResolver resolver) {
    importResolver_ = std::move(resolver);
}

void Cascade::addStylesheet(const Stylesheet& sheet, void* scope,
                             const MediaContext* media, Origin origin) {
    ++generation_;
    // Process @import rules first (imported rules precede this sheet in source order)
    if (importResolver_) {
        for (auto& imp : sheet.imports) {
            // The same relative URL from sheets in different directories
            // names different files, so the base is part of the key.
            std::string importKey = sheet.baseUrl.empty() ? imp.url
                                                          : sheet.baseUrl + '\n' + imp.url;
            if (loadedImports_.count(importKey)) continue;
            loadedImports_.insert(std::move(importKey));

            // Check media condition on the import
            if (!imp.mediaCondition.empty() && media) {
                if (!evaluateMediaQuery(imp.mediaCondition, *media)) continue;
            }

            std::string importedBase;
            std::string css = importResolver_(imp.url, sheet.baseUrl, importedBase);
            if (css.empty()) continue;

            Stylesheet imported = parse(css);
            imported.baseUrl = std::move(importedBase);

            // If import specifies a layer, wrap all imported rules in that
            // layer; a bare `layer` is an anonymous one, which the parser
            // named uniquely, as it does anonymous @layer blocks.
            if (imp.layered) {
                // The sheet's own layers, anonymous ones included, become
                // sublayers of the import's: `@layer b` inside
                // `@import ... layer(a)` is `a.b`.
                auto qualify = [&](std::string& name) {
                    if (!imp.layer.empty() && !name.empty()) name = imp.layer + "." + name;
                };
                for (auto& lb : imported.layerBlocks) qualify(lb.name);
                for (auto& n : imported.layerOrder) qualify(n);
                for (auto& cb : imported.containerBlocks)
                    if (cb.layered) qualify(cb.layer);
                LayerBlock layerBlock;
                layerBlock.name = imp.layer;
                layerBlock.rules = std::move(imported.rules);
                layerBlock.mediaBlocks = std::move(imported.mediaBlocks);
                imported.rules.clear();
                imported.mediaBlocks.clear();
                imported.layerBlocks.push_back(std::move(layerBlock));
                // Unlayered container queries join the import's layer too.
                for (auto& cb : imported.containerBlocks) {
                    if (cb.layered) continue;
                    cb.layered = true;
                    cb.layer = imp.layer;
                }
            }

            addStylesheet(imported, scope, media, origin);
        }
    }

    // Store @font-face rules
    for (auto& ff : sheet.fontFaces) {
        fontFaces_.push_back(ff);
        // src resolves against the declaring sheet, not the document.
        if (fontFaces_.back().baseUrl.empty()) fontFaces_.back().baseUrl = sheet.baseUrl;
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

    // With no media context every @media block applies (permissive).
    auto mediaMatches = [media](const MediaBlock& block) {
        if (!media) return true;
        if (!evaluateMediaQuery(block.condition, *media)) return false;
        for (auto& cond : block.andConditions)
            if (!evaluateMediaQuery(cond, *media)) return false;
        return true;
    };
    // Every applicable rule — plain, @media, @layer, @container, in any
    // nesting — enters in one source order (Rule::sourcePos), so a later
    // plain rule beats an earlier @media or @container rule of the same
    // layer and specificity.
    struct Pending {
        const Rule* rule;
        int layerIdx;                    // -1 = unlayered
        const ContainerBlock* container; // null = no container query
    };
    std::vector<Pending> ordered;
    ordered.reserve(sheet.rules.size());
    auto addRules = [&](const std::vector<Rule>& rules, int layerIdx,
                        const ContainerBlock* cb) {
        for (auto& rule : rules) ordered.push_back({&rule, layerIdx, cb});
    };
    auto addMedia = [&](const std::vector<MediaBlock>& blocks, int layerIdx) {
        for (auto& block : blocks)
            if (mediaMatches(block)) addRules(block.rules, layerIdx, nullptr);
    };

    // Unconditional + @media rules
    addRules(sheet.rules, -1, nullptr);
    addMedia(sheet.mediaBlocks, -1);

    // @layer rules (and @media inside @layer)
    for (auto& layerBlock : sheet.layerBlocks) {
        int layerIdx = getOrCreateLayerIndex(layerBlock.name);
        addRules(layerBlock.rules, layerIdx, nullptr);
        addMedia(layerBlock.mediaBlocks, layerIdx);
    }

    // @container rules whose @media conditions match; the block may sit in
    // (or hold) an @layer, and carry enclosing container queries.
    for (auto& containerBlock : sheet.containerBlocks) {
        if (media) {
            bool ok = true;
            for (auto& cond : containerBlock.mediaConditions)
                if (!evaluateMediaQuery(cond, *media)) { ok = false; break; }
            if (!ok) continue;
        }
        if (!containerBlock.rules.empty()) usesContainers_ = true;
        const int layerIdx =
            containerBlock.layered ? getOrCreateLayerIndex(containerBlock.layer) : -1;
        addRules(containerBlock.rules, layerIdx, &containerBlock);
    }

    std::stable_sort(ordered.begin(), ordered.end(), [](const Pending& a, const Pending& b) {
        return a.rule->sourcePos < b.rule->sourcePos;
    });
    // Each block's query list, with every condition parsed, built once per
    // block rather than once per rule.
    std::unordered_map<const ContainerBlock*, std::vector<ContainerQuery>> blockQueries;
    auto queriesOf = [&](const ContainerBlock* cb) -> const std::vector<ContainerQuery>& {
        auto it = blockQueries.find(cb);
        if (it != blockQueries.end()) return it->second;
        std::vector<ContainerQuery> qs = cb->enclosing;
        qs.push_back({cb->name, cb->condition, cb->parsed});
        for (auto& q : qs)
            if (!q.parsed) q.parsed = ContainerCondition::parse(q.condition);
        return blockQueries.emplace(cb, std::move(qs)).first->second;
    };
    for (const Pending& p : ordered) {
        std::vector<ContainerQuery> queries;
        if (p.container) queries = queriesOf(p.container);
        auto selectors = parseSelectorList(p.rule->selector);
        for (auto& sel : selectors) {
            rules_.push_back({std::move(sel), p.rule->declarations, scope, nextOrder_++,
                              p.layerIdx, origin, queries});
            if (p.rule->startingStyle) {
                rules_.back().startingStyle = true;
                usesStartingStyle_ = true;
            }
            classifyLastRule();
        }
    }
}

namespace {

// The attributes the cascade reads as style input. SVG presentation attributes
// (fill="red", stroke-width="2", ...) are the lowest-priority style source;
// their names are their CSS property names.
constexpr std::string_view kSvgPresAttrs[] = {
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
// `direction` and `unicode-bidi` are presentation attributes only on SVG text
// content elements. Unlike the names above they collide with nothing in HTML
// *because* HTML spells the same idea `dir` — so seeding them unconditionally
// would invent a `<div direction="rtl">` that no browser honours.
constexpr std::string_view kSvgTextPresAttrs[] = {"direction", "unicode-bidi"};
constexpr std::string_view kSvgTextTags[] = {"text", "tspan", "textpath", "tref", "altglyph"};
// Presentation attributes whose names HTML also has as properties with an
// HTML meaning (`display`, `visibility`, `overflow`, `filter`, `mask`):
// seeded only on SVG elements, so a `<div display="none">` stays inert.
constexpr std::string_view kSvgElementPresAttrs[] = {
    "display", "visibility", "overflow", "filter", "mask", "mask-type",
};
constexpr std::string_view kSvgElementTags[] = {
    "svg", "g", "rect", "circle", "ellipse", "line", "polyline", "polygon", "path",
    "text", "tspan", "textpath", "use", "symbol", "switch", "foreignobject",
    "marker", "pattern", "mask", "clippath", "defs",
};

bool equalsLowerAscii(std::string_view s, std::string_view lower) {
    if (s.size() != lower.size()) return false;
    for (size_t i = 0; i < s.size(); i++)
        if (std::tolower(static_cast<unsigned char>(s[i])) != lower[i]) return false;
    return true;
}

bool isSvgTextTag(std::string_view tag) {
    for (std::string_view t : kSvgTextTags)
        if (equalsLowerAscii(tag, t)) return true;
    return false;
}

bool isSvgElementTag(std::string_view tag) {
    for (std::string_view t : kSvgElementTags)
        if (equalsLowerAscii(tag, t)) return true;
    return false;
}

bool isPresAttr(std::string_view name, bool svgText, bool svgElement) {
    for (std::string_view a : kSvgPresAttrs)
        if (a == name) return true;
    if (svgText)
        for (std::string_view a : kSvgTextPresAttrs)
            if (a == name) return true;
    if (svgElement)
        for (std::string_view a : kSvgElementPresAttrs)
            if (a == name) return true;
    return false;
}

bool isTableSpanTag(std::string_view tag) {
    return equalsLowerAscii(tag, "td") || equalsLowerAscii(tag, "th") ||
           equalsLowerAscii(tag, "col") || equalsLowerAscii(tag, "colgroup");
}

} // namespace

// The element's presentation attributes, name and value, in kSvgPresAttrs
// order (then the SVG text ones) — the order the per-name probe produced, so
// the cascade sees them exactly as before whichever way they were found.
void collectPresentationAttrs(const ElementRef& elem,
                              std::vector<std::pair<std::string_view, std::string_view>>& out) {
    const bool svgText = isSvgTextTag(elem.tagName());
    const bool svgElement = svgText || isSvgElementTag(elem.tagName());
    struct Ctx {
        std::vector<std::pair<std::string_view, std::string_view>>* out;
        bool svgText;
        bool svgElement;
    };
    Ctx ctx{&out, svgText, svgElement};
    const bool enumerated = elem.forEachAttribute(
        [](void* c, std::string_view name, std::string_view value) {
            auto* x = static_cast<Ctx*>(c);
            if (!value.empty() && isPresAttr(name, x->svgText, x->svgElement))
                x->out->push_back({name, value});
        },
        &ctx);
    if (enumerated) {
        if (out.size() > 1) {
            auto rank = [](std::string_view n) {
                size_t i = 0;
                for (std::string_view a : kSvgPresAttrs) { if (a == n) return i; ++i; }
                for (std::string_view a : kSvgTextPresAttrs) { if (a == n) return i; ++i; }
                for (std::string_view a : kSvgElementPresAttrs) { if (a == n) return i; ++i; }
                return i;
            };
            std::sort(out.begin(), out.end(),
                      [&](const auto& a, const auto& b) { return rank(a.first) < rank(b.first); });
        }
        return;
    }
    for (std::string_view attr : kSvgPresAttrs) {
        std::string_view v = elem.getAttribute(attr);
        if (!v.empty()) out.push_back({attr, v});
    }
    if (svgText) {
        for (std::string_view attr : kSvgTextPresAttrs) {
            std::string_view v = elem.getAttribute(attr);
            if (!v.empty()) out.push_back({attr, v});
        }
    }
    if (svgElement) {
        for (std::string_view attr : kSvgElementPresAttrs) {
            std::string_view v = elem.getAttribute(attr);
            if (!v.empty()) out.push_back({attr, v});
        }
    }
}

bool Cascade::readsElementAttributes(const ElementRef& elem) const {
    if (isTableSpanTag(elem.tagName())) return true;
    std::vector<std::pair<std::string_view, std::string_view>> pres;
    collectPresentationAttrs(elem, pres);
    return !pres.empty();
}

void Cascade::styleAttributes(const ElementRef& elem, std::string& out) const {
    out.clear();
    auto add = [&out](std::string_view name, std::string_view value) {
        out.append(name);
        out.push_back('=');
        out.append(value);
        out.push_back('\0');
    };
    if (isTableSpanTag(elem.tagName())) {
        for (std::string_view attr : {"colspan", "rowspan", "span"}) {
            std::string_view v = elem.getAttribute(attr);
            if (!v.empty()) add(attr, v);
        }
    }
    std::vector<std::pair<std::string_view, std::string_view>> pres;
    collectPresentationAttrs(elem, pres);
    for (const auto& [name, value] : pres) add(name, value);
}

bool Cascade::parentPropertyIsRead(std::string_view name) {
    return name == "display" || name == "-hl-font-size-keyword" || isInherited(name);
}

ComputedStyle Cascade::resolve(const ElementRef& elem,
                                const std::string& inlineStyle,
                                const ComputedStyle* parentStyle,
                                bool startingStyle) const {
    std::vector<uint32_t> rules;
    matchRules(elem, parentStyle, startingStyle, rules);
    return resolveMatched(elem, rules, inlineStyle, parentStyle);
}

void Cascade::matchRules(const ElementRef& elem, const ComputedStyle* parentStyle,
                         bool startingStyle, std::vector<uint32_t>& out) const {
    out.clear();
    // Candidate rules: everything in the buckets this element's id, classes
    // and tag name can reach, plus the rules that require no name at all.
    // Sorted back into source order so the matched list is exactly what a
    // scan of every rule would have built — resolveMatched()'s stable_sort
    // leans on it.
    std::vector<size_t> candidates;
    candidates.reserve(universalRules_.size() + 32);
    candidates.assign(universalRules_.begin(), universalRules_.end());
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
    if (!tagRules_.empty()) addBucket(tagRules_, toLowerKey(elem.tagName()));
    std::sort(candidates.begin(), candidates.end());
    // A class listed twice (`class="a a"`) reaches its bucket twice.
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    for (size_t ruleIdx : candidates) {
        const auto& rule = rules_[ruleIdx];
        if (rule.startingStyle && !startingStyle) continue;
        // Container queries (nested @container rules each add one).
        if (!rule.containerQueries.empty() &&
            !containerQueriesHold(elem, rule.containerQueries, /*fromSelf=*/false, parentStyle)) {
            continue;
        }
        if (ruleMatchesElement(rule, elem)) out.push_back(static_cast<uint32_t>(ruleIdx));
    }
}

bool Cascade::ruleMatchesElement(const ScopedRule& rule, const ElementRef& elem) const {
    if (rule.isHostSelector) {
        // :host rules are scoped to a shadow root. They match the host element
        // whose shadowRoot() equals the rule's scope.
        if (rule.scope == nullptr) return false;  // :host must be in a shadow stylesheet
        // Simple :host (no descendants): elem IS the host
        // :host with descendants (e.g. :host([attr]) .child): elem is inside the shadow tree
        bool hasDescendant = rule.selector.chain.entries.size() > 1;
        if (hasDescendant) {
            if (elem.scope() != rule.scope) return false;
        } else {
            if (elem.shadowRoot() != rule.scope) return false;
        }
        // The :host pseudo-class handles the rest.
        return rule.selector.matches(elem);
    }
    if (rule.isSlottedSelector) {
        // ::slotted rules are in the shadow scope. They match light DOM children
        // that are distributed into a slot inside that shadow tree.
        if (rule.scope == nullptr) return false;
        auto* slot = elem.assignedSlot();
        if (!slot || slot->scope() != rule.scope) return false;
        // Match the ::slotted() argument against the element
        for (auto& s : rule.selector.chain.entries[0].compound.simples) {
            if (s.type == SimpleSelectorType::PseudoElement && s.value == "slotted") {
                for (auto& inner : s.slottedArg)
                    if (!matchSimple(inner, elem)) return false;
                break;
            }
        }
        return true;
    }
    if (rule.isPartSelector) {
        // ::part rules are in the outer scope. They target elements inside a
        // shadow tree by their part name.
        if (elem.scope() == nullptr) return false;  // element must be in a shadow scope
        if (rule.scope != nullptr) return false;     // ::part rules come from outer/document scope
        std::string_view partName = elem.partName();
        if (partName.empty()) return false;
        // The element's part name list must contain the target part name.
        // (The ancestor part of a chain, "my-element::part(foo)", is not
        // matched against the host.)
        for (auto& s : rule.selector.chain.entries[0].compound.simples) {
            if (s.type == SimpleSelectorType::PseudoElement && s.value == "part") {
                std::string elemParts(partName);
                std::istringstream iss(elemParts);
                std::string p;
                while (iss >> p)
                    if (p == s.partArg) return true;
                return false;
            }
        }
        return false;
    }
    // Scope check: null-scope rules (UA defaults) apply everywhere;
    // non-null-scope rules only match elements in their shadow root
    if (rule.scope != nullptr && rule.scope != elem.scope()) return false;
    return rule.selector.matches(elem);
}

void Cascade::clear() {
    ++generation_;
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
    usesStartingStyle_ = false;
    usesForcedInherit_ = false;
    usesHas_ = false;
    hasSubjects_.clear();
    hasArgSiblings_ = false;
    usesStructuralQualifiers_ = false;
    usesFocusWithin_ = false;
    focusSrcIds_.clear();
    focusSrcClasses_.clear();
    focusSrcTags_.clear();
    focusSrcUniversal_ = false;
    focusSubjects_.clear();
    ancestorClasses_.clear();
    siblingClasses_.clear();
    usesSiblingQualifiers_ = false;
    layerNames_.clear();
    layerRanks_.clear();
    loadedImports_.clear();
}

} // namespace htmlayout::css
