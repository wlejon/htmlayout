#include "layout/multicol.h"
#include "layout/style_cache.h"
#include "layout/style_util.h"

namespace htmlayout::layout {

using layout::styleVal;

bool isMulticolContainer(const LayoutNode* node) {
    const std::string& colCount = styleVal(node, Prop::ColumnCount);
    const std::string& colWidth = styleVal(node, Prop::ColumnWidth);
    return (!colCount.empty() && colCount != "auto") ||
           (!colWidth.empty() && colWidth != "auto");
}

} // namespace htmlayout::layout
