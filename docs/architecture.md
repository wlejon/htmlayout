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
  layout/
    box.h/cpp          — Core types (Rect, Edges, LayoutBox) + tree entry point
    block.h/cpp        — Block formatting context
    inline.h/cpp       — Inline formatting context (line boxes)
    flex.h/cpp         — Flexbox layout
    text.h/cpp         — Text measurement and line breaking
    formatting_context.h/cpp — Dispatch + length resolution helpers
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

**Input**: `".box { color: red; margin: 10px; }"`
**Output**: `[Delim('.'), Ident('box'), Whitespace, LeftBrace, Whitespace, Ident('color'), Colon, Whitespace, Ident('red'), Semicolon, ...]`

Token types: Ident, Function, AtKeyword, Hash, String, Number, Percentage, Dimension, Whitespace, Colon, Semicolon, Comma, LeftBrace, RightBrace, LeftBracket, RightBracket, LeftParen, RightParen, Delim, CDO, CDC, EndOfFile.

Reference: https://www.w3.org/TR/css-syntax-3/#tokenization

### Parser (`parser.h`)

Consumes tokens and produces structured CSS data:

- `Stylesheet` — a list of `Rule`s
- `Rule` — a selector string + a list of `Declaration`s
- `Declaration` — a property name + value string + important flag

Also provides `parseInlineStyle()` for parsing `style="..."` attribute values into declarations.

Reference: https://www.w3.org/TR/css-syntax-3/#parsing

### Selector (`selector.h`)

Parses and matches CSS selectors. This is the critical piece for shadow DOM support.

**Parsing**: Converts selector text into a compiled `Selector` struct that can be efficiently matched.

**Matching**: `Selector::matches(ElementRef&)` checks if a selector matches a given element. The `ElementRef` is an abstract interface — the consumer implements it to bridge their DOM.

**Supported selectors** (implement in this order):

1. **Simple selectors**: `div`, `.class`, `#id`, `*`, `[attr]`, `[attr=value]`
2. **Compound selectors**: `div.class#id`, `.a.b`
3. **Combinators**: descendant (` `), child (`>`), adjacent sibling (`+`), general sibling (`~`)
4. **Pseudo-classes**: `:first-child`, `:last-child`, `:nth-child(n)`, `:not(sel)`, `:hover`, `:focus`, `:active`, `:host`, `:host(sel)`
5. **Pseudo-elements**: `::before`, `::after` (flagged, not matched normally)
6. **Comma-separated lists**: `h1, h2, h3`

**Specificity**: Calculated as (id_count, class_count, type_count) packed into a uint32 for comparison.

**Shadow DOM scoping**: The `ElementRef::scope()` method returns which shadow scope an element belongs to. The cascade uses this to ensure shadow-scoped rules only match elements in their scope. The `:host` pseudo-class matches the shadow host element.

Reference: https://www.w3.org/TR/selectors-4/

### Cascade (`cascade.h`)

Resolves the final computed style for each element. This is the "C" in CSS.

**`Cascade::addStylesheet(sheet, scope)`** — Adds rules to the cascade. The `scope` parameter enables shadow DOM: pass `nullptr` for document-level styles, or a shadow root pointer for shadow-scoped styles.

**`Cascade::resolve(elem, inlineStyle)`** — Computes the final style for an element:

1. Collect all rules whose selector matches `elem` AND whose scope matches `elem.scope()`
2. Sort by: origin (user-agent < author < important), specificity, source order
3. Apply declarations in order (last wins per property)
4. Apply inline style declarations (highest author specificity)
5. For each inherited property not explicitly set, inherit from parent's computed style
6. For each non-inherited property not explicitly set, use initial value

**Output**: `ComputedStyle` (an `unordered_map<string, string>`) — maps property names to their resolved values.

Reference: https://www.w3.org/TR/css-cascade-5/

### Properties (`properties.h`)

Registry of known CSS properties with metadata:

- **name**: the CSS property name
- **initialValue**: default value when not specified
- **inherited**: whether the property inherits from parent

Also provides shorthand expansion: `margin: 10px` → `margin-top: 10px`, `margin-right: 10px`, `margin-bottom: 10px`, `margin-left: 10px`.

68 properties defined covering: box model, flexbox, text/font, visual, and list styles.

---

## Layout Module (`src/layout/`)

### Overview

The layout engine takes a tree of styled nodes and computes the position and size of every box. It implements CSS Visual Formatting Model for three formatting contexts:

1. **Block** — vertical stacking of block-level boxes
2. **Inline** — horizontal flow with line wrapping
3. **Flex** — CSS Flexible Box Layout

