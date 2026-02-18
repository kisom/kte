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
7. [Error Handling Conventions](#error-handling-conventions)
8. [Common Tasks](#common-tasks)

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

## Error Handling Conventions

kte uses standardized error handling patterns to ensure consistency and
reliability across the codebase. This section documents when to use each
pattern and how to integrate with the centralized error handling system.

### Error Propagation Patterns

kte uses three standard patterns for error handling:

#### 1. `bool` + `std::string &err` (I/O and Fallible Operations)

**When to use**: Operations that can fail and need detailed error
messages
(file I/O, network operations, parsing, resource allocation).

**Pattern**:

```cpp
bool OperationName(args..., std::string &err) {
    err.clear();
    
    // Attempt operation
    if (/* operation failed */) {
        err = "Detailed error message with context";
        ErrorHandler::Instance().Error("ComponentName", err, "optional_context");
        return false;
    }
    
    return true;
}
```

**Examples**:

- `Buffer::OpenFromFile(const std::string &path, std::string &err)`
- `Buffer::Save(std::string &err)`
-

`SwapManager::ReplayFile(Buffer &buf, const std::string &path, std::string &err)`

**Guidelines**:

- Always clear `err` at the start of the function
- Provide actionable error messages with context (file paths, operation
  details)
- Call `ErrorHandler::Instance().Error()` for centralized logging
- Return `false` on failure, `true` on success
- Capture `errno` immediately after syscall failures: `int saved_errno =
  errno;`
- Use `std::strerror(saved_errno)` for syscall error messages

#### 2. `void` (Infallible State Changes)

**When to use**: Operations that modify internal state and cannot fail
(setters, cursor movement, flag toggles).

**Pattern**:

```cpp
void SetProperty(Type value) {
    property_ = value;
    // Update related state if needed
}
```

**Examples**:

- `Buffer::SetCursor(std::size_t x, std::size_t y)`
- `Buffer::SetDirty(bool d)`
- `Editor::SetStatus(const std::string &msg)`

**Guidelines**:

- Use for simple state changes that cannot fail
- No error reporting needed
- Keep operations atomic and side-effect free when possible

#### 3. `bool` without error parameter (Control Flow)

**When to use**: Operations where success/failure is sufficient
information
and detailed error messages aren't needed (validation checks, control
flow
decisions).

**Pattern**:

```cpp
bool CheckCondition() const {
    return condition_is_met;
}
```

**Examples**:

- `Editor::SwitchTo(std::size_t index)` - returns false if index invalid
- `Editor::CloseBuffer(std::size_t index)` - returns false if can't
  close

**Guidelines**:

- Use when the caller only needs to know success/failure
- Typically for validation or control flow decisions
- Don't use for operations that need error diagnostics

### ErrorHandler Integration

All error-prone operations should report errors to the centralized
`ErrorHandler` for logging and UI integration.

**Severity Levels**:

```cpp
ErrorHandler::Instance().Info("Component", "message", "context");     // Informational
ErrorHandler::Instance().Warning("Component", "message", "context");  // Warning
ErrorHandler::Instance().Error("Component", "message", "context");    // Error
ErrorHandler::Instance().Critical("Component", "message", "context"); // Critical
```

**When to use each severity**:

- **Info**: Non-error events (file saved, operation completed)
- **Warning**: Recoverable issues (external file modification detected)
- **Error**: Operation failures (file I/O errors, allocation failures)
- **Critical**: Fatal errors (unhandled exceptions, data corruption)

**Component names**: Use the class name ("Buffer", "SwapManager",
"Editor", "main")

**Context**: Optional string providing additional context (filename,
buffer
name, operation details)

### Error Handling in Different Contexts

#### File I/O Operations

```cpp
bool Buffer::Save(std::string &err) const {
    if (!is_file_backed_ || filename_.empty()) {
        err = "Buffer is not file-backed; use SaveAs()";
        return false;
    }
    
    const std::size_t sz = content_.Size();
    const char *data = sz ? content_.Data() : nullptr;
    
    if (!atomic_write_file(filename_, data ? data : "", sz, err)) {
        ErrorHandler::Instance().Error("Buffer", err, filename_);
        return false;
    }
    
    return true;
}
```

#### Syscall Error Handling with EINTR-Safe Wrappers

kte provides EINTR-safe syscall wrappers in `SyscallWrappers.h` that
automatically retry on `EINTR`. **Always use these wrappers instead of
direct syscalls.**

```cpp
#include "SyscallWrappers.h"

bool open_file(const std::string &path, std::string &err) {
    int fd = kte::syscall::Open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        int saved_errno = errno;  // Capture immediately!
        err = "Failed to open file '" + path + "': " + std::strerror(saved_errno);
        ErrorHandler::Instance().Error("Component", err, path);
        return false;
    }
    // ... use fd
    kte::syscall::Close(fd);
    return true;
}
```

**Available EINTR-safe wrappers**:

- `kte::syscall::Open(path, flags, mode)` - wraps `open(2)`
- `kte::syscall::Close(fd)` - wraps `close(2)`
- `kte::syscall::Fsync(fd)` - wraps `fsync(2)`
- `kte::syscall::Fstat(fd, buf)` - wraps `fstat(2)`
- `kte::syscall::Fchmod(fd, mode)` - wraps `fchmod(2)`
- `kte::syscall::Mkstemp(template)` - wraps `mkstemp(3)`

**Note**: `rename(2)` and `unlink(2)` are NOT wrapped because they
operate on filesystem metadata atomically and don't need EINTR retry.

#### Background Thread Errors

```cpp
void background_worker() {
    try {
        // ... work
    } catch (const std::exception &e) {
        std::string msg = std::string("Exception in worker: ") + e.what();
        ErrorHandler::Instance().Error("WorkerThread", msg);
    } catch (...) {
        ErrorHandler::Instance().Error("WorkerThread", "Unknown exception");
    }
}
```

#### Top-Level Exception Handling

```cpp
int main(int argc, char *argv[]) {
    try {
        // ... main logic
        return 0;
    } catch (const std::exception &e) {
        std::string msg = std::string("Unhandled exception: ") + e.what();
        ErrorHandler::Instance().Critical("main", msg);
        std::cerr << "FATAL ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        ErrorHandler::Instance().Critical("main", "Unknown exception");
        std::cerr << "FATAL ERROR: Unknown exception\n";
        return 1;
    }
}
```

### Error Handling Anti-Patterns

**❌ Don't**: Silently ignore errors

```cpp
// BAD
void process() {
    std::string err;
    if (!operation(err)) {
        // Error ignored!
    }
}
```

**✅ Do**: Always handle or propagate errors

```cpp
// GOOD
bool process(std::string &err) {
    if (!operation(err)) {
        // err already set by operation()
        return false;
    }
    return true;
}
```

**❌ Don't**: Use generic error messages

```cpp
// BAD
err = "Operation failed";
```

**✅ Do**: Provide specific, actionable error messages

```cpp
// GOOD
err = "Failed to open file '" + path + "': " + std::strerror(errno);
```

**❌ Don't**: Forget to capture errno

```cpp
// BAD
if (::write(fd, data, len) < 0) {
    // errno might be overwritten by other calls!
    err = std::strerror(errno);
}
```

**✅ Do**: Capture errno immediately

```cpp
// GOOD
if (::write(fd, data, len) < 0) {
    int saved_errno = errno;
    err = std::strerror(saved_errno);
}
```

### Error Log Location

All errors are automatically logged to:

```
~/.local/state/kte/error.log
```

Log format:

```
[2026-02-17 20:12:34.567] [ERROR] SwapManager (buffer.txt): Failed to write swap record
[2026-02-17 20:12:35.123] [CRITICAL] main: Unhandled exception: out of memory
```

### Migration Guide

When updating existing code to follow these conventions:

1. **Identify error-prone operations** - File I/O, syscalls, allocations
2. **Add `std::string &err` parameter** if not present
3. **Add ErrorHandler calls** at all error sites
4. **Capture errno** for syscall failures
5. **Update callers** to handle the error parameter
6. **Write tests** that verify error handling

### Error Recovery Mechanisms

kte implements automatic error recovery for transient failures using
retry logic and circuit breaker patterns.

#### Transient Error Classification

Transient errors are temporary failures that may succeed on retry:

```cpp
#include "ErrorRecovery.h"

bool IsTransientError(int err);  // Returns true for EAGAIN, EWOULDBLOCK, EBUSY, EIO, ETIMEDOUT, ENOSPC, EDQUOT
```

**Transient errors**:

- `EAGAIN` / `EWOULDBLOCK` - Resource temporarily unavailable
- `EBUSY` - Device or resource busy
- `EIO` - I/O error (may be transient on network filesystems)
- `ETIMEDOUT` - Operation timed out
- `ENOSPC` - No space left on device (may become available)
- `EDQUOT` - Disk quota exceeded (may become available)

**Permanent errors** (don't retry):

- `ENOENT` - File not found
- `EACCES` - Permission denied
- `EINVAL` - Invalid argument
- `ENOTDIR` - Not a directory

#### Retry Policies

Three predefined retry policies are available:

```cpp
// Default: 3 attempts, 100ms initial delay, 2x backoff, 5s max delay
RetryPolicy::Default()

// Aggressive: 5 attempts, 50ms initial delay, 1.5x backoff, 2s max delay
// Use for critical operations (swap files, file saves)
RetryPolicy::Aggressive()

// Conservative: 2 attempts, 200ms initial delay, 2.5x backoff, 10s max delay
// Use for non-critical operations
RetryPolicy::Conservative()
```

#### Using RetryOnTransientError

Wrap syscalls with automatic retry on transient errors:

```cpp
#include "ErrorRecovery.h"
#include "SyscallWrappers.h"

bool save_file(const std::string &path, std::string &err) {
    int fd = -1;
    auto open_fn = [&]() -> bool {
        fd = kte::syscall::Open(path.c_str(), O_CREAT | O_WRONLY, 0644);
        return fd >= 0;
    };
    
    if (!kte::RetryOnTransientError(open_fn, kte::RetryPolicy::Aggressive(), err)) {
        if (fd < 0) {
            int saved_errno = errno;
            err = "Failed to open file '" + path + "': " + std::strerror(saved_errno) + err;
        }
        return false;
    }
    
    // ... use fd
    kte::syscall::Close(fd);
    return true;
}
```

**Key points**:

- Lambda must return `bool` (true = success, false = failure)
- Lambda must set `errno` on failure for transient error detection
- Use EINTR-safe syscall wrappers (`kte::syscall::*`) inside lambdas
- Capture errno immediately after failure
- Append retry info to error message (automatically added by
  RetryOnTransientError)

#### Circuit Breaker Pattern

The circuit breaker prevents repeated attempts to failing operations,
enabling graceful degradation.

**States**:

- **Closed** (normal): All requests allowed
- **Open** (failing): Requests rejected immediately, operation disabled
- **HalfOpen** (testing): Limited requests allowed to test recovery

**Configuration** (SwapManager example):

```cpp
CircuitBreaker::Config cfg;
cfg.failure_threshold = 5;      // Open after 5 failures
cfg.timeout = std::chrono::seconds(30);  // Try recovery after 30s
cfg.success_threshold = 2;      // Close after 2 successes in HalfOpen
cfg.window = std::chrono::seconds(60);   // Count failures in 60s window

CircuitBreaker breaker(cfg);
```

**Usage**:

```cpp
// Check before operation
if (!breaker.AllowRequest()) {
    // Circuit is open - graceful degradation
    log_warning("Operation disabled due to repeated failures");
    return;  // Skip operation
}

// Perform operation
if (operation_succeeds()) {
    breaker.RecordSuccess();
} else {
    breaker.RecordFailure();
}
```

**SwapManager Integration**:

The SwapManager uses a circuit breaker to handle repeated swap file
failures:

1. After 5 swap write failures in 60 seconds, circuit opens
2. Swap recording is disabled (graceful degradation)
3. Warning logged once per 60 seconds to avoid spam
4. After 30 seconds, circuit enters HalfOpen state
5. If 2 consecutive operations succeed, circuit closes and swap
   recording resumes

This ensures the editor remains functional even when swap files are
unavailable (disk full, quota exceeded, filesystem errors).

#### Graceful Degradation Strategies

When operations fail repeatedly:

1. **Disable non-critical features** - Swap recording can be disabled
   without affecting editing
2. **Log warnings** - Inform user of degraded operation via ErrorHandler
3. **Rate-limit warnings** - Avoid log spam (e.g., once per 60 seconds)
4. **Automatic recovery** - Circuit breaker automatically tests recovery
5. **Preserve core functionality** - Editor remains usable without swap
   files

**Example** (from SwapManager):

```cpp
if (circuit_open) {
    // Graceful degradation: skip swap write
    static std::atomic<std::uint64_t> last_warning_ns{0};
    const std::uint64_t now = now_ns();
    if (now - last_warning_ns.load() > 60000000000ULL) {
        last_warning_ns.store(now);
        ErrorHandler::Instance().Warning("SwapManager", 
            "Swap operations temporarily disabled due to repeated failures",
            buffer_name);
    }
    return;  // Skip operation, editor continues normally
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
