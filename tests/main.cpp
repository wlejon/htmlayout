#include "test_foundation.h"
#include "test_tokenizer.h"
#include "test_parser.h"
#include "test_selector.h"
#include "test_cascade.h"
#include "test_shorthand.h"
#include "test_layout.h"
#include "test_inline.h"
#include "test_bidi.h"
#include "test_flex.h"
#include "test_hittest.h"
#include "test_calc.h"
#include "test_grid.h"
#include "test_remaining.h"
#include "test_final.h"
#include "test_webcomponents.h"
#include "test_coverage.h"
#include "test_spec_completeness.h"
#include "test_spec_validator.h"
#include "test_transform.h"
#include "test_coverage_extra.h"
#include "test_layout_extra.h"
#include "test_flex_grid_extra.h"
#include "test_more_coverage.h"
#include "test_parser_extra.h"
#include "test_incremental.h"
#include "test_color.h"
#include "test_nesting.h"
#include "test_line_clamp.h"
#include "test_margin_collapse.h"
#include "test_sizing_fixes.h"
#include "test_inline_boxes.h"
#include <cstdio>
#include <cstring>

int g_passed = 0;
int g_failed = 0;

namespace {

struct Suite {
    const char* name;
    void (*fn)();
    bool trailingNewline = true;
};

constexpr Suite kSuites[] = {
    {"foundation", testFoundation, true},
    {"tokenizer", testTokenizer, true},
    {"parser", testParser, true},
    {"selector", testSelector, true},
    {"cascade", testCascade, true},
    {"shorthand", testShorthand, true},
    {"layout", testLayout, true},
    {"inline", testInlineLayout, true},
    {"bidi", test_bidi, true},
    {"flex", testFlexLayout, true},
    {"hittest", testHitTest, true},
    {"calc", testCalc, true},
    {"grid", testGridLayout, true},
    {"remaining", testRemaining, true},
    {"final", testFinal, true},
    {"webcomponents", testWebComponents, true},
    {"coverage", testCoverage, true},
    {"spec_completeness", testSpecCompleteness, true},
    {"spec_compliance", testSpecCompliance, true},
    {"transform", testTransform, true},
    {"coverage_extra", testCoverageExtra, true},
    {"layout_extra", testLayoutExtra, true},
    {"flex_grid_extra", testFlexGridExtra, true},
    {"more_coverage", testMoreCoverage, true},
    {"parser_extra", testParserExtra, true},
    {"incremental", testIncrementalLayout, true},
    {"color", testColor, true},
    {"nesting", testNesting, true},
    {"line_clamp", testLineClamp, true},
    {"margin_collapse", testMarginCollapse, true},
    {"sizing_fixes", testSizingFixes, false},
    {"inline_boxes", testInlineBoxes, true},
};

} // namespace

int main(int argc, char** argv) {
    const char* filter = nullptr;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--list") == 0 || std::strcmp(arg, "-l") == 0) {
            listOnly = true;
        } else if (std::strncmp(arg, "--filter=", 9) == 0) {
            filter = arg + 9;
        } else if ((std::strcmp(arg, "--filter") == 0 || std::strcmp(arg, "-f") == 0) && i + 1 < argc) {
            filter = argv[++i];
        } else if (arg[0] != '-') {
            filter = arg;
        }
    }

    if (listOnly) {
        for (const auto& s : kSuites) {
            if (filter && std::strstr(s.name, filter) == nullptr) continue;
            std::printf("%s\n", s.name);
        }
        return 0;
    }

    printf("=== htmlayout tests ===\n\n");

    for (const auto& s : kSuites) {
        if (filter && std::strstr(s.name, filter) == nullptr) continue;
        s.fn();
        if (s.trailingNewline) {
            printf("\n");
        }
    }

    printf("=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
