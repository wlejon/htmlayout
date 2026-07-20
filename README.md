# htmlayout

[![CI](https://github.com/wlejon/htmlayout/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/htmlayout/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/htmlayout/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/htmlayout/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A standalone C++20 library for CSS parsing, selector matching, style cascade, and box layout. Designed to be embedded in applications that need CSS styling and layout without the overhead of a full browser engine.

htmlayout does **not** own the DOM, render anything, or run JavaScript. You provide the DOM tree and text measurement; htmlayout computes styles and positioned boxes.

## Features

**CSS Engine**
- W3C-compliant tokenizer and parser (Syntax Module Level 3)
- Selectors: type, class, ID, universal, attribute (`[x]`, `[x=y]`, `[x~=y]`, `[x|=y]`, `[x^=y]`, `[x$=y]`, `[x*=y]`, with `i` case-insensitive flag)
- Combinators: descendant, child (`>`), adjacent sibling (`+`), general sibling (`~`)
- Structural pseudo-classes: `:root`, `:empty`, `:first-child`, `:last-child`, `:only-child`, `:first-of-type`, `:last-of-type`, `:only-of-type`, `:nth-child(an+b)`, `:nth-last-child`, `:nth-of-type`, `:nth-last-of-type`
- Logical pseudo-classes: `:not()`, `:is()`, `:where()`, `:has()`
- UI/state pseudo-classes: `:hover`, `:focus`, `:focus-within`, `:focus-visible`, `:active`, `:link`, `:visited`, `:any-link`, `:target`, `:checked`, `:disabled`, `:enabled`, `:required`, `:optional`, `:read-only`, `:read-write`, `:placeholder-shown`, `:indeterminate`, `:defined`
- Pseudo-elements: `::before`, `::after`, `::placeholder`, `::selection`, `::slotted()`, `::part()`
- Full cascade with specificity, source order, `!important`, and inheritance (Cascade Level 5)
- `@layer` cascade layers with spec-compliant priority ordering (including `!important` reversal)
- `@container` queries with named containers and size containment
- Shorthand expansion for ~150 properties (`margin`, `padding`, `border`, `flex`, `grid`, `font`, `container`, `background`, etc.), including multi-layer `background` with `position / size` slash syntax and `border-image` with its `source slice / width / outset repeat` slash syntax (the `border-image-*` longhands are registered with spec initial values; rendering the nine-slice is the consumer's job)
- Color parsing (named, hex, `rgb()`, `rgba()`, `hsl()`, `hsla()`)
- `transform` and `transform-origin` parsing into a 2D affine matrix or 4×4 matrix (3D functions: `translate3d`, `translateZ`, `scale3d`, `scaleZ`, `rotateX/Y/Z`, `rotate3d`, `perspective`, `matrix3d`) for consumer-side rendering
- `@media` query evaluation (`min/max-width`, `min/max-height`, `orientation`, `prefers-color-scheme`, range syntax, logical `or`)
- `@supports` feature queries
- `@import` resolution with consumer-provided callback (with media/layer qualifiers)
- CSS Variables (`var()`) with fallback and inheritance, substituted before shorthand expansion
- Built-in user-agent stylesheet
- Shadow DOM and web component support: `:host`, `:host()`, `:host-context()`, `::slotted()`, `::part()`, scoped stylesheets
- SVG presentation properties (`fill`, `stroke*`, `clip-path`, `paint-order`, `marker-*`, `text-anchor`, `dominant-baseline`, `stop-color`, …) settable via CSS *and* seeded from the matching presentation attribute as the lowest-priority declaration; `direction` / `unicode-bidi` are seeded only on SVG text content elements
- Restyle-scoping hints so consumers can skip work a change cannot reach: `usesHoverPseudo()`, `hoverCanAffect()`, `hoverInvalidatesDescendants()`, `hoverAffectsSiblings()`, `classAffectsDescendants()`, `hasPseudoElementRules()`, `usesContainerQueries()`, `usesForcedInherit()`

**Layout Engine**
- Block formatting context with margin collapsing and floats (`left`, `right`, `clear`)
- Inline formatting context with line wrapping and text alignment (`left`, `right`, `center`, `justify`)
- Flexbox (`flex-direction`, `flex-wrap`, `justify-content`, `align-items`, `align-content`, baseline alignment, grow/shrink, gap, order)
- CSS Grid (templates with `fr` units and `minmax()`, `grid-template-areas`, `span N`, 1-based line placement, `grid-auto-flow`, `grid-auto-columns/rows`, auto-placement, `gap`, `justify-items`, `align-items`, `justify-self`, `align-self`)
- Table layout (`table-row`, `table-cell`, `table-caption`, `border-spacing` (one- or two-value), `border-collapse`, `rowspan`, `colspan`, `caption-side`, `vertical-align`)
- Multi-column layout (`column-count`, `column-width`, `column-gap`, `column-span: all`, `break-before`/`break-after`)
- Length units: `px`, `em`, `%`, `vw`, `vh`, `vmin`, `vmax`, `rem`, `ch`, `ex`, `pt`, `cm`, `mm`, `in`, `pc`
- `calc()` expressions with basic math (`+`, `-`, `*`, `/`) and nested parentheses
- Intrinsic sizing (`min-content`, `max-content`, `fit-content`)
- Bidirectional text: per-line reordering (UAX #9 rule L2) across inline element boundaries, `direction` / `unicode-bidi` isolates, and RTL-aware caret and selection geometry. Character-level resolution comes from the consumer's `TextMetrics::bidiLevels()`; without it the engine still reorders whole boxes by their declared `direction`
- Hit testing with z-order, overflow clipping, and `pointer-events`
- Text geometry queries: caret rect, selection rectangles, and text-node hit testing for editor/selection UIs. Caret positions come from the consumer's shaper cluster map when it provides one, so ligatures and kerning land correctly
- Incremental (dirty-flag) relayout that reuses untouched subtrees, with per-pass statistics (`lastLayoutStats()`)
- `text-overflow: ellipsis`, `overflow-wrap`, `word-break`, `white-space` handling, `text-indent`
- `letter-spacing`, `word-spacing` applied during text measurement
- `display: contents` (children promoted into parent formatting context)
- `position: relative`, `absolute`, `fixed`, `sticky` (layout-time positioning)
- Stacking context creation from `z-index`, `position`, `opacity`, `transform`, `filter`, `isolation`

## Current Limitations

- **Positioning**: `position: sticky` applies a static offset only; scroll-based clamping is not performed (layout-time only).
- **At-rules**: `@font-face` and `@keyframes` are parsed and exposed on the `Cascade` (`Cascade::fontFaces()` / `Cascade::keyframes()`) for the consumer to act on, but the engine itself does no font loading or animation. `@scope` is not implemented.
- **Animations & transitions**: not run — transitions/animations are parsed but produce no time-varying values.
- **Bidirectional text**: the engine reorders each line (rule L2) and honors `direction` isolates, but it does not itself implement the UAX #9 W/N rules that assign character levels. Supply them from ICU (or equivalent) via `TextMetrics::bidiLevels()` / `bidiAware()`; the default reports one uniform level, which reorders boxes but not the characters inside a mixed-direction run.
- **Color**: only legacy color formats (named, hex, `rgb`/`rgba`, `hsl`/`hsla`). `lab()`, `lch()`, `oklab()`, `oklch()`, `color()`, and `color-mix()` are not parsed.
- **Generated content**: `::before` / `::after` boxes participate in layout via consumer-supplied pseudo nodes (`LayoutNode::pseudoBefore()` / `pseudoAfter()`); `content:` string literals, CSS counters, and list-style markers are not synthesized by the engine.
- **Logical properties**: `margin-inline-*`, `padding-inline-*`, `inset-inline-*` etc. expand to physical properties assuming `writing-mode: horizontal-tb` and `direction: ltr`. Vertical writing modes are not fully supported.
- **Grid**: `subgrid` is not implemented.
- **Filters & effects**: `filter`, `backdrop-filter`, and `will-change` are not honored beyond stacking-context effects.
- **Writing modes**: `writing-mode: vertical-*` is parsed but layout is horizontal-tb only.

## Requirements

- C++20 compiler (MSVC 2022, GCC 11+, or Clang 14+)
- CMake 3.24+

## Building

```bash
cmake -B build
cmake --build build --config Debug
```

Run tests:

```bash
# Via ctest (registered as a single test)
ctest --test-dir build -C Debug --output-on-failure

# Or run the binary directly
./build/tests/Debug/htmlayout_test.exe   # Windows
./build/tests/htmlayout_test             # Linux
```

The suite is a single assertion-driven executable — currently 1,925 checks
across tokenizer, parser, selectors, cascade, every formatting context, hit
testing, bidi, text geometry, and incremental relayout.

The `htmlayout` CMake target is an INTERFACE library aggregating the two static
libraries `htmlayout_css` and `htmlayout_layout`. Headers are included relative
to `src/` (`#include "css/cascade.h"`, `#include "layout/box.h"`).

## Continuous Integration

GitHub Actions runs on every push to `main` and every pull request:

- **[CI](.github/workflows/ci.yml)** — builds and runs the test suite via `ctest` across four toolchains: Linux/GCC, Linux/Clang, Windows/MSVC, and macOS/arm64. Debug builds, since the suite is assertion-driven and unoptimized layout math is easier to debug when it fails.
- **Coverage** — a separate job in the same workflow builds with `--coverage` and reports line/branch coverage for `include/` and `src/` with `gcovr` (the vendored gumbo parser and the tests themselves are filtered out). The summary is posted to the job page and the full HTML report is uploaded as a build artifact.
- **[CodeQL](.github/workflows/codeql.yml)** — `security-extended` static analysis of the C++ sources, also on a weekly schedule. gumbo is built ahead of the traced build so upstream findings stay out of this repo's Security tab.
- **[Dependabot](.github/dependabot.yml)** — weekly updates for the pinned GitHub Actions. gumbo is vendored in tree, so there is no package manifest to watch.

## Performance

Layout is built to run every frame, so the engine owns the repeated work rather
than leaving each consumer to discover it:

- **Incremental relayout** — `markDirty()` / `layoutTreeIncremental()` re-run only
  the changed chains and hand back cached subtrees untouched. Flex and grid items
  are reusable across passes, subtree intrinsic widths are cached, and per-node
  subtree hit bounds are only recomputed when something moved.
- **Per-pass caches** — a `MeasureCache` memoizes the consumer's shaper for the
  duration of one pass (min-content sizing, max-content sizing, and line breaking
  otherwise ask the same questions repeatedly), and a `NodeStyleCache` projects
  each node's `ComputedStyle` into a flat array indexed by property on first
  visit. Both are scoped to a single synchronous pass, which is the whole
  invalidation story: nothing they cache can change while the pass runs.
- **Selector indexing** — rules are bucketed by subject key, and pseudo-element
  rules are indexed separately, so a hover flip re-matches only what a rule names.
- **Instrumentation** — `lastLayoutStats()` reports nodes laid out vs. reused vs.
  visited, time split across the three passes (`treeMs` / `absoluteMs` /
  `hitBoundsMs`), style lookups and misses, and measure calls vs. actual shapes.

```bash
# The benchmark must be built and run in Release — timing a Debug build of a
# container-heavy library measures the debug iterators, not the engine.
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --target htmlayout_bench
```

`tools/ab.sh` runs a baseline and the working tree interleaved (min-of-N, built
in a detached git worktree so nothing touches your checkout), and
`tools/parity.sh` diffs both halves against Chromium through the `bro` parity
harness — the check for rewrites that produce subtly wrong layout rather than
failing tests.

## Usage

htmlayout operates on abstract interfaces that you implement to bridge your DOM and font system.

### 1. Implement the interfaces

```cpp
#include "css/selector.h"   // ElementRef
#include "layout/box.h"     // LayoutNode, TextMetrics

// Bridge your DOM for CSS matching. All string accessors return std::string_view
// and children() returns a std::span — the consumer owns the backing storage.
class MyElement : public htmlayout::css::ElementRef {
    std::string_view tagName() const override;
    std::string_view id() const override;
    std::string_view className() const override;
    std::string_view getAttribute(std::string_view name) const override;
    bool hasAttribute(std::string_view name) const override;
    ElementRef* parent() const override;
    std::span<ElementRef* const> children() const override;
    int childIndex() const override;
    int childIndexOfType() const override;
    int siblingCount() const override;
    int siblingCountOfType() const override;
    // Optional state/structure hooks (all have default impls):
    //   isHovered(), isFocused(), isActive(),
    //   isLink(), isVisited(), isFocusWithin(), isFocusVisible(),
    //   isChecked(), isDisabled(), isEnabled(), isRequired(), isOptional(),
    //   isReadOnly(), isReadWrite(), isPlaceholderShown(), isIndeterminate(), isTarget(),
    //   scope(), shadowRoot(), assignedSlot(), partName(), isDefined(),
    //   containerType(), containerName(), containerInlineSize(), containerBlockSize()
};

// Bridge your DOM for layout
class MyLayoutNode : public htmlayout::layout::LayoutNode {
    std::string_view tagName() const override;
    bool isTextNode() const override;
    std::string_view textContent() const override;
    LayoutNode* parent() const override;
    std::span<LayoutNode* const> children() const override;
    const htmlayout::css::ComputedStyle& computedStyle() const override;
    // Optional (all have default impls):
    //   attribute() (presentational HTML attrs like colspan/rowspan/width/height/align),
    //   intrinsicSize() (replaced elements like <input>, <textarea>, <select>),
    //   scrollLeftPx() / scrollTopPx() (for scrollable containers in hit testing),
    //   pseudoBefore() / pseudoAfter() (consumer-synthesized ::before / ::after boxes)
    // After layout, read results from the `box` field; the engine also sets
    // availableHeight / viewportHeight on each node during layout.
};

// Provide text measurement from your font engine
class MyTextMetrics : public htmlayout::layout::TextMetrics {
    float measureWidth(std::string_view text, std::string_view fontFamily,
                       float fontSize, std::string_view fontWeight) override;
    float lineHeight(std::string_view fontFamily, float fontSize,
                     std::string_view fontWeight) override;
    // Optional (all have default impls, so the two pure virtuals above are
    // enough to get running):
    //   ascent() / xHeight() / naturalHeight() — exact font metrics for
    //     baseline alignment, vertical-align: middle, and text-run rects
    //     (defaults approximate 0.8em / 0.5em / lineHeight)
    //   caretXAtOffset() / offsetAtCaretX() / clusterRangeAt() + clusterAware() —
    //     answer caret queries from the shaper's cluster map instead of prefix
    //     widths, which is what makes carets correct across kerning pairs,
    //     ligatures, and Indic syllables
    //   bidiLevels() + bidiAware() — UAX #9 character levels (e.g. from ICU);
    //     without them the engine reorders boxes but not characters
};
```

### 2. Parse CSS and resolve styles

```cpp
using namespace htmlayout::css;

Cascade cascade;
cascade.addStylesheet(defaultUserAgentStylesheet(), nullptr, nullptr, Origin::UserAgent);
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
// Or, with explicit viewport for proper vw/vh resolution:
//   layoutTree(rootNode, Viewport{w, h}, metrics);

// Optionally clip subtrees of overflow:hidden/scroll/auto nodes to their content+padding box:
applyOverflowClipping(rootNode);

// Each node's `box` field now contains positioned results:
// box.contentRect  — {x, y, width, height}
// box.margin, box.padding, box.border — edge sizes
// box.naturalHeight — pre-clamp content height (scroll extent)
// box.textTruncated — true if text-overflow:ellipsis truncated this node
// box.textRuns      — placed run geometry for text nodes
```

### 4. Hit testing

```cpp
LayoutNode* hit = hitTest(rootNode, mouseX, mouseY);
```

### 5. Incremental relayout

```cpp
// Skip the pass entirely when no changed property can move a box:
if (needsRelayout({"color", "background-color"})) {
    markDirty(changedNode);            // or markSubtreeDirty() for a whole branch
    layoutTreeIncremental(rootNode, viewportWidth, metrics);
}

const LayoutStats& s = lastLayoutStats();   // laidOut / reused / visits, per-pass ms
```

### 6. Text geometry (caret and selection)

```cpp
#include "layout/text_geometry.h"

TextHit hit = hitTestText(rootNode, mouseX, mouseY, metrics);

float cx, cy, ch;
if (getCaretRect(rootNode, textNode, srcOffset, metrics, cx, cy, ch)) { /* draw caret */ }

auto rects = getSelectionRects(rootNode, startNode, startOff, endNode, endOff, metrics);
```

## Project Structure

```
src/css/       CSS tokenizer, parser, selector matcher, cascade, properties,
               color, transform, UA stylesheet
src/layout/    Block, inline, flex, grid, table, multi-column layout,
               formatting-context dispatch, hit testing, text breaking,
               bidi line reordering, text geometry (caret/selection),
               per-pass style and measurement caches
tests/         Test suite (single htmlayout_test executable)
bench/         Release-only layout benchmark and sampling profiler
tools/         ab.sh (baseline-vs-working-tree A/B), parity.sh (Chromium parity)
scripts/       coverage.ps1 (local coverage run)
third_party/   Bundled gumbo HTML5 parser
docs/          Architecture document (docs/architecture.md)
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
- **The repeated work belongs to the library** -- caching the shaper, projecting styles, and scoping a restyle are things every consumer would otherwise reinvent, so they live here behind pass-scoped lifetimes that need no invalidation contract.

See [`docs/architecture.md`](docs/architecture.md) for the full design document and API reference.

## License

This project is licensed under the [MIT License](LICENSE).
