#include "test_bidi.h"
#include "test_helpers.h"
#include "layout/bidi_line.h"
#include "layout/measure_cache.h"

#include <string>
#include <vector>

using namespace htmlayout::layout;

namespace {

std::string orderStr(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ' ';
        s += std::to_string(v[i]);
    }
    return s;
}

// A metrics stub that knows one thing about Unicode: Hebrew is right-to-left.
//
// The point is not to reimplement the bidi algorithm — the consumer supplies
// that — but to prove this library asks for levels, believes the answer, and
// turns it into the right permutation. Levels here follow the real rules for
// the strings the tests use: strong RTL characters take the base level rounded
// up to odd, digits inside an RTL context go one level above that, and neutrals
// between two RTL runs join them.
struct HebrewAwareMetrics : public TextMetrics {
    int levelCalls = 0;

    float measureWidth(std::string_view t, std::string_view, float,
                       std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override {
        return 20.0f;
    }

    bool bidiAware() const override { return true; }

    void bidiLevels(std::string_view text, bool rtlBase,
                    std::vector<uint8_t>& out) override {
        levelCalls++;
        const uint8_t base = rtlBase ? 1 : 0;
        out.assign(text.size(), base);

        // Hebrew is U+05D0..U+05EA, which in UTF-8 is 0xD7 followed by a
        // continuation byte. Mark both bytes of each such character.
        std::vector<bool> strongRtl(text.size(), false);
        for (size_t i = 0; i + 1 < text.size(); ++i) {
            if (static_cast<unsigned char>(text[i]) == 0xD7) {
                strongRtl[i] = strongRtl[i + 1] = true;
            }
        }

        bool anyRtl = false;
        for (bool b : strongRtl) if (b) { anyRtl = true; break; }
        if (!anyRtl) return;

        // Rule I1/I2 in the small: RTL characters sit at the first odd level at
        // or above the base; European digits inside an RTL context go one level
        // above that; runs of neutrals between two RTL runs take the RTL level.
        const uint8_t rtlLevel = rtlBase ? 1 : 1;
        const uint8_t digitLevel = static_cast<uint8_t>(rtlLevel + 1);
        for (size_t i = 0; i < text.size(); ++i) {
            if (strongRtl[i]) { out[i] = rtlLevel; continue; }
            const char c = text[i];
            if (c >= '0' && c <= '9') {
                // Only when an RTL run precedes it.
                bool afterRtl = false;
                for (size_t j = i; j-- > 0;) {
                    if (strongRtl[j]) { afterRtl = true; break; }
                    if ((text[j] >= 'a' && text[j] <= 'z') ||
                        (text[j] >= 'A' && text[j] <= 'Z')) break;
                }
                out[i] = afterRtl ? digitLevel : base;
                continue;
            }
            if (c == ' ') {
                // Neutral: RTL if flanked by RTL on both sides.
                bool leftRtl = false, rightRtl = false;
                for (size_t j = i; j-- > 0;) {
                    if (strongRtl[j]) { leftRtl = true; break; }
                    if (c != ' ' || text[j] != ' ') { if (text[j] != ' ') break; }
                }
                for (size_t j = i + 1; j < text.size(); ++j) {
                    if (strongRtl[j]) { rightRtl = true; break; }
                    if (text[j] != ' ') break;
                }
                out[i] = (leftRtl && rightRtl) ? rtlLevel : base;
                continue;
            }
            out[i] = base;  // Latin
        }
    }
};

BidiItem textItem(std::string_view t) {
    BidiItem b;
    b.text = t;
    return b;
}

} // namespace

