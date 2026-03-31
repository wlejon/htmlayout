#include "css/properties.h"
#include <sstream>
#include <algorithm>
#include <cctype>

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
    };
    return props;
}

bool isInherited(const std::string& property) {
    for (auto& p : knownProperties()) {
        if (p.name == property) return p.inherited;
    }
    return false;
}

std::string initialValue(const std::string& property) {
    for (auto& p : knownProperties()) {
        if (p.name == property) return p.initialValue;
    }
    return {};
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

// Expand 1-4 value box shorthand (margin, padding, border-width, etc.)
// 1 value: all four sides
// 2 values: top/bottom, left/right
// 3 values: top, left/right, bottom
// 4 values: top, right, bottom, left
void expandBoxShorthand(const std::string& prefix,
                        const std::vector<std::string>& parts,
                        std::vector<ExpandedDecl>& out) {
    std::string top, right, bottom, left;
    switch (parts.size()) {
        case 1:
            top = right = bottom = left = parts[0];
            break;
        case 2:
            top = bottom = parts[0];
            right = left = parts[1];
            break;
        case 3:
            top = parts[0];
            right = left = parts[1];
            bottom = parts[2];
            break;
        default: // 4+
            top = parts[0];
            right = parts[1];
            bottom = parts[2];
            left = parts.size() > 3 ? parts[3] : parts[2];
            break;
    }
    out.push_back({prefix + "-top", top});
    out.push_back({prefix + "-right", right});
    out.push_back({prefix + "-bottom", bottom});
    out.push_back({prefix + "-left", left});
}

bool isBorderStyle(const std::string& v) {
    return v == "none" || v == "hidden" || v == "dotted" || v == "dashed" ||
           v == "solid" || v == "double" || v == "groove" || v == "ridge" ||
           v == "inset" || v == "outset";
}

bool isBorderWidth(const std::string& v) {
    if (v == "thin" || v == "medium" || v == "thick") return true;
    // Check if it looks like a length (number + optional unit)
    if (v.empty()) return false;
    size_t i = 0;
    if (v[i] == '-' || v[i] == '+') i++;
    bool hasDigit = false;
    while (i < v.size() && (std::isdigit(static_cast<unsigned char>(v[i])) || v[i] == '.')) {
        hasDigit = true;
        i++;
    }
    if (!hasDigit) return false;
    // Rest should be a unit or empty
    std::string unit = v.substr(i);
    return unit.empty() || unit == "px" || unit == "em" || unit == "rem" ||
           unit == "pt" || unit == "%" || unit == "vw" || unit == "vh";
}

// Classify a token as width, style, or color for border shorthand
enum class BorderPart { Width, Style, Color };

BorderPart classifyBorderToken(const std::string& v) {
    if (isBorderStyle(v)) return BorderPart::Style;
    if (isBorderWidth(v)) return BorderPart::Width;
    return BorderPart::Color;
}

// Expand "border: width style color" into border-*-width/style/color for all sides
void expandBorder(const std::vector<std::string>& parts,
                  std::vector<ExpandedDecl>& out) {
    std::string width = "medium", style = "none", color = "currentcolor";
    for (auto& p : parts) {
        switch (classifyBorderToken(p)) {
            case BorderPart::Width: width = p; break;
            case BorderPart::Style: style = p; break;
            case BorderPart::Color: color = p; break;
        }
    }
    const char* sides[] = {"top", "right", "bottom", "left"};
    for (auto side : sides) {
        out.push_back({std::string("border-") + side + "-width", width});
        out.push_back({std::string("border-") + side + "-style", style});
        out.push_back({std::string("border-") + side + "-color", color});
    }
}

// Expand border-top/right/bottom/left shorthand
void expandBorderSide(const std::string& side,
                      const std::vector<std::string>& parts,
                      std::vector<ExpandedDecl>& out) {
    std::string width = "medium", style = "none", color = "currentcolor";
    for (auto& p : parts) {
        switch (classifyBorderToken(p)) {
            case BorderPart::Width: width = p; break;
            case BorderPart::Style: style = p; break;
            case BorderPart::Color: color = p; break;
        }
    }
    out.push_back({"border-" + side + "-width", width});
    out.push_back({"border-" + side + "-style", style});
    out.push_back({"border-" + side + "-color", color});
}

bool isFontWeight(const std::string& v) {
    if (v == "normal" || v == "bold" || v == "bolder" || v == "lighter") return true;
    // Numeric weights: 100-900
    if (v.size() <= 3) {
        bool allDigit = true;
        for (char c : v) if (!std::isdigit(static_cast<unsigned char>(c))) allDigit = false;
        if (allDigit && !v.empty()) {
            int n = std::stoi(v);
            return n >= 100 && n <= 900;
        }
    }
    return false;
}

bool isFontStyle(const std::string& v) {
    return v == "italic" || v == "oblique";
}

bool isFontSize(const std::string& v) {
    // Named sizes
    if (v == "xx-small" || v == "x-small" || v == "small" || v == "medium" ||
        v == "large" || v == "x-large" || v == "xx-large" || v == "smaller" ||
        v == "larger") return true;
    // Length/percentage
    return isBorderWidth(v); // reuse: checks for number+unit
}

} // anonymous namespace

