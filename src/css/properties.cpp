#include "css/properties.h"
#include "css/shorthand_internal.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <string_view>

namespace htmlayout::css {

const std::vector<PropertyDef>& knownProperties() {
    static const std::vector<PropertyDef> props = {
        // Box model
        {"display",           "inline", false},
        {"position",          "static", false},
        {"float",             "none",   false},
        {"clear",             "none",   false},
        {"width",             "auto",   false},
        {"height",            "auto",   false},
        {"min-width",         "auto",   false},
        {"min-height",        "auto",   false},
        {"max-width",         "none",   false},
        {"max-height",        "none",   false},
        {"margin-top",        "0",      false},
        {"margin-right",      "0",      false},
        {"margin-bottom",     "0",      false},
        {"margin-left",       "0",      false},
        {"padding-top",       "0",      false},
        {"padding-right",     "0",      false},
        {"padding-bottom",    "0",      false},
        {"padding-left",      "0",      false},
        {"border-top-width",  "medium", false},
        {"border-right-width","medium", false},
        {"border-bottom-width","medium",false},
        {"border-left-width", "medium", false},
        {"border-top-style",  "none",   false},
        {"border-right-style","none",   false},
        {"border-bottom-style","none",  false},
        {"border-left-style", "none",   false},
        {"border-top-color",  "currentcolor", false},
        {"border-right-color","currentcolor", false},
        {"border-bottom-color","currentcolor",false},
        {"border-left-color", "currentcolor", false},
        // CSS border-image (Backgrounds-3 §6, nine-slice). Not inherited;
        // initial values per spec. The `border-image` shorthand expands into
        // these; the renderer consumes them at paint time (slice clamping to
        // the image bounds is a use-time concern, not a parse-time one).
        {"border-image-source", "none",    false},
        {"border-image-slice",  "100%",    false},
        {"border-image-width",  "1",       false},
        {"border-image-outset", "0",       false},
        {"border-image-repeat", "stretch", false},
        {"overflow",          "visible",false},
        {"overflow-x",        "visible",false},
        {"overflow-y",        "visible",false},
        {"box-sizing",        "content-box", false},
        {"aspect-ratio",      "auto",   false},

        // Flexbox
        {"flex-direction",    "row",       false},
        {"flex-wrap",         "nowrap",    false},
        {"justify-content",   "normal",    false},
        {"align-items",       "normal",    false},
        {"align-content",     "normal",    false},
        {"align-self",        "auto",      false},
        {"flex-grow",         "0",         false},
        {"flex-shrink",       "1",         false},
        {"flex-basis",        "auto",      false},
        {"order",             "0",         false},
        {"gap",               "normal",    false},
        {"row-gap",           "normal",    false},
        {"column-gap",        "normal",    false},

        // Text & font (inherited)
        {"color",             "black",     true},
        {"font-family",       "sans-serif",true},
        {"font-size",         "16px",      true},
        {"font-weight",       "normal",    true},
        {"font-style",        "normal",    true},
        // OpenType feature selection (css/font_features.h resolves both into
        // one feature list); measurement and paint shape with it.
        {"font-variant-numeric",  "normal", true},
        {"font-feature-settings", "normal", true},
        {"line-height",       "normal",    true},
        {"text-align",        "start",     true},
        {"text-indent",       "0",         true},
        {"text-decoration",   "none",      false},
        {"text-transform",    "none",      true},
        {"white-space",       "normal",    true},
        {"word-spacing",      "normal",    true},
        {"letter-spacing",    "normal",    true},
        {"vertical-align",    "baseline",  false},
        {"visibility",        "visible",   true},
        {"cursor",            "auto",      true},

        // Form-control theming (inherited — declare once on body/root).
        // color-scheme: renderers draw dark UA control chrome (range track,
        // checkbox/radio boxes) when the computed value includes "dark".
        // accent-color: the accent parts of controls (checked fill, range
        // fill + thumb).
        {"color-scheme",      "normal",    true},
        {"accent-color",      "auto",      true},

        // Visual
        {"background-color",  "transparent", false},
        {"background-image",  "none",        false},
        {"opacity",           "1",           false},
        {"z-index",           "auto",        false},
        {"top",               "auto",        false},
        {"right",             "auto",        false},
        {"bottom",            "auto",        false},
        {"left",              "auto",        false},

        // List
        {"list-style-type",   "disc",   true},
        {"list-style-position","outside",true},

        // Generated content
        {"content",           "normal",  false},
        {"counter-reset",     "none",    false},
        {"counter-increment", "none",    false},
        {"counter-set",       "none",    false},
        // `quotes` is inherited so <q> descendants pick up an ancestor's
        // custom quote pairs; `auto` resolves to the locale default downstream.
        {"quotes",            "auto",    true},

        // Pointer events (for hit testing)
        {"pointer-events",    "auto",    true},

        // Visual effects (not layout-affecting, but tracked for cascade)
        {"box-shadow",        "none",       false},
        {"border-radius",     "0",          false},
        {"border-top-left-radius",     "0", false},
        {"border-top-right-radius",    "0", false},
        {"border-bottom-right-radius", "0", false},
        {"border-bottom-left-radius",  "0", false},
        {"outline",           "none",       false},
        {"outline-width",     "medium",     false},
        {"outline-style",     "none",       false},
        {"outline-color",     "currentcolor", false},
        {"outline-offset",    "0",          false},

        // Transforms
        {"transform",         "none",       false},
        {"transform-origin",  "50% 50%",    false},
        {"transform-style",   "flat",       false},
        {"perspective",       "none",       false},
        {"perspective-origin","50% 50%",    false},
        {"backface-visibility","visible",   false},

        // Transitions & Animations (parsed but not executed)
        {"transition",            "none",   false},
        {"transition-property",   "all",    false},
        {"transition-duration",   "0s",     false},
        {"transition-timing-function", "ease", false},
        {"transition-delay",      "0s",     false},
        {"transition-behavior",   "normal", false},
        {"animation",             "none",   false},
        {"animation-name",        "none",   false},
        {"animation-duration",    "0s",     false},
        {"animation-timing-function", "ease", false},
        {"animation-delay",       "0s",     false},
        {"animation-iteration-count", "1",  false},
        {"animation-direction",   "normal", false},
        {"animation-fill-mode",   "none",   false},
        {"animation-play-state",  "running", false},

        // Text overflow & wrapping
        {"text-overflow",     "clip",       false},

        // Line clamping (CSS Overflow 4). The `line-clamp` shorthand expands
        // into these three longhands, which block layout reads; the legacy
        // `-webkit-line-clamp` is a property of its own that only applies on
        // a `display: -webkit-box; -webkit-box-orient: vertical` box.
        {"max-lines",         "none",       false},
        {"block-ellipsis",    "no-ellipsis", true},
        {"continue",          "auto",       false},
        {"-webkit-line-clamp","none",       false},
        {"-webkit-box-orient","horizontal", false},
        {"overflow-wrap",     "normal",     true},
        {"word-break",        "normal",     true},

        // Multi-column layout
        {"column-count",      "auto",       false},
        {"column-width",      "auto",       false},
        {"column-gap",        "normal",     false},
        {"column-rule-width", "medium",     false},
        {"column-rule-style", "none",       false},
        {"column-rule-color", "currentcolor", false},
        {"column-fill",       "balance",    false},
        {"column-span",       "none",       false},

        // CSS Shapes (float exclusion geometry)
        {"shape-outside",     "none",       false},

        // Color & background extras
        {"background-position", "0% 0%",    false},
        {"background-repeat",   "repeat",   false},
        {"background-size",     "auto",     false},
        {"background-clip",     "border-box", false},
        {"background-origin",   "padding-box", false},
        {"background-blend-mode", "normal",   false},

        // Direction and writing mode
        {"direction",         "ltr",        true},
        {"writing-mode",      "horizontal-tb", true},
        {"unicode-bidi",      "normal",     false},

        // Table layout
        {"table-layout",      "auto",       false},
        {"border-collapse",   "separate",   true},
        {"border-spacing",    "0",          true},
        {"caption-side",      "top",        true},
        {"empty-cells",       "show",       true},

        // Grid layout
        {"grid-template-columns", "none",   false},
        {"grid-template-rows",    "none",   false},
        {"grid-template-areas",   "none",   false},
        {"grid-auto-columns",     "auto",   false},
        {"grid-auto-rows",        "auto",   false},
        {"grid-auto-flow",        "row",    false},
        {"grid-row-start",        "auto",   false},
        {"grid-row-end",          "auto",   false},
        {"grid-column-start",     "auto",   false},
        {"grid-column-end",       "auto",   false},
        {"grid-row",              "auto",   false},
        {"grid-column",           "auto",   false},
        {"grid-area",             "auto",   false},
        {"justify-items",         "stretch", false},
        {"justify-self",          "auto",   false},
        {"place-items",           "stretch", false},
        {"place-content",         "normal", false},
        {"place-self",            "auto",   false},

        // position: sticky
        // (position is already in the registry; sticky is a valid value)

        // Misc
        {"clip-path",         "none",       false},
        {"filter",            "none",       false},
        // SVG masking: the <mask> an element is masked by, and a <mask>'s
        // own luminance / alpha mode.
        {"mask",              "none",       false},
        {"mask-type",         "luminance",  false},
        {"mix-blend-mode",    "normal",     false},
        {"isolation",         "auto",       false},
        {"object-fit",        "fill",       false},
        // CSS Images 3: how a scaled image is sampled, and whether a photo's
        // EXIF orientation turns it upright. Both inherit.
        {"image-rendering",   "auto",       true},
        {"image-orientation", "from-image", true},
        {"object-position",   "50% 50%",    false},
        {"resize",            "none",       false},
        {"user-select",       "auto",       false},
        {"will-change",       "auto",       false},
        {"contain",           "none",       false},
        {"content-visibility", "visible",   false},

        // Container queries
        {"container-type",    "normal",     false},
        {"container-name",    "none",       false},

        // CSS Logical Properties L1 — these are resolved to physical properties
        // during shorthand expansion based on writing-mode/direction.
        // Registered here so they are recognized as valid properties.
        {"inline-size",       "auto",       false},
        {"block-size",        "auto",       false},
        {"min-inline-size",   "0",          false},
        {"min-block-size",    "0",          false},
        {"max-inline-size",   "none",       false},
        {"max-block-size",    "none",       false},
        {"margin-inline-start",  "0",       false},
        {"margin-inline-end",    "0",       false},
        {"margin-block-start",   "0",       false},
        {"margin-block-end",     "0",       false},
        {"padding-inline-start", "0",       false},
        {"padding-inline-end",   "0",       false},
        {"padding-block-start",  "0",       false},
        {"padding-block-end",    "0",       false},
        {"border-inline-start-width", "medium", false},
        {"border-inline-end-width",   "medium", false},
        {"border-block-start-width",  "medium", false},
        {"border-block-end-width",    "medium", false},
        {"border-inline-start-style", "none",   false},
        {"border-inline-end-style",   "none",   false},
        {"border-block-start-style",  "none",   false},
        {"border-block-end-style",    "none",   false},
        {"border-inline-start-color", "currentcolor", false},
        {"border-inline-end-color",   "currentcolor", false},
        {"border-block-start-color",  "currentcolor", false},
        {"border-block-end-color",    "currentcolor", false},
        {"inset-inline-start", "auto",      false},
        {"inset-inline-end",   "auto",      false},
        {"inset-block-start",  "auto",      false},
        {"inset-block-end",    "auto",      false},

        // SVG presentation properties. These are settable via CSS *and* as SVG
        // presentation attributes (fill="red"); the attribute is seeded as a
        // lowest-priority declaration in Cascade::resolve(). Most are inherited
        // in SVG so a `fill`/`stroke` on a <g> flows to its shape children.
        // `clip-path`, `opacity`, and `color` (drives currentColor) are already
        // registered above.
        {"fill",              "black",      true},
        {"fill-opacity",      "1",          true},
        {"fill-rule",         "nonzero",    true},
        {"stroke",            "none",       true},
        {"stroke-opacity",    "1",          true},
        {"stroke-width",      "1",          true},
        {"stroke-linecap",    "butt",       true},
        {"stroke-linejoin",   "miter",      true},
        {"stroke-miterlimit", "4",          true},
        {"stroke-dasharray",  "none",       true},
        {"stroke-dashoffset", "0",          true},
        {"clip-rule",         "nonzero",    true},
        {"paint-order",       "normal",     true},
        {"stop-color",        "black",      false},
        {"stop-opacity",      "1",          false},
        // SVG text: text-anchor is inherited (a value on <text> flows to its
        // <tspan> chunks); the baseline properties are per-element.
        {"text-anchor",        "start",     true},
        {"dominant-baseline",  "auto",      false},
        {"alignment-baseline", "auto",      false},
        {"baseline-shift",     "baseline",  false},
        // SVG markers are inherited (a value on a <g> flows to descendant
        // shapes); each references a <marker> by url(#id) or is `none`.
        {"marker-start",       "none",      true},
        {"marker-mid",         "none",      true},
        {"marker-end",         "none",      true},
    };
    return props;
}

namespace {

// Cached property lookup map, built once from knownProperties().
struct PropertyCache {
    std::unordered_map<std::string, const PropertyDef*, SvHash, SvEq> map;

