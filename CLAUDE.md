# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**kte** (Kyle's Text Editor) is a C++20 text editor with a terminal-first design (ncurses) and optional GUI frontends (ImGui via SDL2/OpenGL/Freetype, or Qt6). It uses a WordStar/VDE-style command model. The terminal editor is `kte`; the GUI editor is `kge`.

## Build Commands

```bash
# Configure (from project root, build dir is "build")
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=ON -DBUILD_TESTS=ON

# Build everything
cmake --build build

# Build specific targets
cmake --build build --target kte        # terminal editor
cmake --build build --target kge        # GUI editor (requires -DBUILD_GUI=ON)
cmake --build build --target kte_tests  # test suite

# Run all tests
cmake --build build --target kte_tests && ./build/kte_tests
```

There is no single-test runner; the test binary runs all tests. Tests use a minimal custom framework in `tests/Test.h` with `TEST()`, `ASSERT_EQ()`, `ASSERT_TRUE()`, `EXPECT_TRUE()` macros.

### Key CMake Options

| Flag | Default | Purpose |
|------|---------|---------|
| `BUILD_GUI` | ON | Build `kge` (ImGui GUI) |
| `KTE_USE_QT` | OFF | Use Qt6 instead of ImGui for GUI |
| `BUILD_TESTS` | ON | Build test suite |
| `ENABLE_ASAN` | OFF | AddressSanitizer |
| `KTE_STATIC_LINK` | OFF | Static linking (Linux only) |
| `KTE_ENABLE_TREESITTER` | OFF | Tree-sitter syntax highlighting |

### Nix

`flake.nix` provides devshells: `default` (ImGui+debug tools), `terminal`, `qt`.

### Docker (cross-platform Linux testing)

```bash
docker build -t kte-linux . && docker run --rm -v "$(pwd):/kte" kte-linux
```

## Architecture

Three-layer design with strict frontend independence:

```
Frontend Layer (Terminal / ImGui / Qt / Test)
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
- **Buffer** (`Buffer.h/.cc`) - Wraps PieceTable. Prefer `GetLineView(row)` (zero-copy) or `GetLineString(row)` over `Rows()` (legacy, materializes all lines). All text mutations must go through PieceTable API (`insert_text`, `delete_text`) to ensure undo and swap recording work.
- **Editor** (`Editor.h/.cc`) - Top-level state container. Primarily getters/setters; editing logic lives in commands.
- **Command** (`Command.h/.cc`) - 120+ editing commands. This is the main place to add new editing operations. Register via `CommandRegistry::Register()` in `InstallDefaultCommands()`.
- **UndoSystem/UndoTree/UndoNode** - Tree-based undo with branching. Group related ops with `buf.Undo()->BeginGroup()` / `EndGroup()`.
- **Swap** (`Swap.h/.cc`) - Append-only crash recovery journal. Uses circuit breaker pattern for resilience. Files in `~/.local/state/kte/`.
- **Syntax highlighting** (`syntax/`) - Pluggable per-language highlighters registered in `HighlighterRegistry`. Per-line caching with buffer version tracking.

### Frontend Implementations

Each frontend implements three interfaces (`Frontend.h`, `InputHandler.h`, `Renderer.h`):
- **Terminal**: ncurses-based (always built)
- **ImGui**: SDL2+OpenGL+Freetype (built with `-DBUILD_GUI=ON`)
- **Qt**: Qt6 (built with `-DBUILD_GUI=ON -DKTE_USE_QT=ON`)
- **Test**: Programmatic frontend for testing (always built, no UI deps)

## Code Style

- **C++20**, compiled with `-Wall -Wextra -Werror -pedantic`
- **Clang** uses `-stdlib=libc++`
- **Naming**: PascalCase for classes/methods, snake_case for variables, trailing underscore for private members (e.g., `pieces_`)
- **Indentation**: Tabs
- **Error handling**: Fallible ops use `bool func(args..., std::string &err)` pattern. Always clear `err` at start, capture `errno` immediately after syscall failure. Use EINTR-safe wrappers from `SyscallWrappers.h` instead of raw syscalls.
- **ErrorHandler**: Centralized logging to `~/.local/state/kte/error.log` with severity levels (Info/Warning/Error/Critical).

## Testing

Tests live in `tests/test_*.cc`. Use `TestFrontend`/`TestInputHandler`/`TestRenderer` for integration tests that exercise the full Editor+Buffer+Command stack without UI dependencies.

Key test files by area:
- PieceTable: `test_piece_table.cc`
- Buffer I/O: `test_buffer_io.cc`
- Commands: `test_command_semantics.cc`
- Search/replace: `test_search.cc`, `test_search_replace_flow.cc`
- Undo: `test_undo.cc`
- Swap (crash recovery): `test_swap_*.cc` (7 files)
- Reflow: `test_reflow_paragraph.cc`, `test_reflow_indented_bullets.cc`
- Integration: `test_daily_workflows.cc`

## Important Caveats

- `Buffer::Rows()` is legacy; use `GetLineView()` / `GetLineString()` in new code
- `GetLineView()` returns a `string_view` valid only until next buffer modification
- After editing ops, call `ensure_cursor_visible()` to update viewport
- All source files are in the project root (no `src/` directory); tests are in `tests/`; syntax highlighters in `syntax/`; themes in `themes/`; embedded fonts in `fonts/`
- External deps: `ext/imgui/` (Dear ImGui), `ext/tomlplusplus/` (TOML parser)
- GUI config: `~/.config/kte/kge.toml` (TOML preferred over legacy INI)