std::vector<ExpandedDecl> expandShorthand(const std::string& property,
                                           const std::string& value) {
    auto parts = splitValue(value);
    if (parts.empty()) return {{property, value}};

    // margin, padding: 1-4 value box model shorthands
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

    // border-width, border-style, border-color: 1-4 value box shorthands
    if (property == "border-width") {
        std::vector<ExpandedDecl> out;
        const char* sides[] = {"top", "right", "bottom", "left"};
        std::string top, right, bottom, left;
        switch (parts.size()) {
            case 1: top = right = bottom = left = parts[0]; break;
            case 2: top = bottom = parts[0]; right = left = parts[1]; break;
            case 3: top = parts[0]; right = left = parts[1]; bottom = parts[2]; break;
            default: top = parts[0]; right = parts[1]; bottom = parts[2]; left = parts[3]; break;
        }
        out.push_back({"border-top-width", top});
        out.push_back({"border-right-width", right});
        out.push_back({"border-bottom-width", bottom});
        out.push_back({"border-left-width", left});
        return out;
    }
    if (property == "border-style") {
        std::vector<ExpandedDecl> out;
        std::string top, right, bottom, left;
        switch (parts.size()) {
            case 1: top = right = bottom = left = parts[0]; break;
            case 2: top = bottom = parts[0]; right = left = parts[1]; break;
            case 3: top = parts[0]; right = left = parts[1]; bottom = parts[2]; break;
            default: top = parts[0]; right = parts[1]; bottom = parts[2]; left = parts[3]; break;
        }
        out.push_back({"border-top-style", top});
        out.push_back({"border-right-style", right});
        out.push_back({"border-bottom-style", bottom});
        out.push_back({"border-left-style", left});
        return out;
    }
    if (property == "border-color") {
        std::vector<ExpandedDecl> out;
        std::string top, right, bottom, left;
        switch (parts.size()) {
            case 1: top = right = bottom = left = parts[0]; break;
            case 2: top = bottom = parts[0]; right = left = parts[1]; break;
            case 3: top = parts[0]; right = left = parts[1]; bottom = parts[2]; break;
            default: top = parts[0]; right = parts[1]; bottom = parts[2]; left = parts[3]; break;
        }
        out.push_back({"border-top-color", top});
        out.push_back({"border-right-color", right});
        out.push_back({"border-bottom-color", bottom});
        out.push_back({"border-left-color", left});
        return out;
    }

    // border: width style color
    if (property == "border") {
        std::vector<ExpandedDecl> out;
        expandBorder(parts, out);
        return out;
    }

    // border-top, border-right, border-bottom, border-left
    if (property == "border-top" || property == "border-right" ||
        property == "border-bottom" || property == "border-left") {
        std::vector<ExpandedDecl> out;
        std::string side = property.substr(7); // after "border-"
        expandBorderSide(side, parts, out);
        return out;
    }

    // flex: grow shrink basis
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
            // Single number = flex-grow (with basis=0)
            out.push_back({"flex-grow", parts[0]});
            out.push_back({"flex-shrink", "1"});
            out.push_back({"flex-basis", "0"});
        } else if (parts.size() == 2) {
            out.push_back({"flex-grow", parts[0]});
            // Second value: if it looks like a number, it's flex-shrink; otherwise flex-basis
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

    // flex-flow: direction wrap
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

    // gap: row-gap column-gap (or single value for both)
    if (property == "gap") {
        std::vector<ExpandedDecl> out;
        if (parts.size() == 1) {
            out.push_back({"row-gap", parts[0]});
            out.push_back({"column-gap", parts[0]});
        } else {
            out.push_back({"row-gap", parts[0]});
            out.push_back({"column-gap", parts[1]});
        }
        return out;
    }

    // background: color (simplified - just extract color)
    if (property == "background") {
        std::vector<ExpandedDecl> out;
        // Simplified: treat single value as background-color
        out.push_back({"background-color", value});
        out.push_back({"background-image", "none"});
        return out;
    }

    // font: [style] [weight] size[/line-height] family
    if (property == "font") {
        std::vector<ExpandedDecl> out;
        std::string fontStyle = "normal", fontWeight = "normal",
                    fontSize = "16px", lineHeight = "normal", fontFamily;

        size_t i = 0;
        // Optional font-style
        if (i < parts.size() && isFontStyle(parts[i])) {
            fontStyle = parts[i++];
        }
        // Optional font-weight
        if (i < parts.size() && isFontWeight(parts[i])) {
            fontWeight = parts[i++];
        }
        // Required font-size (possibly with /line-height)
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
        // Rest is font-family (rejoin with spaces)
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

    // list-style: type position
    if (property == "list-style") {
        std::vector<ExpandedDecl> out;
        std::string type = "disc", position = "outside";
        for (auto& p : parts) {
            if (p == "inside" || p == "outside")
                position = p;
            else
                type = p;
        }
        out.push_back({"list-style-type", type});
        out.push_back({"list-style-position", position});
        return out;
    }

    // overflow: x y (or single for both - we only have one overflow property)
    if (property == "overflow") {
        // Keep as-is since we only have a single overflow property
        return {{property, parts[0]}};
    }

    // Not a recognized shorthand - return as-is
    return {{property, value}};
}

} // namespace htmlayout::css
