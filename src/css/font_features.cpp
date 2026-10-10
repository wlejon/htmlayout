#include "css/font_features.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace htmlayout::css {

namespace {

using FeatureMap = std::map<std::string, uint32_t>;   // sorted by tag

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

bool isNormal(std::string_view v) {
    v = trim(v);
    if (v.empty()) return true;
    if (v.size() != 6) return false;
    for (size_t i = 0; i < 6; ++i)
        if (std::tolower(static_cast<unsigned char>(v[i])) != "normal"[i]) return false;
    return true;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// font-variant-numeric. Each keyword belongs to a group that may appear once.
bool parseVariantNumeric(std::string_view value, FeatureMap& out) {
    struct Kw { const char* name; const char* tag; int group; };
    static const Kw kKeywords[] = {
        {"lining-nums",        "lnum", 0}, {"oldstyle-nums",     "onum", 0},
        {"proportional-nums",  "pnum", 1}, {"tabular-nums",      "tnum", 1},
        {"diagonal-fractions", "frac", 2}, {"stacked-fractions", "afrc", 2},
        {"ordinal",            "ordn", 3}, {"slashed-zero",      "zero", 4},
    };
    FeatureMap mine;
    bool seen[5] = {};
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && isSpace(value[i])) ++i;
        size_t start = i;
        while (i < value.size() && !isSpace(value[i])) ++i;
        if (i == start) break;
        const std::string word = lower(value.substr(start, i - start));
        const Kw* hit = nullptr;
        for (const auto& k : kKeywords)
            if (word == k.name) { hit = &k; break; }
        if (!hit || seen[hit->group]) return false;   // invalid declaration
        seen[hit->group] = true;
        mine[hit->tag] = 1;
    }
    for (auto& [tag, v] : mine) out[tag] = v;
    return true;
}

// font-feature-settings: a comma list of `"tag" [<integer> | on | off]?`.
bool parseFeatureSettings(std::string_view value, FeatureMap& out) {
    FeatureMap mine;
    size_t i = 0;
    const size_t n = value.size();
    auto skipWs = [&] { while (i < n && isSpace(value[i])) ++i; };
    for (;;) {
        skipWs();
        if (i >= n) return false;                     // empty entry
        const char q = value[i];
        if (q != '"' && q != '\'') return false;
        const size_t tagStart = ++i;
        while (i < n && value[i] != q) ++i;
        if (i >= n) return false;
        std::string tag(value.substr(tagStart, i - tagStart));
        ++i;                                          // closing quote
        if (tag.size() != 4) return false;
        for (char c : tag)
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)
                return false;

        uint32_t v = 1;
        skipWs();
        if (i < n && value[i] != ',') {
            const size_t start = i;
            while (i < n && !isSpace(value[i]) && value[i] != ',') ++i;
            const std::string word = lower(value.substr(start, i - start));
            if (word == "on") {
                v = 1;
            } else if (word == "off") {
                v = 0;
            } else {
                // A non-negative <integer>.
                size_t k = 0;
                if (k < word.size() && word[k] == '+') ++k;
                if (k >= word.size()) return false;
                uint64_t acc = 0;
                for (; k < word.size(); ++k) {
                    if (!std::isdigit(static_cast<unsigned char>(word[k]))) return false;
                    acc = std::min<uint64_t>(acc * 10 + static_cast<uint64_t>(word[k] - '0'),
                                             UINT32_MAX);
                }
                v = static_cast<uint32_t>(acc);
            }
            skipWs();
        }
        mine[tag] = v;                                // a repeated tag: last wins
        if (i >= n) break;
        if (value[i] != ',') return false;
        ++i;
    }
    for (auto& [tag, v] : mine) out[tag] = v;
    return true;
}

} // namespace

std::string resolveFontFeatures(std::string_view variantNumeric,
                                std::string_view featureSettings) {
    FeatureMap features;
    if (!isNormal(variantNumeric)) parseVariantNumeric(trim(variantNumeric), features);
    if (!isNormal(featureSettings)) parseFeatureSettings(trim(featureSettings), features);
    std::string out;
    for (const auto& [tag, v] : features) {
        if (!out.empty()) out += ',';
        out += tag;
        out += '=';
        out += std::to_string(v);
    }
    return out;
}

std::string_view internFontFeatures(std::string_view variantNumeric,
                                    std::string_view featureSettings) {
    if (isNormal(variantNumeric) && isNormal(featureSettings)) return {};

    // Inputs -> interned result, so a repeat costs one lookup and no parse.
    // Both containers are node-based, so a stored string never moves. The set
    // grows with the number of distinct declarations a process ever sees,
    // which is a handful.
    static std::mutex mu;
    static std::unordered_map<std::string, std::string_view> byInput;
    static std::unordered_set<std::string> interned;

    std::string key;
    key.reserve(variantNumeric.size() + featureSettings.size() + 1);
    key.append(variantNumeric);
    key += '\x1f';
    key.append(featureSettings);

    std::lock_guard<std::mutex> lock(mu);
    if (auto it = byInput.find(key); it != byInput.end()) return it->second;
    std::string resolved = resolveFontFeatures(variantNumeric, featureSettings);
    std::string_view view;
    if (!resolved.empty()) view = *interned.insert(std::move(resolved)).first;
    byInput.emplace(std::move(key), view);
    return view;
}

} // namespace htmlayout::css
