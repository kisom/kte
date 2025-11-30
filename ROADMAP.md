kte ROADMAP — from skeleton to a working editor

Scope for “working editor” v0.1
- Runs in a terminal; opens files passed on the CLI or an empty buffer.
- Basic navigation, insert/delete, newline handling.
- Status line and message area; shows filename, dirty flag, cursor position.
- Save file(s) to disk safely; quit/confirm on dirty buffers.
- Core ke key chords: C-g (cancel), C-k s/x/q/C-q, C-l, basic arrows, Enter/Backspace, C-s (simple find).

Guiding principles
- Keep the core small and understandable; evolve incrementally.
- Separate model (Buffer/Editor), control (Input/Command), and view (Renderer).
- Favor terminal first; GUI hooks arrive later behind interfaces.

✓ Milestone 0 — Wire up a minimal app shell
1. main.cpp
   - Replace demo printing with real startup using `Editor`.
   - Parse CLI args; open each path into a buffer (create empty if none). ✓ when `kte file1 file2` loads buffers and exits cleanly.
2. Editor integration
   - Ensure `Editor` can open/switch/close buffers and hold status messages.
   - Add a temporary “headless loop” to prove open/save calls work.

✓ Milestone 1 — Command model
1. Command vocabulary
   - Flesh out `Command.h/.cpp`: enums/struct for operations and data (e.g., InsertChar, MoveCursor, Save, Quit, FindNext, etc.).
   - Provide a dispatcher entry point callable from the input layer to mutate `Editor`/`Buffer`.
   - Definition of done: commands exist for minimal edit/navigation/save/quit; no rendering yet.

✓ Milestone 2 — Terminal input
1. Input interfaces
   - Add `InputHandler.h` interface plus `TerminalInputHandler` implementation.
   - Terminal input via ncurses (`getch`, `keypad`, non‑blocking with `nodelay`), basic key decoding (arrows, Ctrl, ESC sequences).
2. Keymap
   - Map ke chords to `Command` (C-k prefix handling, C-g cancel, C-l refresh, C-k s/x/q/C-q, C-s find start, text input → InsertChar).
3. Event loop
   - Introduce the core loop in main: read key → translate to `Command` → dispatch → trigger render.

Milestone 3 — Terminal renderer
1. View interfaces
   - Add `Renderer.h` with `TerminalRenderer` implementation (ncurses‑based).
2. Minimal draw
   - Render viewport lines from current buffer; draw status bar (filename, dirty, row:col, message).
   - Handle scrolling when cursor moves past edges; support window resize (SIGWINCH).
3. Cursor
   - Place terminal cursor at logical buffer location (account for tabs later; start with plain text).

Milestone 4 — Buffer fundamentals to support editing
1. GapBuffer
   - Ensure `GapBuffer` supports insert char, backspace, delete, newline, and efficient cursor moves.
2. Buffer API
   - File I/O (open/save), dirty tracking, encoding/line ending kept simple (UTF‑8, LF) for v0.1.
   - Cursor state, mark (optional later), and viewport bookkeeping.
3. Basic motions
   - Left/Right/Up/Down, Home/End, PageUp/PageDown; word f/b (optional in v0.1).

Milestone 5 — Core editing loop complete
1. Tighten loop timing
   - Ensure keystroke→update→render latency is reliably low; avoid unnecessary redraws.
2. Status/messages
   - `Editor::SetStatus()` shows transient messages; C-l forces full refresh.
3. Prompts
   - Minimal prompt line for save‑as/confirm quit; blocking read in prompt mode is acceptable for v0.1.

Milestone 6 — Search (minimal)
1. Incremental search (C-s)
   - Simple forward substring search with live highlight of current match; arrow keys navigate matches while in search mode (ke‑style quirk acceptable).
   - ESC/C-g exits search; Enter confirms and leaves cursor on match.

Milestone 7 — Safety and polish for v0.1
1. Safe writes
   - Write to temp file then rename; preserve permissions where possible.
2. Dirty/quit logic
   - Confirm on quit when any buffer is dirty; `C-k C-q` bypasses confirmation.
3. Resize/terminal quirks
   - Handle small terminals gracefully; no crashes on narrow widths.
4. Basic tests
   - Unit tests for `GapBuffer`, Buffer open/save round‑trip, and command mapping.

Out of scope for v0.1 (tracked, not blocking)
- Undo/redo, regex search, kill ring, word motions, tabs/render width, syntax highlighting, piece table selection, GUI.

Implementation notes (files to add)
- Input: `InputHandler.h`, `TerminalInputHandler.cpp/h` (ncurses).
- Rendering: `Renderer.h`, `TerminalRenderer.cpp/h` (ncurses).
- Prompt helpers: minimal utility for line input in raw mode.
- Platform: small termios wrapper; SIGWINCH handler.

Acceptance checklist for v0.1
- Start: `./kte [files]` opens files or an empty buffer.
- Edit: insert text, backspace, newlines; move cursor; content scrolls.
- Save: `C-k s` writes file atomically; dirty flag clears; status shows bytes written.
- Quit: `C-k q` confirms if dirty; `C-k C-q` exits without confirm; `C-k x` save+exit.
- Refresh: `C-l` redraws.
- Search: `C-s` finds next while typing; ESC cancels.

Next concrete step
- Stabilize cursor placement and scrolling logic; add resize handling and begin minimal prompt for save‑as.