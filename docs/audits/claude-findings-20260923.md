# Bug and Performance Audit

**Project:** kte (Kyle's Text Editor)
**Date:** 2026-09-23
**Auditor:** Claude Code (automated review, four parallel reviewers plus
spot verification)
**Baseline:** `d8d2ae8` (debug build clean, `kte_tests` 0 failures)

---

## Method

The tree was split into four review areas: core text model (PieceTable,
Buffer, OptimizedSearch), undo and swap (UndoSystem/Tree/Node, Swap,
ErrorRecovery, SyscallWrappers), commands (Command.cc, KKeymap), and
frontends/syntax (Terminal, ImGui, Qt, Editor, `syntax/`). Every finding
below was traced to a concrete scenario. Findings marked **[repro]** were
reproduced with a small test program; the rest were confirmed by reading
the code.

Finding IDs are stable and are referenced by fix commits:

- **M** — memory safety (crash, out-of-bounds, use-after-free, data race)
- **D** — data integrity (undo, swap journal, text corruption)
- **B** — incorrect behaviour
- **P** — performance

---

## M — Memory safety

### M1. PieceTable assignment keeps a stale line index **[repro]**

`PieceTable.cc:47-98`. Copy and move assignment copy `pieces_`, `add_`,
and `total_size_`, but leave the destination's `line_index_` and
`line_index_dirty_` untouched. `Editor::CloseBuffer` erases from
`std::vector<Buffer>`, which move-assigns every later buffer into the
previous slot.

Scenario: open A (`"line1\nline2\nline3\n"`), open B (`"x"`), render
both, close A. Slot 0 now holds B's text with A's index: `LineCount()`
returns 4 and `GetLineView(1)` builds a view over bytes 6–12 of a 1-byte
buffer.

Fix: mark the line index dirty (and clear it) in both assignment
operators.

### M2. Background highlighter thread races buffer mutation

`syntax/HighlighterEngine.cc:184-249`. `PrefetchViewport` (called from
the terminal and ImGui renderers) queues rows for a worker thread which
calls `buf.GetLineString`, walking `pieces_` and reading `add_`. The main
thread's `PieceTable::Insert`/`Delete` mutate both without any shared
lock; `add_.append` may reallocate. The worker also stores a raw
`Buffer*` (`pending_.buf`) which dangles when `AddBuffer`'s `push_back`
reallocates or `CloseBuffer`'s `erase` shifts buffers.

Fix: remove the background warm-up, or feed it an immutable snapshot.

### M3. Per-window SwapManager leaves dangling recorders (ImGui multi-window)

`ImGuiFrontend.cc:381-385`, `Editor.cc:205-213`. Each window's `Editor`
owns its own `SwapManager` but shares the buffer vector. A buffer created
from a secondary window keeps a `swap_rec_` pointing into that window's
manager; closing the window destroys it, and the next edit in the main
window calls into freed memory. `Rehome` from the wrong manager also
returns `nullptr`, silently disabling journaling for the primary's
buffers.

Fix: share one `SwapManager` across windows, and detach every buffer an
`Editor` attached when that `Editor` is destroyed.

### M4. Recursive undo-tree free overflows the stack **[repro]**

`UndoTree.cc:12` (`free_node_graph`), also `UndoSystem.cc:354`
(`is_descendant`, `dfs_find_parent`). Recursion depth equals history
length. About 300k nodes segfaults a debug build on an 8 MB stack;
replace-all over ~150k matches (two nodes each) followed by close or
reload is enough.

Fix: free iteratively with an explicit stack.

### M5. Jump-to-mark places the cursor past the end of the buffer

`Command.cc:3720`. Marks are not adjusted on edits and
`cmd_jump_to_mark` does not clamp. After deleting lines below a mark,
C-k j puts the cursor on a nonexistent row; `cmd_move_left` and
`cmd_move_up` (`:3866`, `:3988`) then index `rows[y]` out of bounds.

Fix: clamp in `cmd_jump_to_mark` and in `compute_mark_region`.

---

## D — Data integrity

### D1. Smart-newline undo deletes the wrong text **[repro]**

`Command.cc:3139-3146`. The indent is inserted and the cursor moved past
it before `u->Begin(UndoType::Insert)`; `UndoSystem::Begin` records the
cursor position, so the node is placed `indent.size()` columns too far
right. `"    foobar"` + SmartNewline + two undos yields
`"    foo    next"` (the following line's text is spliced in and `bar`
is lost).

Fix: `Begin` at `new_x`, then insert, then move the cursor; group the
newline and the indent.

### D2. Cursor column left past end-of-line corrupts undo

`cmd_scroll_up`/`cmd_scroll_down` (`Command.cc:4178`, `:4215`, bound to
the mouse wheel), replace-all's cursor restore (`:2580`), and
`cmd_jump_to_mark` call `SetCursor` without clamping x. `delete_text`
does not clamp; it treats the excess as crossing the newline.
Backspace then joins lines with an empty (dropped) undo node, and
typing followed by undo deletes a newline instead of the typed
character.

Fix: clamp x to the line length in `Buffer::SetCursor`.

### D3. `delete_row` on a final line without trailing newline leaves a stray `\n`

`Buffer.cc:803`. Affects `delete_region` (C-w, M-d, M-Backspace), regex
replace (`Command.cc:2996`), reflow (`:4884`), and the kill ring.
`"foo\nbar"` + M-d at end of `foo` yields `"foo\n"`.

### D4. Phantom empty row after a trailing newline

`PieceTable::LineCount()` counts the empty row after a final `\n`.
C-k C-d on that row records a `DeleteRow("")` whose undo inserts a
line (`Command.cc:3516`); regex replace of `^` matches it and appends
an extra line (`:2985`).

### D5. Missing or wrong undo grouping

- Plain replace-all (`Command.cc:2537-2577`) has no group: N
  replacements need 2N undos, and the final Insert stays pending so the
  next typed characters coalesce into it.
- Visual-line newline (`:3043-3075`) records no undo at all.

### D6. Reflow on a blank line merges the adjacent paragraphs

`Command.cc:4592`. `"aaa\n\nbbb"` with the cursor on the blank line and
ESC q gives `"aaa bbb"`.

### D7. Swap journal is fragile exactly where it matters

- `Swap.cc:1344-1373`: a torn final record (the power-loss case) makes
  the whole file unreadable instead of replaying the valid prefix.
- `Swap.cc:1134-1157`, `:1187`, `:1224`: after any dropped record
  (breaker open, >16 MB payload, partial write) later records still
  replay against a different base state; a partial write leaves bytes
  that later records are appended behind.
- `Swap.cc:1040` vs `:1187`: checkpoints allow 4 GB but the record
  length is 24 bits; buffers over 16 MB are never checkpointed and the
  journal grows without bound.
- `Swap.cc:644`, `:708`: a failed header write leaves an fd with no
  header; answering N to "delete corrupt swap?" appends new edits to the
  corrupt file.

### D8. Circuit breaker failure window never expires

`ErrorRecovery.cc:77`. `last_failure_time_` is updated before
`IsWindowExpired()` reads it, so elapsed time is always zero; five
failures hours apart still open the breaker.

### D9. Stale syntax highlighting after undo-to-clean or reload

`Buffer.h:391-400`. `version_` is bumped only in `SetDirty(true)`. Undo
back to the saved node calls `SetDirty(false)`, and `OpenFromFile`
(reload) never bumps it, so `HighlighterEngine` keeps serving spans for
the old text.

---

## B — Behaviour

### B1. `set_escdelay` is compiled out

`TerminalFrontend.cc:46`. `set_escdelay` is a function in ncurses, not a
macro, so `#ifdef set_escdelay` is always false and ESC waits the
1000 ms default.

### B2. Terminal resize runs C-g

`TerminalInputHandler.cc:167-171` maps `KEY_RESIZE` to
`CommandId::Refresh`, the cancel handler: it clears the mark, cancels
prompts (including the swap-recovery prompt, so the file is never
opened), and resets the universal argument.

### B3. ESC-as-Meta flag is not cleared on most paths

`TerminalInputHandler.cc:56-279`. ESC followed by an arrow, Enter, Tab,
or a control chord leaves `esc_meta` set; the next plain `d` runs M-d.
Same in `ImGuiInputHandler.cc:43-73`.

### B4. C-h shadows Backspace in the terminal

The generic control lookup (`TerminalInputHandler.cc:264`) maps ^H to
SearchReplace before the Backspace branch (`:307`) runs.

### B5. Byte-based editing splits UTF-8

`Command.cc:3259`, `:3352`, `:3861-3947`. Backspace, delete, and
horizontal motion step by byte: Backspace on `é` leaves a lone `0xC3`,
and C-b/C-f land inside code points.

### B6. Horizontal scroll counts bytes, renderer counts cells

`compute_render_x` (`Command.cc:52-60`) counts bytes while
`TerminalRenderer.cc:269-322` skips display cells: a long CJK line
scrolls to blank with the cursor off-screen.

### B7. GUI draws non-ASCII text as replacement glyphs

Highlighters emit one span per non-ASCII byte and `ImGuiRenderer.cc`
(~510) / `QtFrontend.cc` (~333) decode each span separately.

### B8. ImGui drops the character after Ctrl+V

`ImGuiInputHandler.cc:362`, `:382`, `:401` set a suppress-next-text flag
for chords that produce no text event.

### B9. Control characters mis-measured in the terminal

`TerminalRenderer.cc:314`, `:364`, `:413` count C0 controls as width 1;
ncurses draws them as `^X`.

### B10. Qt tab width disagrees with the command layer

`QtFrontend.cc:138` uses 4; `Command.cc` uses 8.

### B11. Smart newline edits the old buffer while a prompt is open

`Command.cc:3131`. With a prompt active, `cmd_newline` accepts it and
the saved indent is inserted through a possibly stale `Buffer*`.

### B12. Line count stale after merged appends **[repro]**

`PieceTable.cc:220-260`. `addPieceBack`/`addPieceFront` merge paths
skip `InvalidateLineIndex()`. Latent today.

### B13. Syntax highlighter edge cases

Go raw strings and Rust multi-line strings do not carry state across
lines; shell `$#` is highlighted as a comment; a C++ `#define` line
opening `/*` does not carry the comment.

---

## P — Performance

### P1. Whole-file `Rows()` rebuild per frame

Renderers call `buf->Rows()` every frame (`TerminalRenderer.cc:45`,
`ImGuiRenderer.cc:69`, `QtFrontend.cc:477`), and
`ensure_cursor_visible` (`Command.cc:91`) after every command. After an
edit this rebuilds a `std::string` per line, each via two `GetRange`
calls.

### P2. Whole-buffer materialization for one line

`Buffer::GetLineView` (`Buffer.cc:633`) calls `content_.Data()`, which
copies the entire file; the line index is rebuilt byte-by-byte after
every edit.

### P3. Re-highlight from row 0 on every keystroke

`SetDirty(true)` calls `InvalidateFrom(0)`; stateful highlighters then
re-lex from the top of the file to the viewport.

### P4. Quadratic region commands

Replace-all, `delete_region`, indent/unindent, reflow, and repeated
backspace re-fetch `Rows()` after each mutation inside their loops.

### P5. Opening a file checkpoints into a journal that is then deleted

`Swap.cc:493` snapshots, writes, and fsyncs the whole file into
`unnamed-N.swp` before removing it.

### P6. Boyer–Moore bad-character shift degenerates to 1

`OptimizedSearch.cc:63`, `:96`: when the mismatched byte is absent from
the pattern the shift is 1 instead of `j`. Test-only today.

---

## Recommended order

1. M1 (one-line fix)
2. M2 (remove or snapshot the warm-up thread)
3. D2 + M5 (clamp in `SetCursor` and jump-to-mark)
4. D1 (smart-newline undo order)
5. D7 (accept a torn final swap record)
6. P1/P2 (`Nrows()` + `GetLineString` in hot paths)

---

## Fix status

Updated as fixes land on `claude/funny-fermat-gj2vm5`.

| ID | Status |
|----|--------|
| M1 | fixed: assignment operators invalidate the line index |
| M2 | fixed: background warm-up thread removed; visible rows highlighted synchronously |
| M3 | fixed: editors sharing buffers share the owner's SwapManager |
| M4 | fixed: iterative free/search; `clear()` also leaked root siblings, now frees all |
| M5 | fixed: jump-to-mark clamps; move up/down clamp the row (move-down also underflowed) |
| D1 | fixed: `Begin` before the cursor moves; newline + indent grouped |
| D2 | fixed for scroll up/down and replace-all restore |
| D3 | fixed: kill-region, regex replace, reflow and kill-line edit byte ranges in place |
| D4 | fixed: regex replace skips the row after a final newline (a zero-width pattern used to hang the editor); kill-line there is a no-op |
| D5 | fixed: replace-all one undo group; visual-line newline recorded as one group |
| D6 | fixed: reflow on a blank line is a no-op |
| D7 | fixed: torn tail replays the valid prefix; lost records mark a gap resynced by checkpoint; partial writes truncated; header failure closes; large inserts split; oversize checkpoints skipped; declined corrupt swap moved to `.corrupt` |
| D8 | fixed: window checked before recording the failure |
| D9 | fixed: every text mutation bumps the version (including undo to the saved state); reload and replace also clear undo/highlighting |
| B1 | fixed: guard on `NCURSES_VERSION` |
| B2 | fixed: `KEY_RESIZE` issues no command |
| B3 | fixed: ESC-meta flag consumed on every key; ESC ^H and ESC Enter reach their bindings |
| B4 | not changed: C-h is a deliberate binding (search & replace); terminals whose terminfo `kbs` is ^H are translated to KEY_BACKSPACE by ncurses |
| B5 | fixed: backspace/delete/left/right step over whole UTF-8 characters; up/down snap to a boundary |
| B6 | fixed: horizontal scroll and mouse column use display cells (mbrtowc/wcwidth) |
| B7–B10, B13 | open (GUI-only or highlighter edge cases) |
| B11 | fixed: SmartNewline with a prompt open delegates to Newline |
| B12 | fixed: merged appends invalidate the line index |
| P1 | fixed: `ensure_cursor_visible` and the terminal/ImGui renderers no longer call `Rows()` |
| P2 | fixed: line index updated in place on insert/delete (14.7 ms -> 0.28 ms per keystroke on a 38 MB buffer) |
| P3 | fixed: highlighting invalidated from the edited row (46 ms -> 0.14 ms per keystroke near the end of a 100k-line C++ file) |
| P4 | replace-all and delete-region fixed; indent/unindent loops unchanged |
| P5 | fixed: no throwaway checkpoint on filename change |
| P6 | open (test-only code) |

Regression tests: `tests/test_audit_regressions.cc`. Each test fails on the
unfixed code (M4 by crashing).

---

## Follow-up review rounds

After the first batch of fixes, further review rounds (an adversarial
review of each batch of changes, plus fresh reviews of areas the first
audit covered lightly) found further defects. All of the following are
fixed on this branch, each with a regression test that fails on the code
before the fix (unless noted).

**Crash / hang**
- Opening a directory (bad_alloc exit), a FIFO (open blocked forever), or
  a device node (later replaced by a regular file on save).
- Filesystem exceptions (ENAMETOOLONG, EACCES, deleted cwd) and any other
  exception escaping a command terminated the editor.
- Regex search/replace, and renderer match highlighting, overflowed the
  stack on long lines (std::regex recursion); a zero-width regex replace
  hung the editor (D4).
- Piped (non-tty) stdin was executed as commands, then spun at full CPU.

**Data loss / corruption**
- Recovery deleted the swap it was about to replay when another named
  buffer was current (pre-existing).
- Recovered sessions appended behind a torn final record.
- Reload left the old journal (discarded edits came back on recovery);
  reload discarded unsaved edits without confirmation and "reloaded" a
  deleted file as empty.
- Two buffers (duplicate open, save-as onto an open file) or two
  sessions shared one journal; closing/saving one deleted the other's
  live journal. Journals are now flock()ed; a session never removes a
  journal it does not own; unnamed journals include the pid; distinct
  paths no longer map to one swap name.
- Journals now record the base file's size, mtime and CRC-32 and are
  not replayed onto a file that changed afterwards.
- Quit checked only the current buffer; Enter at "Recover?" / "Save
  changes?" took the destructive branch; the close prompt's save
  overwrote on-disk changes; save-as overwrote other files silently.
- Kill-line with a count, stale marks in kill-region, commands running
  behind an open prompt (read-only bypass, wrong buffer closed), paste
  into a prompt, nested undo groups, UTF-8 splitting in word motions.
- Save: fsync retried after failure (could report success for lost
  data); new files got 0600; symlinks replaced; hard links split;
  owner and setuid/setgid bits lost; umask race with the writer thread.
- Writer-thread fsync raced with closes (fd reuse; kept a closed
  journal's lock alive); crc32 table init race; Close() retried EINTR.

**Performance**
- Motion/delete commands rebuilt every line after an edit (80-90 ms per
  keystroke on 500k lines; C-u 3000 C-d took 273 s, now 0.25 s).
- Line index invalidated by consolidation; incremental index added.
- Idle redraw every 16 ms rescanned long lines (one core busy); paste
  handled one key per frame; RowsView retained unbounded copies.

**Fourth round (stress testing)**
- Undo/redo of multi-line text left the cursor past the end of the
  line; later edits and their undo records went astray and
  delete-word-prev read out of bounds (ASan).
- A recovered buffer looked clean after one edit and its undo; closing
  it discarded the recovered text and journal. Save-and-quit did not
  record the saved state; stale visual-line selections pointed past the
  end of the buffer; open undo groups survived an exception.
- A reload that ran out of memory emptied the buffer, and the next save
  truncated the file. Exceptions from deferred opens ended the editor.
  A leftover hard-link copy blocked every later save; a file in a
  read-only directory could not be saved.
- The journal writer's periodic fsync held the lock every keystroke
  needs (typing froze on slow disks); every open and save re-read the
  whole file under that lock; two ThreadSanitizer races.
- Replace-all, yank and repeat counts issued one edit per match, line
  or repetition (quadratic: minutes for 229k matches, now 0.14 s).
- With LANG unset, multibyte input was dropped. Renderer caches and the
  reload confirmation were keyed on buffer addresses, which are reused
  after a close; a reload confirmation survived other commands; a swap
  recovery prompt from a deferred open was not drawn until a key.

**Fifth round (review of the fourth round's changes)**
- The save fallbacks added in round four (write in place when no temp
  file or hard-link copy can be made) also fired on ENOSPC/EDQUOT/EIO,
  truncating the file and then failing: the file on disk was left
  corrupt. They now apply only when the directory refuses a new name
  (EACCES, EPERM, ENAMETOOLONG); otherwise the save fails and the file
  is untouched.
- Whole-buffer replace recorded the entire old and new text for undo on
  every run (about 150 MB more per replace-all on a 50 MB file); only
  the span between the common prefix and suffix is now recorded.
- Regex search silently never found matches on lines over 20,000 bytes;
  the skip now applies only to search-as-you-type (at 2,000 bytes, where
  the worst case is well under a second), says so in the status line,
  and moving to the next/previous match searches every line.
- After a lost journal record, the recovery checkpoint was requested
  only by later edits, at most once a second; if the first attempt
  failed and the user then stopped typing, the journal stayed behind
  indefinitely. The editor loop now retries it each second while idle
  (this was also why SwapReplay_LostRecord_ResyncsWithCheckpoint was
  flaky under load).
- Undoing C-k C-k (and redoing an inserted row) journaled the row and
  its newline as two records; a checkpoint triggered by the first
  already contained the newline, so recovery silently produced an
  extra blank line.
- Visual-line yank with the cursor moved out of the selection (C-k a,
  Enter) left it past the end of a line or the buffer, and text typed
  there could not be undone. Every command now leaves the cursor
  inside the buffer (checked in the command dispatcher).
- ENABLE_ASAN instrumented only C sources, i.e. none of the project;
  it now applies to C++ as well. (Earlier "ASan clean" results in this
  audit came from that uninstrumented build; the full suite has since
  passed under a correctly instrumented ASan build with leak checks,
  and the stress fuzzers ran with ASan and UBSan.)

**Known limitations (not fixed)**
- Catastrophic regex backtracking (e.g. `(a*)*b`) can still take very
  long; std::regex has no time limit.
- Typing in a multi-megabyte single line costs O(line length) per
  keystroke (cursor column computed by scanning the line).
- GUI-only items B7, B8, B10 and highlighter edge cases B13 are
  unchanged; the ImGui and Qt frontends could not be built in the review
  environment (changed GUI files were syntax-checked where possible).
- A journal whose buffer exceeds 16 MiB cannot be checkpointed; after a
  lost record such a journal stays incomplete until the file is saved
  (reported to the user).
