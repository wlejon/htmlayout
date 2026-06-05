# CLAUDE.md

## What This Project Is

htmlayout is a standalone C++20 library providing CSS parsing, selector matching, style cascade, and box layout for HTML documents. It is consumed by other projects (like `bro`) as a dependency. It does NOT own the DOM, render anything, or run JavaScript.

See `docs/architecture.md` for the full design, implementation plan, and API reference.

## Build Commands

```bash
# Configure
cmake -B build

# Build (debug)
cmake --build build --config Debug

# Build (release)
cmake --build build --config Release

# Run tests (Windows)
./build/tests/Debug/htmlayout_test.exe

# Run tests (Linux)
./build/tests/htmlayout_test
```

Builds with MSVC 2022 (Windows) and GCC/Clang (Linux). C++20 required.

## Project Structure

```
src/css/       — CSS tokenizer, parser, selector matcher, cascade, properties,
                 color, transform, UA stylesheet
src/layout/    — Block, inline, flex, grid, table, and multi-column layout;
                 formatting-context dispatch, hit testing, text breaking,
                 text geometry (caret/selection)
third_party/   — gumbo HTML parser (bundled)
tests/         — Single htmlayout_test executable
docs/          — Architecture document (docs/architecture.md)
```

Targets: `htmlayout` (INTERFACE) aggregates the two static libs `htmlayout_css`
and `htmlayout_layout`. Headers are included relative to `src/` (e.g.
`#include "css/cascade.h"`, `#include "layout/box.h"`).

## Namespace

- `htmlayout::css` — CSS types and functions
- `htmlayout::layout` — layout types and functions

## Key Design Points

- **Consumer provides the DOM and fonts** via abstract interfaces — `ElementRef` (CSS matching), `LayoutNode` (layout), and `TextMetrics` (text measurement). String accessors take/return `std::string_view` and child lists are `std::span<... const>`; the consumer owns the backing storage for the duration of the call.
- **Shadow DOM scoping is built-in** — `Cascade::addStylesheet(sheet, scope, media, origin)` plus `ElementRef::scope()` / `shadowRoot()` / `assignedSlot()` / `partName()`. `:host`, `:host-context()`, `::slotted()`, `::part()` all supported.
- **Modern CSS surface** — cascade layers (`@layer`), container queries (`@container`), `@media` / `@supports` / `@import`, CSS variables, and a layout engine spanning block/inline/flex/grid/table/multi-column. See `README.md` for the full feature matrix and current limitations.
- **No global state** — `Cascade` is instance-based; layout is a pure tree walk. Multiple instances coexist.
- **No rendering, no animation** — layout outputs positioned boxes (consumer draws them); `@keyframes` / `@font-face` are parsed and exposed on `Cascade` for the consumer to act on, but the engine runs no animations or font loading.

