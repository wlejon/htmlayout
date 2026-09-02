#pragma once
#include "css/parser.h"
#include "css/selector.h"
#include "css/properties.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace htmlayout::css {

// Computed style: the final resolved CSS properties for one element.
//
// Keyed heterogeneously (see SvHash/SvEq in properties.h): layout reads this map
// hundreds of thousands of times per pass, almost always with a string literal,
// and a plain std::string-keyed map would build — and for any name longer than
// the 15-char small-string limit, heap-allocate — a key on every single read.
using StyleMap = std::unordered_map<std::string, std::string, SvHash, SvEq>;

// The map holds what the element itself computed: the properties its rules set
// and the ordinary inherited values copied from its parent. Custom properties
// (`--*`) are the exception. A design-token sheet declares a hundred of them on
// :root and every element inherits all of them, and copying that set into every
// element's map was the single largest cost of resolving a style — a hundred
// string insertions, and a hundred destructions when the style was replaced,
// per element, for values that are identical down the whole tree. So inherited
// custom properties are not in the map. They are a flat set shared by pointer
// (`inheritedVars`): an element that declares none hands its children the very
// pointer it was handed, and only an element that declares some builds a new
// set (its inherited set plus its own) — once, for all of its children.
//
// Two consequences for readers. `find("--x")` on the map answers only for an
// element's OWN declarations; the value an element actually sees is
// `customProperty("--x")`. And a change to an ancestor's custom properties
// shows up on a descendant as a different `inheritedVars` pointer, which is
// what a consumer's restyle scoping compares (the pointer is kept stable when
// the set's contents do not change — see stableChildVarsFrom).
class ComputedStyle : public StyleMap {
public:
    using StyleMap::StyleMap;
    ComputedStyle() = default;
    ComputedStyle(const StyleMap& m) : StyleMap(m) {}
    ComputedStyle(StyleMap&& m) : StyleMap(std::move(m)) {}

    // Custom properties inherited from ancestors (never this element's own).
    std::shared_ptr<const StyleMap> inheritedVars;

    static bool isCustom(std::string_view name) {
        return name.size() >= 2 && name[0] == '-' && name[1] == '-';
    }

    // The custom property `name` as this element sees it: its own declaration
    // first, then the inherited set. Null when neither defines it.
    const std::string* customProperty(std::string_view name) const {
        auto it = find(name);
        if (it != end()) return &it->second;
        if (inheritedVars) {
            auto vit = inheritedVars->find(name);
            if (vit != inheritedVars->end()) return &vit->second;
        }
        return nullptr;
    }

    // The set this element's children inherit: its own custom properties laid
    // over the set it inherited. Built on first use and cached, so a parent
    // with a hundred tokens builds the set once rather than per child; a parent
    // with no custom properties of its own passes its inherited pointer through.
    std::shared_ptr<const StyleMap> varsForChildren() const {
        if (childVarsBuilt_) return childVars_;
        childVarsBuilt_ = true;
        bool own = false;
        for (auto& kv : *this) if (isCustom(kv.first)) { own = true; break; }
        if (!own) { childVars_ = inheritedVars; return childVars_; }
        auto m = std::make_shared<StyleMap>();
        if (inheritedVars) *m = *inheritedVars;
        for (auto& kv : *this) if (isCustom(kv.first)) (*m)[kv.first] = kv.second;
        childVars_ = m;
        return childVars_;
    }

    // Keep the children's pointer stable across a re-resolve: if the set this
    // style would hand down equals the one `previous` (the style it replaces)
    // handed down, adopt that pointer, so descendants comparing pointers see
    // no change when nothing changed.
    void stableChildVarsFrom(const ComputedStyle& previous) {
        if (!previous.childVarsBuilt_) return;
        auto mine = varsForChildren();
        auto theirs = previous.childVars_;
        if (mine == theirs) return;
        if (mine && theirs && *mine == *theirs) childVars_ = theirs;
    }

private:
    mutable std::shared_ptr<const StyleMap> childVars_;
    mutable bool childVarsBuilt_ = false;
};

// Stylesheet origin for the cascade
enum class Origin { UserAgent, Author };

// Callback to resolve @import URLs. Returns the CSS text for the given URL.
// The consumer is responsible for file I/O, URL resolution, etc.
using ImportResolver = std::function<std::string(const std::string& url)>;

