#include "test_foundation.h"
#include "test_helpers.h"
#include "css/tokenizer.h"
#include "css/parser.h"
#include "css/selector.h"
#include "css/properties.h"
#include "layout/box.h"
#include <gumbo.h>

void testFoundation() {
    printf("--- Foundation ---\n");

    const char* html = "<html><body><div class=\"box\">Hello</div></body></html>";
    GumboOutput* output = gumbo_parse(html);
    check(output && output->root, "gumbo: parse HTML");

    GumboNode* root = output->root;
    GumboNode* body = nullptr;
    for (unsigned i = 0; i < root->v.element.children.length; i++) {
        GumboNode* child = static_cast<GumboNode*>(root->v.element.children.data[i]);
        if (child->type == GUMBO_NODE_ELEMENT &&
            child->v.element.tag == GUMBO_TAG_BODY) {
            body = child;
            break;
        }
    }
    check(body != nullptr, "gumbo: find <body>");
    gumbo_destroy_output(&kGumboDefaultOptions, output);

    check(htmlayout::css::knownProperties().size() > 0, "properties: known properties exist");
    check(htmlayout::css::isInherited("color") == true, "properties: color is inherited");
    check(htmlayout::css::isInherited("margin-top") == false, "properties: margin-top not inherited");

    auto sel = htmlayout::css::parseSelector(".box");
    check(sel.raw == ".box", "selector: parse returns raw text");

    check(sizeof(htmlayout::layout::LayoutBox) > 0, "layout: LayoutBox defined");
    check(sizeof(htmlayout::layout::Rect) > 0, "layout: Rect defined");
}
