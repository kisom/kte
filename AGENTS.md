# kte

kte (Kyle's Text Editor) is a C++20 text editor with a terminal-first design (ncurses) and an optional ImGui GUI frontend (SDL2/OpenGL/Freetype); it uses a WordStar/VDE-style command model. Standards: none. The Metacircular service standards do not apply here.

## Bootstrap (run once per checkout, idempotent)
nix develop (default shell; also provides a `terminal` devshell) or system SDL2/Freetype/ncurses if not using nix.

## Gate (run before saying "done"; must exit 0)
make gate            # what it runs: configure (cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=ON -DBUILD_TESTS=ON) -> build (cmake --build build) -> test (./build/kte_tests)
Fast check while editing: cmake --build build --target kte_tests && ./build/kte_tests
Also run clang-tidy on changed files via build/compile_commands.json (not part of `make gate`; clang-tidy is not on this Mac's PATH).

## Run
Terminal editor: `./build/kte <file>` (always built).
GUI editor: `./build/kge <file>` (built when `-DBUILD_GUI=ON`; requires SDL2/OpenGL/Freetype).
Docker (cross-platform Linux testing): `docker build -t kte-linux . && docker run --rm -v "$(pwd):/kte" kte-linux`.

## Sensors the harness watches
- `make gate` exit status and last 40 lines; `kte_tests` runs via ctest too (`ctest --test-dir build`)
- clang-tidy warning count on the diff (when clang-tidy is available)
- for GUI bugs: launch `./build/kge` and report what was seen; a green test suite is not proof

## Rules (do not re-litigate)
- Release: bump CMakeLists version, checkpoint, `./make-release.sh`, report the SHA256 it prints; a GitHub push failure at the same commit is fine; then update `/opt/homebrew/Library/Taps/kisom/homebrew-tap`.
- All text mutations must go through the PieceTable API (`insert_text`, `delete_text`) so undo and swap recording work.
- Read lines with `Nrows()` and `GetLineString()` (a copy, cheap on any buffer) or `GetLineView()` (zero-copy when the line lies within one piece; a line straddling an edit materializes the whole buffer); `GetLineView()` and `ContentView()` are valid only until the next buffer modification.
- C++20, compiled with `-Wall -Wextra -Werror -pedantic`; Clang uses `-stdlib=libc++`.
- Naming: PascalCase for classes/methods, snake_case for variables, trailing underscore for private members (e.g. `pieces_`); indentation is tabs.
- Fallible ops use `bool func(args..., std::string &err)`: clear `err` at start, capture `errno` immediately after syscall failure, use EINTR-safe wrappers from `SyscallWrappers.h` instead of raw syscalls.
- After editing ops, call `ensure_cursor_visible()` to update the viewport.

## Docs that govern
- `docs/ke.md` — canonical keybinding/spec reference, inherited from the predecessor editor `ke`
- `REWRITE.md`, `ROADMAP.md`, `CONFIG.md` — design rationale, roadmap, and config reference
- `README.md` — project overview

## Remote target
None: kte builds and runs locally (or in the Docker/CI Linux image); it has no remote deployment target.

## Architecture

Three-layer design with strict frontend independence:

```
Frontend Layer (Terminal / ImGui / Test)
  InputHandler.h, Renderer.h, Frontend.h interfaces
        ↓
Command Layer
  CommandId enum → CommandRegistry → handler functions in Command.cc
        ↓
Core Model Layer
  Editor → Buffer → PieceTable
  UndoSystem (tree-based, records at PieceTable level)
  SwapManager (crash recovery journal per buffer)
```

### Core Components

- **PieceTable** (`PieceTable.h/.cc`) - Text storage. Lazy materialization; most ops work on the piece list directly. Line index and materialization caches must be invalidated on content changes.
- **Buffer** (`Buffer.h/.cc`) - Wraps PieceTable.
- **Editor** (`Editor.h/.cc`) - Top-level state container. Primarily getters/setters; editing logic lives in commands.
- **Command** (`Command.h/.cc`) - 120+ editing commands. This is the main place to add new editing operations. Register via `CommandRegistry::Register()` in `InstallDefaultCommands()`.
- **UndoSystem/UndoTree/UndoNode** - Tree-based undo with branching. Group related ops with `buf.Undo()->BeginGroup()` / `EndGroup()`.
- **Swap** (`Swap.h/.cc`) - Append-only crash recovery journal. Uses circuit breaker pattern for resilience. Files in `~/.local/state/kte/`.
- **Syntax highlighting** (`syntax/`) - Pluggable per-language highlighters registered in `HighlighterRegistry`. Per-line caching with buffer version tracking.

### Frontend Implementations

Each frontend implements three interfaces (`Frontend.h`, `InputHandler.h`, `Renderer.h`):
- **Terminal**: ncurses-based (always built)
- **ImGui**: SDL2+OpenGL+Freetype (built with `-DBUILD_GUI=ON`)
- **Test**: Programmatic frontend for testing (always built, no UI deps)

### Key CMake Options

| Flag | Default | Purpose |
|------|---------|---------|
| `BUILD_GUI` | ON | Build `kge` (ImGui GUI) |
| `BUILD_TESTS` | ON | Build test suite |
| `ENABLE_ASAN` | OFF | AddressSanitizer |
| `KTE_STATIC_LINK` | OFF | Static linking (Linux only) |
| `KTE_ENABLE_TREESITTER` | OFF | Tree-sitter syntax highlighting |
| `KTE_USE_PCRE2` | ON | Regex search/replace via PCRE2 when `libpcre2-8` is found (std::regex otherwise; see `RegexEngine.h`) |

## Testing

Tests live in `tests/test_*.cc`, run by the single `kte_tests` binary (also registered with ctest as `add_test(NAME kte_tests COMMAND kte_tests)`); `./build/kte_tests Swap_ Undo_` runs only the tests whose names contain one of the arguments. Tests use a minimal custom framework in `tests/Test.h` with `TEST()`, `ASSERT_EQ()`, `ASSERT_TRUE()`, `EXPECT_TRUE()` macros. Use `TestFrontend`/`TestInputHandler`/`TestRenderer` for integration tests that exercise the full Editor+Buffer+Command stack without UI dependencies.

Key test files by area:
- PieceTable: `test_piece_table.cc`
- Buffer I/O: `test_buffer_io.cc`
- Commands: `test_command_semantics.cc`
- Search/replace: `test_search_replace_flow.cc`
- Undo: `test_undo.cc`
- Swap (crash recovery): `test_swap_*.cc` (7 files)
- Reflow: `test_reflow_paragraph.cc`, `test_reflow_indented_bullets.cc`
- Integration: `test_daily_workflows.cc`

## Important Caveats

- `GetLineView()` and `ContentView()` return a `string_view` valid only until the next buffer modification; views of different lines need not be contiguous.
- Buffers, PieceTable and HighlighterEngine are used from the main thread only (the swap writer thread receives copies of the bytes).
- All source files are in the project root (no `src/` directory); tests are in `tests/`; syntax highlighters in `syntax/`; themes in `themes/`; embedded fonts in `fonts/`.
- External deps: `ext/imgui/` (Dear ImGui), `ext/tomlplusplus/` (TOML parser).
- GUI config: `~/.config/kte/kge.toml` (TOML preferred over legacy INI).
- ErrorHandler: centralized logging to `~/.local/state/kte/error.log` with severity levels (Info/Warning/Error/Critical).
