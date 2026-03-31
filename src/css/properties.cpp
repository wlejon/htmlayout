#include "css/properties.h"

namespace htmlayout::css {

const std::vector<PropertyDef>& knownProperties() {
    static const std::vector<PropertyDef> props = {
        // Box model
        {"display",           "inline", false},
        {"position",          "static", false},
        {"float",             "none",   false},
        {"clear",             "none",   false},
        {"width",             "auto",   false},
        {"height",            "auto",   false},
        {"min-width",         "0",      false},
        {"min-height",        "0",      false},
        {"max-width",         "none",   false},
        {"max-height",        "none",   false},
        {"margin-top",        "0",      false},
        {"margin-right",      "0",      false},
        {"margin-bottom",     "0",      false},
        {"margin-left",       "0",      false},
        {"padding-top",       "0",      false},
        {"padding-right",     "0",      false},
        {"padding-bottom",    "0",      false},
        {"padding-left",      "0",      false},
        {"border-top-width",  "medium", false},
        {"border-right-width","medium", false},
        {"border-bottom-width","medium",false},
        {"border-left-width", "medium", false},
        {"border-top-style",  "none",   false},
        {"border-right-style","none",   false},
        {"border-bottom-style","none",  false},
        {"border-left-style", "none",   false},
        {"border-top-color",  "currentcolor", false},
        {"border-right-color","currentcolor", false},
        {"border-bottom-color","currentcolor",false},
        {"border-left-color", "currentcolor", false},
        {"overflow",          "visible",false},
        {"box-sizing",        "content-box", false},

        // Flexbox
        {"flex-direction",    "row",       false},
        {"flex-wrap",         "nowrap",    false},
        {"justify-content",   "flex-start",false},
        {"align-items",       "stretch",   false},
        {"align-content",     "stretch",   false},
        {"align-self",        "auto",      false},
        {"flex-grow",         "0",         false},
        {"flex-shrink",       "1",         false},
        {"flex-basis",        "auto",      false},
        {"order",             "0",         false},
        {"gap",               "0",         false},

        // Text & font (inherited)
        {"color",             "black",     true},
        {"font-family",       "sans-serif",true},
        {"font-size",         "16px",      true},
        {"font-weight",       "normal",    true},
        {"font-style",        "normal",    true},
        {"line-height",       "normal",    true},
        {"text-align",        "start",     true},
        {"text-decoration",   "none",      false},
        {"text-transform",    "none",      true},
        {"white-space",       "normal",    true},
        {"word-spacing",      "normal",    true},
        {"letter-spacing",    "normal",    true},
        {"vertical-align",    "baseline",  false},
        {"visibility",        "visible",   true},
        {"cursor",            "auto",      true},

        // Visual
        {"background-color",  "transparent", false},
        {"background-image",  "none",        false},
        {"opacity",           "1",           false},
        {"z-index",           "auto",        false},
        {"top",               "auto",        false},
        {"right",             "auto",        false},
        {"bottom",            "auto",        false},
        {"left",              "auto",        false},

        // List
        {"list-style-type",   "disc",   true},
        {"list-style-position","outside",true},
    };
    return props;
}

bool isInherited(const std::string& property) {
    for (auto& p : knownProperties()) {
        if (p.name == property) return p.inherited;
    }
    return false;
}

std::string initialValue(const std::string& property) {
    for (auto& p : knownProperties()) {
        if (p.name == property) return p.initialValue;
    }
    return {};
}

std::vector<ExpandedDecl> expandShorthand(const std::string& property,
                                           const std::string& value) {
    // TODO: Implement shorthand expansion (margin, padding, border, flex, etc.)
    return {{property, value}};
}

} // namespace htmlayout::css
