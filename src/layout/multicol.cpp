#include "layout/multicol.h"
#include "layout/style_util.h"

namespace htmlayout::layout {

using layout::styleVal;

bool isMulticolContainer(const css::ComputedStyle& style) {
    const std::string& colCount = styleVal(style, "column-count");
    const std::string& colWidth = styleVal(style, "column-width");
    return (!colCount.empty() && colCount != "auto") ||
           (!colWidth.empty() && colWidth != "auto");
}

} // namespace htmlayout::layout
