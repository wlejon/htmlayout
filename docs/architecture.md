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

# Benchmarks must be Release — see bench/CMakeLists.txt
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --target htmlayout_bench
```

Uses Visual Studio generator (multi-config) on Windows. Vcpkg at D:/vcpkg is auto-detected. MSVC 2022, C++20. CI additionally builds with GCC and Clang on Linux and on macOS/arm64.

`htmlayout` is an INTERFACE target aggregating two static libraries,
`htmlayout_css` and `htmlayout_layout`; `htmlayout_layout` links `htmlayout_css`.
Include paths are rooted at `src/`.

## Project Structure

```
src/
  css/
    tokenizer.h/cpp    — CSS tokenizer (text → tokens)
    parser.h/cpp       — CSS parser (tokens → rules + declarations)
    selector.h/cpp     — CSS selector parser + matcher
    cascade.h/cpp      — Style cascade with scope support (shadow DOM),
                         rule indexing, and restyle-scoping hints
    properties.h/cpp   — CSS property registry (defaults, inheritance, shorthands)
    color.h/cpp        — Color parsing (named, hex, rgb, hsl)
    transform.h/cpp    — transform / transform-origin → 2D affine or 4x4 matrix
    ua_stylesheet.h/cpp — Built-in default styles
  layout/
    box.h/cpp          — Core types (Rect, Edges, LayoutBox), the LayoutNode /
                         TextMetrics interfaces, tree entry point, hit testing,
                         and LayoutStats
    block.h/cpp        — Block formatting context (including floats and BFC)
    inline.h/cpp       — Inline formatting context (line boxes and IFC)
    flex.h/cpp         — Flexbox layout
    grid.h/cpp         — CSS Grid layout
    table.h/cpp        — Table layout
    text.h/cpp         — Text measurement and line breaking
    bidi_line.h/cpp    — Per-line bidi reordering (UAX #9 rule L2)
    text_geometry.h/cpp — Caret rects, selection rects, text-node hit testing
    style_cache.h/cpp  — Per-pass projection of ComputedStyle into a flat array
    measure_cache.h    — Per-pass memo in front of the consumer's shaper
    style_props.h      — The Prop enum layout reads styles by
    multicol.h/cpp     — Multi-column container detection
    formatting_context.h/cpp — Dispatch + length resolution + calc()
third_party/
  gumbo/               — HTML5 parser (C library)
tests/                 — Single htmlayout_test executable (1,925 assertions)
bench/                 — Release-only layout benchmark + sampling profiler
tools/                 — ab.sh (A/B against a baseline worktree),
                         parity.sh (bro-vs-Chromium parity, both halves)
scripts/               — coverage.ps1
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

Supports `@media`, `@supports`, `@layer`, `@container`, and `@import` rules.

Reference: https://www.w3.org/TR/css-syntax-3/#parsing

### Selector (`selector.h`)

Parses and matches CSS selectors. This is the critical piece for shadow DOM support.

**Parsing**: Converts selector text into a compiled `Selector` struct that can be efficiently matched.

**Matching**: `Selector::matches(ElementRef&)` checks if a selector matches a given element. The `ElementRef` is an abstract interface — the consumer implements it to bridge their DOM.

**Supported selectors**:

1. **Simple selectors**: `div`, `.class`, `#id`, `*`, `[attr]`, `[attr=value]`, `[attr^=val]`, `[attr$=val]`, `[attr*=val]`, `[attr~=val]`, `[attr|=val]`
2. **Compound selectors**: `div.class#id`, `.a.b`
3. **Combinators**: descendant (` `), child (`>`), adjacent sibling (`+`), general sibling (`~`)
4. **Pseudo-classes**: `:first-child`, `:last-child`, `:nth-child(n)`, `:nth-last-child(n)`, `:nth-of-type(n)`, `:nth-last-of-type(n)`, `:only-child`, `:first-of-type`, `:last-of-type`, `:only-of-type`, `:root`, `:empty`, `:not(sel)`, `:is(sel)`, `:where(sel)`, `:has(sel)`, `:hover`, `:focus`, `:active`, `:focus-within`, `:focus-visible`, `:any-link`, `:link`, `:visited`, `:checked`, `:disabled`, `:enabled`, `:required`, `:optional`, `:read-only`, `:read-write`, `:placeholder-shown`, `:indeterminate`, `:target`, `:host`, `:host(sel)`, `:host-context(sel)`, `:defined`
5. **Pseudo-elements**: `::before`, `::after`, `::placeholder`, `::selection`, `::slotted(sel)`, `::part(name)`
6. **Comma-separated lists**: `h1, h2, h3`

**Specificity**: Calculated as (id_count, class_count, type_count) packed into a uint32 for comparison.

**Shadow DOM scoping**: The `ElementRef::scope()` method returns which shadow scope an element belongs to. The cascade uses this to ensure shadow-scoped rules only match elements in their scope. `:host` matches the shadow host; `::slotted` matches light DOM children; `::part` matches elements inside a shadow tree from the outer scope.

Reference: https://www.w3.org/TR/selectors-4/

### Cascade (`cascade.h`)

Resolves the final computed style for each element. This is the "C" in CSS.

**`Cascade::addStylesheet(sheet, scope, mediaContext, origin)`** — Adds rules to the cascade.
- `scope`: `nullptr` for document-level styles, or a shadow root pointer for shadow-scoped styles.
- `mediaContext`: Optional viewport dimensions to filter `@media` rules at addition time.
- `origin`: `Origin::Author` (default) or `Origin::UserAgent` — used by the `revert` keyword.

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

**Output**: `ComputedStyle` — maps longhand property names to their resolved values. The map is heterogeneously keyed (`SvHash`/`SvEq`), because layout reads it hundreds of thousands of times per pass with string literals and a `std::string`-keyed map would construct — and often heap-allocate — a key on every read.

SVG presentation attributes (`fill`, `stroke-width`, …) enter the cascade at step 0 as specificity-0 declarations: they beat inheritance but lose to any author rule or inline style. `direction` and `unicode-bidi` are seeded only on SVG text content elements, since HTML spells the same idea `dir`.

**Rule indexing**: rules are bucketed by subject key (id / class / tag), and pseudo-element rules are indexed by name, so matching probes a handful of candidates instead of scanning the sheet.

**Restyle-scoping hints**: the cascade records what its rules can reach so a consumer can decide *not* to re-resolve. `usesHoverPseudo()`, `hoverCanAffect()`, `hoverInvalidatesDescendants()`, and `hoverAffectsSiblings()` bound a hover flip to the elements whose rule set actually changed; `classAffectsDescendants()` does the same for a class toggle; `hasPseudoElementRules()` and `contentIsStateful()` gate the generated-content pass; `usesContainerQueries()` tells the consumer a second style pass after layout is needed; `usesForcedInherit()` warns that an inherited-value diff is not a sufficient restyle boundary. All are conservative in the presence of `:has()`, whose matching runs the other way.

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
void layoutTree(LayoutNode* root, const Viewport& viewport, TextMetrics& metrics);
```

Walks the tree, resolves CSS lengths, and dispatches each node to the appropriate formatting context based on its `display` property. The `Viewport` overload supplies a height, which `vh`/`vmin`/`vmax` need.

`layoutTree()` runs three passes: in-flow layout, absolute/fixed positioning, and per-node subtree hit-bounds caching. Only the first is incremental, which is why `LayoutStats` times them separately — a pass whose cost does not move with `laidOut` is being spent in one of the other two.

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
7. Multi-column: `column-count`, `column-width`, `column-gap`, `column-span: all`, `break-before`/`break-after` column breaks.

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

Implements CSS Grid (L1 + L2 features):
1. Parse track lists: handles `repeat()`, `minmax()`, `fr` units.
2. `repeat(auto-fill, ...)` and `repeat(auto-fit, ...)` for responsive layouts.
3. Named grid lines: `[name]` syntax with resolution in placement properties.
4. Resolve track sizes: distributes available space to fractional tracks.
5. Implicit track sizing via `grid-auto-rows` and `grid-auto-columns`.
6. Item placement: 1-based line indices, named lines, `grid-area`, and `grid-template-areas`.
7. Auto-placement: fills available cells following `grid-auto-flow`.
8. auto-fit empty track collapsing.
9. Gap support between tracks.

### Table Layout (`table.h`)

Implements Table layout:
1. Collect rows, cells, and captions.
2. Generate anonymous rows/cells for missing table structure.
3. Handle `rowspan` and `colspan` spanning.
4. Distribute column widths proportionally based on content.
5. Align cells in rows and stretch heights to match.
6. Support `border-spacing`, `border-collapse`, `caption-side`, and `vertical-align`.

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

Finds the deepest node whose layout box contains the point (x, y). Respects z-order (later siblings on top), overflow clipping, and `pointer-events: none`. Each node caches the bounds of its own subtree during layout, so a miss prunes the whole branch instead of descending it.

### Bidi Reordering (`bidi_line.h`)

UAX #9 resolves embedding levels over a *paragraph* and reorders them per *line*, which is why this runs after line breaking rather than inside the text splitter: only once the breaker has decided which runs share a line is there a line to reorder. Every inline formatting context funnels through here — the dedicated IFC, the anonymous-block IFC, and the pure-inline path.

The split of responsibility with the consumer:

- The engine implements rule L2 (reverse maximal runs from the highest level down to the lowest odd level), treats an item whose own `direction` opposes the base as an isolate, and walks an inline element's subtree so bidi does not stop at a `<b>` boundary.
- The consumer supplies character levels through `TextMetrics::bidiLevels()` (e.g. from ICU) and advertises it with `bidiAware()`. The default assigns one uniform level, which reorders boxes by their declared direction but not the characters inside a mixed-direction run.

### Text Geometry (`text_geometry.h`)

Caret rect, selection rectangles (emitted per direction run), and text-node hit testing, for consumers building editors or selection UIs.

Caret positions come from `TextMetrics::caretXAtOffset()` / `offsetAtCaretX()` / `clusterRangeAt()` when the consumer implements them from a shaper's cluster map. The defaults reproduce prefix-width measurement, which is wrong for any font that kerns and wrong by construction where two characters ligate — so a consumer with a real shaper should say so via `clusterAware()`.

### Incremental Relayout

`markDirty(node)` marks a node and its ancestor chain; `markSubtreeDirty(node)` marks a whole branch. `layoutTreeIncremental()` then re-runs only the dirty chains and hands back cached subtrees untouched. Flex and grid items are reusable across passes, and subtree intrinsic widths are cached.

`needsRelayout({props...})` lets a consumer skip the pass entirely when no changed property can move a box. Document-global inputs (viewport size, root font size) have no local signal, so the root compares them and dirties the whole tree when they move.

### Per-Pass Caches

Both caches are scoped to one synchronous layout pass, and that scope *is* the invalidation story — nothing they cache can change while the pass runs, and the next pass starts empty.

- **`MeasureCache`** wraps the consumer's `TextMetrics`. Min-content sizing, max-content sizing, and line breaking each walk the same words, so one pass over a large document makes tens of thousands of `measureWidth()` calls resolving to a few hundred distinct questions. Shaping is the most expensive thing layout asks anyone to do, so the repeats belong to the library rather than to every consumer in turn.
- **`NodeStyleCache`** projects a node's `ComputedStyle` into a flat array indexed by `Prop` on its first visit in a pass. Layout visits a node ~2.75 times per pass and asks each for every property it knows about; most of those miss, and a miss pays a map probe *plus* a second lookup in the property registry for the initial value. Keeping the cache pass-scoped is deliberate: a consumer may rewrite a `ComputedStyle` without marking the node dirty (a paint-only `:hover` does exactly that), so a cache outliving the pass could hand hit testing a stale transform.

Outside a pass, reads go to the live map.

### Instrumentation (`LayoutStats`)

`lastLayoutStats()` reports, per pass: nodes laid out, reused, and visited; the three passes' times; style lookups and misses; and measure calls vs. calls that reached the shaper. `styleLookupHistogram()` / `styleLookupSiteHistogram()` attribute lookups to a property or a call site. `bench/` samples a Release build; `tools/ab.sh` compares a change against a baseline built in a detached worktree, interleaved and reported as min-of-N; `tools/parity.sh` diffs both halves against Chromium through the `bro` parity harness, which is what catches a rewrite that binds a property to the wrong node without failing a test.

---

## Known Limitations

- **Positioning**: `position: sticky` applies a static offset only; scroll-based clamping is not performed (layout-time only).
- **At-rules**: `@font-face` and `@keyframes` are parsed and exposed on `Cascade` (`fontFaces()` / `keyframes()`) for the consumer to act on; the engine loads no fonts and runs no animations. `@scope` is not implemented.
- **Animations & transitions**: parsed, never advanced — no time-varying values.
- **Bidirectional text**: the engine reorders lines and honors isolates, but does not implement the UAX #9 W/N rules that assign character levels; supply those via `TextMetrics::bidiLevels()`.
- **Color**: legacy formats only (named, hex, `rgb`/`rgba`, `hsl`/`hsla`). No `lab()`, `lch()`, `oklab()`, `oklch()`, `color()`, or `color-mix()`.
- **Generated content**: `::before` / `::after` boxes lay out via consumer-supplied pseudo nodes; the engine synthesizes no `content:` strings, counters, or list markers.
- **Logical properties**: Map to physical properties assuming `writing-mode: horizontal-tb` and `direction: ltr`. Vertical writing modes are not supported.
- **Grid subgrid**: `subgrid` keyword is not implemented.
- **Filters & effects**: `filter`, `backdrop-filter`, `will-change` affect stacking context creation only.

---

## Design Principles

1. **No global state** — all state lives in Cascade, Stylesheet, etc. Multiple instances can coexist.
2. **Consumer owns the DOM** — htmlayout never allocates or owns DOM nodes. It operates on abstract interfaces.
3. **Scope-aware by design** — shadow DOM scoping is a first-class parameter, not an afterthought.
4. **Minimal dependencies** — only gumbo (C library) for HTML parsing. Everything else is standalone C++20.
5. **Testable in isolation** — each module (tokenizer, parser, selector, cascade, layout) can be tested independently with mock inputs.
6. **The repeated work belongs to the library** — caching the shaper, projecting styles, scoping a restyle, and pruning hit tests are all things a consumer would otherwise rediscover. They live here, behind pass-scoped lifetimes that need no invalidation contract.
