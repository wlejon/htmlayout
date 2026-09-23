#pragma once

// Container query conditions (CSS Containment 3 §5).
//
//   <container-query>  = not <query-in-parens>
//                      | <query-in-parens> [ [ and <query-in-parens> ]*
//                                          | [ or  <query-in-parens> ]* ]
//   <query-in-parens>  = ( <container-query> ) | ( <size-feature> )
//                      | style( <style-query> ) | <general-enclosed>
//   <style-query>      = the same boolean shape over <style-in-parens>, or a
//                        bare <style-feature>
//
// A condition is parsed once and evaluated per element against its query
// container, in three-valued logic: a feature the container cannot answer
// (a block-axis size on an inline-size container, a length unit with no
// context, a general-enclosed term, a style() test of a standard property)
// is unknown, `not unknown` is unknown, and a query that ends unknown does
// not match.

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace htmlayout::css {

// What one query container offers the evaluator.
struct ContainerQueryEnv {
    float inlineSize = 0;
    float blockSize = 0;
    bool hasInlineAxis = false;   // container-type: inline-size or size
    bool hasBlockAxis = false;    // container-type: size
    // The container's computed value of `property` (a custom property such
    // as "--theme", or "font-size" for em); false when it is not known.
    std::function<bool(std::string_view property, std::string& out)> styleValue;
};

class ContainerCondition {
public:
    // Parse a condition (the query without its container name). Returns null
    // when the text is not a valid <container-query>; such a query never
    // matches.
    static std::shared_ptr<const ContainerCondition> parse(const std::string& text);

    // Does the condition test size features / style features? A size query
    // needs a size container; a style query can be answered by any element.
    bool usesSize() const { return usesSize_; }
    bool usesStyle() const { return usesStyle_; }

    // True only when the condition evaluates to true (not unknown).
    bool matches(const ContainerQueryEnv& env) const;

    enum class Kind { Not, And, Or, Size, Style, Unknown };
    enum class Cmp { Lt, Le, Gt, Ge, Eq };
    enum class Feature { Width, Height, InlineSize, BlockSize, AspectRatio, Orientation };
    struct Value {
        // A length (`unit` "px", "em", ... or "" for a unitless 0), a ratio
        // (num / den), or an orientation keyword.
        double num = 0;
        double den = 1;
        std::string unit;
        std::string keyword;
        bool isRatio = false;
    };
    struct Node {
        Kind kind = Kind::Unknown;
        std::vector<int> kids;
        // Size: `feature` compared with up to two values. A plain
        // `(feature: v)` / `(min-feature: v)` is one comparison; the range
        // forms are one or two. No comparisons = boolean context.
        Feature feature = Feature::Width;
        struct Test { Cmp cmp; Value value; bool valueFirst; };
        std::vector<Test> tests;
        bool featureValid = true;   // false: min-/max- on a discrete feature etc.
        // Style: a custom property name, and its normalized value (empty
        // `hasValue` false = the bare `(--name)` form).
        std::string property;
        std::string value;
        bool hasValue = false;
    };

private:
    std::vector<Node> nodes_;
    int root_ = -1;
    bool usesSize_ = false;
    bool usesStyle_ = false;
    friend class ContainerConditionParser;
};

// Tokens of a CSS value re-joined with whitespace collapsed to one space and
// trimmed, so "  a   b " and "a b" compare equal as style() values do.
std::string normalizeStyleValue(const std::string& value);

} // namespace htmlayout::css
