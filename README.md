# htmlayout

A standalone C++20 library for CSS parsing, selector matching, style cascade, and box layout. Designed to be embedded in applications that need CSS styling and layout without the overhead of a full browser engine.

htmlayout does **not** own the DOM, render anything, or run JavaScript. You provide the DOM tree and text measurement; htmlayout computes styles and positioned boxes.

## Features

**CSS Engine**
- W3C-compliant tokenizer and parser (Syntax Module Level 3)
- Selector matching: type, class, ID, attribute, pseudo-classes (`:nth-child`, `:not`, `:is`, `:where`, `:hover`, `:focus`, `:defined`, etc.), pseudo-elements (`::before`, `::after`)
- Combinators: descendant, child (`>`), adjacent sibling (`+`), general sibling (`~`)
- Full cascade with specificity, source order, `!important`, and inheritance (Cascade Level 5)
- `@layer` cascade layers with spec-compliant priority ordering (including `!important` reversal)
- `@container` queries with named containers and size containment
- Shorthand expansion for ~150 properties (`margin`, `padding`, `border`, `flex`, `grid`, `font`, `container`, etc.)
- Color parsing (named, hex, `rgb()`, `rgba()`, `hsl()`, `hsla()`)
- `@media` query evaluation (basic conditions: `min/max-width`, `min/max-height`, `orientation`)
- CSS Variables (`var()`) with fallback and inheritance
- Built-in user-agent stylesheet
- Shadow DOM and web component support: `:host`, `:host()`, `:host-context()`, `::slotted()`, `::part()`, scoped stylesheets

**Layout Engine**
- Block formatting context with margin collapsing and floats (`left`, `right`, `clear`)
- Inline formatting context with line wrapping and text alignment (`left`, `right`, `center`, `justify`)
- Flexbox (`flex-direction`, `flex-wrap`, `justify-content`, `align-items`, grow/shrink, gap, order)
- CSS Grid (templates, auto-placement, 1-based line placement, gap, `fr` units)
- Table layout (simplified algorithm; handles `table-row`, `table-cell`, `table-caption`, `border-spacing`, `border-collapse`)
- Basic Multi-column layout (`column-count`, `column-width`, `column-gap`)
- Length units: `px`, `em`, `%`, `vw`, `vh`, `vmin`, `vmax`, `rem`, `ch`, `ex`, `pt`, `cm`, `mm`, `in`, `pc`
- `calc()` expressions with basic math (`+`, `-`, `*`, `/`) and nested parentheses
- Intrinsic sizing (`min-content`, `max-content`, `fit-content`)
- Hit testing with z-order, overflow clipping, and `pointer-events`
- Incremental (dirty-flag) relayout
- `text-overflow: ellipsis`, `overflow-wrap`, `word-break`, `white-space` handling
- `position: relative`, `absolute`, `fixed`, `sticky` (layout-time positioning)

## Current Limitations

- **Flexbox**: `align-content` is parsed but not currently used for distributing flex lines.
- **Table Layout**: Simplified distribution; does not currently support `rowspan` or `colspan`.
- **Grid Layout**: Named areas (`grid-template-areas`) are parsed but not used for placement.
- **Multi-column**: Basic redistribution; lacks advanced break controls and `column-span: all`.
- **Queries**: Range syntax (`width > 500px`) and logical `or` are not yet supported in `@media` and `@container`.
- **Keywords**: `revert` is currently treated as `unset`.

## Requirements

- MSVC 2022 (C++20)
- CMake 3.24+
- Vcpkg (optional, auto-detected at `D:/vcpkg`)

## Building

```bash
cmake -B build
cmake --build build --config Debug
```

Run tests:

```bash
./build/tests/Debug/htmlayout_test.exe
```

## Usage

htmlayout operates on abstract interfaces that you implement to bridge your DOM and font system.

### 1. Implement the interfaces

