#pragma once
#include "layout/box.h"
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace htmlayout::layout {

// A memo in front of the consumer's TextMetrics, alive for exactly one layout pass.
//
// Layout asks the same question over and over. On a 4,848-node document one pass
// makes 59,917 measureWidth() calls that resolve to 457 distinct questions — 131
// repeats each — because min-content sizing, max-content sizing and line breaking
// each walk the same words, and a third of the calls (20,139) ask the width of a
// single space, which is a constant per font.
//
// Shaping is the most expensive thing layout asks anyone to do, so the repeats
// belong to the library, not to every consumer in turn. (bro had already built
// its own cache after finding a relayout spending 15 of its 16ms shaping — but a
// hit there still cost it a hash and two string constructions, and a consumer
// that hasn't discovered this yet pays the shaper 131 times over.)
//
// Scoped to a single pass, which is what makes it safe with no invalidation at
// all: a font cannot finish loading in the middle of a synchronous layout, so the
// consumer's answers cannot change underneath us, and the next pass starts empty.
class MeasureCache final : public TextMetrics {
public:
    explicit MeasureCache(TextMetrics& inner) : inner_(inner) {}

    float measureWidth(std::string_view text, std::string_view family,
                       float size, std::string_view weight) override {
        measureCalls++;
        layoutStatsMut().textMeasures++;
        if (auto it = widths_.find(TextKeyRef{text, family, size, weight});
            it != widths_.end())
            return it->second;
        layoutStatsMut().textShaped++;
        float w = inner_.measureWidth(text, family, size, weight);
        // A document with unboundedly many distinct words still only has as many
        // as it has words, and the map dies with the pass — the cap is a backstop
        // against a pathological one, not an eviction policy. Past it, everything
        // still works, it just goes to the shaper.
        if (widths_.size() < kMaxWidths)
            widths_.emplace(TextKey{std::string(text), std::string(family), size,
                                    std::string(weight)}, w);
        return w;
    }

    // The four font-keyed metrics don't depend on the text at all, so they share
    // one entry per (family, size, weight) and are filled in as they're asked for.
    float lineHeight(std::string_view f, float s, std::string_view w) override {
        return fontMetric(f, s, w, &Font::lineHeight,
                          [&] { return inner_.lineHeight(f, s, w); });
    }
    float ascent(std::string_view f, float s, std::string_view w) override {
        return fontMetric(f, s, w, &Font::ascent,
                          [&] { return inner_.ascent(f, s, w); });
    }
    float xHeight(std::string_view f, float s, std::string_view w) override {
        return fontMetric(f, s, w, &Font::xHeight,
                          [&] { return inner_.xHeight(f, s, w); });
    }
    float naturalHeight(std::string_view f, float s, std::string_view w) override {
        return fontMetric(f, s, w, &Font::naturalHeight,
                          [&] { return inner_.naturalHeight(f, s, w); });
    }

    // Caret geometry passes straight through, uncached.
    //
    // Forwarding matters more than caching here. The base class answers these
    // from prefix measurement, so a MeasureCache that did NOT override them
    // would quietly downgrade a consumer with a real shaper to the fallback
    // — and do it invisibly, since the answers would still be plausible.
    // Layout itself never asks these (caret and selection call the consumer's
    // metrics directly), so there is no repeat traffic to memoize.
    bool clusterAware() const override { return inner_.clusterAware(); }
    CaretXPair caretXAtOffset(std::string_view t, int off, std::string_view f,
                              float s, std::string_view w) override {
        return inner_.caretXAtOffset(t, off, f, s, w);
    }
    int offsetAtCaretX(std::string_view t, float x, std::string_view f,
                       float s, std::string_view w) override {
        return inner_.offsetAtCaretX(t, x, f, s, w);
    }
    ClusterSpan clusterRangeAt(std::string_view t, int off, std::string_view f,
                               float s, std::string_view w) override {
        return inner_.clusterRangeAt(t, off, f, s, w);
    }

    TextMetrics& inner() { return inner_; }

private:
    static constexpr size_t kMaxWidths = 1 << 16;

    // Owning key and a non-owning probe of it. Heterogeneous lookup (is_transparent
    // below) is the whole point: a hit must not build a std::string out of a word
    // it already has a string_view of — that would trade shaping for allocating.
    struct TextKey {
        std::string text, family;
        float size;
        std::string weight;
    };
    struct TextKeyRef {
        std::string_view text, family;
        float size;
        std::string_view weight;
    };
    struct FontKey {
        std::string family;
        float size;
        std::string weight;
    };
    struct FontKeyRef {
        std::string_view family;
        float size;
        std::string_view weight;
    };

    static size_t mix(size_t h, size_t v) { return h * 1000003u ^ v; }
    static size_t hashOf(std::string_view t, std::string_view f, float s,
                         std::string_view w) {
        size_t h = std::hash<std::string_view>{}(t);
        h = mix(h, std::hash<std::string_view>{}(f));
        h = mix(h, std::hash<float>{}(s));
        return mix(h, std::hash<std::string_view>{}(w));
    }

    struct TextHash {
        using is_transparent = void;
        size_t operator()(const TextKey& k) const {
            return hashOf(k.text, k.family, k.size, k.weight);
        }
        size_t operator()(const TextKeyRef& k) const {
            return hashOf(k.text, k.family, k.size, k.weight);
        }
    };
    struct TextEq {
        using is_transparent = void;
        static bool eq(const TextKey& a, std::string_view t, std::string_view f,
                       float s, std::string_view w) {
            return a.size == s && a.text == t && a.family == f && a.weight == w;
        }
        bool operator()(const TextKey& a, const TextKey& b) const {
            return eq(a, b.text, b.family, b.size, b.weight);
        }
        bool operator()(const TextKey& a, const TextKeyRef& b) const {
            return eq(a, b.text, b.family, b.size, b.weight);
        }
        bool operator()(const TextKeyRef& a, const TextKey& b) const {
            return eq(b, a.text, a.family, a.size, a.weight);
        }
    };
    struct FontHash {
        using is_transparent = void;
        size_t operator()(const FontKey& k) const {
            return hashOf({}, k.family, k.size, k.weight);
        }
        size_t operator()(const FontKeyRef& k) const {
            return hashOf({}, k.family, k.size, k.weight);
        }
    };
    struct FontEq {
        using is_transparent = void;
        static bool eq(const FontKey& a, std::string_view f, float s,
                       std::string_view w) {
            return a.size == s && a.family == f && a.weight == w;
        }
        bool operator()(const FontKey& a, const FontKey& b) const {
            return eq(a, b.family, b.size, b.weight);
        }
        bool operator()(const FontKey& a, const FontKeyRef& b) const {
            return eq(a, b.family, b.size, b.weight);
        }
        bool operator()(const FontKeyRef& a, const FontKey& b) const {
            return eq(b, a.family, a.size, a.weight);
        }
    };

    // NaN = not asked for yet. Any real metric is a number, so there is no value
    // a backend could legitimately return that this would shadow.
    struct Font {
        float lineHeight = std::numeric_limits<float>::quiet_NaN();
        float ascent = std::numeric_limits<float>::quiet_NaN();
        float xHeight = std::numeric_limits<float>::quiet_NaN();
        float naturalHeight = std::numeric_limits<float>::quiet_NaN();
    };

    template <typename Compute>
    float fontMetric(std::string_view f, float s, std::string_view w,
                     float Font::*slot, Compute compute) {
        auto it = fonts_.find(FontKeyRef{f, s, w});
        if (it == fonts_.end())
            it = fonts_.emplace(FontKey{std::string(f), s, std::string(w)}, Font{}).first;
        float& cached = it->second.*slot;
        if (std::isnan(cached)) cached = compute();
        return cached;
    }

    TextMetrics& inner_;
    std::unordered_map<TextKey, float, TextHash, TextEq> widths_;
    std::unordered_map<FontKey, Font, FontHash, FontEq> fonts_;
};

} // namespace htmlayout::layout
