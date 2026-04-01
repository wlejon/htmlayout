# htmlayout — Architecture & Implementation Guide

## What This Is

htmlayout is a standalone C++20 library that provides CSS parsing, selector matching, style cascade, and box layout for HTML documents. It is designed to be composed into a larger application (like a game UI framework or lightweight browser) alongside other pieces:

- **HTML parsing**: gumbo (bundled, MSVC-compatible fork from litehtml)
- **CSS engine**: htmlayout::css (this library)
- **Layout engine**: htmlayout::layout (this library)
- **Rendering**: provided by the consumer (e.g. Skia, SDL, etc.)
- **DOM tree**: provided by the consumer
- **JavaScript**: provided by the consumer (e.g. QuickJS)

htmlayout does NOT own the DOM or render anything. It computes styles and positions. The consumer provides abstract interfaces (`ElementRef` for CSS, `LayoutNode` for layout, `TextMetrics` for text measurement) and reads back computed styles and positioned boxes.

## Build

```bash
cmake -B build
cmake --build build --config Debug
./build/tests/Debug/htmlayout_test.exe
```

Uses Visual Studio generator (multi-config) on Windows. Vcpkg at D:/vcpkg is auto-detected. MSVC 2022, C++20.

## Project Structure

```
src/
  css/
    tokenizer.h/cpp    — CSS tokenizer (text → tokens)
    parser.h/cpp       — CSS parser (tokens → rules + declarations)
    selector.h/cpp     — CSS selector parser + matcher
    cascade.h/cpp      — Style cascade with scope support (shadow DOM)
    properties.h/cpp   — CSS property registry (defaults, inheritance, shorthands)
    ua_stylesheet.h/cpp — Built-in default styles
  layout/
    box.h/cpp          — Core types (Rect, Edges, LayoutBox) + tree entry point
    block.h/cpp        — Block formatting context (including floats and BFC)
    inline.h/cpp       — Inline formatting context (line boxes and IFC)
    flex.h/cpp         — Flexbox layout
    grid.h/cpp         — CSS Grid layout
    table.h/cpp        — Table layout
    text.h/cpp         — Text measurement and line breaking
    formatting_context.h/cpp — Dispatch + length resolution + calc()
third_party/
  gumbo/               — HTML5 parser (C library)
tests/
  main.cpp             — Integration test harness
docs/
  architecture.md      — This file
```

## Namespace

- `htmlayout::css` — all CSS types and functions
- `htmlayout::layout` — all layout types and functions

---

## CSS Module (`src/css/`)

### Tokenizer (`tokenizer.h`)

Implements the W3C CSS Syntax Module Level 3 tokenizer. Converts a CSS string into a flat vector of tokens.

Token types: Ident, Function, AtKeyword, Hash, String, Number, Percentage, Dimension, Whitespace, Colon, Semicolon, Comma, LeftBrace, RightBrace, LeftBracket, RightBracket, LeftParen, RightParen, Delim, CDO, CDC, EndOfFile.

Reference: https://www.w3.org/TR/css-syntax-3/#tokenization

### Parser (`parser.h`)

Consumes tokens and produces structured CSS data:

- `Stylesheet` — a list of `Rule`s, `MediaBlock`s, `LayerBlock`s, and `ContainerBlock`s.
- `Rule` — a selector string + a list of `Declaration`s.
- `Declaration` — a property name + value string + important flag.

Supports `@media`, `@supports`, `@layer`, and `@container` rules.

Reference: https://www.w3.org/TR/css-syntax-3/#parsing

### Selector (`selector.h`)

Parses and matches CSS selectors. This is the critical piece for shadow DOM support.

**Parsing**: Converts selector text into a compiled `Selector` struct that can be efficiently matched.

**Matching**: `Selector::matches(ElementRef&)` checks if a selector matches a given element. The `ElementRef` is an abstract interface — the consumer implements it to bridge their DOM.

**Supported selectors**:

