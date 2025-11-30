kte — Kyle's Text Editor

Vision
-------
kte will be a small, fast, and understandable text editor with a
terminal‑first UX and an optional ImGui GUI. It modernizes the
original ke editor while preserving its familiar WordStar/VDE‑style
command model and Emacs‑influenced ergonomics. The focus is on
simplicity of design, excellent latency, and pragmatic features you
can learn and keep in your head.

I am experimenting with using Jetbrains Junie to assist in
development, largely as a way to learn the effective use of agentic
coding.

Project Goals
-------------

- Keep the core minimal and readable; favor straightforward data
  structures (gap buffer, piece table) and incremental evolution.
- Round‑trip editing of large files with low latency in a terminal
  environment.
- Preserve ke keybindings and command semantics wherever sensible;
  smooth migration for ke users.
- Provide a clean separation between core model, input, and rendering
  so a GUI can grow independently of the TUI.
- Minimize dependencies; the GUI layer remains optional and isolated.

User Experience (intended)
--------------------------

- Terminal first: instant startup, responsive editing, no surprises
  over SSH.
- Optional GUI: an ImGui‑based window with tabs, menus, and
  palette—sharing the same editor core and command model.
- Discoverable command model: WordStar/VDE style with a `C-k` prefix,
  Emacs‑like incremental search, and context help.
- Sensible defaults with a simple config file for remaps and theme
  selection.
- Respect the file system: no magic project files; autosave and
  crash‑recovery journals are opt‑in and visible.

Core Features (roadmapped)
--------------------------

- Buffers and windows
    - Multiple file buffers; fast switching, closing, and reopening.
    - Split views (horizontal/vertical) in TUI and tiled panels in
      GUI.
- Editing primitives
    - Gap buffer (primary) with an alternative piece table for
      large‑edit scenarios.
    - Kill/yank ring, word/sentence/paragraph motions, and rectangle
      ops.
    - Undo/redo with grouped edits and time‑travel scrubbing.
- Search and replace
    - Incremental search (C-s) and regex search (C-r) with live
      highlighting.
    - Multi‑file grep with a quickfix list; replace with confirm.
- Files and projects
    - Robust encoding/line‑ending detection; safe writes (atomic where
      possible).
    - File tree sidebar (GUI) and quick‑open palette.
    - Lightweight session restore.
- Language niceties (opt‑in, no runtime servers required)
    - Syntax highlighting via fast, table‑driven lexers.
    - Basic indentation rules per language; trailing whitespace/EOF
      newline helpers.
- Extensibility (later)
    - Command palette actions backed by the core command model.
    - Small C++ plugin ABI and a scripting shim for config‑time
      customization.

Interfaces
----------

- CLI: the primary interface. `kte [files]` starts in the terminal,
  adopting your `$TERM` capabilities. Terminal mode is implemented
  using ncurses.
- GUI: an optional ImGui‑based frontend that embeds the same editor
  core.

Architecture (intended)
-----------------------

- Core model
    - Buffer: file I/O, cursor/mark, viewport state, and edit
      operations.
    - GapBuffer: fast in‑memory text structure for typical edits.
    - PieceTable: alternative representation for heavy insert/delete
      workflows.
- Controller layer
    - InputHandler interface with `TerminalInputHandler` and
      `GUIInputHandler` implementations.
    - Command: normalized operations (save, kill, yank, move, search,
      etc.).
- View layer
    - Renderer interface with `TerminalRenderer` and `GUIRenderer`
      implementations.
- Editor: top‑level state managing buffers, messaging, and global
  flags.

Performance and Reliability Targets
-----------------------------------

- Sub‑millisecond keystroke to screen update on typical files in TUI.
- Sustain fluid editing on multi‑megabyte files; graceful degradation
  on very large files.
- Atomic/safe writes; autosave and crash‑recovery journals are
  explicit and transparent.

Keybindings
-----------
kte maintains ke’s command model while internals evolve. Highlights (subject to refinement):

