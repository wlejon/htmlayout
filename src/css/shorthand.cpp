#include "css/properties.h"
#include "css/shorthand_internal.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace htmlayout::css {

namespace {

// Safe integer parsing that returns a default on failure.
int safeStoi(const std::string& s, int defaultVal = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return defaultVal;
    }
}

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
    // Per spec the `border` shorthand also resets border-image. Resetting the
    // source alone is what disables border-image painting (the remaining
    // longhands are inert without a source), and it keeps the expansion of
    // this very common shorthand small.
    out.push_back({"border-image-source", "none"});
}

// border-image shorthand:
//   <source> || <slice> [/ <width>]? [/ <outset>]? || <repeat>
// e.g. `url(x.png) 30 fill / 20px / 5 round space`. Token classes are
// unambiguous (url()/gradient()/none = source, the four tiling keywords =
// repeat, `fill` belongs to slice), so source and repeat may appear anywhere;
// the numeric groups are assigned slice -> width -> outset in slash order.
// Always emits all five longhands (shorthand semantics reset the rest).
void expandBorderImage(const std::string& value,
                       std::vector<ExpandedDecl>& out) {
    // Tokenize: whitespace-separated at paren/quote depth 0; '/' is its own
    // token at depth 0 (slashes inside url(...) paths stay put).
    std::vector<std::string> toks;
    {
        std::string t;
        int depth = 0;
        bool inQ = false;
        char qc = 0;
        for (char c : value) {
            if (inQ) { t += c; if (c == qc) inQ = false; continue; }
            if (c == '"' || c == '\'') { inQ = true; qc = c; t += c; continue; }
            if (c == '(') { ++depth; t += c; continue; }
            if (c == ')') { --depth; t += c; continue; }
            if (depth == 0 && std::isspace(static_cast<unsigned char>(c))) {
                if (!t.empty()) { toks.push_back(t); t.clear(); }
                continue;
            }
            if (depth == 0 && c == '/') {
                if (!t.empty()) { toks.push_back(t); t.clear(); }
                toks.push_back("/");
                continue;
            }
            t += c;
        }
        if (!t.empty()) toks.push_back(t);
    }

    std::string source = "none";
    std::vector<std::string> slice, width, outset, repeat;
    bool fill = false;
    int slot = 0;  // 0 = slice, 1 = width, 2 = outset
    for (auto& t : toks) {
        std::string lower = t;
        for (auto& ch : lower)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (t == "/") { if (slot < 2) ++slot; continue; }
        if (lower == "none") { source = "none"; continue; }
        if (lower.compare(0, 4, "url(") == 0 ||
            lower.compare(0, 6, "image(") == 0 ||
            lower.find("-gradient(") != std::string::npos ||
            lower.compare(0, 16, "linear-gradient(") == 0 ||
            lower.compare(0, 16, "radial-gradient(") == 0 ||
            lower.compare(0, 15, "conic-gradient(") == 0) {
            source = t;
            continue;
        }
        if (lower == "stretch" || lower == "repeat" || lower == "round" ||
            lower == "space") {
            if (repeat.size() < 2) repeat.push_back(lower);
            continue;
        }
        if (lower == "fill") { fill = true; continue; }
        if (slot == 0) slice.push_back(t);
        else if (slot == 1) width.push_back(t);
        else outset.push_back(t);
    }

    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += " ";
            s += v[i];
        }
        return s;
    };
    std::string sliceVal = slice.empty() ? "100%" : join(slice);
    if (fill) sliceVal += " fill";
    out.push_back({"border-image-source", source});
    out.push_back({"border-image-slice", sliceVal});
    out.push_back({"border-image-width", width.empty() ? "1" : join(width)});
    out.push_back({"border-image-outset", outset.empty() ? "0" : join(outset)});
    out.push_back({"border-image-repeat", repeat.empty() ? "stretch" : join(repeat)});
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

