#include "test_foundation.h"
#include "test_tokenizer.h"
#include "test_parser.h"
#include "test_selector.h"
#include "test_cascade.h"
#include "test_shorthand.h"
#include "test_layout.h"
#include "test_inline.h"
#include "test_flex.h"
#include "test_hittest.h"
#include "test_calc.h"
#include "test_grid.h"
#include "test_remaining.h"
#include "test_final.h"
#include "test_webcomponents.h"
#include <cstdio>

int g_passed = 0;
int g_failed = 0;

int main() {
    printf("=== htmlayout tests ===\n\n");

    testFoundation();
    printf("\n");

    testTokenizer();
    printf("\n");

    testParser();
    printf("\n");

    testSelector();
    printf("\n");

    testCascade();
    printf("\n");

    testShorthand();
    printf("\n");

    testLayout();
    printf("\n");

    testInlineLayout();
    printf("\n");

    testFlexLayout();
    printf("\n");

    testHitTest();
    printf("\n");

    testCalc();
    printf("\n");

    testGridLayout();
    printf("\n");

    testRemaining();
    printf("\n");

    testFinal();
    printf("\n");

    testWebComponents();
    printf("\n");

    printf("=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