1. **Simple selectors**: `div`, `.class`, `#id`, `*`, `[attr]`, `[attr=value]`, `[attr^=val]`, `[attr$=val]`, `[attr*=val]`, `[attr~=val]`, `[attr|=val]`
2. **Compound selectors**: `div.class#id`, `.a.b`
3. **Combinators**: descendant (` `), child (`>`), adjacent sibling (`+`), general sibling (`~`)
4. **Pseudo-classes**: `:first-child`, `:last-child`, `:nth-child(n)`, `:not(sel)`, `:is(sel)`, `:where(sel)`, `:hover`, `:focus`, `:active`, `:host`, `:host(sel)`, `:host-context(sel)`, `:defined`
5. **Pseudo-elements**: `::before`, `::after`, `::slotted(sel)`, `::part(name)`
6. **Comma-separated lists**: `h1, h2, h3`

**Specificity**: Calculated as (id_count, class_count, type_count) packed into a uint32 for comparison.

**Shadow DOM scoping**: The `ElementRef::scope()` method returns which shadow scope an element belongs to. The cascade uses this to ensure shadow-scoped rules only match elements in their scope. `:host` matches the shadow host; `::slotted` matches light DOM children; `::part` matches elements inside a shadow tree from the outer scope.

Reference: https://www.w3.org/TR/selectors-4/

### Cascade (`cascade.h`)

Resolves the final computed style for each element. This is the "C" in CSS.

**`Cascade::addStylesheet(sheet, scope, mediaContext)`** — Adds rules to the cascade.
- `scope`: `nullptr` for document-level styles, or a shadow root pointer for shadow-scoped styles.
- `mediaContext`: Optional viewport dimensions to filter `@media` rules at addition time.

**`Cascade::resolve(elem, inlineStyle, parentStyle)`** — Computes the final style for an element:

1. Collect matching rules based on element scope and Shadow DOM pseudo-classes.
2. Sort by: Origin & Importance (UA < Author < Author !important < UA !important).
3. Sort by: `@layer` priority (unlayered wins for normal; earlier layers win for important).
4. Sort by: Specificity (inline style is max).
5. Sort by: Source order.
6. Apply declarations (last wins per property).
7. Resolve `var()` references with fallback and inheritance.
8. Resolve `inherit`, `initial`, `unset`, `revert` keywords.
9. Inherit properties from `parentStyle` if `inherited: true`.
10. Fall back to `initialValue` for remaining properties.

**Output**: `ComputedStyle` (`unordered_map<string, string>`) — maps longhand property names to their resolved values.

Reference: https://www.w3.org/TR/css-cascade-5/

### Properties (`properties.h`)

Registry of known CSS properties (~150 properties) with metadata:

- **name**: the CSS property name.
- **initialValue**: default value when not specified.
- **inherited**: whether the property inherits from parent.

Also provides shorthand expansion for many properties: `margin`, `padding`, `border`, `flex`, `grid`, `background`, `font`, `container`, etc.

---

## Layout Module (`src/layout/`)

### Overview

The layout engine takes a tree of styled nodes and computes the position and size of every box. It implements the CSS Visual Formatting Model.