std::string asciiLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// line-clamp (CSS Overflow 4):
//   none | auto | [ <integer [1,∞]> || <'block-ellipsis'> ] -webkit-legacy?
// into max-lines / block-ellipsis / continue. An invalid value expands to
// nothing, so the declaration is dropped.
void expandLineClamp(const std::vector<std::string>& parts, std::vector<ExpandedDecl>& out) {
    auto emit = [&](std::string maxLines, std::string ellipsis, std::string cont) {
        out.push_back({"max-lines", std::move(maxLines)});
        out.push_back({"block-ellipsis", std::move(ellipsis)});
        out.push_back({"continue", std::move(cont)});
    };
    if (parts.size() == 1) {
        std::string v = asciiLower(parts[0]);
        if (v == "inherit" || v == "initial" || v == "unset" || v == "revert" ||
            v == "revert-layer") {
            emit(v, v, v);
            return;
        }
        if (v == "none") { emit("none", "no-ellipsis", "auto"); return; }
        if (v == "auto") { emit("none", "auto", "collapse"); return; }
    }
    std::string count, ellipsis;
    bool legacy = false;
    for (size_t i = 0; i < parts.size(); i++) {
        const std::string& p = parts[i];
        std::string lower = asciiLower(p);
        if (legacy) return;  // -webkit-legacy comes last
        if (lower == "-webkit-legacy") {
            if (i == 0) return;  // needs something to modify
            legacy = true;
            continue;
        }
        bool digits = !p.empty() && std::all_of(p.begin(), p.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
        if (digits || (p.size() > 1 && p[0] == '+' &&
                       std::all_of(p.begin() + 1, p.end(), [](char c) {
                           return c >= '0' && c <= '9';
                       }))) {
            if (!count.empty()) return;
            std::string d = p[0] == '+' ? p.substr(1) : p;
            size_t nz = d.find_first_not_of('0');
            if (nz == std::string::npos) return;  // 0 is out of range
            count = d.substr(nz);
            continue;
        }
        bool isString = p.size() >= 2 && (p[0] == '"' || p[0] == '\'') && p.back() == p[0];
        if (isString || lower == "auto" || lower == "no-ellipsis") {
            if (!ellipsis.empty()) return;
            ellipsis = isString ? p : lower;
            continue;
        }
        return;
    }
    if (count.empty() && ellipsis.empty()) return;
    emit(count.empty() ? "none" : count, ellipsis.empty() ? "auto" : ellipsis,
         legacy ? "-webkit-legacy" : "collapse");
}

} // anonymous namespace

