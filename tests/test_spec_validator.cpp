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
    SpecElement(SpecNode* o) : owner(o) {}

    std::string tagName() const override;
    std::string id() const override;
    std::string className() const override;
    std::string getAttribute(const std::string& name) const override;
    bool hasAttribute(const std::string& name) const override;
    ElementRef* parent() const override;
    std::vector<ElementRef*> children() const override;
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
    std::string partName() const override { return ""; }
    bool isDefined() const override { return true; }
    std::string containerType() const override;
    std::string containerName() const override;
    float containerInlineSize() const override { return 0; }
    float containerBlockSize() const override { return 0; }
};

struct SpecNode : public LayoutNode {
    GumboNode* node;
    SpecNode* parentNode = nullptr;
    std::vector<SpecNode*> childNodes;
    ComputedStyle style;
    SpecElement elementBridge;

    SpecNode(GumboNode* n, SpecNode* p = nullptr) 
        : node(n), parentNode(p), elementBridge(this) {}
    
    ~SpecNode() { for (auto* c : childNodes) delete c; }

    // LayoutNode implementation
    std::string tagName() const override {
        if (node->type == GUMBO_NODE_ELEMENT) return gumbo_normalized_tagname(node->v.element.tag);
        return "";
    }
    bool isTextNode() const override { 
        return node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE; 
    }
    std::string textContent() const override { 
        return isTextNode() ? node->v.text.text : ""; 
    }
    LayoutNode* parent() const override { return parentNode; }
    std::vector<LayoutNode*> children() const override {
        std::vector<LayoutNode*> res;
        for (auto* c : childNodes) res.push_back(c);
        return res;
    }
    const ComputedStyle& computedStyle() const override { return style; }
};

// Implement SpecElement methods using SpecNode
std::string SpecElement::tagName() const { return owner->tagName(); }
std::string SpecElement::id() const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return "";
    GumboAttribute* attr = gumbo_get_attribute(&owner->node->v.element.attributes, "id");
    return attr ? attr->value : "";
}
std::string SpecElement::className() const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return "";
    GumboAttribute* attr = gumbo_get_attribute(&owner->node->v.element.attributes, "class");
    return attr ? attr->value : "";
}
std::string SpecElement::getAttribute(const std::string& name) const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return "";
    GumboAttribute* attr = gumbo_get_attribute(&owner->node->v.element.attributes, name.c_str());
    return attr ? attr->value : "";
}
bool SpecElement::hasAttribute(const std::string& name) const {
    if (owner->node->type != GUMBO_NODE_ELEMENT) return false;
    return gumbo_get_attribute(&owner->node->v.element.attributes, name.c_str()) != nullptr;
}
ElementRef* SpecElement::parent() const {
    return owner->parentNode ? &owner->parentNode->elementBridge : nullptr;
}
std::vector<ElementRef*> SpecElement::children() const {
    std::vector<ElementRef*> res;
    for (auto* c : owner->childNodes) {
        if (c->node->type == GUMBO_NODE_ELEMENT) res.push_back(&c->elementBridge);
    }
    return res;
}
int SpecElement::childIndex() const {
    if (!owner->parentNode) return 0;
    int i = 0;
    for (auto* c : owner->parentNode->childNodes) {
        if (c == owner) return i;
        if (c->node->type == GUMBO_NODE_ELEMENT) i++;
    }
    return 0;
}
int SpecElement::childIndexOfType() const {
    if (!owner->parentNode) return 0;
    int i = 0;
    std::string tag = tagName();
    for (auto* c : owner->parentNode->childNodes) {
        if (c == owner) return i;
        if (c->node->type == GUMBO_NODE_ELEMENT && c->tagName() == tag) i++;
    }
    return 0;
}
int SpecElement::siblingCount() const {
    if (!owner->parentNode) return 1;
    int count = 0;
    for (auto* c : owner->parentNode->childNodes) {
        if (c->node->type == GUMBO_NODE_ELEMENT) count++;
    }
    return count;
}
int SpecElement::siblingCountOfType() const {
    if (!owner->parentNode) return 1;
    int count = 0;
    std::string tag = tagName();
    for (auto* c : owner->parentNode->childNodes) {
        if (c->node->type == GUMBO_NODE_ELEMENT && c->tagName() == tag) count++;
    }
    return count;
}
std::string SpecElement::containerType() const {
    return owner->style.count("container-type") ? owner->style.at("container-type") : "none";
}
std::string SpecElement::containerName() const {
    return owner->style.count("container-name") ? owner->style.at("container-name") : "";
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
        resolveAllStyles(child, cascade, &node->style);
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
    float measureWidth(const std::string& text, const std::string&, float fontSize, const std::string&) override {
        return text.length() * fontSize * 0.6f;
    }
    float lineHeight(const std::string&, float fontSize, const std::string&) override {
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
    for (auto* child : node->childNodes) validateExpectations(child, exp);
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
