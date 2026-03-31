#pragma once
#include "css/selector.h"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

// Global test counters
extern int g_passed;
extern int g_failed;

inline void check(bool cond, const char* name) {
    if (cond) {
        printf("  PASS: %s\n", name);
        g_passed++;
    } else {
        printf("  FAIL: %s\n", name);
        g_failed++;
    }
}

// Mock DOM element for selector/cascade testing.
struct MockElement : public htmlayout::css::ElementRef {
    std::string tag;
    std::string elemId;
    std::string classes;  // space-separated
    std::unordered_map<std::string, std::string> attrs;
    MockElement* parentElem = nullptr;
    std::vector<MockElement*> childElems;
    bool hovered = false;
    bool focused = false;
    bool active = false;
    void* scopePtr = nullptr;
    void* shadowRootPtr = nullptr;
    MockElement* assignedSlotElem = nullptr;
    std::string partNames;
    bool defined = true;
    std::string contType = "none";
    std::string contName;
    float contInlineSize = 0;
    float contBlockSize = 0;

    std::string tagName() const override { return tag; }
    std::string id() const override { return elemId; }
    std::string className() const override { return classes; }

    std::string getAttribute(const std::string& name) const override {
        auto it = attrs.find(name);
        return it != attrs.end() ? it->second : "";
    }
    bool hasAttribute(const std::string& name) const override {
        return attrs.count(name) > 0;
    }
    ElementRef* parent() const override { return parentElem; }
    std::vector<ElementRef*> children() const override {
        return {childElems.begin(), childElems.end()};
    }
    int childIndex() const override {
        if (!parentElem) return 0;
        for (int i = 0; i < static_cast<int>(parentElem->childElems.size()); i++) {
            if (parentElem->childElems[i] == this) return i;
        }
        return 0;
    }
    int childIndexOfType() const override {
        if (!parentElem) return 0;
        int idx = 0;
        for (auto* c : parentElem->childElems) {
            if (c == this) return idx;
            if (c->tag == tag) idx++;
        }
        return 0;
    }
    int siblingCount() const override {
        if (!parentElem) return 1;
        return static_cast<int>(parentElem->childElems.size());
    }
    int siblingCountOfType() const override {
        if (!parentElem) return 1;
        int count = 0;
        for (auto* c : parentElem->childElems) {
            if (c->tag == tag) count++;
        }
        return count;
    }
    bool isHovered() const override { return hovered; }
    bool isFocused() const override { return focused; }
    bool isActive() const override { return active; }
    void* scope() const override { return scopePtr; }
    void* shadowRoot() const override { return shadowRootPtr; }
    ElementRef* assignedSlot() const override { return assignedSlotElem; }
    std::string partName() const override { return partNames; }
    bool isDefined() const override { return defined; }
    std::string containerType() const override { return contType; }
    std::string containerName() const override { return contName; }
    float containerInlineSize() const override { return contInlineSize; }
    float containerBlockSize() const override { return contBlockSize; }

    void addChild(MockElement* child) {
        child->parentElem = this;
        childElems.push_back(child);
    }
};
