#pragma once
#include "css/properties.h"
#include <string>
#include <vector>

namespace htmlayout::css {

std::vector<ExpandedDecl> expandAnimationShorthand(const std::string& value);
std::vector<ExpandedDecl> expandTransitionShorthand(const std::string& value);
std::vector<ExpandedDecl> expandBackgroundShorthand(const std::string& value);

} // namespace htmlayout::css