    PropertyCache() {
        for (auto& p : knownProperties()) {
            map[p.name] = &p;
        }
    }

    const PropertyDef* find(std::string_view name) const {
        auto it = map.find(name);
        return it != map.end() ? it->second : nullptr;
    }
};

const PropertyCache& cache() {
    static const PropertyCache instance;
    return instance;
}

} // anonymous namespace

bool isInherited(std::string_view property) {
    auto* def = cache().find(property);
    return def ? def->inherited : false;
}

std::string initialValue(std::string_view property) {
    auto* def = cache().find(property);
    return def ? def->initialValue : std::string{};
}

const std::string& initialValueRef(std::string_view property) {
    static const std::string empty;
    auto* def = cache().find(property);
    return def ? def->initialValue : empty;
}

bool isShorthandProperty(std::string_view property) {
    // The overwhelming majority of declarations name a longhand that has no
    // branch in expandShorthand() and falls straight through to the identity
    // return — but only after splitValue() has tokenised the value and the
    // whole ladder of name comparisons has been walked and missed. This set
    // lets the expander (and the cascade, which then never calls it) skip all
    // of that. It is exactly the properties with a `property == "..."` branch
    // in expandShorthand(); keep the two in sync.
    static const std::unordered_set<std::string_view> kExpandable = {
        "margin", "padding", "inset", "border-width", "border-style",
        "border-color", "border", "border-top", "border-right",
        "border-bottom", "border-left", "border-radius", "border-image",
        "flex", "flex-flow",
        "gap", "background", "font", "transition", "animation", "outline",
        "list-style", "columns", "column-rule", "overflow", "container",
        "line-clamp",
        "grid-area", "grid-column", "grid-row", "grid-template",
        "place-content", "place-items", "place-self",
        // Logical properties (remapped to physical / logical longhands below).
        "block-size", "inline-size", "min-block-size", "min-inline-size",
        "max-block-size", "max-inline-size",
        "margin-block", "margin-block-start", "margin-block-end",
        "margin-inline", "margin-inline-start", "margin-inline-end",
        "padding-block", "padding-block-start", "padding-block-end",
        "padding-inline", "padding-inline-start", "padding-inline-end",
        "inset-block", "inset-block-start", "inset-block-end",
        "inset-inline", "inset-inline-start", "inset-inline-end",
        "border-block", "border-block-color", "border-block-style",
        "border-block-width", "border-block-start", "border-block-start-color",
        "border-block-start-style", "border-block-start-width",
        "border-block-end", "border-block-end-color", "border-block-end-style",
        "border-block-end-width",
        "border-inline", "border-inline-color", "border-inline-style",
        "border-inline-width", "border-inline-start",
        "border-inline-start-color", "border-inline-start-style",
        "border-inline-start-width", "border-inline-end",
        "border-inline-end-color", "border-inline-end-style",
        "border-inline-end-width",
    };
    return kExpandable.find(property) != kExpandable.end();
}

// `animation` (CSS Animations 1 §3.1): comma-separated layers, each
//   <time> || <easing-function> || <time> || <iteration-count> ||
//   <direction> || <fill-mode> || <play-state> || [ none | <keyframes-name> ]
// The first time is the duration, the second the delay; a keyword is taken
// by the first longhand it fits, so the name is whatever is left. Tokens are
// split at spaces and commas OUTSIDE parentheses, so cubic-bezier(0.4, 0,
// 0.2, 1) and steps(4, end) stay one easing token.
std::vector<ExpandedDecl> expandAnimationShorthand(const std::string& value) {
    static const char* kLonghands[] = {
        "animation-name", "animation-duration", "animation-timing-function",
        "animation-delay", "animation-iteration-count", "animation-direction",
        "animation-fill-mode", "animation-play-state",
    };
    std::vector<ExpandedDecl> out;
    out.push_back({"animation", value});

    std::string v = value;
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.erase(v.begin());
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
    std::string lower = v;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" ||
        lower == "revert-layer") {
        for (const char* p : kLonghands) out.push_back({p, lower});
        return out;
    }

