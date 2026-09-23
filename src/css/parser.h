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
// Nested style rules (css-nesting-1) are desugared by the parser into flat
// rules whose selector has `&` resolved, so a Rule never nests.
struct Rule {
    std::string selector;               // raw selector text
    std::vector<Declaration> declarations;
    // Position in the sheet's source order across rules, @media blocks, and
    // @layer blocks. Nesting can place a conditional rule (say, a nested
    // @media) between two plain rules, so the cascade orders by this rather
    // than by which list a rule sits in. 0 for rules not built by parse().
    size_t sourcePos = 0;
};

// A @media block: condition + contained rules
struct MediaBlock {
    std::string condition;          // e.g. "(min-width: 768px)"
    std::vector<Rule> rules;
    // Enclosing conditions that must also match: an @media nested inside
    // another @media, or inside a style rule that sits in an @media.
    std::vector<std::string> andConditions;
};

// A @layer block: named cascade layer with contained rules
struct LayerBlock {
    std::string name;               // e.g. "reset", "base.utilities"
    std::vector<Rule> rules;
    std::vector<MediaBlock> mediaBlocks;
};

// One container query: `[name] <condition>`.
struct ContainerQuery {
    std::string name;               // container name (empty = any container)
    std::string condition;          // e.g. "(min-width: 400px)"
};

// A @container block: container query with contained rules
struct ContainerBlock {
    std::string name;               // container name (empty = any container)
    std::string condition;           // e.g. "(min-width: 400px)"
    std::vector<Rule> rules;
    // @media conditions that must also match: the block sits inside @media,
    // or holds an @media nested in the container query.
    std::vector<std::string> mediaConditions;
    // Enclosing @container queries, outermost first, that must also match: a
    // container query nested in another. Each finds its own query container
    // (css-contain-3 §2.2).
    std::vector<ContainerQuery> enclosing;
    // The cascade layer the rules belong to: the block sits inside @layer,
    // or holds an @layer. `layer` is the qualified name ("" = anonymous).
    bool layered = false;
    std::string layer;
};

// An @import rule: url + optional media/layer qualifiers
struct ImportRule {
    std::string url;
    std::string mediaCondition;  // e.g. "print", "(max-width: 600px)", or empty
    std::string layer;           // e.g. "reset"; empty with `layered` = anonymous layer
    bool layered = false;        // `layer` or `layer(name)` was given
};

// A @keyframes rule: a single keyframe stop (e.g. "0%", "50%", "from", "to")
struct KeyframeStop {
    float offset;  // 0.0–1.0 (from=0, to=1)
    std::vector<Declaration> declarations;
};

// A @keyframes block: named animation with keyframe stops
struct KeyframeBlock {
    std::string name;
    std::vector<KeyframeStop> stops;
};

// A @font-face rule: declares a custom font family
struct FontFaceRule {
    std::string family;       // font-family name
    std::string src;          // url(...) source
    int weight = 400;         // font-weight (100-900)
    bool italic = false;      // font-style: italic
};

// A parsed stylesheet
struct Stylesheet {
    std::vector<ImportRule> imports;
    std::vector<Rule> rules;
    std::vector<MediaBlock> mediaBlocks;
    std::vector<LayerBlock> layerBlocks;
    std::vector<ContainerBlock> containerBlocks;
    std::vector<KeyframeBlock> keyframes;
    std::vector<FontFaceRule> fontFaces;
    std::vector<std::string> layerOrder;  // declared layer ordering from @layer statements
};

// Media query evaluation context — consumers set this to describe the viewport
struct MediaContext {
    float viewportWidth = 0;
    float viewportHeight = 0;
    std::string mediaType = "screen"; // "screen", "print", "all"
    std::string colorScheme = "light"; // "light" or "dark" — (prefers-color-scheme)
};

// Evaluate whether a @media condition string matches the given context
bool evaluateMediaQuery(const std::string& condition, const MediaContext& ctx);

// Parse a CSS string into a Stylesheet
Stylesheet parse(const std::string& css);

// Parse an inline style string into declarations
std::vector<Declaration> parseInlineStyle(const std::string& style);

} // namespace htmlayout::css
