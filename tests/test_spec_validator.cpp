#include "test_helpers.h"
#include "css/parser.h"
#include "css/cascade.h"
#include "css/selector.h"
#include "css/ua_stylesheet.h"
#include "layout/box.h"
#include "layout/formatting_context.h"
#include <gumbo.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <cmath>

using namespace htmlayout::css;
using namespace htmlayout::layout;

// Forward declaration
struct SpecNode;

// Bridge to ElementRef
struct SpecElement : public ElementRef {
    SpecNode* owner;
    mutable std::vector<ElementRef*> childElemsCache;
    SpecElement(SpecNode* o) : owner(o) {}

    std::string_view tagName() const override;
    std::string_view id() const override;
    std::string_view className() const override;
    std::string_view getAttribute(std::string_view name) const override;
    bool hasAttribute(std::string_view name) const override;
    ElementRef* parent() const override;
    std::span<ElementRef* const> children() const override;
    int childIndex() const override;
    int childIndexOfType() const override;
    int siblingCount() const override;
    int siblingCountOfType() const override;
    bool isHovered() const override { return false; }
    bool isFocused() const override { return false; }
    bool isActive() const override { return false; }
    void* scope() const override { return nullptr; }
    void* shadowRoot() const override { return nullptr; }
    ElementRef* assignedSlot() const override { return nullptr; }
    std::string_view partName() const override { return ""; }
    bool isDefined() const override { return true; }
    std::string_view containerType() const override;
    std::string_view containerName() const override;
    float containerInlineSize() const override { return 0; }
    float containerBlockSize() const override { return 0; }
};

struct SpecNode : public LayoutNode {
    GumboNode* node;
    SpecNode* parentNode = nullptr;
    std::vector<LayoutNode*> childNodes; // stores SpecNode* upcast; static_cast back where needed
    ComputedStyle style;
    SpecElement elementBridge;

    SpecNode(GumboNode* n, SpecNode* p = nullptr)
        : node(n), parentNode(p), elementBridge(this) {}

    ~SpecNode() { for (auto* c : childNodes) delete static_cast<SpecNode*>(c); }

    // LayoutNode implementation
    std::string_view tagName() const override {
        if (node->type == GUMBO_NODE_ELEMENT) return gumbo_normalized_tagname(node->v.element.tag);
        return "";
    }
    bool isTextNode() const override {
        return node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE;
    }
    std::string_view textContent() const override {
        return isTextNode() ? std::string_view(node->v.text.text) : std::string_view{};
    }
    LayoutNode* parent() const override { return parentNode; }
    std::span<LayoutNode* const> children() const override { return childNodes; }
    const ComputedStyle& computedStyle() const override { return style; }
};

// Implement SpecElement methods using SpecNode
std::string_view SpecElement::tagName() const { return owner->tagName(); }
std::string_view SpecElement::id() const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return "";
    GumboAttribute* attr = gumbo_get_attribute(&owner->node->v.element.attributes, "id");
    return attr ? std::string_view(attr->value) : std::string_view{};
}
std::string_view SpecElement::className() const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return "";
    GumboAttribute* attr = gumbo_get_attribute(&owner->node->v.element.attributes, "class");
    return attr ? std::string_view(attr->value) : std::string_view{};
}
std::string_view SpecElement::getAttribute(std::string_view name) const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return "";
    GumboAttribute* attr = gumbo_get_attribute(&owner->node->v.element.attributes, std::string(name).c_str());
    return attr ? std::string_view(attr->value) : std::string_view{};
}
bool SpecElement::hasAttribute(std::string_view name) const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return false;
    return gumbo_get_attribute(&owner->node->v.element.attributes, std::string(name).c_str()) != nullptr;
}
ElementRef* SpecElement::parent() const {
    return owner->parentNode ? &owner->parentNode->elementBridge : nullptr;
}
std::span<ElementRef* const> SpecElement::children() const {
    childElemsCache.clear();
    for (auto* c : owner->childNodes) {
        auto* sn = static_cast<SpecNode*>(c);
        if (sn->node->type == GUMBO_NODE_ELEMENT) childElemsCache.push_back(&sn->elementBridge);
    }
    return childElemsCache;
}
int SpecElement::childIndex() const {
    if (!owner->parentNode) return 0;
    int i = 0;
    for (auto* c : owner->parentNode->childNodes) {
        auto* sn = static_cast<SpecNode*>(c);
        if (sn == owner) return i;
        if (sn->node->type == GUMBO_NODE_ELEMENT) i++;
    }
    return 0;
}
int SpecElement::childIndexOfType() const {
    if (!owner->parentNode) return 0;
    int i = 0;
    std::string_view tag = tagName();
    for (auto* c : owner->parentNode->childNodes) {
        auto* sn = static_cast<SpecNode*>(c);
        if (sn == owner) return i;
        if (sn->node->type == GUMBO_NODE_ELEMENT && sn->tagName() == tag) i++;
    }
    return 0;
}
int SpecElement::siblingCount() const {
    if (!owner->parentNode) return 1;
    int count = 0;
    for (auto* c : owner->parentNode->childNodes) {
        auto* sn = static_cast<SpecNode*>(c);
        if (sn->node->type == GUMBO_NODE_ELEMENT) count++;
    }
    return count;
}
int SpecElement::siblingCountOfType() const {
    if (!owner->parentNode) return 1;
    int count = 0;
    std::string_view tag = tagName();
    for (auto* c : owner->parentNode->childNodes) {
        auto* sn = static_cast<SpecNode*>(c);
        if (sn->node->type == GUMBO_NODE_ELEMENT && sn->tagName() == tag) count++;
    }
    return count;
}
std::string_view SpecElement::containerType() const {
    return owner->style.count("container-type") ? std::string_view(owner->style.at("container-type")) : std::string_view("none");
}
std::string_view SpecElement::containerName() const {
    return owner->style.count("container-name") ? std::string_view(owner->style.at("container-name")) : std::string_view{};
}

