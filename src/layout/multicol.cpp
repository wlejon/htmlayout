#include "layout/multicol.h"

namespace htmlayout::layout {

namespace {

const std::string& styleVal(const css::ComputedStyle& style, const std::string& prop) {
    static const std::string empty;
    auto it = style.find(prop);
    return it != style.end() ? it->second : empty;
}

} // anonymous namespace

bool isMulticolContainer(const css::ComputedStyle& style) {
    const std::string& colCount = styleVal(style, "column-count");
    const std::string& colWidth = styleVal(style, "column-width");
    return (!colCount.empty() && colCount != "auto") ||
           (!colWidth.empty() && colWidth != "auto");
}

} // namespace htmlayout::layout
