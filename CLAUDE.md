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

# Or via ctest (the suite is registered as a single test)
ctest --test-dir build -C Debug --output-on-failure
```

Builds with MSVC 2022 (Windows) and GCC/Clang (Linux). C++20 required. The suite
is one assertion-driven executable; it should end with `0 failed`.

Performance work has its own tooling — never time a Debug build:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --target htmlayout_bench

./tools/ab.sh       # this working tree vs. a baseline worktree, interleaved, min-of-N
./tools/parity.sh   # bro-vs-Chromium parity for both halves (see ../broparity)
```

## Project Structure

```
src/css/       — CSS tokenizer, parser, selector matcher, cascade, properties,
                 color, transform, UA stylesheet
src/layout/    — Block, inline, flex, grid, table, and multi-column layout;
                 formatting-context dispatch, hit testing, text breaking,
                 bidi line reordering, text geometry (caret/selection),
                 per-pass style and measurement caches
third_party/   — gumbo HTML parser (bundled)
tests/         — Single htmlayout_test executable
bench/         — Release-only layout benchmark + sampling profiler
tools/         — ab.sh, parity.sh
scripts/       — coverage.ps1
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
- **Built to run every frame** — `markDirty()` + `layoutTreeIncremental()` reuse untouched subtrees; a per-pass `MeasureCache` and `NodeStyleCache` absorb the repeated shaping and style lookups; `Cascade` exposes hints (`hoverCanAffect()`, `classAffectsDescendants()`, …) so consumers can skip restyle work a change cannot reach. Both caches are scoped to a single synchronous pass — that scope is the entire invalidation story, so don't extend their lifetime. `lastLayoutStats()` reports what a pass actually did.
- **Optional precision hooks on `TextMetrics`** — `ascent()`/`xHeight()`/`naturalHeight()`, cluster-aware caret queries, and UAX #9 `bidiLevels()` all have defaults so a two-method consumer keeps working; the defaults are approximations, and correctness must never *depend* on `clusterAware()` / `bidiAware()` being true.

