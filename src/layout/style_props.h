#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The property table and the projected-style struct, split out of style_cache.h so
// that box.h can include them. LayoutNode holds a NodeStyleCache by unique_ptr, and
// a consumer that subclasses LayoutNode has to be able to destroy and move one — it
// cannot do either through an incomplete type, and it has no reason to include the
// rest of style_cache.h to get one. (bro found this the hard way: every TU of ours
// happened to include style_cache.h, so the whole library compiled and only the
// consumer broke.)

namespace htmlayout::layout {

// Every property layout reads. Generated from the call sites rather than written
// by hand, so this list cannot quietly fall behind the code that uses it.
#define HTMLAYOUT_LAYOUT_PROPS(X) \
    X(XFlowCollapse,       "-x-flow-collapse")      \
    X(AlignContent,        "align-content")         \
    X(AlignItems,          "align-items")           \
    X(AlignSelf,           "align-self")            \
    X(AspectRatio,         "aspect-ratio")          \
    X(BorderBottomStyle,   "border-bottom-style")   \
    X(BorderBottomWidth,   "border-bottom-width")   \
    X(BorderCollapse,      "border-collapse")       \
    X(BorderLeftStyle,     "border-left-style")     \
    X(BorderLeftWidth,     "border-left-width")     \
    X(BorderRightStyle,    "border-right-style")    \
    X(BorderRightWidth,    "border-right-width")    \
    X(BorderSpacing,       "border-spacing")        \
    X(BorderTopStyle,      "border-top-style")      \
    X(BorderTopWidth,      "border-top-width")      \
    X(Bottom,              "bottom")                \
    X(BoxSizing,           "box-sizing")            \
    X(BreakAfter,          "break-after")           \
    X(BreakBefore,         "break-before")          \
    X(CaptionSide,         "caption-side")          \
    X(Clear,               "clear")                 \
    X(ColumnCount,         "column-count")          \
    X(ColumnFill,          "column-fill")           \
    X(ColumnGap,           "column-gap")            \
    X(ColumnSpan,          "column-span")           \
    X(ColumnWidth,         "column-width")          \
    X(Contain,             "contain")               \
    X(ContainerType,       "container-type")        \
    X(ContentVisibility,   "content-visibility")    \
    X(Direction,           "direction")             \
    X(Display,             "display")               \
    X(Filter,              "filter")                \
    X(FlexBasis,           "flex-basis")            \
    X(FlexDirection,       "flex-direction")        \
    X(FlexGrow,            "flex-grow")             \
    X(FlexShrink,          "flex-shrink")           \
    X(FlexWrap,            "flex-wrap")             \
    X(Float,               "float")                 \
    X(FontFamily,          "font-family")           \
    X(FontSize,            "font-size")             \
    X(FontWeight,          "font-weight")           \
    X(GridArea,            "grid-area")             \
    X(GridAutoColumns,     "grid-auto-columns")     \
    X(GridAutoFlow,        "grid-auto-flow")        \
    X(GridAutoRows,        "grid-auto-rows")        \
    X(GridColumn,          "grid-column")           \
    X(GridColumnEnd,       "grid-column-end")       \
    X(GridColumnStart,     "grid-column-start")     \
    X(GridRow,             "grid-row")              \
    X(GridRowEnd,          "grid-row-end")          \
    X(GridRowStart,        "grid-row-start")        \
    X(GridTemplateAreas,   "grid-template-areas")   \
    X(GridTemplateColumns, "grid-template-columns") \
    X(GridTemplateRows,    "grid-template-rows")    \
    X(Height,              "height")                \
    X(Isolation,           "isolation")             \
    X(JustifyContent,      "justify-content")       \
    X(JustifyItems,        "justify-items")         \
    X(JustifySelf,         "justify-self")          \
    X(Left,                "left")                  \
    X(LetterSpacing,       "letter-spacing")        \
    X(LineHeight,          "line-height")           \
    X(ListStylePosition,   "list-style-position")   \
    X(ListStyleType,       "list-style-type")       \
    X(MarginBottom,        "margin-bottom")         \
    X(MarginLeft,          "margin-left")           \
    X(MarginRight,         "margin-right")          \
    X(MarginTop,           "margin-top")            \
    X(MaxHeight,           "max-height")            \
    X(MaxWidth,            "max-width")             \
    X(MinHeight,           "min-height")            \
    X(MinWidth,            "min-width")             \
    X(Opacity,             "opacity")               \
    X(Order,               "order")                 \
    X(Overflow,            "overflow")              \
    X(OverflowWrap,        "overflow-wrap")         \
    X(OverflowX,           "overflow-x")            \
    X(OverflowY,           "overflow-y")            \
    X(PaddingBottom,       "padding-bottom")        \
    X(PaddingLeft,         "padding-left")          \
    X(PaddingRight,        "padding-right")         \
    X(PaddingTop,          "padding-top")           \
    X(PointerEvents,       "pointer-events")        \
    X(Position,            "position")              \
    X(Right,               "right")                 \
    X(RowGap,              "row-gap")               \
    X(ShapeOutside,        "shape-outside")         \
    X(Span,                "span")                  \
    X(TextAlign,           "text-align")            \
    X(TextIndent,          "text-indent")           \
    X(TextOverflow,        "text-overflow")         \
    X(TextTransform,       "text-transform")        \
    X(Top,                 "top")                   \
    X(Transform,           "transform")             \
    X(TransformOrigin,     "transform-origin")      \
    X(VerticalAlign,       "vertical-align")        \
    X(Visibility,          "visibility")            \
    X(WhiteSpace,          "white-space")           \
    X(Width,               "width")                 \
    X(WordBreak,           "word-break")            \
    X(WordSpacing,         "word-spacing")          \
    X(ZIndex,              "z-index")

enum class Prop : uint8_t {
#define X(id, name) id,
    HTMLAYOUT_LAYOUT_PROPS(X)
#undef X
    Count
};
inline constexpr size_t kPropCount = size_t(Prop::Count);

// Prop -> CSS name. The inverse (name -> Prop) lives in style_cache.cpp: it is
// needed only when building a cache, never when reading one.
inline constexpr std::string_view kPropNames[kPropCount] = {
#define X(id, name) name,
    HTMLAYOUT_LAYOUT_PROPS(X)
#undef X
};

// One node's projection. slot[p] is never null: a property the cascade did not set
// points at that property's initial value, which is a stable static — so a miss
// costs the same single load as a hit, and the second hash lookup disappears.
struct NodeStyleCache {
    std::array<const std::string*, kPropCount> slot{};
};

} // namespace htmlayout::layout