// A cascade context resolves which CSS rules apply to which elements.
// It supports scoping for shadow DOM: rules in a shadow scope only match
// elements in that same scope.
class Cascade {
    // Rule indices, keyed by something an element must have to match them.
    // Heterogeneous (SvHash/SvEq): an element's classes and tag name are
    // string_views into the DOM, and a bucket probe must not allocate a
    // std::string per class per element to ask.
    using RuleBuckets =
        std::unordered_map<std::string, std::vector<size_t>, SvHash, SvEq>;
    using KeySet = std::unordered_set<std::string, SvHash, SvEq>;

public:
    // Set a callback to resolve @import URLs into CSS text.
    // When set, addStylesheet() will automatically resolve and inline imports.
    // Each URL is resolved at most once (cached by URL string).
    void setImportResolver(ImportResolver resolver);

    // Add a stylesheet to a given scope.
    // scope = nullptr means document-level (global).
    // scope = shadow_root_ptr means shadow-scoped styles.
    // If a MediaContext is provided, @media blocks are conditionally included.
    // origin distinguishes UA from author styles (used by the `revert` keyword).
    void addStylesheet(const Stylesheet& sheet, void* scope = nullptr,
                       const MediaContext* media = nullptr,
                       Origin origin = Origin::Author);

    // Resolve computed style for an element.
    // Considers: author styles, inline styles, inheritance, initial values.
    // Only matches rules whose scope matches the element's scope.
    // parentStyle: the computed style of the parent element (for inheritance).
    //   Pass nullptr for root elements.
    ComputedStyle resolve(const ElementRef& elem,
                          const std::string& inlineStyle = {},
                          const ComputedStyle* parentStyle = nullptr) const;

    // Resolve computed style for a pseudo-element (::before or ::after).
    // Returns an empty style if no rules target this pseudo-element.
    // The "content" property determines what text to generate.
    // elemStyle is the computed style of the originating element (for inheritance).
    ComputedStyle resolvePseudo(const ElementRef& elem,
                                const std::string& pseudoName,
                                const ComputedStyle& elemStyle) const;

    // Access stored @keyframes rules (populated by addStylesheet)
    const std::vector<KeyframeBlock>& keyframes() const { return keyframes_; }

    // Access stored @font-face rules (populated by addStylesheet)
    const std::vector<FontFaceRule>& fontFaces() const { return fontFaces_; }

    // True if any added rule uses the :hover pseudo-class anywhere in its
    // selector (including nested inside :not()/:is()/:where()/:has()).
    // Lets the consumer skip all hover-driven restyle work — a hover-target
    // change cannot alter computed style on a page with no :hover rules.
    bool usesHoverPseudo() const { return usesHover_; }

    // When the pointer moves, :hover flips on one chain of elements and the
    // consumer re-resolves them. Which OTHER elements around them can now match
    // different rules? Only those named as the subject of a rule that mentions
    // :hover in a non-subject compound (`.row:hover .label`, `.tab:hover + .x`)
    // — for every other element in the neighbourhood the rule set is exactly
    // what it was, so its computed style cannot have changed and it does not
    // have to re-resolve. Ask this per element instead of re-resolving the
    // whole subtree under the hovered element, which on a big app is thousands
    // of selector matches per mouse move.
    //
    // `classList` is the raw class attribute (space-separated). Returns true
    // conservatively when a hover-descendant rule's subject names nothing this
    // can key on (`.row:hover > *`, `.row:hover [data-x]`) or when the sheet
    // uses :has(), whose matching runs the other way.
    // Can a :hover flip re-match an element OUTSIDE the hovered element's own
    // subtree? Only through a sibling combinator hanging directly off the
    // :hover compound (`.tab:hover + .panel`, `.tab:hover ~ .panel p`) — with a
    // descendant or child combinator there (`.row:hover .label`) every element
    // the rule can reach is under the hovered element. Lets the consumer scope
    // the hover re-match to the flipped elements' subtrees, instead of having to
    // include their siblings' subtrees too.
    bool hoverAffectsSiblings() const { return hoverSiblings_ || usesHas_; }

    // The other half of the question: is THIS element one that a hover-descendant
    // rule names on the :hover side (`.row:hover .label` → the `.row`)? A flip on
    // anything else — the container the pointer crossed on its way, the gap
    // between two rows — re-matches nothing below it, so it opens no scope at
    // all. Without this the mere act of hovering a big container would re-resolve
    // every element under it that any hover rule anywhere names.
    bool hoverInvalidatesDescendants(std::string_view tag, std::string_view id,
                                     std::string_view classList) const {
        if (!usesHover_) return false;
        if (hoverSrcUniversal_ || usesHas_) return true;
        return keysHit(hoverSrcIds_, hoverSrcClasses_, hoverSrcTags_,
                       tag, id, classList);
    }

