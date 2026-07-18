#pragma once

// -----------------------------------------------------------------------------
// Bidi reordering at line-box construction.
//
// UAX #9 resolves embedding levels over a PARAGRAPH and reorders them per LINE.
// That is why this is a separate step run after line breaking rather than
// something the text splitter could have done: only once the breaker has
// decided which runs share a line is there a line to reorder, and only the
// whole paragraph gives the W and N rules the context they need.
//
// Every inline formatting context in this library funnels through here — the
// dedicated IFC, the anonymous-block IFC, and the pure-inline path all lay out
// a vector of items and then need the same permutation.
// -----------------------------------------------------------------------------

#include "layout/box.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace htmlayout::layout {

// One line item, as bidi sees it. Callers build these from whatever their own
// item type is; nothing else about an item matters to reordering.
struct BidiItem {
    // The item's display text. Empty for an atomic inline box, which
    // participates as a single neutral character (CSS says an atomic inline is
    // U+FFFC OBJECT REPLACEMENT CHARACTER for bidi purposes) rather than as
    // its contents.
    std::string_view text;

    // The item's own `direction` differs from the line's base direction. Such
    // an item is an isolate: its contents resolve independently and the item as
    // a whole sits at the opposite level, whatever its text says. This is what
    // makes `<span dir="rtl">` work regardless of the characters inside it.
    bool opposesBase = false;

    // Not part of the visual ordering (a forced break). Kept in place so the
    // caller's indices stay meaningful.
    bool excluded = false;
};

// The visual order of `items` — visual slot -> index into `items`. Excluded
// items keep their logical position.
//
// `metrics` supplies the character-level resolution; with a consumer that has
// no Unicode implementation this still reorders correctly by `direction` alone,
// which is what the library did before text bidi existed.
std::vector<int> visualOrderForLine(const std::vector<BidiItem>& items,
                                    bool rtlBase,
                                    TextMetrics& metrics);

} // namespace htmlayout::layout
