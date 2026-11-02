#pragma once
#include "test_helpers.h"
#include "css/tokenizer.h"
#include "css/parser.h"
#include "css/selector.h"
#include "css/cascade.h"
#include "css/properties.h"
#include "css/color.h"
#include "css/ua_stylesheet.h"
#include "layout/box.h"
#include "layout/text.h"
#include "layout/formatting_context.h"
#include "layout/multicol.h"
#include <cmath>
#include <unordered_map>

// ---- Shared Mock Node ----

struct CovNode : public htmlayout::layout::LayoutNode {
    std::string tag = "div";
    bool isText = false;
    std::string text;
    CovNode* parentNode = nullptr;
    std::vector<htmlayout::layout::LayoutNode*> childNodes;
    htmlayout::css::ComputedStyle style_;
    bool hasIntrinsic = false;
    float intrW = 0, intrH = 0;

    std::string_view tagName() const override { return tag; }
    bool isTextNode() const override { return isText; }
    std::string_view textContent() const override { return text; }
    htmlayout::layout::LayoutNode* parent() const override { return parentNode; }
    std::span<htmlayout::layout::LayoutNode* const> children() const override {
        return childNodes;
    }
    const htmlayout::css::ComputedStyle& computedStyle() const override { return style_; }
    bool intrinsicSize(float& w, float& h, float) const override {
        if (!hasIntrinsic) return false;
        w = intrW; h = intrH; return true;
    }

    void addChild(CovNode* c) { c->parentNode = this; childNodes.push_back(c); }

    void initBlock() {
        style_["display"] = "block";
        style_["position"] = "static";
        style_["width"] = "auto";
        style_["height"] = "auto";
        style_["min-width"] = "0";
        style_["min-height"] = "0";
        style_["max-width"] = "none";
        style_["max-height"] = "none";
        style_["margin-top"] = "0";
        style_["margin-right"] = "0";
        style_["margin-bottom"] = "0";
        style_["margin-left"] = "0";
        style_["padding-top"] = "0";
        style_["padding-right"] = "0";
        style_["padding-bottom"] = "0";
        style_["padding-left"] = "0";
        style_["border-top-width"] = "0";
        style_["border-right-width"] = "0";
        style_["border-bottom-width"] = "0";
        style_["border-left-width"] = "0";
        style_["border-top-style"] = "none";
        style_["border-right-style"] = "none";
        style_["border-bottom-style"] = "none";
        style_["border-left-style"] = "none";
        style_["box-sizing"] = "content-box";
        style_["font-size"] = "16px";
        style_["font-family"] = "monospace";
        style_["font-weight"] = "normal";
        style_["overflow"] = "visible";
        style_["white-space"] = "normal";
        style_["text-align"] = "left";
        style_["vertical-align"] = "baseline";
    }
    void initInline() { initBlock(); style_["display"] = "inline"; }
    void initFlex() { initBlock(); style_["display"] = "flex";
        style_["flex-direction"] = "row"; style_["flex-wrap"] = "nowrap";
        style_["justify-content"] = "flex-start"; style_["align-items"] = "stretch";
        style_["align-content"] = "stretch";
        style_["gap"] = "0"; style_["row-gap"] = "0"; style_["column-gap"] = "0";
    }
    void initFlexItem() { initBlock();
        style_["flex-grow"] = "0"; style_["flex-shrink"] = "1";
        style_["flex-basis"] = "auto"; style_["align-self"] = "auto"; style_["order"] = "0";
    }
    void initGrid() { initBlock(); style_["display"] = "grid";
        style_["row-gap"] = "0"; style_["column-gap"] = "0";
    }
    void initTable() { initBlock(); style_["display"] = "table";
        style_["border-collapse"] = "separate"; style_["border-spacing"] = "0";
    }
};

struct CovMetrics : public htmlayout::layout::TextMetrics {
    float measureWidth(std::string_view t, std::string_view, float, std::string_view) override {
        return static_cast<float>(t.size()) * 10.0f;
    }
    float lineHeight(std::string_view, float, std::string_view) override { return 20.0f; }
};

inline bool approx(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

void testCoverageCss();
void testCoverageLayout();
void testCoverageGridFlex();
void testCoverage();
