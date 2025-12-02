Syntax highlighting in kte
==========================

Overview
--------

kte provides lightweight syntax highlighting with a pluggable highlighter interface. The initial implementation targets C/C++ and focuses on speed and responsiveness.

Core types
----------

- `TokenKind` — token categories (keywords, types, strings, comments, numbers, preprocessor, operators, punctuation, identifiers, whitespace, etc.).
- `HighlightSpan` — a half-open column range `[col_start, col_end)` with a `TokenKind`.
- `LineHighlight` — a vector of `HighlightSpan` and the buffer `version` used to compute it.

Engine and caching
------------------

- `HighlighterEngine` maintains a per-line cache of `LineHighlight` keyed by row and buffer version.
- Cache invalidation occurs when the buffer version changes or when the buffer calls `InvalidateFrom(row)`, which clears cached lines and line states from `row` downward.
- The engine supports both stateless and stateful highlighters. For stateful highlighters, it memoizes a simple per-line state and computes lines sequentially when necessary.

Stateful highlighters
---------------------

- `LanguageHighlighter` is the base interface for stateless per-line tokenization.
- `StatefulHighlighter` extends it with a `LineState` and the method `HighlightLineStateful(buf, row, prev_state, out)`.
- The engine detects `StatefulHighlighter` via dynamic_cast and feeds each line the previous line’s state, caching the resulting state per line.

C/C++ highlighter
-----------------

- `CppHighlighter` implements `StatefulHighlighter`.
- Stateless constructs: line comments `//`, strings `"..."`, chars `'...'`, numbers, identifiers (keywords/types), preprocessor at beginning of line after leading whitespace, operators/punctuation, and whitespace.
- Stateful constructs (v2):
  - Multi-line block comments `/* ... */` — the state records whether the next line continues a comment.
  - Raw strings `R"delim(... )delim"` — the state tracks whether we are inside a raw string and its delimiter `delim` until the closing sequence appears.

Limitations and TODOs
---------------------

- Raw string detection is intentionally simple and does not handle all corner cases of the C++ standard.
- Preprocessor handling is line-based; continuation lines with `\\` are not yet tracked.
- No semantic analysis; identifiers are classified via small keyword/type sets.
- Additional languages (JSON, Markdown, Shell, Python, Go, Rust, Lisp, …) are planned.
- Terminal color mapping is conservative to support 8/16-color terminals. Rich color-pair themes can be added later.

Renderer integration
--------------------

- Terminal and GUI renderers request line spans via `Highlighter()->GetLine(buf, row, buf.Version())`.
- Search highlight and cursor overlays take precedence over syntax colors.