    bool hoverCanAffect(std::string_view tag, std::string_view id,
                        std::string_view classList) const {
        if (!usesHover_) return false;
        if (hoverDescUniversal_ || usesHas_) return true;
        return keysHit(hoverDescIds_, hoverDescClasses_, hoverDescTags_,
                       tag, id, classList);
    }

    // True if any @container rule was added. Container queries read the
    // container's laid-out size, which trails style resolution by one layout
    // pass — consumers should re-resolve styles once after layout when this
    // is set.
    bool usesContainerQueries() const { return usesContainers_; }

    // True if any rule forces `inherit` on a property that does not normally
    // inherit. Such a declaration ties a child's computed value to a parent
    // property that is not part of the inherited set, so a consumer scoping its
    // restyle by an inherited-value diff must re-resolve the whole subtree
    // instead. See classifyLastRule().
    bool usesForcedInherit() const { return usesForcedInherit_; }

    // Can adding or removing this class from an element change which rules match
    // that element's DESCENDANTS? Only if the class appears in a non-subject
    // compound of some selector (`.dark .btn`, `.open > li`) — a class that is
    // only ever a subject qualifier (`.btn.active`) restyles just the element it
    // is on. Lets a consumer keep `panel.classList.toggle('on')` off the whole
    // subtree's restyle bill, which is what makes it affordable per frame.
    //
    // Conservative when the sheet uses :has(), whose matching runs the other way
    // (a class on a descendant decides an ancestor's match), so no subject/
    // ancestor split of the class names describes it.
    bool classAffectsDescendants(const std::string& cls) const {
        return usesHas_ || ancestorClasses_.count(cls) > 0;
    }

    // True if any rule targets ::<name> (e.g. "before", "after"). A document
    // with no such rule can generate no content, so the consumer can skip the
    // whole generated-content pass instead of asking every element.
    bool hasPseudoElementRules(const std::string& name) const {
        auto it = pseudoRules_.find(name);
        return it != pseudoRules_.end() && !it->second.empty();
    }

    // Does a `content` value depend on state that accumulates across the whole
    // document — a counter scope, or the quote-nesting depth — rather than only
    // on its originating element? These are the only generated-content features
    // that force a document-order pass over every element; anything else (a
    // literal, attr(), a url()) a consumer can resolve for one element alone.
    //
    // Test the value a pseudo-element actually RESOLVED, not the stylesheet: a
    // UA sheet carries `q::before { content: open-quote }`, so "does any rule
    // use quotes" is true for every document ever and gates nothing.
    static bool contentIsStateful(const std::string& v) {
        return v.find("counter(") != std::string::npos ||
               v.find("counters(") != std::string::npos ||
               v.find("open-quote") != std::string::npos ||   // also no-open-quote
               v.find("close-quote") != std::string::npos;    // also no-close-quote
    }

    // Clear all stylesheets
    void clear();

private:
    struct ScopedRule {
        Selector selector;
        std::vector<Declaration> declarations;
        void* scope = nullptr;  // which shadow root, or nullptr for global
        size_t order = 0;       // insertion order for stable sort
        int layerOrder = -1;    // -1 = unlayered (highest priority), >=0 = layer index
        Origin origin = Origin::Author;
        // Container query: if non-empty, this rule only applies when the container condition is met
        std::string containerName;     // required container name (empty = any)
        std::string containerCondition; // e.g. "(min-width: 400px)"
        // Pre-classified selector type (avoids scanning simples per-element)
        bool isHostSelector = false;
        bool isSlottedSelector = false;
        bool isPartSelector = false;
        // For a rule targeting ::before/::after: the selector with the
        // pseudo-element stripped from its subject, built once here so that
        // resolvePseudo() matches it directly instead of rebuilding it per
        // element, and whether the rule sets `content` — the one property that
        // decides if the pseudo generates a box at all.
        Selector pseudoSelector;
        bool declaresContent = false;
    };
    // Does this simple selector (or anything nested in its :not()/:is()/
    // :where()/:has()/:host() args) use the :hover pseudo-class?
    static bool simpleMentionsHover(const SimpleSelector& s) {
        if (s.type == SimpleSelectorType::PseudoClass && s.value == "hover")
            return true;
        for (auto& n : s.notArg)     if (simpleMentionsHover(n)) return true;
        for (auto& n : s.hostArg)    if (simpleMentionsHover(n)) return true;
        for (auto& n : s.slottedArg) if (simpleMentionsHover(n)) return true;
        for (auto& c : s.selectorListArg)
            for (auto& sub : c.simples) if (simpleMentionsHover(sub)) return true;
        for (auto& rel : s.relativeArgs)
            for (auto& entry : rel.chain.entries)
                for (auto& sub : entry.compound.simples)
                    if (simpleMentionsHover(sub)) return true;
        return false;
    }