std::vector<ExpandedDecl> expandShorthand(const std::string& property,
                                           const std::string& value) {
    if (!isShorthandProperty(property))
        return {{property, value}};

    auto parts = splitValue(value);
    if (parts.empty()) return {{property, value}};

    if (property == "line-clamp") {
        std::vector<ExpandedDecl> out;
        expandLineClamp(parts, out);
        return out;
    }

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

    if (property == "border-image") {
        std::vector<ExpandedDecl> out;
        expandBorderImage(value, out);
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
        return expandBackgroundShorthand(value);
    }

    if (property == "font") {
        std::vector<ExpandedDecl> out;
        // A CSS-wide keyword sets every longhand to itself (CSS Cascade 4
        // §7.3) — `button { font: inherit }` inherits family, size, weight,
        // style and line-height alike.
        if (parts.size() == 1) {
            std::string kw = asciiLower(parts[0]);
            if (kw == "inherit" || kw == "initial" || kw == "unset" || kw == "revert" ||
                kw == "revert-layer") {
                for (const char* p : {"font-style", "font-weight", "font-size", "line-height",
                                      "font-family", "font-variant-numeric"})
                    out.push_back({p, kw});
                return out;
            }
        }
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
        // The shorthand resets every font-variant-* longhand (CSS Fonts 4
        // §2.8); font-feature-settings is not one of its subproperties.
        out.push_back({"font-variant-numeric", "normal"});
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
        // overflow shorthand: 1 value = both axes; 2 values = x y
        std::string x = parts[0];
        std::string y = parts.size() > 1 ? parts[1] : parts[0];
        return {{"overflow", x}, {"overflow-x", x}, {"overflow-y", y}};
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

    // transition shorthand: the full value plus the five longhands.
    if (property == "transition") {
        return expandTransitionShorthand(value);
    }

    // animation shorthand: the full value (for getComputedStyle().animation)
    // plus the eight longhands, each a comma list with one entry per layer.
    if (property == "animation") {
        return expandAnimationShorthand(value);
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

    // CSS Logical Properties L1 — map to physical properties.
    // For horizontal-tb writing mode (the only one fully supported):
    //   inline axis = horizontal (start=left, end=right for ltr)
    //   block axis = vertical (start=top, end=bottom)

    // Logical sizing properties
    if (property == "inline-size") return {{"width", value}};
    if (property == "block-size") return {{"height", value}};
    if (property == "min-inline-size") return {{"min-width", value}};
    if (property == "min-block-size") return {{"min-height", value}};
    if (property == "max-inline-size") return {{"max-width", value}};
    if (property == "max-block-size") return {{"max-height", value}};

    // Logical margin longhands. Block-axis maps to physical directly (writing
    // mode is horizontal-tb); inline-axis (start/end) depends on `direction`
    // and is resolved to physical (left/right) after the cascade knows it
    // (Cascade::resolve, see resolveInlineLogical). Keep the logical longhand
    // in the map so that step verbatim.
    if (property == "margin-inline-start") return {{"margin-inline-start", value}};
    if (property == "margin-inline-end") return {{"margin-inline-end", value}};
    if (property == "margin-block-start") return {{"margin-top", value}};
    if (property == "margin-block-end") return {{"margin-bottom", value}};

    // Logical margin shorthands
    if (property == "margin-inline") {
        return {{"margin-inline-start", parts[0]},
                {"margin-inline-end", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "margin-block") {
        return {{"margin-top", parts[0]},
                {"margin-bottom", parts.size() > 1 ? parts[1] : parts[0]}};
    }

    // Logical padding longhands
    if (property == "padding-inline-start") return {{"padding-inline-start", value}};
    if (property == "padding-inline-end") return {{"padding-inline-end", value}};
    if (property == "padding-block-start") return {{"padding-top", value}};
    if (property == "padding-block-end") return {{"padding-bottom", value}};

    // Logical padding shorthands
    if (property == "padding-inline") {
        return {{"padding-inline-start", parts[0]},
                {"padding-inline-end", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "padding-block") {
        return {{"padding-top", parts[0]},
                {"padding-bottom", parts.size() > 1 ? parts[1] : parts[0]}};
    }

    // Logical border longhands. Inline-axis width/style/color stay logical and
    // resolve to physical after the cascade (direction-dependent); block-axis
    // maps to physical directly.
    if (property == "border-inline-start-width") return {{"border-inline-start-width", value}};
    if (property == "border-inline-end-width") return {{"border-inline-end-width", value}};
    if (property == "border-block-start-width") return {{"border-top-width", value}};
    if (property == "border-block-end-width") return {{"border-bottom-width", value}};
    if (property == "border-inline-start-style") return {{"border-inline-start-style", value}};
    if (property == "border-inline-end-style") return {{"border-inline-end-style", value}};
    if (property == "border-block-start-style") return {{"border-top-style", value}};
    if (property == "border-block-end-style") return {{"border-bottom-style", value}};
    if (property == "border-inline-start-color") return {{"border-inline-start-color", value}};
    if (property == "border-inline-end-color") return {{"border-inline-end-color", value}};
    if (property == "border-block-start-color") return {{"border-top-color", value}};
    if (property == "border-block-end-color") return {{"border-bottom-color", value}};

    // Logical border shorthands (border-inline-start, border-block-end, etc.)
    if (property == "border-inline-start") {
        auto bc = parseBorderTokens(parts);
        return {{"border-inline-start-width", bc.width}, {"border-inline-start-style", bc.style}, {"border-inline-start-color", bc.color}};
    }
    if (property == "border-inline-end") {
        auto bc = parseBorderTokens(parts);
        return {{"border-inline-end-width", bc.width}, {"border-inline-end-style", bc.style}, {"border-inline-end-color", bc.color}};
    }
    if (property == "border-block-start") {
        auto bc = parseBorderTokens(parts);
        return {{"border-top-width", bc.width}, {"border-top-style", bc.style}, {"border-top-color", bc.color}};
    }
    if (property == "border-block-end") {
        auto bc = parseBorderTokens(parts);
        return {{"border-bottom-width", bc.width}, {"border-bottom-style", bc.style}, {"border-bottom-color", bc.color}};
    }

    // border-inline / border-block (apply same to both sides)
    if (property == "border-inline") {
        auto bc = parseBorderTokens(parts);
        return {{"border-inline-start-width", bc.width}, {"border-inline-start-style", bc.style}, {"border-inline-start-color", bc.color},
                {"border-inline-end-width", bc.width}, {"border-inline-end-style", bc.style}, {"border-inline-end-color", bc.color}};
    }
    if (property == "border-block") {
        auto bc = parseBorderTokens(parts);
        return {{"border-top-width", bc.width}, {"border-top-style", bc.style}, {"border-top-color", bc.color},
                {"border-bottom-width", bc.width}, {"border-bottom-style", bc.style}, {"border-bottom-color", bc.color}};
    }

    // border-inline-width / border-block-width (1 or 2 values)
    if (property == "border-inline-width") {
        return {{"border-inline-start-width", parts[0]},
                {"border-inline-end-width", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "border-block-width") {
        return {{"border-top-width", parts[0]},
                {"border-bottom-width", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "border-inline-style") {
        return {{"border-inline-start-style", parts[0]},
                {"border-inline-end-style", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "border-block-style") {
        return {{"border-top-style", parts[0]},
                {"border-bottom-style", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "border-inline-color") {
        return {{"border-inline-start-color", parts[0]},
                {"border-inline-end-color", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "border-block-color") {
        return {{"border-top-color", parts[0]},
                {"border-bottom-color", parts.size() > 1 ? parts[1] : parts[0]}};
    }

    // Logical inset longhands. Inline-axis stays logical (direction-dependent);
    // block-axis maps to physical directly.
    if (property == "inset-inline-start") return {{"inset-inline-start", value}};
    if (property == "inset-inline-end") return {{"inset-inline-end", value}};
    if (property == "inset-block-start") return {{"top", value}};
    if (property == "inset-block-end") return {{"bottom", value}};

    // Logical inset shorthands
    if (property == "inset-inline") {
        return {{"inset-inline-start", parts[0]}, {"inset-inline-end", parts.size() > 1 ? parts[1] : parts[0]}};
    }
    if (property == "inset-block") {
        return {{"top", parts[0]}, {"bottom", parts.size() > 1 ? parts[1] : parts[0]}};
    }

    return {{property, value}};
}

} // namespace htmlayout::css
