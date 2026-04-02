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

# Run tests
./build/tests/Debug/htmlayout_test.exe
```

Uses the Visual Studio generator (multi-config). MSVC 2022, C++20.

## Project Structure

```
src/css/       — CSS tokenizer, parser, selector matcher, cascade, properties
src/layout/    — Block, inline, and flex box layout engine
third_party/   — gumbo HTML parser (bundled)
tests/         — Test executable
docs/          — Architecture document
```

## Namespace

- `htmlayout::css` — CSS types and functions
- `htmlayout::layout` — layout types and functions

## Key Design Points

- **Consumer provides the DOM** via abstract interfaces (`ElementRef`, `LayoutNode`)
- **Shadow DOM scoping is built-in** — `Cascade::addStylesheet(sheet, scope)` and `ElementRef::scope()`
- **No global state** — everything is instance-based
- **No rendering** — layout outputs positioned boxes, consumer draws them