    // Collect every class name mentioned by this simple selector, including the
    // ones nested inside :not()/:is()/:where()/:has()/:host()/::slotted() args.
    static void collectClasses(const SimpleSelector& s,
                               std::unordered_set<std::string>& out) {
        if (s.type == SimpleSelectorType::Class) out.insert(s.value);
        for (auto& n : s.notArg)     collectClasses(n, out);
        for (auto& n : s.hostArg)    collectClasses(n, out);
        for (auto& n : s.slottedArg) collectClasses(n, out);
        for (auto& c : s.selectorListArg)
            for (auto& sub : c.simples) collectClasses(sub, out);
        for (auto& rel : s.relativeArgs)
            for (auto& entry : rel.chain.entries)
                for (auto& sub : entry.compound.simples) collectClasses(sub, out);
    }

    static bool simpleUsesHas(const SimpleSelector& s) {
        if (!s.relativeArgs.empty()) return true;
        for (auto& n : s.notArg)     if (simpleUsesHas(n)) return true;
        for (auto& n : s.hostArg)    if (simpleUsesHas(n)) return true;
        for (auto& n : s.slottedArg) if (simpleUsesHas(n)) return true;
        for (auto& c : s.selectorListArg)
            for (auto& sub : c.simples) if (simpleUsesHas(sub)) return true;
        return false;
    }

