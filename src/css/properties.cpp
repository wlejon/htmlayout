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

    return {{property, value}};
}

} // namespace htmlayout::css