```cpp
#include "css/selector.h"   // ElementRef
#include "layout/box.h"     // LayoutNode, TextMetrics

// Bridge your DOM for CSS matching
class MyElement : public htmlayout::css::ElementRef {
    std::string tagName() const override;
    std::string id() const override;
    std::string className() const override;
    std::string getAttribute(const std::string& name) const override;
    bool hasAttribute(const std::string& name) const override;
    ElementRef* parent() const override;
    std::vector<ElementRef*> children() const override;
    int childIndex() const override;
    int childIndexOfType() const override;
    int siblingCount() const override;
    int siblingCountOfType() const override;
    // Optional: isHovered(), isFocused(), isActive(), scope(),
    //   shadowRoot(), assignedSlot(), partName(), isDefined(),
    //   containerType(), containerName(), containerInlineSize(), containerBlockSize()
};

// Bridge your DOM for layout
class MyLayoutNode : public htmlayout::layout::LayoutNode {
    std::string tagName() const override;
    bool isTextNode() const override;
    std::string textContent() const override;
    LayoutNode* parent() const override;
    std::vector<LayoutNode*> children() const override;
    const htmlayout::css::ComputedStyle& computedStyle() const override;
    // After layout, read results from the `box` field
};

// Provide text measurement from your font engine
class MyTextMetrics : public htmlayout::layout::TextMetrics {
    float measureWidth(const std::string& text, const std::string& fontFamily,
                       float fontSize, const std::string& fontWeight) override;
    float lineHeight(const std::string& fontFamily, float fontSize,
                     const std::string& fontWeight) override;
};
```

### 2. Parse CSS and resolve styles

```cpp
using namespace htmlayout::css;

Cascade cascade;
cascade.addStylesheet(defaultUserAgentStylesheet());
cascade.addStylesheet(parse(yourCSS));

// For Shadow DOM scoped styles:
cascade.addStylesheet(parse(shadowCSS), shadowRootPtr);

// Resolve computed style per element
ComputedStyle style = cascade.resolve(element, inlineStyleStr, &parentStyle);
```

### 3. Run layout

```cpp
using namespace htmlayout::layout;

MyTextMetrics metrics;
layoutTree(rootNode, viewportWidth, metrics);

// Each node's `box` field now contains positioned results:
// box.contentRect  — {x, y, width, height}
// box.margin, box.padding, box.border — edge sizes
```

### 4. Hit testing

```cpp
LayoutNode* hit = hitTest(rootNode, mouseX, mouseY);
```

### 5. Incremental relayout

```cpp
markDirty(changedNode);
layoutTreeIncremental(rootNode, viewportWidth, metrics);
```

## Project Structure

```
src/css/       CSS tokenizer, parser, selector matcher, cascade, properties, color
src/layout/    Block, inline, flex, grid, table layout, hit testing, text breaking
tests/         Test suite (~5,700 lines across 15 test files)
third_party/   Bundled gumbo HTML5 parser
docs/          Architecture document
```

## Architecture

htmlayout is split into two modules:

- **`htmlayout::css`** -- Tokenizes and parses CSS, matches selectors against elements, and resolves the cascade to produce computed styles.
- **`htmlayout::layout`** -- Takes a tree of nodes with computed styles and produces positioned boxes using block, inline, flex, grid, or table formatting.

Key design principles:

- **Consumer owns the DOM** -- htmlayout never allocates or manages DOM nodes. You implement `ElementRef` and `LayoutNode` to bridge your own tree.
- **No global state** -- `Cascade` is instance-based. Layout is a pure tree walk. Multiple independent instances can coexist.
- **Web component support** -- Full Shadow DOM scoping with `:host`, `:host-context()`, `::slotted()`, `::part()`, and `:defined`. Cascade layers (`@layer`) and container queries (`@container`) for modern component authoring.
- **No rendering** -- Layout outputs positioned boxes. Drawing is your responsibility.

See [`docs/architecture.md`](docs/architecture.md) for the full design document and API reference.

## License

See the project's license file for terms.