    // Classify :host/:slotted/::part flags after inserting a rule
    void classifyLastRule() {
        auto& rule = rules_.back();

        // entries[0] is the subject; entries[1..] are the ancestor and sibling
        // qualifiers. A class named there is one whose presence on some OTHER
        // element decides this rule's match, so changing it has to re-match the
        // subtree below. A class named only in entries[0] does not.
        for (size_t i = 1; i < rule.selector.chain.entries.size(); i++)
            for (auto& s : rule.selector.chain.entries[i].compound.simples)
                collectClasses(s, ancestorClasses_);
        if (!usesHas_) {
            for (auto& entry : rule.selector.chain.entries) {
                for (auto& s : entry.compound.simples) {
                    if (simpleUsesHas(s)) { usesHas_ = true; break; }
                }
                if (usesHas_) break;
            }
        }
        // `inherit` on a property that does NOT normally inherit (border,
        // background, width…) makes that child's computed value depend on its
        // parent's value for a property the parent never passes down. A consumer
        // that decides whether to re-resolve a subtree by diffing the parent's
        // *inherited* values would miss it, so flag the sheet and let them fall
        // back. `color: inherit` and friends need no flag — the inherited diff
        // already sees those.
        // Expand first: `font: inherit` is five longhands that all inherit
        // anyway, and the shorthand name itself is not in the property table.
        if (!usesForcedInherit_) {
            for (auto& d : rule.declarations) {
                if (d.value != "inherit") continue;
                for (auto& e : expandShorthand(d.property, d.value)) {
                    if (!isInherited(e.property)) { usesForcedInherit_ = true; break; }
                }
                if (usesForcedInherit_) break;
            }
        }
        // Track whether any rule anywhere uses :hover (across every compound in
        // the chain, not just the subject) so the consumer can skip hover
        // restyle work on pages with no :hover rules.
        if (!usesHover_) {
            for (auto& entry : rule.selector.chain.entries) {
                for (auto& s : entry.compound.simples) {
                    if (simpleMentionsHover(s)) { usesHover_ = true; break; }
                }
                if (usesHover_) break;
            }
        }
        if (rule.selector.chain.entries.empty()) return;
        size_t idx = rules_.size() - 1;
        const auto& subject = rule.selector.chain.entries[0].compound;
        bool isPseudoElementRule = false;
        for (auto& s : subject.simples) {
            if (s.type == SimpleSelectorType::PseudoClass &&
                (s.value == "host" || s.value == "host-context")) {
                rule.isHostSelector = true;
            } else if (s.type == SimpleSelectorType::PseudoElement && s.value == "slotted") {
                rule.isSlottedSelector = true;
            } else if (s.type == SimpleSelectorType::PseudoElement && s.value == "part") {
                rule.isPartSelector = true;
            } else if (s.type == SimpleSelectorType::PseudoElement) {
                // Bucket by pseudo-element name so resolvePseudo() considers only
                // the handful of rules that could target it, rather than rescanning
                // every rule in the sheet (UA sheet included) once per element.
                pseudoRules_[s.value].push_back(idx);
                isPseudoElementRule = true;
            }
        }
        if (isPseudoElementRule) {
            // The originating element is what the rest of the selector describes:
            // `.brackets::before` is `.brackets`, `*::after` is `*`. Strip the
            // pseudo-element from the subject once; an empty subject compound
            // matches everything, which is what `::before` alone means.
            rule.pseudoSelector = rule.selector;
            auto& simples = rule.pseudoSelector.chain.entries[0].compound.simples;
            simples.erase(std::remove_if(simples.begin(), simples.end(),
                                         [](const SimpleSelector& s) {
                                             return s.type == SimpleSelectorType::PseudoElement;
                                         }),
                          simples.end());
            for (auto& d : rule.declarations)
                if (d.property == "content") { rule.declaresContent = true; break; }
        }
        const SimpleSelector *idSimple, *classSimple, *tagSimple;
        compoundKey(subject, idSimple, classSimple, tagSimple);

        // A :hover in a non-subject compound (`.row:hover .label`) makes this
        // rule's subject re-match when the hover of the element that compound
        // names flips. Record BOTH sides: what the hovered element must be
        // (hoverSrc*, so a flip on anything else opens no scope) and what the
        // subject must be (hoverDesc*, so only elements that can be it re-resolve
        // inside that scope).
        for (size_t i = 1; i < rule.selector.chain.entries.size(); i++) {
            auto& entry = rule.selector.chain.entries[i];
            bool mentions = false;
            for (auto& s : entry.compound.simples)
                if (simpleMentionsHover(s)) { mentions = true; break; }
            if (!mentions) continue;
            addKey(entry.compound, hoverSrcIds_, hoverSrcClasses_, hoverSrcTags_,
                   hoverSrcUniversal_);
            addKey(subject, hoverDescIds_, hoverDescClasses_, hoverDescTags_,
                   hoverDescUniversal_);
            // entry.combinator is what connects this compound toward the
            // subject. A sibling one steps out of the hovered element's subtree;
            // a descendant or child one cannot (a sibling of a descendant is
            // still a descendant), and neither can anything further right.
            if (entry.combinator == Combinator::AdjacentSibling ||
                entry.combinator == Combinator::GeneralSibling)
                hoverSiblings_ = true;
        }

        // Index the rule by one simple selector its subject compound REQUIRES —
        // an id, else a class, else a tag name. An element that lacks that id /
        // class / tag cannot match the rule, so resolve() only has to consider
        // the buckets its own name and attributes reach, plus the rules that
        // require nothing (`*`, `[attr]`, `:hover` alone). Without this every
        // element matched against every rule in the sheet: with a 300-rule app
        // sheet that is ~300 selector matches (each lowercasing the tag name,
        // each re-splitting the class list) per element per restyle, and a
        // hover that re-resolves a subtree pays it for the whole subtree.
        //
        // Only positive top-level simples index: a class named inside :not() or
        // :is() is not required, and host/slotted/part rules match on channels
        // (shadow scope, part name) that have nothing to do with the subject's
        // tag, so they all stay in the always-considered bucket.
        if (rule.isHostSelector || rule.isSlottedSelector || rule.isPartSelector)
            universalRules_.push_back(idx);
        else if (idSimple)    idRules_[idSimple->value].push_back(idx);
        else if (classSimple) classRules_[classSimple->value].push_back(idx);
        else if (tagSimple)   tagRules_[toLowerKey(tagSimple->value)].push_back(idx);
        else                  universalRules_.push_back(idx);
    }

