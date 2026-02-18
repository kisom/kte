# kte Developer Guide

Welcome to kte development! This guide will help you understand the
codebase, make changes, and contribute effectively.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Core Components](#core-components)
3. [Code Organization](#code-organization)
4. [Building and Testing](#building-and-testing)
5. [Making Changes](#making-changes)
6. [Code Style](#code-style)
7. [Common Tasks](#common-tasks)

## Architecture Overview

kte follows a clean separation of concerns with three main layers:

```
┌─────────────────────────────────────────┐
│  Frontend Layer (Terminal/ImGui/Qt)     │
│  - TerminalFrontend / ImGuiFrontend     │
│  - InputHandler + Renderer interfaces   │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│  Command Layer                          │
│  - Command registry and execution       │
│  - All editing operations               │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│  Core Model Layer                       │
│  - Editor (top-level state)             │
│  - Buffer (document model)              │
│  - PieceTable (text storage)            │
│  - UndoSystem (undo/redo)               │
│  - SwapManager (crash recovery)         │
└─────────────────────────────────────────┘
```

### Design Principles

- **Frontend Independence**: Core editing logic is independent of UI.
  Frontends implement `Frontend`, `InputHandler`, and `Renderer`
  interfaces.
- **Command Pattern**: All editing operations go through the command
  system, enabling consistent undo/redo and testing.
- **Piece Table**: Efficient text storage using a piece table data
  structure that avoids copying large buffers.
- **Lazy Materialization**: Text is materialized on-demand to minimize
  memory allocations.

## Core Components

### Editor (`Editor.h/.cc`)

The top-level editor state container. Manages:

- Multiple buffers
- Editor modes (normal, k-command prefix, prompts)
- Kill ring (clipboard history)
- Universal argument state
- Search state
- Status messages
- Swap file management

**Key Insight**: Editor is primarily a state holder with many
getter/setter pairs. It doesn't contain editing logic - that's in
commands.

### Buffer (`Buffer.h/.cc`)

Represents an open document. Manages:

- File I/O (open, save, external modification detection)
- Cursor position and viewport offsets
- Mark (selection start point)
- Visual line mode state
- Syntax highlighting integration
- Undo system integration
- Swap recording integration

**Key Insight**: Buffer wraps a PieceTable and provides a higher-level
interface. The nested `Buffer::Line` class is a legacy wrapper that has
been largely phased out in favor of direct PieceTable operations.

**Line Access APIs**: Buffer provides three ways to access line content:

- `GetLineView(row)` - Zero-copy `string_view` (fastest, 11x faster than
  Rows())
- `GetLineString(row)` - Returns `std::string` copy (1.7x faster than
  Rows())
- `Rows()` - Materializes all lines into cache (legacy, avoid in new
  code)

See `docs/BENCHMARKS.md` for detailed performance analysis and usage
guidance.

### PieceTable (`PieceTable.h/.cc`)

The core text storage data structure. Provides:

- Efficient insert/delete operations without copying entire buffer
- Line-based queries (line count, get line, line ranges)
- Position conversion (byte offset ↔ line/column)
- Substring extraction
- Search functionality
- Automatic consolidation to prevent piece fragmentation

**Key Insight**: PieceTable uses lazy materialization - the full text is
only assembled when `Data()` is called. Most operations work directly on
the piece list.

### UndoSystem (`UndoSystem.h/.cc`, `UndoTree.h/.cc`, `UndoNode.h/.cc`)

Implements undo/redo with a tree structure supporting:

- Linear undo/redo
- Branching history (future enhancement)
- Checkpointing and compaction
- Memory-efficient node pooling

**Key Insight**: The undo system records operations at the PieceTable
level, not at the command level.

### Command System (`Command.h/.cc`)

All editing operations are implemented as commands:

- File operations (save, open, close)
- Navigation (move cursor, page up/down, word movement)
- Editing (insert, delete, kill, yank)
- Search and replace
- Buffer management
- Configuration (syntax, theme, font)

**Key Insight**: `Command.cc` is currently a monolithic 5000-line file.
This is the biggest maintainability challenge in the codebase.

### Frontend Abstraction

Three interfaces define the frontend contract:

- **Frontend** (`Frontend.h`): Top-level lifecycle (Init/Step/Shutdown)
- **InputHandler** (`InputHandler.h`): Converts UI events to commands
- **Renderer** (`Renderer.h`): Draws the editor state

Implementations:

- **Terminal**: ncurses-based (`TerminalFrontend`,
  `TerminalInputHandler`, `TerminalRenderer`)
- **ImGui**: Dear ImGui-based (`ImGuiFrontend`, `ImGuiInputHandler`,
  `ImGuiRenderer`)
- **Qt**: Qt-based (`QtFrontend`, `QtInputHandler`, `QtRenderer`)
- **Test**: Programmatic testing (`TestFrontend`, `TestInputHandler`,
  `TestRenderer`)

## Code Organization

### Directory Structure

```
kte/
├── *.h, *.cc           # Core implementation (root level)
├── main.cc             # Entry point
├── docs/               # Documentation
│   ├── ke.md          # Original ke editor reference (keybindings)
│   ├── swap.md        # Swap file design
│   ├── syntax.md      # Syntax highlighting
│   ├── themes.md      # Theme system
│   └── plans/         # Design documents
├── tests/              # Test suite
│   ├── Test.h         # Minimal test framework
│   ├── TestRunner.cc  # Test runner
│   └── test_*.cc      # Individual test files
├── syntax/             # Syntax highlighting engines
├── fonts/              # Embedded fonts for GUI
├── themes/             # Color themes
└── ext/                # External dependencies (imgui)
```

### File Naming Conventions

- Headers: `ComponentName.h`
- Implementation: `ComponentName.cc`
- Tests: `test_feature_name.cc`

### Key Files by Size

Large files that may need attention:

- `Command.cc` (4995 lines) - **Needs refactoring**: Consider splitting
  into logical groups
- `Swap.cc` (1300 lines) - Crash recovery system (migrated to direct
  PieceTable operations)
- `QtFrontend.cc` (985 lines) - Qt integration
- `ImGuiRenderer.cc` (930 lines) - ImGui rendering
- `PieceTable.cc` (800 lines) - Core data structure
- `Buffer.cc` (763 lines) - Document model

## Building and Testing

### Build System

kte uses CMake with multiple build profiles:

```bash
# Debug build (terminal only)
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug

# Release build with GUI
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON
cmake --build cmake-build-release

# Build specific target
cmake --build cmake-build-debug --target kte_tests
```

### CMake Targets

- `kte` - Terminal editor executable
- `kge` - GUI editor executable (when `BUILD_GUI=ON`)
- `kte_tests` - Test suite
- `imgui` - Dear ImGui library (when `BUILD_GUI=ON`)

### Running Tests

```bash
# Build and run all tests
cmake --build cmake-build-debug --target kte_tests && ./cmake-build-debug/kte_tests

# Run tests with verbose output
./cmake-build-debug/kte_tests
```

### Test Organization

The test suite uses a minimal custom framework (`Test.h`):

```cpp
TEST(TestName) {
    // Test body
    ASSERT_EQ(actual, expected);
    ASSERT_TRUE(condition);
    EXPECT_TRUE(condition);  // Non-fatal
}
```

Test files by category:

- **Core Data Structures**:
    - `test_piece_table.cc` - PieceTable operations, line indexing,
      random edits
    - `test_buffer_rows.cc` - Buffer row operations
    - `test_buffer_io.cc` - File I/O (open, save, SaveAs)

- **Editing Operations**:
    - `test_command_semantics.cc` - Command execution
    - `test_kkeymap.cc` - Keybinding system
    - `test_visual_line_mode.cc` - Visual line selection

- **Search and Replace**:
    - `test_search.cc` - Search functionality
    - `test_search_replace_flow.cc` - Interactive search/replace

- **Text Reflow**:
    - `test_reflow_paragraph.cc` - Paragraph reformatting
    - `test_reflow_indented_bullets.cc` - Indented list handling

- **Undo System**:
    - `test_undo.cc` - Undo/redo operations

- **Swap Files** (Crash Recovery):
    - `test_swap_recorder.cc` - Recording operations
    - `test_swap_writer.cc` - Writing swap files
    - `test_swap_replay.cc` - Replaying operations
    - `test_swap_recovery_prompt.cc` - Recovery UI
    - `test_swap_cleanup.cc` - Cleanup logic
    - `test_swap_git_editor.cc` - Git editor integration

- **Performance and Migration**:
    - `test_benchmarks.cc` - Performance benchmarks for core operations
    - `test_migration_coverage.cc` - Buffer::Line migration validation

- **Integration Tests**:
    - `test_daily_workflows.cc` - Real-world editing scenarios
    - `test_daily_driver_harness.cc` - Workflow test infrastructure

**Total**: 98 tests across 22 test files. See `docs/BENCHMARKS.md` for
performance benchmark results.

### Docker/Podman for Linux Builds

A minimal `Dockerfile` is provided for **testing Linux builds** without
requiring a native Linux system. The Dockerfile creates a build
environment container with all necessary dependencies. Your source tree
is mounted into the container at runtime, allowing you to test
compilation and run tests on Linux.

**Important**: This is intended for testing Linux builds, not for
running
kte locally. The container expects the source tree to be mounted when
run.

This is particularly useful for:

- **macOS/Windows developers** testing Linux compatibility
- **CI/CD pipelines** ensuring cross-platform builds
- **Reproducible builds** with a known Alpine Linux 3.19 environment

#### Prerequisites

Install Docker or Podman:

- **macOS**: `brew install podman` (Docker Desktop also works)
- **Linux**: Use your distribution's package manager
- **Windows**: Docker Desktop or Podman Desktop

If using Podman on macOS, start the VM:

```bash
podman machine init
podman machine start
```

#### Building the Docker Image

The Dockerfile installs all build dependencies including GUI support (
g++ 13.2.1, CMake 3.27.8, ncurses-dev, SDL2, OpenGL/Mesa, Freetype). It
does not copy or build the source code.

From the project root:

```bash
# Build the environment image
docker build -t kte-linux .

# Or with Podman
podman build -t kte-linux .
```

#### Testing Linux Builds

Mount your source tree and run the build + tests:

```bash
# Build and test (default command)
docker run --rm -v "$(pwd):/kte" kte-linux

# Expected output: "98 tests passed, 0 failed"
```

The default command builds both `kte` (terminal) and `kge` (GUI)
executables with full GUI support (`-DBUILD_GUI=ON`) and runs the
complete test suite.

#### Custom Build Commands

```bash
# Open a shell in the build environment
docker run --rm -it -v "$(pwd):/kte" kte-linux /bin/bash

# Then inside the container:
cmake -B build -DBUILD_GUI=ON -DBUILD_TESTS=ON
cmake --build build --target kte      # Terminal version
cmake --build build --target kge      # GUI version
cmake --build build --target kte_tests
./build/kte_tests

# Or run kte directly
./build/kte --help

# Terminal-only build (smaller, faster)
cmake -B build -DBUILD_GUI=OFF -DBUILD_TESTS=ON
cmake --build build --target kte
```

#### Running kte Interactively

To test kte's terminal UI on Linux:

```bash
# Run kte with a file from your host system
docker run --rm -it -v "$(pwd):/kte" kte-linux sh -c "cmake -B build -DBUILD_GUI=OFF && cmake --build build --target kte && ./build/kte README.md"
```

#### CI/CD Integration

Example GitHub Actions workflow:

```yaml
- name: Test Linux Build
  run: |
    docker build -t kte-linux .
    docker run --rm -v "${{ github.workspace }}:/kte" kte-linux
```

#### Troubleshooting

**"Cannot connect to Podman socket"** (macOS):

```bash
podman machine start
```

**"Permission denied"** (Linux):

```bash
# Add your user to the docker group
sudo usermod -aG docker $USER
# Log out and back in
```

**Build fails with ncurses errors**:
The Dockerfile explicitly installs `ncurses-dev` (wide-character
ncurses). If you modify the Dockerfile, ensure this dependency remains.

**"No such file or directory" errors**:
Ensure you're mounting the source tree with `-v "$(pwd):/kte"` when
running the container.

### Writing Tests

When adding new functionality:

1. **Add a test first** - Write a failing test that demonstrates the
   desired behavior
2. **Use descriptive names** - Test names should explain what's being
   validated
3. **Test edge cases** - Empty buffers, EOF, beginning of file, etc.
4. **Use TestFrontend** - For integration tests, use the programmatic
   test frontend

Example test structure:

```cpp
TEST(Feature_Behavior_Scenario) {
    // Setup
    Buffer buf;
    buf.insert_text(0, 0, "test content\n");
    
    // Exercise
    buf.delete_text(0, 5, 4);
    
    // Verify
    ASSERT_EQ(buf.GetLineString(0), std::string("test\n"));
}
```

## Making Changes

### Development Workflow

1. **Understand the change scope**:
    - Pure UI change? → Modify frontend only
    - New editing operation? → Add command in `Command.cc`
    - Core data structure? → Modify `PieceTable` or `Buffer`

2. **Find relevant code**:
    - Use `git grep` or IDE search to find similar functionality
    - Check `Command.cc` for existing command patterns
    - Look at tests to understand expected behavior

3. **Make the change**:
    - Follow existing code style (see below)
    - Add or update tests
    - Update documentation if needed

4. **Test thoroughly**:
    - Run the full test suite
    - Manually test in both terminal and GUI (if applicable)
    - Test edge cases (empty files, large files, EOF, etc.)

### Common Pitfalls

- **Don't modify `Buffer::Rows()` directly** - Use the PieceTable API (
  `insert_text`, `delete_text`, etc.) to ensure undo and swap recording
  work correctly.
- **Prefer efficient line access** - Use `GetLineView()` for read-only
  access (11x faster than `Rows()`), or `GetLineString()` when you need
  a copy. Avoid `Rows()` in new code.
- **Remember to invalidate caches** - If you modify PieceTable
  internals, ensure line index and materialization caches are
  invalidated.
- **Cursor visibility** - After editing operations, call
  `ensure_cursor_visible()` to update viewport offsets.
- **Undo boundaries** - Use `buf.Undo()->BeginGroup()` and `EndGroup()`
  to group related operations.
- **GetLineView() lifetime** - The returned `string_view` is only valid
  until the next buffer modification. Use immediately or copy to
  `std::string`.

## Code Style

kte uses C++20 with these conventions:

### Naming

- **Classes/Structs**: `PascalCase` (e.g., `PieceTable`, `Buffer`)
- **Functions/Methods**: `PascalCase` (e.g., `GetLine`, `Insert`)
- **Variables**: `snake_case` with trailing underscore for members (
  e.g., `total_size_`, `line_index_`)
- **Constants**: `snake_case` or `UPPER_CASE` depending on context
- **Private members**: Trailing underscore (e.g., `pieces_`, `dirty_`)

### Formatting

- **Indentation**: Tabs (width 8 in most files, but follow existing
  style)
- **Braces**: Opening brace on same line for functions, control
  structures
- **Line length**: No strict limit, but keep reasonable (~100-120 chars)
- **Includes**: Group by category (system, external, project) with blank
  lines between

### Comments

- **File headers**: Brief description of the file's purpose
- **Function comments**: Explain non-obvious behavior, not what the code
  obviously does
- **Inline comments**: Explain *why*, not *what*
- **TODO comments**: Use `TODO:` prefix for future work

Example:

```cpp
// Consolidate small pieces to prevent fragmentation.
// This is a heuristic: we only consolidate when piece count exceeds
// a threshold, and we cap the bytes processed per consolidation run.
void maybeConsolidate() {
    if (pieces_.size() < piece_limit_)
        return;
    // ... implementation
}
```

## Common Tasks

### Adding a New Command

1. **Define the command function** in `Command.cc`:

```cpp
bool cmd_my_feature(CommandContext &ctx) {
    Editor &ed = ctx.ed;
    Buffer *buf = ed.CurrentBuffer();
    if (!buf) return false;
    
    // Implement the command
    buf->insert_text(buf->Cury(), buf->Curx(), "text");
    
    return true;
}
```

2. **Register the command** in `InstallDefaultCommands()`:

```cpp
CommandRegistry::Register({
    CommandId::MyFeature,
    "my-feature",
    "Description of what it does",
    cmd_my_feature
});
```

3. **Add keybinding** in the appropriate `InputHandler` (e.g.,
   `TerminalInputHandler.cc`).

4. **Write tests** in `tests/test_command_semantics.cc` or a new test
   file.

### Adding a New Frontend

1. **Implement the three interfaces**:
    - `Frontend` - Lifecycle management
    - `InputHandler` - Event → Command translation
    - `Renderer` - Draw the editor state

2. **Study existing implementations**:
    - `TerminalFrontend` - Simplest, good starting point
    - `ImGuiFrontend` - More complex, shows GUI patterns

3. **Register in `main.cc`** to make it selectable.

### Modifying the PieceTable

The PieceTable is performance-critical. When making changes:

1. **Understand the piece list** - Each piece references a range in
   either `original_` or `add_` buffer
2. **Maintain invariants**:
    - `total_size_` must match sum of piece lengths
    - Line index must be invalidated on content changes
    - Version must increment on mutations
3. **Test thoroughly** - Use `test_piece_table.cc` random edit test as a
   reference model
4. **Profile if needed** - Large file performance is a key goal

### Adding Syntax Highlighting

1. **Create a new highlighter** in `syntax/` directory:
    - Inherit from `HighlighterEngine`
    - Implement `HighlightLine()` method

2. **Register in `HighlighterRegistry`** (
   `syntax/HighlighterRegistry.cc`)

3. **Add file extension mapping** in the registry

4. **Test with sample files** of that language

### Debugging Tips

- **Use the test frontend** - Write a test that reproduces the issue
- **Enable assertions** - Build in Debug mode
- **Check swap files** - Look in `/tmp/kte-swap-*` for recorded
  operations
- **Print debugging** - Use `std::cerr` (stdout is used by ncurses)
- **GDB/LLDB** - Standard debuggers work fine with kte

## Getting Help

- **Read the code** - kte is designed to be understandable; follow the
  data flow
- **Check existing tests** - Tests often show how to use APIs correctly
- **Look at git history** - See how similar features were implemented
- **Read design docs** - Check `docs/plans/` for design rationale

## Future Improvements

Areas where the codebase could be improved:

1. **Split Command.cc** - Break into logical groups (editing,
   navigation, file ops, etc.)
2. **Complete Buffer::Line migration** - A few legacy editing functions
   in Command.cc still use `Buffer::Rows()` directly (see lines 86-90
   comment)
3. **Add more inline documentation** - Especially for complex algorithms
4. **Improve test coverage** - Add more edge case tests (current: 98
   tests)
5. **Performance profiling** - Continue monitoring performance with
   benchmark suite
6. **API documentation** - Consider adding Doxygen-style comments

---

Welcome aboard! Start small, read the code, and don't hesitate to ask
questions.