    // Layers, each a list of tokens.
    std::vector<std::vector<std::string>> layers(1);
    std::string tok;
    int depth = 0;
    auto flush = [&] {
        if (!tok.empty()) layers.back().push_back(tok);
        tok.clear();
    };
    for (char c : v) {
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        if (depth == 0 && c == ',') { flush(); layers.emplace_back(); continue; }
        if (depth == 0 && std::isspace(static_cast<unsigned char>(c))) { flush(); continue; }
        tok += c;
    }
    flush();

    auto isTime = [](const std::string& t) {
        char* end = nullptr;
        std::strtod(t.c_str(), &end);
        if (end == t.c_str()) return false;
        std::string unit(end);
        for (char& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return unit == "s" || unit == "ms";
    };
    auto isNumber = [](const std::string& t) {
        char* end = nullptr;
        std::strtod(t.c_str(), &end);
        return end != t.c_str() && *end == '\0';
    };
    auto isEasing = [](const std::string& l) {
        return l == "linear" || l == "ease" || l == "ease-in" || l == "ease-out" ||
               l == "ease-in-out" || l == "step-start" || l == "step-end" ||
               l.rfind("cubic-bezier(", 0) == 0 || l.rfind("steps(", 0) == 0 ||
               l.rfind("linear(", 0) == 0;
    };

    std::string lists[8];
    for (size_t li = 0; li < layers.size(); ++li) {
        std::string name = "none", duration = "0s", easing = "ease", delay = "0s",
                    iter = "1", direction = "normal", fill = "none", play = "running";
        bool haveDuration = false, haveDelay = false, haveEasing = false, haveIter = false,
             haveDirection = false, haveFill = false, havePlay = false, haveName = false;
        for (const std::string& t : layers[li]) {
            std::string l = t;
            for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (isTime(t)) {
                if (!haveDuration) { duration = l; haveDuration = true; }
                else if (!haveDelay) { delay = l; haveDelay = true; }
            } else if (!haveEasing && isEasing(l)) {
                easing = l; haveEasing = true;
            } else if (!haveIter && (l == "infinite" || isNumber(t))) {
                iter = l; haveIter = true;
            } else if (!haveDirection && (l == "normal" || l == "reverse" || l == "alternate" ||
                                          l == "alternate-reverse")) {
                direction = l; haveDirection = true;
            } else if (!haveFill && (l == "none" || l == "forwards" || l == "backwards" ||
                                     l == "both")) {
                fill = l; haveFill = true;
            } else if (!havePlay && (l == "running" || l == "paused")) {
                play = l; havePlay = true;
            } else if (!haveName) {
                name = t; haveName = true;  // keyframes names keep their case
            }
        }
        const std::string vals[8] = {name, duration, easing, delay, iter, direction, fill, play};
        for (int k = 0; k < 8; ++k) {
            if (li) lists[k] += ", ";
            lists[k] += vals[k];
        }
    }
    for (int k = 0; k < 8; ++k) out.push_back({kLonghands[k], lists[k]});
    return out;
}

// transition: the full value (for getComputedStyle().transition) plus the five
// longhands, each a comma list with one entry per transition. Expanding here is
// what lets a later longhand (transition-behavior: allow-discrete after a
// `transition`) override the shorthand's reset in cascade order.
std::vector<ExpandedDecl> expandTransitionShorthand(const std::string& value) {
    static const char* kLonghands[] = {
        "transition-property", "transition-duration", "transition-timing-function",
        "transition-delay", "transition-behavior",
    };
    std::vector<ExpandedDecl> out;
    out.push_back({"transition", value});

    std::string v = value;
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.erase(v.begin());
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
    std::string lower = v;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" ||
        lower == "revert-layer") {
        for (const char* p : kLonghands) out.push_back({p, lower});
        return out;
    }

