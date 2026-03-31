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

// A parsed stylesheet
struct Stylesheet {
    std::vector<Rule> rules;
};

// Parse a CSS string into a Stylesheet
Stylesheet parse(const std::string& css);

// Parse an inline style string into declarations
std::vector<Declaration> parseInlineStyle(const std::string& style);

} // namespace htmlayout::css