SpecNode* buildTree(GumboNode* node, SpecNode* parent = nullptr) {
    SpecNode* sn = new SpecNode(node, parent);
    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboVector* children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            sn->childNodes.push_back(buildTree((GumboNode*)children->data[i], sn));
        }
    }
    return sn;
}

void resolveAllStyles(SpecNode* node, Cascade& cascade, const ComputedStyle* parentStyle = nullptr) {
    if (node->node->type == GUMBO_NODE_ELEMENT) {
        node->style = cascade.resolve(node->elementBridge, "", parentStyle);
    } else if (parentStyle) {
        node->style = *parentStyle;
    }
    for (auto* child : node->childNodes) {
        resolveAllStyles(static_cast<SpecNode*>(child), cascade, &node->style);
    }
}

struct SpecExpectation {
    std::string selector;
    std::map<std::string, std::string> computedStyles;
    std::map<std::string, float> boxProps; 
};

struct SpecTestCase {
    std::string name;
    std::string specReference;
    std::string html;
    std::string css;
    std::vector<SpecExpectation> expectations;
};

struct SpecMetrics : public TextMetrics {
    float measureWidth(std::string_view text, std::string_view, float fontSize, std::string_view) override {
        return text.length() * fontSize * 0.6f;
    }
    float lineHeight(std::string_view, float fontSize, std::string_view) override {
        return fontSize * 1.2f;
    }
};

void validateExpectations(SpecNode* node, const SpecExpectation& exp) {
    if (node->node->type == GUMBO_NODE_ELEMENT) {
        Selector sel = parseSelector(exp.selector);
        if (sel.matches(node->elementBridge)) {
            for (auto const& [prop, val] : exp.computedStyles) {
                check(node->style.count(prop) && node->style.at(prop) == val, 
                      (exp.selector + " " + prop + " == " + val).c_str());
            }
            for (auto const& [prop, val] : exp.boxProps) {
                float actual = 0;
                if (prop == "width") actual = node->box.contentRect.width;
                else if (prop == "height") actual = node->box.contentRect.height;
                else if (prop == "x") actual = node->box.contentRect.x;
                else if (prop == "y") actual = node->box.contentRect.y;
                check(std::abs(actual - val) < 1.0f, 
                      (exp.selector + " " + prop + " approx " + std::to_string(val)).c_str());
            }
        }
    }
    for (auto* child : node->childNodes) validateExpectations(static_cast<SpecNode*>(child), exp);
}

void runSpecTest(const SpecTestCase& test) {
    printf("Running Spec Test: %s (%s)\n", test.name.c_str(), test.specReference.c_str());

    GumboOutput* output = gumbo_parse(test.html.c_str());
    SpecNode* root = buildTree(output->root);

    Stylesheet sheet = parse(test.css);
    Cascade cascade;
    cascade.addStylesheet(defaultUserAgentStylesheet());
    cascade.addStylesheet(sheet);

    resolveAllStyles(root, cascade);

    SpecMetrics metrics;
    layoutTree(root, 800.0f, metrics);

    for (const auto& exp : test.expectations) {
        validateExpectations(root, exp);
    }

    delete root;
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}