void test_bidi() {
    printf("--- Bidi: rule L2 reverses by level ---\n");
    {
        // The canonical shapes. Levels are what an implementation reports for a
        // line; L2 turns them into paint order.
        check(orderStr(TextMetrics::reorderVisual({0, 0, 0})) == "0 1 2",
              "all LTR: order unchanged");
        check(orderStr(TextMetrics::reorderVisual({1, 1, 1})) == "2 1 0",
              "all RTL: order reversed");
        check(orderStr(TextMetrics::reorderVisual({0, 1, 0})) == "0 1 2",
              "one RTL run inside LTR: single item, nothing to reverse");
        check(orderStr(TextMetrics::reorderVisual({0, 1, 1, 0})) == "0 2 1 3",
              "RTL island reverses in place");
        check(orderStr(TextMetrics::reorderVisual({1, 2, 1})) == "2 1 0",
              "LTR island inside RTL: outer reversed, island kept in order");
        check(orderStr(TextMetrics::reorderVisual({1, 2, 2, 1})) == "3 1 2 0",
              "multi-item LTR island stays left-to-right inside a reversed line");
        check(orderStr(TextMetrics::reorderVisual({2, 2})) == "0 1",
              "even levels only: no odd level, so no reversal");
        check(orderStr(TextMetrics::reorderVisual({})) == "",
              "empty line");
    }

    printf("--- Bidi: line items take levels from their text ---\n");
    {
        HebrewAwareMetrics m;
        const std::string alef = "\xD7\x90\xD7\x91\xD7\x92";   // three Hebrew letters
        const std::string dalet = "\xD7\x93\xD7\x94\xD7\x95";

        // A Hebrew paragraph in an LTR block still reads right to left.
        std::vector<BidiItem> items{textItem(alef), textItem(" "), textItem(dalet)};
        check(orderStr(visualOrderForLine(items, false, m)) == "2 1 0",
              "Hebrew words reverse under an LTR base");
        check(m.levelCalls > 0, "levels were asked for, not assumed");

        // The same content with an RTL base reverses too, and text-align puts
        // it at the other edge — but the ORDER is the same question.
        check(orderStr(visualOrderForLine(items, true, m)) == "2 1 0",
              "Hebrew words reverse under an RTL base");
    }

    printf("--- Bidi: mixed direction and numbers ---\n");
    {
        HebrewAwareMetrics m;
        const std::string alef = "\xD7\x90\xD7\x91\xD7\x92";

        // "start <hebrew> end" — only the Hebrew reverses, and a lone RTL run
        // of one item looks unchanged.
        std::vector<BidiItem> one{textItem("start"), textItem(" "),
                                  textItem(alef), textItem(" "), textItem("end")};
        check(orderStr(visualOrderForLine(one, false, m)) == "0 1 2 3 4",
              "a single RTL item between LTR items does not move");

        // Two Hebrew words swap; the Latin on either side does not.
        std::vector<BidiItem> two{textItem("start"), textItem(" "), textItem(alef),
                                  textItem(" "), textItem(alef), textItem(" "),
                                  textItem("end")};
        check(orderStr(visualOrderForLine(two, false, m)) == "0 1 4 3 2 5 6",
              "the Hebrew island reverses and the Latin around it does not");

        // Digits after Hebrew are one level higher, so they keep their own
        // left-to-right order inside the reversed run.
        std::vector<BidiItem> num{textItem(alef), textItem(" "), textItem("12"),
                                  textItem(" "), textItem(alef)};
        check(orderStr(visualOrderForLine(num, true, m)) == "4 3 2 1 0",
              "digits inside an RTL line ride along with the reversal");
    }

    printf("--- Bidi: an item's own direction isolates it ---\n");
    {
        HebrewAwareMetrics m;
        BidiItem span;
        span.opposesBase = true;   // <span dir="rtl"> inside an LTR block

        std::vector<BidiItem> items{textItem("a"), span, textItem("b")};
        check(orderStr(visualOrderForLine(items, false, m)) == "0 1 2",
              "a lone opposing item sits where it is");

        std::vector<BidiItem> two{textItem("a"), span, span, textItem("b")};
        check(orderStr(visualOrderForLine(two, false, m)) == "0 2 1 3",
              "two adjacent opposing items swap");
    }

    printf("--- Bidi: rule L1 resets trailing whitespace ---\n");
    {
        HebrewAwareMetrics m;
        const std::string alef = "\xD7\x90\xD7\x91\xD7\x92";

        // Without L1 the trailing space would be part of the RTL run and end up
        // at the far left of the line. L1 puts it back at the paragraph level,
        // so it stays at the trailing edge.
        std::vector<BidiItem> items{textItem(alef), textItem(" "),
                                    textItem(alef), textItem("  ")};
        check(orderStr(visualOrderForLine(items, false, m)) == "2 1 0 3",
              "trailing whitespace stays at the line's end");
    }

    printf("--- Bidi: forced breaks keep their place ---\n");
    {
        HebrewAwareMetrics m;
        const std::string alef = "\xD7\x90\xD7\x91\xD7\x92";
        BidiItem br;
        br.excluded = true;

        std::vector<BidiItem> items{textItem(alef), textItem(" "),
                                    textItem(alef), br};
        // The break is not text and takes the paragraph level, so it does not
        // join the reversed run.
        check(orderStr(visualOrderForLine(items, false, m)) == "2 1 0 3",
              "a <br> is not dragged into the reversal");
    }

    printf("--- Bidi: MeasureCache forwards rather than answering ---\n");
    {
        // The decorator in front of the consumer's metrics must not swallow
        // these: the base class's default answer looks plausible and would
        // silently put every RTL paragraph back into logical order.
        HebrewAwareMetrics inner;
        MeasureCache cache(inner);
        check(cache.bidiAware(), "bidiAware() reaches the consumer");

        const std::string alef = "\xD7\x90\xD7\x91\xD7\x92";
        std::vector<uint8_t> a, b;
        cache.bidiLevels(alef, false, a);
        check(!a.empty() && a[0] == 1, "levels come from the consumer");

        const int before = inner.levelCalls;
        cache.bidiLevels(alef, false, b);
        check(b == a, "a repeat gives the same answer");
        check(inner.levelCalls == before, "and does not ask the consumer twice");
    }
}