    // Does the element's id / tag / class list hit any of these key sets?
    bool keysHit(const KeySet& ids, const KeySet& classes, const KeySet& tags,
                 std::string_view tag, std::string_view id,
                 std::string_view classList) const {
        if (!id.empty() && !ids.empty() && ids.count(id)) return true;
        if (!tags.empty() && tags.count(toLowerKey(tag))) return true;
        if (classes.empty()) return false;
        size_t pos = 0;
        while (pos < classList.size()) {
            size_t start = classList.find_first_not_of(" \t\n\r\f", pos);
            if (start == std::string_view::npos) break;
            size_t end = classList.find_first_of(" \t\n\r\f", start);
            if (end == std::string_view::npos) end = classList.size();
            if (classes.count(classList.substr(start, end - start))) return true;
            pos = end;
        }
        return false;
    }

    // The one simple selector a compound REQUIRES of an element, most selective
    // first: an id, else a class, else a tag. Only positive top-level simples —
    // a name inside :not() / :is() is not required.
    static void compoundKey(const CompoundSelector& c,
                            const SimpleSelector*& idS,
                            const SimpleSelector*& classS,
                            const SimpleSelector*& tagS) {
        idS = classS = tagS = nullptr;
        for (auto& s : c.simples) {
            if (s.type == SimpleSelectorType::Id && !idS) idS = &s;
            else if (s.type == SimpleSelectorType::Class && !classS) classS = &s;
            else if (s.type == SimpleSelectorType::Tag && !tagS) tagS = &s;
        }
    }

    // Record what a compound requires into one of the key sets, or flag the set
    // as unkeyed when it requires nothing (`:hover .label`, `.row:hover > *`).
    void addKey(const CompoundSelector& c, KeySet& ids, KeySet& classes,
                KeySet& tags, bool& universal) {
        const SimpleSelector *idS, *classS, *tagS;
        compoundKey(c, idS, classS, tagS);
        if (idS)         ids.insert(idS->value);
        else if (classS) classes.insert(classS->value);
        else if (tagS)   tags.insert(toLowerKey(tagS->value));
        else             universal = true;
    }

    static std::string toLowerKey(std::string_view s) {
        std::string out(s);
        for (auto& c : out)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return out;
    }

    std::vector<ScopedRule> rules_;
    std::vector<KeyframeBlock> keyframes_;
    std::vector<FontFaceRule> fontFaces_;
    // Pseudo-element name -> indices into rules_ whose subject targets it.
    // Indices stay valid: rules_ is only appended to, and clear() drops both.
    std::unordered_map<std::string, std::vector<size_t>> pseudoRules_;
    // Subject-key rule index (see classifyLastRule). Same index validity.
    RuleBuckets idRules_;
    RuleBuckets classRules_;
    RuleBuckets tagRules_;            // keys lowercased
    std::vector<size_t> universalRules_;   // rules whose subject requires no name
    // The two sides of a hover-descendant rule (`.row:hover .label`): what the
    // hovered element must be (Src, the `.row`) and what its subject must be
    // (Desc, the `.label`). See hoverInvalidatesDescendants / hoverCanAffect.
    KeySet hoverSrcIds_,  hoverSrcClasses_,  hoverSrcTags_;
    KeySet hoverDescIds_, hoverDescClasses_, hoverDescTags_;   // tags lowercased
    bool hoverSrcUniversal_ = false;       // a `:hover` with no name of its own
    bool hoverDescUniversal_ = false;      // a hover-descendant subject requires no name
    bool hoverSiblings_ = false;           // a :hover reaches its subject through + or ~
    size_t nextOrder_ = 0;
    bool usesHover_ = false;  // any rule uses :hover (set in classifyLastRule)
    bool usesContainers_ = false; // any @container rule added
    bool usesForcedInherit_ = false; // any `inherit` on a non-inherited property
    bool usesHas_ = false;           // any rule uses :has()
    // Class names appearing in a non-subject compound of some selector — the
    // only classes whose presence on an element decides a DESCENDANT's match.
    std::unordered_set<std::string> ancestorClasses_;

    // @import resolution
    ImportResolver importResolver_;
    std::unordered_set<std::string> loadedImports_;

    // Layer ordering: maps layer name -> index. Lower index = lower priority.
    std::vector<std::string> layerNames_;
    int getOrCreateLayerIndex(const std::string& name);

    // Evaluate a container query condition against an element's container ancestors
    bool evaluateContainerQuery(const ElementRef& elem,
                                const std::string& containerName,
                                const std::string& condition) const;
};

} // namespace htmlayout::css
