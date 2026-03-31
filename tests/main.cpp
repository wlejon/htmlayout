#include "css/tokenizer.h"
#include "css/parser.h"
#include "css/selector.h"
#include "css/cascade.h"
#include "css/properties.h"
#include "layout/box.h"
#include "layout/formatting_context.h"

// Gumbo HTML parser
#include <gumbo.h>

#include <cstdio>
#include <cstdlib>
#include <string>

// Verify all libraries link and basic APIs are callable.
int main() {
    printf("=== htmlayout foundation test ===\n\n");

    // 1. Gumbo HTML parsing
    printf("[gumbo] Parsing HTML... ");
    const char* html = "<html><body><div class=\"box\">Hello</div></body></html>";
    GumboOutput* output = gumbo_parse(html);
    if (output && output->root) {
        printf("OK (root type=%d)\n", output->root->type);
    } else {
        printf("FAIL\n");
        return 1;
    }

    // Walk the tree to confirm structure
    GumboNode* root = output->root;
    printf("[gumbo] Root tag: %s\n",
           gumbo_normalized_tagname(root->v.element.tag));

    // Find body > div
    GumboNode* body = nullptr;
    for (unsigned i = 0; i < root->v.element.children.length; i++) {
        GumboNode* child = static_cast<GumboNode*>(root->v.element.children.data[i]);
        if (child->type == GUMBO_NODE_ELEMENT &&
            child->v.element.tag == GUMBO_TAG_BODY) {
            body = child;
            break;
        }
    }
    if (body) {
        printf("[gumbo] Found <body> with %u children\n",
               body->v.element.children.length);
    }

    gumbo_destroy_output(&kGumboDefaultOptions, output);

    // 2. CSS tokenizer
    printf("\n[css] Tokenizing... ");
    auto tokens = htmlayout::css::tokenize(".box { color: red; }");
    printf("OK (%zu tokens)\n", tokens.size());

    // 3. CSS parser
    printf("[css] Parsing stylesheet... ");
    auto sheet = htmlayout::css::parse(".box { color: red; margin: 10px; }");
    printf("OK (%zu rules)\n", sheet.rules.size());

    // 4. CSS properties
    printf("[css] Known properties: %zu\n",
           htmlayout::css::knownProperties().size());
    printf("[css] 'color' inherited: %s\n",
           htmlayout::css::isInherited("color") ? "yes" : "no");
    printf("[css] 'margin-top' inherited: %s\n",
           htmlayout::css::isInherited("margin-top") ? "yes" : "no");

    // 5. CSS selector
    printf("[css] Parsing selector... ");
    auto sel = htmlayout::css::parseSelector(".box");
    printf("OK (raw='%s')\n", sel.raw.c_str());

    // 6. Cascade
    printf("[css] Cascade created\n");
    htmlayout::css::Cascade cascade;
    cascade.addStylesheet(sheet);

    // 7. Layout types
    printf("\n[layout] LayoutBox size: %zu bytes\n",
           sizeof(htmlayout::layout::LayoutBox));
    printf("[layout] Rect size: %zu bytes\n",
           sizeof(htmlayout::layout::Rect));

    printf("\n=== All foundation checks passed ===\n");
    return 0;
}
