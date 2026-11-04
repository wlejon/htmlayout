#pragma once
// CSS lengths to px: a number with its unit, and the math functions
// (calc(), min(), max(), clamp(), nestable) over them. Layout resolves its
// lengths here, and so does the transform parser, so `translate(calc(...))`
// means what `width: calc(...)` means.
//
// The document-wide references (viewport, root font size) and the font-metric
// context of the element being resolved (ch, ex) are thread-local, set by
// layout before each pass / element (layout::setLayoutViewport and friends).
#include <string>
#include <string_view>

namespace htmlayout::css {

// Viewport for vw/vh/vmin/vmax; 0 means unknown, and those units then resolve
// against the percentage reference instead.
void setLengthViewport(float width, float height);
// The root element's font size, for rem (16 until set).
void setLengthRootFontSize(float px);
// ch and ex of the element being resolved, in px; 0 restores the 0.5em fallback.
void setLengthFontMetrics(float chPx, float exPx);

// One length token (`12px`, `50%`, `2rem`, `thin`): % resolves against
// referenceSize, em against fontSize. Unknown units are taken as px.
float resolveSingleLength(const std::string& value, float referenceSize, float fontSize);

// True when `value` is calc()/min()/max()/clamp() as a whole.
bool isLengthMathFunction(std::string_view value);

// A whole math-function expression (`calc(-100% - 12px)`).
float evalLengthCalc(const std::string& expr, float referenceSize, float fontSize);

} // namespace htmlayout::css