- K‑command prefix: `C-k` enters k‑command mode; exit with `ESC` or
  `C-g`.
- Save/Exit: `C-k s` (save), `C-k x` or `C-k C-x` (save and exit),
  `C-k q` (quit with confirm), `C-k C-q` (quit immediately).
- Editing: `C-k d` (kill to EOL), `C-k C-d` (kill line), `C-k
  BACKSPACE` (kill to BOL), `C-w` (kill region), `C-y` ( yank), `C-u`
  (universal argument).
- Navigation/Search: `C-s` (incremental find), `C-r` (regex search),
  `ESC f/b` (word next/prev), `ESC BACKSPACE` (delete previous word).
- Buffers/Files: `C-k e` (open), `C-k b`/`C-k p` (switch), `C-k c`
  (close), `C-k C-r` (reload).
- Misc: `C-l` (refresh), `C-g` (cancel), `C-k m` (run make), `C-k g`
  (goto line).

See `ke.md` for the canonical ke reference retained for now.

Build and Run
-------------
Prerequisites: C++17 compiler, CMake, and ncurses development headers/libs.

Dependencies by platform
------------------------

- macOS (Homebrew)
  - Terminal (default):
    - `brew install ncurses`
  - Optional GUI (enable with `-DBUILD_GUI=ON`):
    - `brew install sdl2 freetype`
    - OpenGL is provided by the system framework on macOS; no package needed.

- Debian/Ubuntu
  - Terminal (default):
    - `sudo apt-get install -y libncurses5-dev libncursesw5-dev`
  - Optional GUI (enable with `-DBUILD_GUI=ON`):
    - `sudo apt-get install -y libsdl2-dev libfreetype6-dev mesa-common-dev`
    - The `mesa-common-dev` package provides OpenGL headers/libs (`libGL`).

- NixOS/Nix
  - Terminal (default):
    - Ad-hoc shell: `nix-shell -p cmake gcc ncurses`
  - Optional GUI (enable with `-DBUILD_GUI=ON`):
    - Ad-hoc shell: `nix-shell -p cmake gcc ncurses SDL2 freetype libGL`
  - With flakes/devshell (example `flake.nix` inputs not provided): include
    `ncurses` for TUI, and `SDL2`, `freetype`, `libGL` for GUI in your devShell.

Notes
-----

- The GUI is OFF by default to keep SDL/OpenGL/Freetype optional. Enable it by
  configuring with `-DBUILD_GUI=ON` and ensuring the GUI deps above are
  installed for your platform.
- If you previously configured with GUI ON and want to disable it, reconfigure
  the build directory with `-DBUILD_GUI=OFF`.

Example build:

```
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

Run:

```
./cmake-build-debug/kte [files]
```

GUI build example
-----------------

To build with the optional GUI (after installing the GUI dependencies listed above):

```
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=ON
cmake --build cmake-build-debug
./cmake-build-debug/kte --gui [files]
```

Status
------

- The project is under active evolution toward the above architecture
  and UX. The terminal interface now uses ncurses for input and
  rendering. GUI work will follow as a thin, optional layer. ke
  compatibility remains a primary constraint while internals modernize.

Roadmap (high level)
--------------------

1. Solidify core buffer model (gap buffer), file I/O, and
   ke‑compatible commands.
2. Introduce structured undo/redo and search/replace with
   highlighting.
3. Stabilize terminal renderer and input handling across common
   terminals. (initial ncurses implementation landed)
4. Add piece table as an alternative backend with runtime selection
   per buffer.
5. Optional GUI frontend using ImGui; shared command palette.
6. Language niceties (syntax highlighting, indentation rules) behind a
   zero‑deps, fast path.
7. Session restore, autosave/journaling, and safe write guarantees.
8. Extensibility hooks with a small, stable API.
References
----------

- [ke](https://git.wntrmute.dev/kyle/ke) manual and keybinding
  reference: `ke.md`
- Inspirations: Antirez’ kilo, WordStar/VDE, Emacs, and `mg(1)`