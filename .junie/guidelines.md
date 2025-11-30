# Project Guidelines

kte is Kyle's Text Editor — a simple, fast text editor written in C++17. It
replaces the earlier C implementation, ke (see the ke manual in `ke.md`). The
design draws inspiration from Antirez' kilo, with keybindings rooted in the
WordStar/VDE family and emacs. The spiritual parent is `mg(1)`.

These guidelines summarize the goals, interfaces, key operations, and current
development practices for kte.

Style note: all code should be formatted with the current CLion C++ style.

## Goals

- Keep the core small, fast, and understandable.
- Provide an ncurses-based terminal-first editing experience, with an optional ImGui GUI.
- Preserve familiar keybindings from ke while modernizing the internals.
- Favor simple data structures (e.g., gap buffer) and incremental evolution.

## Interfaces

- Command-line interface: the primary interface today.
- GUI: planned ImGui-based interface.

## Build and Run

Prerequisites: a C++17 compiler, CMake, and ncurses development headers/libs.

- macOS (Homebrew): `brew install ncurses`
- Debian/Ubuntu: `sudo apt-get install libncurses5-dev libncursesw5-dev`

- Configure and build (example):
    - `cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug`
    - `cmake --build cmake-build-debug`
- Run:
    - `./cmake-build-debug/kte [files]`

Project entry point: `main.cpp`

## Core Components (current codebase)

- Buffer: editing model and file I/O (`Buffer.h/.cpp`).
- GapBuffer: editable in-memory text representation (`GapBuffer.h/.cpp`).
- PieceTable: experimental/alternative representation (`PieceTable.h/.cpp`).
- InputHandler: interface for handling text input (`InputHandler.h/`), along
  with `TerminalInputHandler` (ncurses-based) and `GUIInputHandler`.
- Renderer: interface for rendering text (`Renderer.h`), along with
  `TerminalRenderer` (ncurses-based) and `GUIRenderer`.
- Editor: top-level editor state (`Editor.h/.cpp`).
- Command: command model (`Command.h/.cpp`).
- General purpose editor functionality (`Editing.h/.cpp`)

## Keybindings (inherited from ke)

kte aims to maintain ke’s command model while internals evolve. See `ke.md` for
the full reference. Highlights:

- K-command prefix: `C-k` enters k-command mode; exit with `ESC` or `C-g`.
- Save/Exit: `C-k s` (save), `C-k x` or `C-k C-x` (save and exit), `C-k q` (quit
  with confirm), `C-k C-q` (quit immediately).
- Editing: `C-k d` (kill to EOL), `C-k C-d` (kill line), `C-k BACKSPACE` (kill
  to BOL), `C-w` (kill region), `C-y` (yank), `C-u` (universal argument).
- Navigation/Search: `C-s` (incremental find), `C-r` (regex search), `ESC f/b`
  (word next/prev), `ESC BACKSPACE` (delete previous word).
- Buffers/Files: `C-k e` (open), `C-k b`/`C-k p` (switch), `C-k c` (close),
  `C-k C-r` (reload).
- Misc: `C-l` (refresh), `C-g` (cancel), `C-k m` (run make), `C-k g` (goto line).

Known behavior from ke retained for now:

- Incremental search navigates results with arrow keys; search restarts from
  the top on each invocation (known bug to be revisited).

## Contributing/Development Notes

- C++ standard: C++17.
- Style: match existing file formatting and minimal-comment style.
- Keep dependencies minimal; ImGui integration will be isolated behind a GUI
  module.
- Prefer small, focused changes that preserve ke’s UX unless explicitly changing
  behavior.

## References

- Previous editor manual: `ke.md` (canonical keybinding/spec reference for now).
- Inspiration: kilo, WordStar/VDE, emacs, `mg(1)`.