    std::vector<std::vector<std::string>> items(1);
    std::string tok;
    int depth = 0;
    auto flush = [&] {
        if (!tok.empty()) items.back().push_back(tok);
        tok.clear();
    };
    for (char c : v) {
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        if (depth == 0 && c == ',') { flush(); items.emplace_back(); continue; }
        if (depth == 0 && std::isspace(static_cast<unsigned char>(c))) { flush(); continue; }
        tok += c;
    }
    flush();

    auto isTime = [](const std::string& t) {
        char* end = nullptr;
        std::strtod(t.c_str(), &end);
        if (end == t.c_str()) return false;
        std::string unit(end);
        for (char& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return unit == "s" || unit == "ms";
    };
    auto isEasing = [](const std::string& l) {
        return l == "linear" || l == "ease" || l == "ease-in" || l == "ease-out" ||
               l == "ease-in-out" || l == "step-start" || l == "step-end" ||
               l.rfind("cubic-bezier(", 0) == 0 || l.rfind("steps(", 0) == 0 ||
               l.rfind("linear(", 0) == 0;
    };

    std::string lists[5];
    for (size_t i = 0; i < items.size(); ++i) {
        std::string prop = "all", duration = "0s", easing = "ease", delay = "0s",
                    behavior = "normal";
        bool haveDuration = false, haveDelay = false, haveEasing = false,
             haveProp = false, haveBehavior = false;
        for (const std::string& t : items[i]) {
            std::string l = t;
            for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (isTime(t)) {
                if (!haveDuration) { duration = l; haveDuration = true; }
                else if (!haveDelay) { delay = l; haveDelay = true; }
            } else if (!haveEasing && isEasing(l)) {
                easing = l; haveEasing = true;
            } else if (!haveBehavior && (l == "normal" || l == "allow-discrete")) {
                behavior = l; haveBehavior = true;
            } else if (!haveProp) {
                prop = l; haveProp = true;
            }
        }
        const std::string vals[5] = {prop, duration, easing, delay, behavior};
        for (int k = 0; k < 5; ++k) {
            if (i) lists[k] += ", ";
            lists[k] += vals[k];
        }
    }
    for (int k = 0; k < 5; ++k) out.push_back({kLonghands[k], lists[k]});
    return out;
}

std::vector<ExpandedDecl> expandBackgroundShorthand(const std::string& value) {
    // CSS spec: 'background' shorthand resets all sub-properties.
    // Multi-layer: comma-separated layers. Only the final layer may
    // carry <bg-color>; earlier layers are images (or a final layer
    // may also be color-only).
    std::vector<ExpandedDecl> result = {
        {"background-color", "transparent"},
        {"background-image", "none"},
        {"background-position", "0% 0%"},
        {"background-size", "auto"},
        {"background-repeat", "repeat"},
        {"background-clip", "border-box"},
        {"background-origin", "padding-box"},
    };

    if (value.empty() || value == "none") return result;
    if (value == "transparent") {
        result[0].value = "transparent";
        return result;
    }

    // Split top-level (paren/quote-aware) commas into layers.
    std::vector<std::string> layers;
    {
        std::string cur;
        int depth = 0;
        bool inQ = false;
        char qc = 0;
        for (size_t i = 0; i < value.size(); ++i) {
            char c = value[i];
            if (inQ) { cur += c; if (c == qc) inQ = false; continue; }
            if (c == '"' || c == '\'') { inQ = true; qc = c; cur += c; continue; }
            if (c == '(') { ++depth; cur += c; continue; }
            if (c == ')') { --depth; cur += c; continue; }
            if (c == ',' && depth == 0) {
                layers.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (!cur.empty() || !layers.empty()) layers.push_back(cur);
    }

    auto trim = [](std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    };

    auto isPositionKeyword = [](const std::string& l) {
        return l == "left" || l == "right" || l == "top" ||
               l == "bottom" || l == "center";
    };
    auto isLengthOrPercent = [](const std::string& t) {
        if (t.empty()) return false;
        // A length/percent/calc value (or "0", or "auto" handled separately)
        char c0 = t[0];
        if (c0 == '-' || c0 == '+' || c0 == '.' || std::isdigit(static_cast<unsigned char>(c0))) return true;
        if (t.size() >= 5 && t.substr(0, 5) == "calc(") return true;
        return false;
    };

    // Per-layer values
    std::vector<std::string> layerImages;
    std::vector<std::string> layerPositions;
    std::vector<std::string> layerSizes;
    std::vector<std::string> layerRepeats;
    std::vector<std::string> layerClips;
    std::vector<std::string> layerOrigins;

    std::string layerColor; // only legal on the last layer
    size_t numLayers = layers.size();
    for (size_t li = 0; li < numLayers; ++li) {
        std::string layer = layers[li];
        trim(layer);

        // Tokenize this layer (whitespace, paren-aware)
        std::vector<std::string> toks;
        {
            std::string t;
            int depth = 0;
            bool inQ = false;
            char qc = 0;
            for (size_t i = 0; i < layer.size(); ++i) {
                char c = layer[i];
                if (inQ) { t += c; if (c == qc) inQ = false; continue; }
                if (c == '"' || c == '\'') { inQ = true; qc = c; t += c; continue; }
                if (c == '(') { ++depth; t += c; continue; }
                if (c == ')') { --depth; t += c; continue; }
                if (depth == 0 && std::isspace(static_cast<unsigned char>(c))) {
                    if (!t.empty()) { toks.push_back(t); t.clear(); }
                    continue;
                }
                // The position/size separator '/' may be written without
                // surrounding spaces (`center/contain`). Emit it as its own
                // token so the slash handling below sees it. Only at depth 0
                // (paths inside url(...) keep their slashes).
                if (depth == 0 && c == '/') {
                    if (!t.empty()) { toks.push_back(t); t.clear(); }
                    toks.push_back("/");
                    continue;
                }
                t += c;
            }
            if (!t.empty()) toks.push_back(t);
        }

        std::string image;
        std::vector<std::string> positionToks;
        std::vector<std::string> sizeToks;
        std::string repeat;
        std::string clip;
        std::string origin;
        std::string color;

        // Walk tokens; '/' separates position from size.
        bool sawSlash = false;
        int boxKeywordCount = 0;
        for (size_t ti = 0; ti < toks.size(); ++ti) {
            const std::string& t = toks[ti];
            std::string lower = t;
            for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (t == "/") { sawSlash = true; continue; }

            if (lower.substr(0, 4) == "url(" ||
                lower.substr(0, 16) == "linear-gradient(" ||
                lower.substr(0, 16) == "radial-gradient(" ||
                lower.substr(0, 15) == "conic-gradient(" ||
                lower.substr(0, 26) == "repeating-linear-gradient(" ||
                lower.substr(0, 26) == "repeating-radial-gradient(" ||
                lower.substr(0, 25) == "repeating-conic-gradient(" ||
                lower.substr(0, 6) == "image(" ||
                lower == "none") {
                image = (lower == "none") ? "none" : t;
                continue;
            }
            if (lower == "repeat" || lower == "no-repeat" || lower == "repeat-x" ||
                lower == "repeat-y" || lower == "space" || lower == "round") {
                repeat = lower;
                continue;
            }
            if (lower == "border-box" || lower == "padding-box" || lower == "content-box") {
                // First box keyword sets origin (and clip), second sets clip.
                if (boxKeywordCount == 0) { origin = lower; clip = lower; }
                else clip = lower;
                ++boxKeywordCount;
                continue;
            }
            if (lower == "fixed" || lower == "scroll" || lower == "local") {
                // background-attachment — not tracked separately yet, ignore.
                continue;
            }
            if (sawSlash) {
                if (lower == "auto" || lower == "cover" || lower == "contain" ||
                    isLengthOrPercent(t)) {
                    sizeToks.push_back(t);
                    continue;
                }
            }
            if (isPositionKeyword(lower) || isLengthOrPercent(t)) {
                positionToks.push_back(t);
                continue;
            }
            // Anything else: candidate color (only valid on last layer).
            if (li + 1 == numLayers) {
                if (color.empty()) color = t;
                else color += " " + t; // e.g. rgb(...) already one token
            }
        }

        if (image.empty()) image = "none";
        std::string position;
        if (positionToks.empty()) position = "0% 0%";
        else if (positionToks.size() == 1) position = positionToks[0];
        else {
            position = positionToks[0];
            for (size_t k = 1; k < positionToks.size(); ++k) position += " " + positionToks[k];
        }
        std::string size;
        if (sizeToks.empty()) size = "auto";
        else if (sizeToks.size() == 1) size = sizeToks[0];
        else {
            size = sizeToks[0];
            for (size_t k = 1; k < sizeToks.size(); ++k) size += " " + sizeToks[k];
        }
        if (repeat.empty()) repeat = "repeat";
        if (origin.empty()) origin = "padding-box";
        if (clip.empty()) clip = "border-box";

        layerImages.push_back(image);
        layerPositions.push_back(position);
        layerSizes.push_back(size);
        layerRepeats.push_back(repeat);
        layerOrigins.push_back(origin);
        layerClips.push_back(clip);
        if (li + 1 == numLayers) layerColor = color;
    }

    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += ", ";
            s += v[i];
        }
        return s;
    };

    if (!layerColor.empty()) result[0].value = layerColor;
    result[1].value = join(layerImages);
    result[2].value = join(layerPositions);
    result[3].value = join(layerSizes);
    result[4].value = join(layerRepeats);
    result[5].value = join(layerClips);
    result[6].value = join(layerOrigins);

    return result;
}

} // namespace htmlayout::css
