#pragma once
#include "css/tokenizer.h"
#include <string>
#include <vector>
#include <memory>

namespace htmlayout::css {

// A single CSS declaration: property: value
struct Declaration {
    std::string property;   // e.g. "color", "margin-left"
    std::string value;      // e.g. "red", "10px"
    bool important = false;
};

// A CSS selector + its declarations
struct Rule {
    std::string selector;               // raw selector text
    std::vector<Declaration> declarations;
};

// A @media block: condition + contained rules
struct MediaBlock {
    std::string condition;          // e.g. "(min-width: 768px)"
    std::vector<Rule> rules;
};

// A @layer block: named cascade layer with contained rules
struct LayerBlock {
    std::string name;               // e.g. "reset", "base.utilities"
    std::vector<Rule> rules;
    std::vector<MediaBlock> mediaBlocks;
};

// A @container block: container query with contained rules
struct ContainerBlock {
    std::string name;               // container name (empty = any container)
    std::string condition;           // e.g. "(min-width: 400px)"
    std::vector<Rule> rules;
};

// An @import rule: url + optional media/layer qualifiers
struct ImportRule {
    std::string url;
    std::string mediaCondition;  // e.g. "print", "(max-width: 600px)", or empty
    std::string layer;           // e.g. "reset", or empty; "\" means anonymous layer
};

// A parsed stylesheet
struct Stylesheet {
    std::vector<ImportRule> imports;
    std::vector<Rule> rules;
    std::vector<MediaBlock> mediaBlocks;
    std::vector<LayerBlock> layerBlocks;
    std::vector<ContainerBlock> containerBlocks;
    std::vector<std::string> layerOrder;  // declared layer ordering from @layer statements
};

// Media query evaluation context — consumers set this to describe the viewport
struct MediaContext {
    float viewportWidth = 0;
    float viewportHeight = 0;
    std::string mediaType = "screen"; // "screen", "print", "all"
};

// Evaluate whether a @media condition string matches the given context
bool evaluateMediaQuery(const std::string& condition, const MediaContext& ctx);

// Parse a CSS string into a Stylesheet
Stylesheet parse(const std::string& css);

// Parse an inline style string into declarations
std::vector<Declaration> parseInlineStyle(const std::string& style);

} // namespace htmlayout::css
