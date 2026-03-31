#pragma once
#include "layout/box.h"

namespace htmlayout::layout {

// Multi-column layout is handled within block.cpp's layoutBlock function.
// When column-count or column-width is set on a block, children are
// redistributed into columns after initial layout.

// Check if a node has multi-column properties set
bool isMulticolContainer(const css::ComputedStyle& style);

} // namespace htmlayout::layout