void testSpecCompliance() {
    printf("--- Specification Compliance Runner ---\n");

    std::vector<SpecTestCase> suite = {
        {
            "Flexbox: Basic row growth",
            "CSS-FLEXBOX-1 Section 7.1",
            "<div class='container'><div class='item1'></div><div class='item2'></div></div>",
            ".container { display: flex; width: 400px; } .item1 { flex-grow: 1; height: 50px; } .item2 { flex-grow: 3; height: 50px; }",
            {
                {".item1", {}, {{"width", 100.0f}}},
                {".item2", {}, {{"width", 300.0f}}}
            }
        },
        {
            "Grid: Fixed tracks",
            "CSS-GRID-1 Section 7.2",
            "<div class='grid'><div class='a'></div><div class='b'></div></div>",
            ".grid { display: grid; grid-template-columns: 100px 200px; width: 500px; }",
            {
                {".a", {}, {{"width", 100.0f}}},
                {".b", {}, {{"width", 200.0f}, {"x", 100.0f}}}
            }
        },
        {
            "Box Model: Padding and Border",
            "CSS2.1 Section 8.1",
            "<div class='box'></div>",
            ".box { width: 100px; padding: 10px; border: 5px solid black; box-sizing: content-box; }",
            {
                {".box", {}, {{"width", 100.0f}}} 
            }
        },
        {
            "Selectors L4: :has() pseudo-class",
            "SELECTORS-4 Section 3.6.4",
            "<div class='parent'><div class='child'></div></div><div class='other'></div>",
            ".parent:has(.child) { color: red; }",
            {
                {".parent", {{"color", "red"}}, {}}
            }
        },
        {
            "Cascade L5: @layer precedence",
            "CSS-CASCADE-5 Section 6.4",
            "<div class='target'></div>",
            "@layer base { .target { color: red; } } @layer theme { .target { color: blue; } }",
            {
                {".target", {{"color", "blue"}}, {}}
            }
        },
        {
            "Values L4: calc() expressions",
            "CSS-VALUES-4 Section 10.2",
            "<div class='box'></div>",
            "body { margin: 0; } .box { width: calc(100% - 100px); }",
            {
                {".box", {}, {{"width", 700.0f}}} // 800 - 100
            }
        },
        {
            "Table L3: Basic row/cell layout",
            "CSS-TABLE-3 Section 3",
            "<table><tr><td class='c1'></td><td class='c2'></td></tr></table>",
            "body { margin: 0; } table { width: 400px; border-spacing: 0; border-collapse: collapse; } td { height: 50px; padding: 0; border: none; }",
            {
                {".c1", {}, {{"width", 200.0f}}},
                {".c2", {}, {{"width", 200.0f}, {"x", 200.0f}}}
            }
        },
        {
            "Multicol L1: Basic column distribution",
            "CSS-MULTICOL-1 Section 2",
            "<div class='multicol'><div class='item'></div><div class='item'></div></div>",
            "body { margin: 0; } .multicol { column-count: 2; width: 400px; column-gap: 0; } .item { height: 100px; }",
            {
                {".item:first-child", {}, {{"width", 200.0f}}},
                {".item:last-child", {}, {{"x", 200.0f}}}
            }
        },
        {
            "Containment L2: contain: size",
            "CSS-CONTAIN-2 Section 3.1",
            "<div class='box'><div class='child'></div></div>",
            ".box { contain: size; width: 100px; height: 100px; } .child { width: 200px; height: 200px; }",
            {
                {".box", {}, {{"width", 100.0f}, {"height", 100.0f}}}
            }
        },
        {
            "Logical Properties L1: margin-inline-start",
            "CSS-LOGICAL-1 Section 4.1",
            "<div class='box'></div>",
            ".box { margin-inline-start: 20px; direction: ltr; }",
            {
                {".box", {{"margin-left", "20px"}}, {}}
            }
        },
        {
            "Conditional Rules L3: @media query",
            "CSS-CONDITIONAL-3 Section 2",
            "<div class='target'></div>",
            ".target { color: red; } @media (min-width: 500px) { .target { color: green; } }",
            {
                {".target", {{"color", "green"}}, {}}
            }
        }
    };

    for (const auto& test : suite) {
        runSpecTest(test);
    }
}
