### Objective
Introduce fast, minimal‑dependency syntax highlighting to kte, consistent with current architecture (Editor/Buffer + GUI/Terminal renderers), preserving ke UX and performance.

### Guiding principles
- Keep core small and fast; no heavy deps (C++17 only). 
- Start simple (stateless line regex), evolve incrementally (stateful, caching). 
- Work in both Terminal (ncurses) and GUI (ImGui) with consistent token classes and theme mapping. 
- Integrate without disrupting existing search highlight, selection, or cursor rendering.

### Scope of v1
- Languages: plain text (off), C/C++ minimal set (keywords, types, strings, chars, comments, numbers, preprocessor). 
- Stateless per‑line highlighting; handle single‑line comments and strings; defer multi‑line state to v2. 
- Toggle: `:syntax on|off` and per‑buffer filetype selection.

### Architecture
1. Core types (new):
   - `enum class TokenKind { Default, Keyword, Type, String, Char, Comment, Number, Preproc, Constant, Function, Operator, Punctuation, Identifier, Whitespace, Error };`
   - `struct HighlightSpan { int col_start; int col_end; TokenKind kind; };`  // 0‑based columns in buffer indices per rendered line
   - `struct LineHighlight { std::vector<HighlightSpan> spans; uint64_t version; };`

2. Interfaces (new):
   - `class LanguageHighlighter { public: virtual ~LanguageHighlighter() = default; virtual void HighlightLine(const Buffer& buf, int row, std::vector<HighlightSpan>& out) const = 0; virtual bool Stateful() const { return false; } };`
   - `class HighlighterEngine { public: void SetHighlighter(std::unique_ptr<LanguageHighlighter>); const LineHighlight& GetLine(const Buffer&, int row, uint64_t buf_version); void InvalidateFrom(int row); };`
   - `class HighlighterRegistry { public: static const LanguageHighlighter& ForFiletype(std::string_view ft); static std::string DetectForPath(std::string_view path, std::string_view first_line); };`

3. Editor/Buffer integration:
   - Per‑Buffer settings: `bool syntax_enabled; std::string filetype; std::unique_ptr<HighlighterEngine> highlighter;`
   - Buffer emits a monotonically increasing `version` on edit; renderers request line highlights by `(row, version)`. 
   - Invalidate cache minimally on edits (v1: current line only; v2: from current line down when stateful constructs present).

### Rendering integration
- TerminalRenderer/GUIRenderer changes:
  - During line rendering, query `Editor.CurrentBuffer()->highlighter->GetLine(buf, row, buf_version)` to obtain spans.
  - Apply token styles while drawing glyph runs. 
- Z‑order and blending:
  1) Backgrounds (e.g., selection, search highlight rectangles) 
  2) Text with syntax colors 
  3) Cursor/IME decorations
- Search highlights must remain visible over syntax colors:
  - Terminal: combine color/attr with reverse/bold for search; if color conflicts, prefer search. 
  - GUI: draw semi‑transparent rects behind text (already present); keep syntax color for text.

### Theme and color mapping
- Extend `GUITheme.h` with a `SyntaxPalette` mapping `TokenKind -> ImVec4 ink` (and optional background tint for comments/strings disabled by default). Provide default Light/Dark palettes. 
- Terminal: map `TokenKind` to ncurses color pairs where available; degrade gracefully on 8/16‑color terminals (e.g., comments=dim, keywords=bold, strings=yellow/green if available). 

### Language detection
- v1: by file extension; allow manual `:set filetype=<lang>`. 
- v2: add shebang detection for scripts, simple modelines (optional).

### Commands/UX
- `:syntax on|off` — global default; buffer inherits on open. 
- `:set filetype=<lang>` — per‑buffer override. 
- `:syntax reload` — rebuild patterns/themes. 
- Status line shows filetype and syntax state when changed.

### Implementation plan (phased)
1. Phase 1 — Minimal regex highlighter for C/C++
   - Implement `CppRegexHighlighter : LanguageHighlighter` with precompiled `std::regex` (or hand‑rolled simple scanners to avoid regex backtracking). Classes: line comment `//…`, block comment start `/*` (no state), string `"…"`, char `'…'` (no multiline), numbers, keywords/types, preprocessor `^\s*#\w+`. 
   - Add `HighlighterEngine` with a simple per‑row cache keyed by `(row, buf_version)`; no background worker. 
   - Integrate into both renderers; add palette to `GUITheme.h`; add terminal color selection. 
   - Add commands. 

2. Phase 2 — Stateful constructs and more languages
   - Add state machine for multiline comments `/*…*/` and multiline strings (C++11 raw strings), with invalidation from edit line downward until state stabilizes. 
   - Add simple highlighters: JSON (strings, numbers, booleans, null, punctuation), Markdown (headers/emphasis/code fences), Shell (comments, strings, keywords), Go (types, constants, keywords), Python (strings, comments, keywords), Rust (strings, comments, keywords), Lisp (comments, strings, keywords),. 
   - Filetype detection by extension + shebang. 

3. Phase 3 — Performance and caching
   - Viewport‑first highlighting: compute only visible rows each frame; background task warms cache around viewport. 
   - Reuse span buffers, avoid allocations; small‑vector optimization if needed. 
   - Bench with large files; ensure O(n_visible) cost per frame.

4. Phase 4 — Extensibility
   - Public registration API for external highlighters. 
   - Optional Tree‑sitter adapter behind a compile flag (off by default) to keep dependencies minimal. 

### Data flow (per frame)
- Renderer asks Editor for Buffer and viewport rows. 
- For each row: `engine.GetLine(buf, row, buf.version)` → spans. 
- Renderer emits runs with style from `SyntaxPalette[kind]`. 
- Search highlights are applied as separate background rectangles (GUI) or attribute toggles (Terminal), not overriding text color.

### Testing
- Unit tests for tokenization per language: golden inputs → spans. 
- Fuzz/edge cases: escaped quotes, numeric literals, preprocessor lines. 
- Renderer tests with `TestRenderer` asserting the sequence of style changes for a line. 
- Performance tests: highlight 1k visible lines repeatedly; assert time under threshold.

### Risks and mitigations
- Regex backtracking/perf: prefer linear scans; precompute keyword tables; avoid nested regex. 
- Terminal color limitations: feature‑detect colors; provide bold/dim fallbacks. 
- Stateful correctness: invalidate conservatively (from edit line downward) and cap work per frame.

### Deliverables
- New files: `Highlight.h/.cc`, `HighlighterEngine.h/.cc`, `LanguageHighlighter.h`, `CppHighlighter.h/.cc`, optional `HighlighterRegistry.h/.cc`. 
- Renderer updates: `GUIRenderer.cc`, `TerminalRenderer.cc` to consume spans. 
- Theming: `GUITheme.h` additions for syntax colors. 
- Editor/Buffer: per‑buffer syntax settings and highlighter handle. 
- Commands in `Command.cc` and help text updates. 
- Docs: README/ROADMAP update and a brief `docs/syntax.md`. 
- Tests: unit and renderer golden tests.