The consumer provides:
- `LayoutNode` — abstract interface for DOM nodes (tag, text content, children, computed style).
- `TextMetrics` — abstract interface for text measurement (the consumer's font/text engine).

Layout writes results directly into each `LayoutNode::box` (a `LayoutBox` struct with content rect, margin, padding, border).

### Entry Point (`box.h`)

```cpp
void layoutTree(LayoutNode* root, float viewportWidth, TextMetrics& metrics);
```

Walks the tree, resolves CSS lengths, and dispatches each node to the appropriate formatting context based on its `display` property.

### Formatting Context Dispatch (`formatting_context.h`)

`layoutNode()` reads the computed `display` property and dispatches:

| display value | Layout function |
|---------------|----------------|
| `block`, `list-item` | `layoutBlock()` |
| `inline`, `inline-block` | `layoutInline()` |
| `flex`, `inline-flex` | `layoutFlex()` |
| `grid`, `inline-grid` | `layoutGrid()` |
| `table`, `inline-table` | `layoutTable()` |
| `none` | skip (zero-size box) |

**Length Resolution**:
- `resolveLength()` handles `px`, `em`, `rem`, `%`, `vw`, `vh`, `vmin`, `vmax`, etc.
- `evalCalc()` implements a recursive-descent parser for `calc()` expressions with math operations.

### Block Layout (`block.h`)

Implements Block Formatting Context (BFC):

1. Lay out children top-to-bottom.
2. Each block child gets the full available width (minus margins).
3. Margin collapsing: adjacent vertical margins (including through empty boxes) collapse.
4. Floats: `float: left/right` items are pulled out of flow; subsequent content wraps around them.
5. `clear: left/right/both` moves content past preceding floats.
6. Intrinsic sizing: `min-content`, `max-content`, `fit-content` width resolution.
7. Multi-column: Basic redistribution into columns if `column-count` or `column-width` is set.

### Inline Layout (`inline.h`)

Implements Inline Formatting Context (IFC):

1. Inline children flow left-to-right.
2. When a line is full, wrap to the next line (line box).
3. Text nodes are broken at word boundaries (respects `overflow-wrap`, `word-break`).
4. `vertical-align` positions inline boxes on the line.
5. `text-align` aligns content within the line box (`left`, `right`, `center`, `justify`).
6. `inline-block` elements participate in inline flow but have block layout internally.

### Flex Layout (`flex.h`)

Implements CSS Flexible Box Layout:

1. Determine main axis (`flex-direction`).
2. Collect flex items.
3. Resolve flex base sizes (`flex-basis`).
4. Distribute free space (`flex-grow`) or remove overflow (`flex-shrink`).
5. Handle wrapping (`flex-wrap`).
6. Align items on cross axis (`align-items`, `align-self`).
7. Align content across flex lines (`align-content`).
8. Apply `justify-content` for main axis distribution.
9. Handle `gap` between items.
10. Handle `order` property for visual reordering.

### Grid Layout (`grid.h`)

Implements CSS Grid:
1. Parse track lists: handles `repeat()`, `minmax()`, `fr` units.
2. Resolve track sizes: distributes available space to fractional tracks.
3. Item placement: 1-based line indices and `grid-area`.
4. Auto-placement: basic algorithm to fill available cells.

### Table Layout (`table.h`)

Implements a simplified Table layout:
1. Collect rows, cells, and captions.
2. Generate anonymous rows/cells for missing table structure.
3. Distribute column widths proportionally based on content.
4. Align cells in rows and stretch heights to match.

### Text Layout (`text.h`)

Breaks text into runs that fit within available width:

1. Split on whitespace (respecting `white-space` property).
2. Measure each word using `TextMetrics` callback.
3. Greedily pack words into lines that fit.
4. Handle `white-space: pre` (preserve whitespace, no wrapping).
5. Handle `white-space: nowrap` (no wrapping).
6. Return `TextRun` structs with text, width, height.

### Hit Testing (`box.h`)

```cpp
LayoutNode* hitTest(LayoutNode* root, float x, float y);
```

Finds the deepest node whose layout box contains the point (x, y). Respects z-order (later siblings on top), overflow clipping, and `pointer-events: none`.

---

## Missing & Incomplete Features

- **Flexbox**: `align-content` is parsed but not currently used for distributing flex lines.
- **CSS Range Queries**: range syntax like `@media (width > 500px)` is not supported.
- **Query Logic**: Only `and` and `not` are supported in queries; `or` is missing.
- **Table Details**: No support for `rowspan` or `colspan`; simplified width distribution.
- **Grid Areas**: `grid-template-areas` names are parsed but not used for placement.
- **Multi-column**: Basic redistribution; no column-span or advanced break controls.
- **Revert**: `revert` keyword is currently treated as `unset`.

---

## Design Principles

1. **No global state** — all state lives in Cascade, Stylesheet, etc. Multiple instances can coexist.
2. **Consumer owns the DOM** — htmlayout never allocates or owns DOM nodes. It operates on abstract interfaces.
3. **Scope-aware by design** — shadow DOM scoping is a first-class parameter, not an afterthought.
4. **Minimal dependencies** — only gumbo (C library) for HTML parsing. Everything else is standalone C++20.
5. **Testable in isolation** — each module (tokenizer, parser, selector, cascade, layout) can be tested independently with mock inputs.
