#pragma once
// Internal: math functions (calc(), min(), max(), clamp(), abs(), sign()) in
// colour components, CSS Values 4 typed arithmetic over the three types a
// colour channel can hold. Channel keywords of the relative colour syntax
// (`r`, `h`, `alpha`, ...) are <number> operands. Not part of the public API.
#include <optional>
#include <span>
#include <string_view>

namespace htmlayout::css::colorcalc {

enum class Type { Number, Percent, Angle };  // an Angle's value is in degrees

struct Value {
    double v = 0;
    Type type = Type::Number;
};

struct Channel {
    std::string_view name;
    double value;
};

// True for the function names evaluate() understands.
bool isMathFunction(std::string_view name);

// Evaluates `name(inner)`; nullopt when it is malformed or mixes types in a way
// CSS Values 4 forbids (e.g. `50% + 10`). NaN results come back as 0, per
// CSS Values 4 §10.9's rule for a top-level NaN.
std::optional<Value> evaluate(std::string_view name, std::string_view inner,
                              std::span<const Channel> channels);

} // namespace htmlayout::css::colorcalc
