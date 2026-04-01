#include "css/properties.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <stdexcept>

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
        {"min-width",         "0",      false},
        {"min-height",        "0",      false},
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
        {"overflow",          "visible",false},
        {"box-sizing",        "content-box", false},

        // Flexbox
        {"flex-direction",    "row",       false},
        {"flex-wrap",         "nowrap",    false},
        {"justify-content",   "flex-start",false},
        {"align-items",       "stretch",   false},
        {"align-content",     "stretch",   false},
        {"align-self",        "auto",      false},
        {"flex-grow",         "0",         false},
        {"flex-shrink",       "1",         false},
        {"flex-basis",        "auto",      false},
        {"order",             "0",         false},
        {"gap",               "0",         false},
        {"row-gap",           "0",         false},
        {"column-gap",        "0",         false},

        // Text & font (inherited)
        {"color",             "black",     true},
        {"font-family",       "sans-serif",true},
        {"font-size",         "16px",      true},
        {"font-weight",       "normal",    true},
        {"font-style",        "normal",    true},
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

        // Transitions & Animations (parsed but not executed)
        {"transition",            "none",   false},
        {"transition-property",   "all",    false},
        {"transition-duration",   "0s",     false},
        {"transition-timing-function", "ease", false},
        {"transition-delay",      "0s",     false},
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

        // Color & background extras
        {"background-position", "0% 0%",    false},
        {"background-repeat",   "repeat",   false},
        {"background-size",     "auto",     false},
        {"background-clip",     "border-box", false},
        {"background-origin",   "padding-box", false},

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
        {"mix-blend-mode",    "normal",     false},
        {"isolation",         "auto",       false},
        {"object-fit",        "fill",       false},
        {"object-position",   "50% 50%",    false},
        {"resize",            "none",       false},
        {"user-select",       "auto",       false},
        {"will-change",       "auto",       false},
        {"contain",           "none",       false},

        // Container queries
        {"container-type",    "normal",     false},
        {"container-name",    "none",       false},
    };
    return props;
}

namespace {

// Cached property lookup map, built once from knownProperties().
struct PropertyCache {
    std::unordered_map<std::string, const PropertyDef*> map;

    PropertyCache() {
        for (auto& p : knownProperties()) {
            map[p.name] = &p;
        }
    }

    const PropertyDef* find(const std::string& name) const {
        auto it = map.find(name);
        return it != map.end() ? it->second : nullptr;
    }
};

const PropertyCache& cache() {
    static const PropertyCache instance;
    return instance;
}

// Safe integer parsing that returns a default on failure.
int safeStoi(const std::string& s, int defaultVal = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return defaultVal;
    }
}

} // anonymous namespace

bool isInherited(const std::string& property) {
    auto* def = cache().find(property);
    return def ? def->inherited : false;
}

std::string initialValue(const std::string& property) {
    auto* def = cache().find(property);
    return def ? def->initialValue : std::string{};
}

