#pragma once
#include <string>
#include <string_view>

namespace htmlayout::css {

// OpenType features from `font-variant-numeric` and `font-feature-settings`.
//
// Both properties resolve to one list of (tag, value) features, written in a
// canonical text form: `tag=value` pairs joined by commas, sorted by tag, one
// entry per tag ("tnum=1,zero=1"). Equal feature sets give equal strings, so
// the string is usable as-is in a measurement or shaping cache key, and the
// empty string means "no features" — the common case, which every consumer can
// keep on exactly its featureless path.
//
// font-variant-numeric: normal | [ lining-nums | oldstyle-nums ] ||
//   [ proportional-nums | tabular-nums ] || [ diagonal-fractions |
//   stacked-fractions ] || ordinal || slashed-zero
//   → lnum / onum, pnum / tnum, frac / afrc, ordn, zero (all = 1).
// font-feature-settings: normal | [ <string> [ <integer> | on | off ]? ]#
//   → the tag with 1 (no value / on), 0 (off) or the integer.
//
// font-feature-settings is applied after font-variant-numeric, so it wins for
// a tag both set (CSS Fonts 4 §7.2). A value that does not parse (an unknown
// keyword, two values from one group, a tag that is not four printable ASCII
// characters) makes that property contribute nothing, as an invalid
// declaration would.
std::string resolveFontFeatures(std::string_view variantNumeric,
                                std::string_view featureSettings);

// resolveFontFeatures(), interned: the returned view stays valid for the life
// of the process. Both properties at `normal` (or empty) return an empty view
// without taking the intern lock. Safe to call from any thread.
std::string_view internFontFeatures(std::string_view variantNumeric,
                                    std::string_view featureSettings);

} // namespace htmlayout::css
