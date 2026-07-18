#include "layout/bidi_line.h"

#include <cctype>

namespace htmlayout::layout {

namespace {

// U+FFFC OBJECT REPLACEMENT CHARACTER, the stand-in CSS gives an atomic inline
// box when the bidi algorithm runs over a line. Its bidi class is ON, so it
// takes the direction of whatever surrounds it — which is exactly how an image
// or an inline-block between two Arabic words should behave.
constexpr char kObjectReplacement[] = "\xEF\xBF\xBC";

// How much of an inline element's text is worth gathering to decide its level.
// The level comes from the first strong character; anything past the first few
// dozen bytes cannot change the answer for the item as a whole.
constexpr size_t kSubtreeTextBudget = 256;

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Rule L1 asks whether a trailing item is whitespace. Whitespace that reaches
// layout has already been collapsed to ASCII spaces by the text splitter, so
// this does not need the full Unicode whitespace set.
// Empty is NOT whitespace here. An item with no display text of its own is an
// element or an atomic box, not a space — and treating it as one would let the
// L1 scan below walk straight past it and reset the whole line to the paragraph
// level, undoing the reordering it was supposed to refine.
bool isAllWhitespace(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!isAsciiSpace(c)) return false;
    }
    return true;
}

// Append a subtree's text, depth first. Bounded: bidi only ever needs enough
// characters to find the strong ones that decide the item's level, and an
// inline element holding a whole article is not a reason to build a copy of it.
void appendSubtreeText(const LayoutNode* n, std::string& out, size_t budget) {
    if (!n || out.size() >= budget) return;
    if (n->isTextNode()) {
        std::string_view t = n->textContent();
        if (out.size() + t.size() > budget) t = t.substr(0, budget - out.size());
        out.append(t);
        return;
    }
    for (const LayoutNode* c : n->children()) {
        if (out.size() >= budget) return;
        appendSubtreeText(c, out, budget);
    }
}

} // namespace

std::vector<int> visualOrderForLine(const std::vector<BidiItem>& items,
                                    bool rtlBase,
                                    TextMetrics& metrics) {
    const uint8_t baseLevel = rtlBase ? 1 : 0;
    std::vector<int> identity(items.size());
    for (size_t i = 0; i < items.size(); ++i) identity[i] = static_cast<int>(i);
    if (items.size() < 2) return identity;

    // Fast path. With an LTR base and no item claiming an opposing direction,
    // the only thing that could produce a non-zero level is an RTL character in
    // the text — so if the consumer cannot see characters, or there is no text
    // at all, the answer is the identity and there is nothing to build.
    bool anyOpposing = false;
    bool anyText = false;
    for (const auto& it : items) {
        if (it.opposesBase) anyOpposing = true;
        if (!it.text.empty() || it.subtree) anyText = true;
    }
    const bool canResolveText = metrics.bidiAware() && anyText;
    if (!rtlBase && !anyOpposing && !canResolveText) return identity;

    // Build the line's logical text and remember where each item starts in it.
    // An item that declares its own direction contributes its replacement
    // character rather than its text: its contents resolve as a separate
    // paragraph and must not leak strong characters into this one.
    std::string line;
    std::vector<size_t> itemStart(items.size(), 0);
    for (size_t i = 0; i < items.size(); ++i) {
        itemStart[i] = line.size();
        if (items[i].excluded) continue;
        if (items[i].opposesBase) {
            line += kObjectReplacement;
        } else if (!items[i].text.empty()) {
            line.append(items[i].text);
        } else if (items[i].subtree) {
            const size_t before = line.size();
            appendSubtreeText(items[i].subtree, line, before + kSubtreeTextBudget);
            // An element with no text of its own is an atomic box: an image, an
            // empty inline, an inline-block sized by CSS.
            if (line.size() == before) line += kObjectReplacement;
        } else {
            line += kObjectReplacement;
        }
    }

    std::vector<uint8_t> byteLevels;
    if (canResolveText) {
        metrics.bidiLevels(line, rtlBase, byteLevels);
    }

    std::vector<uint8_t> levels(items.size(), baseLevel);
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].excluded) { levels[i] = baseLevel; continue; }
        if (items[i].opposesBase) {
            // An isolate at the opposite direction: one level above the base,
            // which is what puts it on the other side of its neighbours.
            levels[i] = static_cast<uint8_t>(baseLevel + 1);
            continue;
        }
        const size_t at = itemStart[i];
        if (at < byteLevels.size()) {
            levels[i] = byteLevels[at];
        }
    }

    // Rule L1: whitespace at the end of a line reverts to the paragraph level,
    // so a trailing space after an RTL word does not get dragged to the far
    // side of the line.
    for (size_t i = items.size(); i-- > 0;) {
        if (items[i].excluded) continue;
        if (!isAllWhitespace(items[i].text)) break;
        levels[i] = baseLevel;
    }

    return TextMetrics::reorderVisual(levels);
}

} // namespace htmlayout::layout