namespace {

// Split a CSS value string into whitespace-separated parts,
// respecting quoted strings and parenthesized groups.
std::vector<std::string> splitValue(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    bool inQuote = false;
    char quoteChar = 0;

    for (size_t i = 0; i < value.size(); i++) {
        char c = value[i];
        if (inQuote) {
            current += c;
            if (c == quoteChar) inQuote = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            inQuote = true;
            quoteChar = c;
            current += c;
            continue;
        }
        if (c == '(') { parenDepth++; current += c; continue; }
        if (c == ')') { parenDepth--; current += c; continue; }
        if (parenDepth > 0) { current += c; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// Resolve 1-4 values into top/right/bottom/left per CSS box model.
struct BoxValues {
    std::string top, right, bottom, left;
};

BoxValues resolveBoxValues(const std::vector<std::string>& parts) {
    BoxValues bv;
    switch (parts.size()) {
        case 1:
            bv.top = bv.right = bv.bottom = bv.left = parts[0];
            break;
        case 2:
            bv.top = bv.bottom = parts[0];
            bv.right = bv.left = parts[1];
            break;
        case 3:
            bv.top = parts[0];
            bv.right = bv.left = parts[1];
            bv.bottom = parts[2];
            break;
        default:
            bv.top = parts[0];
            bv.right = parts[1];
            bv.bottom = parts[2];
            bv.left = parts.size() > 3 ? parts[3] : parts[2];
            break;
    }
    return bv;
}

// Expand 1-4 value box shorthand using a prefix (e.g. "margin", "padding")
// or a prefix+suffix pattern (e.g. "border-" + side + "-width").
void expandBoxShorthand(const std::string& prefix,
                        const std::vector<std::string>& parts,
                        std::vector<ExpandedDecl>& out) {
    auto bv = resolveBoxValues(parts);
    out.push_back({prefix + "-top", bv.top});
    out.push_back({prefix + "-right", bv.right});
    out.push_back({prefix + "-bottom", bv.bottom});
    out.push_back({prefix + "-left", bv.left});
}

// Expand 1-4 value shorthand for border sub-properties like border-width,
// border-style, border-color where longhand names are "border-{side}-{suffix}".
void expandBorderBoxShorthand(const std::string& suffix,
                              const std::vector<std::string>& parts,
                              std::vector<ExpandedDecl>& out) {
    auto bv = resolveBoxValues(parts);
    out.push_back({"border-top-" + suffix, bv.top});
    out.push_back({"border-right-" + suffix, bv.right});
    out.push_back({"border-bottom-" + suffix, bv.bottom});
    out.push_back({"border-left-" + suffix, bv.left});
}

bool isBorderStyle(const std::string& v) {
    return v == "none" || v == "hidden" || v == "dotted" || v == "dashed" ||
           v == "solid" || v == "double" || v == "groove" || v == "ridge" ||
           v == "inset" || v == "outset";
}

bool isBorderWidth(const std::string& v) {
    if (v == "thin" || v == "medium" || v == "thick") return true;
    if (v.empty()) return false;
    size_t i = 0;
    if (v[i] == '-' || v[i] == '+') i++;
    bool hasDigit = false;
    while (i < v.size() && (std::isdigit(static_cast<unsigned char>(v[i])) || v[i] == '.')) {
        hasDigit = true;
        i++;
    }
    if (!hasDigit) return false;
    std::string unit = v.substr(i);
    return unit.empty() || unit == "px" || unit == "em" || unit == "rem" ||
           unit == "pt" || unit == "%" || unit == "vw" || unit == "vh";
}

enum class BorderPart { Width, Style, Color };

BorderPart classifyBorderToken(const std::string& v) {
    if (isBorderStyle(v)) return BorderPart::Style;
    if (isBorderWidth(v)) return BorderPart::Width;
    return BorderPart::Color;
}

// Parse "width style color" tokens into their classified parts.
struct BorderComponents {
    std::string width = "medium";
    std::string style = "none";
    std::string color = "currentcolor";
};

BorderComponents parseBorderTokens(const std::vector<std::string>& parts) {
    BorderComponents bc;
    for (auto& p : parts) {
        switch (classifyBorderToken(p)) {
            case BorderPart::Width: bc.width = p; break;
            case BorderPart::Style: bc.style = p; break;
            case BorderPart::Color: bc.color = p; break;
        }
    }
    return bc;
}

void expandBorder(const std::vector<std::string>& parts,
                  std::vector<ExpandedDecl>& out) {
    auto bc = parseBorderTokens(parts);
    const char* sides[] = {"top", "right", "bottom", "left"};
    for (auto side : sides) {
        out.push_back({std::string("border-") + side + "-width", bc.width});
        out.push_back({std::string("border-") + side + "-style", bc.style});
        out.push_back({std::string("border-") + side + "-color", bc.color});
    }
}

void expandBorderSide(const std::string& side,
                      const std::vector<std::string>& parts,
                      std::vector<ExpandedDecl>& out) {
    auto bc = parseBorderTokens(parts);
    out.push_back({"border-" + side + "-width", bc.width});
    out.push_back({"border-" + side + "-style", bc.style});
    out.push_back({"border-" + side + "-color", bc.color});
}

bool isFontWeight(const std::string& v) {
    if (v == "normal" || v == "bold" || v == "bolder" || v == "lighter") return true;
    if (v.size() <= 3) {
        bool allDigit = true;
        for (char c : v) if (!std::isdigit(static_cast<unsigned char>(c))) allDigit = false;
        if (allDigit && !v.empty()) {
            int n = safeStoi(v);
            return n >= 100 && n <= 900;
        }
    }
    return false;
}

bool isFontStyle(const std::string& v) {
    return v == "italic" || v == "oblique";
}

} // anonymous namespace

std::vector<ExpandedDecl> expandShorthand(const std::string& property,
                                           const std::string& value) {
    auto parts = splitValue(value);
    if (parts.empty()) return {{property, value}};

    if (property == "margin") {
        std::vector<ExpandedDecl> out;
        expandBoxShorthand("margin", parts, out);
        return out;
    }
    if (property == "padding") {
        std::vector<ExpandedDecl> out;
        expandBoxShorthand("padding", parts, out);
        return out;
    }
    if (property == "inset") {
        auto bv = resolveBoxValues(parts);
        return {{"top", bv.top}, {"right", bv.right}, {"bottom", bv.bottom}, {"left", bv.left}};
    }

    if (property == "border-width") {
        std::vector<ExpandedDecl> out;
        expandBorderBoxShorthand("width", parts, out);
        return out;
    }
    if (property == "border-style") {
        std::vector<ExpandedDecl> out;
        expandBorderBoxShorthand("style", parts, out);
        return out;
    }
    if (property == "border-color") {
        std::vector<ExpandedDecl> out;
        expandBorderBoxShorthand("color", parts, out);
        return out;
    }

    if (property == "border") {
        std::vector<ExpandedDecl> out;
        expandBorder(parts, out);
        return out;
    }

    if (property == "border-top" || property == "border-right" ||
        property == "border-bottom" || property == "border-left") {
        std::vector<ExpandedDecl> out;
        std::string side = property.substr(7); // after "border-"
        expandBorderSide(side, parts, out);
        return out;
    }

    if (property == "flex") {
        std::vector<ExpandedDecl> out;
        if (value == "none") {
            out.push_back({"flex-grow", "0"});
            out.push_back({"flex-shrink", "0"});
            out.push_back({"flex-basis", "auto"});
        } else if (value == "auto") {
            out.push_back({"flex-grow", "1"});
            out.push_back({"flex-shrink", "1"});
            out.push_back({"flex-basis", "auto"});
        } else if (parts.size() == 1) {
            out.push_back({"flex-grow", parts[0]});
            out.push_back({"flex-shrink", "1"});
            out.push_back({"flex-basis", "0"});
        } else if (parts.size() == 2) {
            out.push_back({"flex-grow", parts[0]});
            if (isBorderWidth(parts[1]) && parts[1].find_first_of("abcdefghijklmnopqrstuvwxyz%") != std::string::npos) {
                out.push_back({"flex-shrink", "1"});
                out.push_back({"flex-basis", parts[1]});
            } else {
                out.push_back({"flex-shrink", parts[1]});
                out.push_back({"flex-basis", "0"});
            }
        } else {
            out.push_back({"flex-grow", parts[0]});
            out.push_back({"flex-shrink", parts[1]});
            out.push_back({"flex-basis", parts[2]});
        }
        return out;
    }

    if (property == "flex-flow") {
        std::vector<ExpandedDecl> out;
        std::string dir = "row", wrap = "nowrap";
        for (auto& p : parts) {
            if (p == "row" || p == "row-reverse" || p == "column" || p == "column-reverse")
                dir = p;
            else if (p == "wrap" || p == "nowrap" || p == "wrap-reverse")
                wrap = p;
        }
        out.push_back({"flex-direction", dir});
        out.push_back({"flex-wrap", wrap});
        return out;
    }

    if (property == "gap") {
        std::vector<ExpandedDecl> out;
        out.push_back({"row-gap", parts[0]});
        out.push_back({"column-gap", parts.size() > 1 ? parts[1] : parts[0]});
        return out;
    }

    if (property == "background") {
        return {{"background-color", value}, {"background-image", "none"}};
    }

    if (property == "font") {
        std::vector<ExpandedDecl> out;
        std::string fontStyle = "normal", fontWeight = "normal",
                    fontSize = "16px", lineHeight = "normal", fontFamily;

        size_t i = 0;
        if (i < parts.size() && isFontStyle(parts[i]))
            fontStyle = parts[i++];
        if (i < parts.size() && isFontWeight(parts[i]))
            fontWeight = parts[i++];
        if (i < parts.size()) {
            auto& sizeStr = parts[i];
            auto slashPos = sizeStr.find('/');
            if (slashPos != std::string::npos) {
                fontSize = sizeStr.substr(0, slashPos);
                lineHeight = sizeStr.substr(slashPos + 1);
            } else {
                fontSize = sizeStr;
            }
            i++;
        }
        for (; i < parts.size(); i++) {
            if (!fontFamily.empty()) fontFamily += " ";
            fontFamily += parts[i];
        }
        if (fontFamily.empty()) fontFamily = "sans-serif";

        out.push_back({"font-style", fontStyle});
        out.push_back({"font-weight", fontWeight});
        out.push_back({"font-size", fontSize});
        out.push_back({"line-height", lineHeight});
        out.push_back({"font-family", fontFamily});
        return out;
    }

    if (property == "list-style") {
        std::string type = "disc", position = "outside";
        for (auto& p : parts) {
            if (p == "inside" || p == "outside")
                position = p;
            else
                type = p;
        }
        return {{"list-style-type", type}, {"list-style-position", position}};
    }

    if (property == "overflow") {
        return {{property, parts[0]}};
    }

    // border-radius shorthand: 1-4 values -> individual corners
    if (property == "border-radius") {
        std::vector<ExpandedDecl> out;
        // Split on '/' for horizontal/vertical radii
        auto slashPos = value.find('/');
        if (slashPos == std::string::npos) {
            auto bv = resolveBoxValues(parts);
            out.push_back({"border-top-left-radius", bv.top});
            out.push_back({"border-top-right-radius", bv.right});
            out.push_back({"border-bottom-right-radius", bv.bottom});
            out.push_back({"border-bottom-left-radius", bv.left});
        } else {
            // For now, just store the full value on each corner
            out.push_back({"border-top-left-radius", value});
            out.push_back({"border-top-right-radius", value});
            out.push_back({"border-bottom-right-radius", value});
            out.push_back({"border-bottom-left-radius", value});
        }
        return out;
    }

    // outline shorthand
    if (property == "outline") {
        std::vector<ExpandedDecl> out;
        std::string width = "medium", style = "none", color = "currentcolor";
        for (auto& p : parts) {
            if (isBorderStyle(p)) style = p;
            else if (isBorderWidth(p)) width = p;
            else color = p;
        }
        out.push_back({"outline-width", width});
        out.push_back({"outline-style", style});
        out.push_back({"outline-color", color});
        return out;
    }

    // transition shorthand — store full value, also set sub-properties from first transition
    if (property == "transition") {
        // Keep the full value and set component properties for the first transition
        return {{property, value}};
    }

    // animation shorthand — store full value
    if (property == "animation") {
        return {{property, value}};
    }

    // columns shorthand: column-width column-count
    if (property == "columns") {
        std::vector<ExpandedDecl> out;
        std::string width = "auto", count = "auto";
        for (auto& p : parts) {
            // Numeric-only values are column-count; values with units are column-width
            bool isNumber = true;
            for (char c : p) {
                if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
                    isNumber = false;
                    break;
                }
            }
            if (p == "auto") continue;
            if (isNumber) count = p;
            else width = p;
        }
        out.push_back({"column-width", width});
        out.push_back({"column-count", count});
        return out;
    }

    // grid-row shorthand: start / end
    if (property == "grid-row") {
        return {{property, value}};
    }

    // grid-column shorthand: start / end
    if (property == "grid-column") {
        return {{property, value}};
    }

    // grid-area shorthand: row-start / col-start / row-end / col-end
    if (property == "grid-area") {
        return {{property, value}};
    }

    // grid-template shorthand — just store sub-properties
    if (property == "grid-template") {
        return {{property, value}};
    }

    // place-items shorthand: align-items justify-items
    if (property == "place-items") {
        std::vector<ExpandedDecl> out;
        out.push_back({"align-items", parts[0]});
        out.push_back({"justify-items", parts.size() > 1 ? parts[1] : parts[0]});
        return out;
    }

    // place-content shorthand: align-content justify-content
    if (property == "place-content") {
        std::vector<ExpandedDecl> out;
        out.push_back({"align-content", parts[0]});
        out.push_back({"justify-content", parts.size() > 1 ? parts[1] : parts[0]});
        return out;
    }

    // place-self shorthand: align-self justify-self
    if (property == "place-self") {
        std::vector<ExpandedDecl> out;
        out.push_back({"align-self", parts[0]});
        out.push_back({"justify-self", parts.size() > 1 ? parts[1] : parts[0]});
        return out;
    }

    // column-rule shorthand
    if (property == "column-rule") {
        std::vector<ExpandedDecl> out;
        auto bc = parseBorderTokens(parts);
        out.push_back({"column-rule-width", bc.width});
        out.push_back({"column-rule-style", bc.style});
        out.push_back({"column-rule-color", bc.color});
        return out;
    }

    // container shorthand: type / name
    if (property == "container") {
        std::vector<ExpandedDecl> out;
        auto slashPos = value.find('/');
        if (slashPos != std::string::npos) {
            std::string type = value.substr(0, slashPos);
            std::string name = value.substr(slashPos + 1);
            // Trim
            auto trimStr = [](std::string& s) {
                size_t a = s.find_first_not_of(" \t"); size_t b = s.find_last_not_of(" \t");
                s = (a != std::string::npos) ? s.substr(a, b - a + 1) : "";
            };
            trimStr(type);
            trimStr(name);
            out.push_back({"container-type", type});
            out.push_back({"container-name", name});
        } else {
            out.push_back({"container-type", value});
            out.push_back({"container-name", "none"});
        }
        return out;
    }

    return {{property, value}};
}

} // namespace htmlayout::css
