// Media query evaluation: @media conditions and window.matchMedia() against a
// consumer-supplied MediaContext (viewport, media type, color scheme, device
// pixel ratio).
#include "css/parser.h"
#include "../from_chars_compat.h"
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace htmlayout::css {

namespace {

// Parse a length value like "768px" to pixels
float parseMediaLength(const std::string& s) {
    float num = 0;
    const char* begin = s.data();
    const char* end = begin + s.size();
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
    if (ec != std::errc()) return 0;
    // unit is ignored for now (assume px)
    return num;
}

// Trim whitespace helper
void trimInPlace(std::string& str) {
    size_t a = str.find_first_not_of(" \t\n\r\f");
    size_t b = str.find_last_not_of(" \t\n\r\f");
    str = (a == std::string::npos) ? "" : str.substr(a, b - a + 1);
}

// Compare helper: apply a comparison operator
bool applyComparison(float lhs, const std::string& op, float rhs) {
    if (op == ">") return lhs > rhs;
    if (op == ">=") return lhs >= rhs;
    if (op == "<") return lhs < rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == "=") return std::fabs(lhs - rhs) < 1e-4f;
    return false;
}

// A resolution feature, answered from MediaContext::resolution (dppx).
// `-webkit-device-pixel-ratio` is the prefixed spelling engines still honour.
bool isResolutionFeature(const std::string& name) {
    return name == "resolution" || name == "-webkit-device-pixel-ratio";
}

// Parse a <resolution> ("2dppx", "2x", "192dpi", "75.6dpcm") into dppx. A bare
// number is taken as dppx, which is what the -webkit-*-device-pixel-ratio
// features carry. Returns -1 when the value is not a resolution.
float parseMediaResolution(const std::string& s) {
    float num = 0;
    const char* begin = s.data();
    const char* end = begin + s.size();
    auto [ptr, ec] = htmlayout::from_chars_fp(begin, end, num);
    if (ec != std::errc()) return -1;
    std::string unit(ptr, end);
    trimInPlace(unit);
    for (auto& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (unit.empty() || unit == "dppx" || unit == "x") return num;
    if (unit == "dpi") return num / 96.0f;
    if (unit == "dpcm") return num * 2.54f / 96.0f;
    return -1;
}

// The comparison operand for `feature`: a resolution for resolution features,
// a length otherwise.
float parseMediaOperand(const std::string& feature, const std::string& s) {
    return isResolutionFeature(feature) ? parseMediaResolution(s) : parseMediaLength(s);
}

// Resolve a media feature name to its value from the context
float resolveMediaFeatureValue(const std::string& name, const MediaContext& ctx) {
    if (name == "width") return ctx.viewportWidth;
    if (name == "height") return ctx.viewportHeight;
    if (isResolutionFeature(name)) return ctx.resolution;
    return -1;
}

// Try to parse range syntax: "width > 500px", "width >= 500px",
// "500px < width", "500px <= width <= 800px"
// Returns true if parsed as range, false if not range syntax.
bool tryEvaluateRange(const std::string& f, const MediaContext& ctx, bool& result) {
    // Look for comparison operators: >=, <=, >, <, =
    // Possible forms:
    //   feature > value
    //   feature >= value
    //   value < feature
    //   value <= feature <= value2

    // Find comparison operators
    struct CompPart { std::string token; bool isOp; };
    std::vector<CompPart> parts;
    size_t i = 0;
    std::string current;
    while (i < f.size()) {
        if ((f[i] == '>' || f[i] == '<' || f[i] == '=') && i > 0) {
            if (!current.empty()) {
                std::string t = current; trimInPlace(t);
                if (!t.empty()) parts.push_back({t, false});
                current.clear();
            }
            std::string op(1, f[i]);
            if (i + 1 < f.size() && f[i + 1] == '=') { op += '='; i++; }
            parts.push_back({op, true});
        } else {
            current += f[i];
        }
        i++;
    }
    if (!current.empty()) {
        std::string t = current; trimInPlace(t);
        if (!t.empty()) parts.push_back({t, false});
    }

    // Need at least: value op value (3 parts) with at least one operator
    if (parts.size() < 3) return false;

    // Check if this looks like range syntax (has comparison operators)
    bool hasOp = false;
    for (auto& p : parts) if (p.isOp) { hasOp = true; break; }
    if (!hasOp) return false;

    // Simple range: feature op value  or  value op feature
    if (parts.size() == 3 && parts[1].isOp) {
        float featureVal = resolveMediaFeatureValue(parts[0].token, ctx);
        if (featureVal >= 0) {
            // feature op value
            float rhs = parseMediaOperand(parts[0].token, parts[2].token);
            result = applyComparison(featureVal, parts[1].token, rhs);
            return true;
        }
        featureVal = resolveMediaFeatureValue(parts[2].token, ctx);
        if (featureVal >= 0) {
            // value op feature
            float lhs = parseMediaOperand(parts[2].token, parts[0].token);
            result = applyComparison(lhs, parts[1].token, featureVal);
            return true;
        }
    }

    // Chained range: value op feature op value2 (5 parts)
    if (parts.size() == 5 && parts[1].isOp && parts[3].isOp) {
        float featureVal = resolveMediaFeatureValue(parts[2].token, ctx);
        if (featureVal >= 0) {
            float lhs = parseMediaOperand(parts[2].token, parts[0].token);
            float rhs = parseMediaOperand(parts[2].token, parts[4].token);
            result = applyComparison(lhs, parts[1].token, featureVal) &&
                     applyComparison(featureVal, parts[3].token, rhs);
            return true;
        }
    }

    return false;
}

// Evaluate a single media feature like "(min-width: 768px)" or "(width > 500px)"
bool evaluateMediaFeature(const std::string& feature, const MediaContext& ctx) {
    // Strip parens and trim
    std::string f = feature;
    if (!f.empty() && f.front() == '(') f.erase(0, 1);
    if (!f.empty() && f.back() == ')') f.pop_back();
    trimInPlace(f);

    if (f.empty()) return false;

    // Try range syntax first
    bool rangeResult = false;
    if (tryEvaluateRange(f, ctx, rangeResult)) {
        return rangeResult;
    }

    auto colonPos = f.find(':');
    if (colonPos == std::string::npos) {
        // Boolean feature, e.g. (color)
        return true;
    }

    std::string name = f.substr(0, colonPos);
    std::string value = f.substr(colonPos + 1);
    trimInPlace(name); trimInPlace(value);

    // Discrete (non-length) features first.
    if (name == "prefers-color-scheme") {
        for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value == ctx.colorScheme;
    }

    // Resolution (dppx) against the device pixel ratio, standard and
    // -webkit- prefixed spellings.
    {
        std::string feat = name;
        int bound = 0;  // -1 max, 0 exact, +1 min
        auto stripPrefix = [&](const std::string& plain, const std::string& prefixed) {
            if (feat.rfind(plain, 0) == 0) { feat.erase(0, plain.size()); return true; }
            if (feat.rfind(prefixed, 0) == 0) {
                feat = "-webkit-" + feat.substr(prefixed.size());
                return true;
            }
            return false;
        };
        if (stripPrefix("min-", "-webkit-min-")) bound = 1;
        else if (stripPrefix("max-", "-webkit-max-")) bound = -1;
        if (isResolutionFeature(feat)) {
            float res = parseMediaResolution(value);
            if (res < 0) return false;
            if (bound > 0) return ctx.resolution >= res - 1e-4f;
            if (bound < 0) return ctx.resolution <= res + 1e-4f;
            return std::fabs(ctx.resolution - res) < 1e-4f;
        }
    }

    float val = parseMediaLength(value);

    if (name == "min-width") return ctx.viewportWidth >= val;
    if (name == "max-width") return ctx.viewportWidth <= val;
    if (name == "min-height") return ctx.viewportHeight >= val;
    if (name == "max-height") return ctx.viewportHeight <= val;
    if (name == "width") return ctx.viewportWidth == val;
    if (name == "height") return ctx.viewportHeight == val;
    if (name == "orientation") {
        if (value == "portrait") return ctx.viewportHeight >= ctx.viewportWidth;
        if (value == "landscape") return ctx.viewportWidth > ctx.viewportHeight;
    }

    return false;
}

} // anonymous namespace

bool evaluateMediaQuery(const std::string& condition, const MediaContext& ctx) {
    if (condition.empty() || condition == "all") return true;

    // Media query LIST: top-level commas separate independent queries;
    // the list matches when ANY query matches (Media Queries §2.1).
    {
        int depth = 0;
        size_t start = 0;
        std::vector<std::string> parts;
        for (size_t i = 0; i < condition.size(); i++) {
            char c = condition[i];
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (c == ',' && depth == 0) {
                parts.push_back(condition.substr(start, i - start));
                start = i + 1;
            }
        }
        if (!parts.empty()) {
            parts.push_back(condition.substr(start));
            for (auto& p : parts) {
                std::string q = p;
                trimInPlace(q);
                if (!q.empty() && evaluateMediaQuery(q, ctx)) return true;
            }
            return false;
        }
    }

    // Handle media type prefixes: "screen and (...)", "print", etc.
    std::string cond = condition;

    // Simple "not" prefix
    bool negate = false;
    if (cond.size() > 4 && cond.substr(0, 4) == "not ") {
        negate = true;
        cond = cond.substr(4);
    }

    // Check for media type
    std::string mediaType;
    auto andPos = cond.find(" and ");
    if (andPos != std::string::npos && cond[0] != '(') {
        mediaType = cond.substr(0, andPos);
        cond = cond.substr(andPos + 5);
    } else if (cond[0] != '(') {
        mediaType = cond;
        cond.clear();
    }

    // Trim media type
    if (!mediaType.empty()) {
        for (auto& c : mediaType) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t s = mediaType.find_first_not_of(" \t");
        size_t e = mediaType.find_last_not_of(" \t");
        if (s != std::string::npos) mediaType = mediaType.substr(s, e - s + 1);

        if (mediaType != "all" && mediaType != ctx.mediaType) {
            return negate;
        }
    }

    if (cond.empty()) return !negate;

    // Extract parenthesized features and connecting keywords (and/or)
    std::vector<std::string> features;
    std::vector<std::string> connectors; // "and" or "or" between features

    size_t i = 0;
    while (i < cond.size()) {
        auto paren = cond.find('(', i);
        if (paren == std::string::npos) break;

        // Check for connector keyword between previous feature and this one
        if (!features.empty()) {
            std::string between = cond.substr(i, paren - i);
            trimInPlace(between);
            if (between == "or") connectors.push_back("or");
            else connectors.push_back("and"); // default is "and"
        }

        int depth = 0;
        size_t j = paren;
        while (j < cond.size()) {
            if (cond[j] == '(') depth++;
            else if (cond[j] == ')') { depth--; if (depth == 0) break; }
            j++;
        }
        features.push_back(cond.substr(paren, j - paren + 1));
        i = j + 1;
    }

    // Evaluate with and/or logic
    bool result = true;
    if (!features.empty()) {
        result = evaluateMediaFeature(features[0], ctx);
        for (size_t fi = 1; fi < features.size(); fi++) {
            bool val = evaluateMediaFeature(features[fi], ctx);
            if (fi - 1 < connectors.size() && connectors[fi - 1] == "or") {
                result = result || val;
            } else {
                result = result && val;
            }
        }
    }

    return negate ? !result : result;
}


} // namespace htmlayout::css
