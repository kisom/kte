# Project Guidelines

kte is Kyle's Text Editor — a simple, fast text editor written in C++17.
It
replaces the earlier C implementation, ke (see the ke manual in
`docs/ke.md`). The
design draws inspiration from Antirez' kilo, with keybindings rooted in
the
WordStar/VDE family and emacs. The spiritual parent is `mg(1)`.

These guidelines summarize the goals, interfaces, key operations, and
current
development practices for kte.

## Goals

- Keep the core small, fast, and understandable.
- Provide an ncurses-based terminal-first editing experience, with an
  additional ImGui GUI.
- Preserve familiar keybindings from ke while modernizing the internals.
- Favor simple data structures (e.g., piece table) and incremental
  evolution.

Project entry point: `main.cpp`

## Core Components (current codebase)

- Buffer: editing model and file I/O (`Buffer.h/.cpp`).
- PieceTable: editable in-memory text representation (
  `PieceTable.h/.cpp`).
- InputHandler: interface for handling text input (`InputHandler.h/`),
  along
  with `TerminalInputHandler` (ncurses-based) and `GUIInputHandler`.
- Renderer: interface for rendering text (`Renderer.h`), along with
  `TerminalRenderer` (ncurses-based) and `GUIRenderer`.
- Editor: top-level editor state (`Editor.h/.cpp`).
- Command: command model (`Command.h/.cpp`).
- General purpose editor functionality (`Editing.h/.cpp`)

## Keybindings (inherited from ke)

The file `docs/ke.md` contains the canonical reference for keybindings.

## Contributing/Development Notes

- C++ standard: C++17.
- Keep dependencies minimal.
- Prefer small, focused changes that preserve ke’s UX unless explicitly
  changing
  behavior.

## References

- Previous editor manual: `ke.md` (canonical keybinding/spec reference
  for now).
- Inspiration: kilo, WordStar/VDE, emacs, `mg(1)`.