The consumer provides:
- `LayoutNode` — abstract interface for DOM nodes (tag, text content, children, computed style)
- `TextMetrics` — abstract interface for text measurement (the consumer's font/text engine)

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
| `none` | skip (zero-size box) |

Also provides `resolveLength()` for converting CSS values to pixels:
- `px` — absolute pixels
- `em` — relative to font size
- `%` — relative to containing block
- `auto` — context-dependent (computed during layout)

### Block Layout (`block.h`)

Implements Block Formatting Context (BFC):

1. Lay out children top-to-bottom
2. Each block child gets the full available width (minus margins)
3. Margin collapsing: adjacent vertical margins collapse to the larger value
4. `width: auto` expands to fill available space
5. `height: auto` shrinks to fit content
6. `overflow: hidden/scroll/auto` creates a new BFC

Reference: https://www.w3.org/TR/CSS2/visuren.html#block-formatting

### Inline Layout (`inline.h`)

Implements Inline Formatting Context (IFC):

1. Inline children flow left-to-right
2. When a line is full, wrap to the next line (line box)
3. Text nodes are broken at word boundaries
4. `vertical-align` positions inline boxes on the line
5. `text-align` aligns content within the line box
6. `inline-block` elements participate in inline flow but have block layout internally

Reference: https://www.w3.org/TR/CSS2/visuren.html#inline-formatting

### Flex Layout (`flex.h`)

Implements CSS Flexible Box Layout:

1. Determine main axis (`flex-direction: row | column`)
2. Collect flex items
3. Resolve flex base sizes (`flex-basis`)
4. Distribute free space (`flex-grow`) or remove overflow (`flex-shrink`)
5. Handle wrapping (`flex-wrap`)
6. Align items on cross axis (`align-items`, `align-self`)
7. Align content across flex lines (`align-content`)
8. Apply `justify-content` for main axis distribution
9. Handle `gap` between items
10. Handle `order` property for visual reordering

Reference: https://www.w3.org/TR/css-flexbox-1/

### Text Layout (`text.h`)

Breaks text into runs that fit within available width:

1. Split on whitespace (respecting `white-space` property)
2. Measure each word using `TextMetrics` callback
3. Greedily pack words into lines that fit
4. Handle `white-space: pre` (preserve whitespace, no wrapping)
5. Handle `white-space: nowrap` (no wrapping)
6. Return `TextRun` structs with text, width, height

### Hit Testing (`box.h`)

```cpp
LayoutNode* hitTest(LayoutNode* root, float x, float y);
```

Finds the deepest node whose layout box contains the point (x, y). Respects z-order (later siblings on top), overflow clipping, and `pointer-events: none`.

---

## How Consumers Use This

### 1. Parse HTML (using gumbo directly)

```cpp
GumboOutput* output = gumbo_parse(html);
// Walk output->root to build your own DOM tree
gumbo_destroy_output(&kGumboDefaultOptions, output);
```

### 2. Parse and cascade CSS

```cpp
using namespace htmlayout::css;
Cascade cascade;

// Add user-agent defaults
cascade.addStylesheet(parse(uaCSS));

// Add document stylesheets
cascade.addStylesheet(parse(authorCSS));

// Add shadow-scoped stylesheet
cascade.addStylesheet(parse(shadowCSS), shadowRootPtr);

// Resolve style for each element
ComputedStyle style = cascade.resolve(myElementRef, inlineStyleStr);
```

### 3. Layout

```cpp
using namespace htmlayout::layout;
layoutTree(rootNode, viewportWidth, myTextMetrics);

// Read results
for (auto* child : rootNode->children()) {
    auto& box = child->box;
    // box.contentRect.x, .y, .width, .height
    // box.margin, box.padding, box.border
}
```

### 4. Render (consumer's job)

Walk the laid-out tree, read each node's `LayoutBox`, and draw using your renderer:
- Fill backgrounds at the padding box
- Draw borders at the border box
- Draw text at the content position
- Clip children for overflow

---

## Implementation Priority

Build and test in this order. Each step should have passing tests before moving to the next.

### Phase 1: CSS Tokenizer + Parser
- Tokenize CSS text into tokens
- Parse tokens into Rule/Declaration structures
- Parse inline styles
- **Test**: round-trip parsing of common CSS

### Phase 2: Selector Parser + Matcher
- Parse selector text into compiled Selector
- Implement simple selector matching (tag, class, id, attribute)
- Implement compound selectors
- Implement combinators (descendant, child, sibling)
- Calculate specificity
- **Test**: match selectors against mock ElementRef implementations

### Phase 3: Cascade
- Collect matching rules for an element
- Sort by specificity and source order
- Apply declarations, inline style override
- Implement inheritance (walk parent chain)
- Implement initial values
- Implement shadow DOM scoping (scope parameter)
- **Test**: resolve computed styles for styled documents

### Phase 4: Shorthand Expansion
- margin, padding, border, flex, background, font shorthands
- **Test**: expand and verify longhand outputs

### Phase 5: Length Resolution + Block Layout
- Resolve px, em, %, auto to pixel values
- Implement block formatting context
- Margin collapsing
- Width/height auto sizing
- **Test**: position blocks in a vertical flow

### Phase 6: Inline Layout
- Line box construction
- Word wrapping via TextMetrics
- text-align, vertical-align
- inline-block
- **Test**: wrap text into lines, position inline elements

### Phase 7: Flex Layout
- Main/cross axis, flex-grow/shrink
- Wrapping, alignment
- Gap, order
- **Test**: flex containers with various configurations

### Phase 8: Hit Testing
- Point-in-box traversal
- Z-order (painting order)
- Overflow clipping
- **Test**: hit test various layouts

---

## Testing Strategy

Tests are in `tests/`. The test harness is a simple executable that prints pass/fail.

For CSS parsing and selectors: test against strings with known expected outputs.
For layout: create mock `LayoutNode` trees with hardcoded computed styles, run layout, and assert positions/sizes match expected values.

The layout tests should be derived from the CSS spec examples and common patterns (centered block, flexbox row with gap, text wrapping at boundary, margin collapsing, etc.).

---

## Design Principles

1. **No global state** — all state lives in Cascade, Stylesheet, etc. Multiple instances can coexist.
2. **Consumer owns the DOM** — htmlayout never allocates or owns DOM nodes. It operates on abstract interfaces.
3. **Scope-aware by design** — shadow DOM scoping is a first-class parameter, not an afterthought.
4. **Minimal dependencies** — only gumbo (C library) for HTML parsing. Everything else is standalone C++20.
5. **Testable in isolation** — each module (tokenizer, parser, selector, cascade, layout) can be tested independently with mock inputs.
