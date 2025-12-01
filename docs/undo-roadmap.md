Undo System Overhaul Roadmap (emacs-style undo-tree)

Context: macOS, C++17 project, ncurses terminal and SDL2/ImGui GUI frontends. Date: 2025-12-01.

Purpose

- Define a clear, incremental plan to implement a robust, non-linear undo system inspired by emacs' undo-tree.
- Align implementation with docs/undo.md and fix gaps observed in docs/undo-state.md.
- Provide test cases and acceptance criteria so a junior engineer or agentic coding system can execute the plan safely.

References

- Specification: docs/undo.md (API, invariants, batching rules, raw buffer ops)
- Current snapshot and recent fix: docs/undo-state.md (GUI mapping notes; Begin/Append ordering fix)
- Code: UndoSystem.{h,cc}, UndoTree.{h,cc}, UndoNode.{h,cc}, Buffer.{h,cc}, Command.{h,cc}, GUI/Terminal InputHandlers,
  KKeymap.

Instrumentation (KTE_UNDO_DEBUG)

- How to enable
  - Build with the CMake option `-DKTE_UNDO_DEBUG=ON` to enable concise instrumentation logs from `UndoSystem`.
  - The following targets receive the `KTE_UNDO_DEBUG` compile definition when ON:
    - `kte` (terminal), `kge` (GUI), and `test_undo` (tests).
  - Examples:
    ```sh
    # Terminal build with tests and instrumentation ON
    cmake -S . -B cmake-build-term -DBUILD_TESTS=ON -DBUILD_GUI=OFF -DKTE_UNDO_DEBUG=ON
    cmake --build cmake-build-term --target test_undo -j
    ./cmake-build-term/test_undo 2> undo.log

    # GUI build (requires SDL2/OpenGL/Freetype toolchain) with instrumentation ON
    cmake -S . -B cmake-build-gui -DBUILD_GUI=ON -DKTE_UNDO_DEBUG=ON
    cmake --build cmake-build-gui --target kge -j
    # Run kge and perform actions; logs go to stderr
    ```

- What it logs
  - Each Begin/Append/commit/undo/redo operation prints a single `[UNDO]` line with:
    - current cursor `(row,col)`, pointer to `pending`, its type/row/col/text-size, and pointers to `current`/`saved`.
  - Example fields: `[UNDO] Begin cur=(0,0) pending=0x... t=Insert r=0 c=0 nlen=2 current=0x... saved=0x...`

- Example trace snippets
  - Typing a contiguous word ("Hello") batches into a single Insert node; one commit occurs before the subsequent undo:
    ```text
    [UNDO] Begin cur=(0,0) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x0 saved=0x0
    [UNDO] commit:enter cur=(0,0) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x0 saved=0x0
    [UNDO] Begin:new cur=(0,0) pending=0x... t=Insert r=0 c=0 nlen=0 current=0x0 saved=0x0
    [UNDO] Append:sv cur=(0,0) pending=0x... t=Insert r=0 c=0 nlen=1 current=0x0 saved=0x0
    ... (more Append as characters are typed) ...
    [UNDO] commit:enter cur=(0,5) pending=0x... t=Insert r=0 c=0 nlen=5 current=0x0 saved=0x0
    [UNDO] commit:done cur=(0,5) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x... saved=0x0
    ```

  - Undo then Redo across that batch:
    ```text
    [UNDO] commit:enter cur=(0,5) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x... saved=0x0
    [UNDO] undo cur=(0,5) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x0 saved=0x0
    [UNDO] commit:enter cur=(0,5) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x0 saved=0x0
    [UNDO] redo cur=(0,5) pending=0x0 t=- r=-1 c=-1 nlen=0 current=0x... saved=0x0
    ```

  - Newline and backspace/delete traces follow the same pattern with `t=Newline` or `t=Delete` and immediate commit for newline.
    Capture by running `kge`/`kte` with `KTE_UNDO_DEBUG=ON` and performing the actions; append representative 3–6 line snippets to docs.

Notes

- Pointer values and exact cursor positions in the logs depend on the runtime and actions; this is expected.
- Keep `KTE_UNDO_DEBUG` OFF by default in CI/release to avoid noisy logs and any performance impact.

̄1) Current State Summary (from docs/undo-state.md)

- Terminal (kte): Keybindings and UndoSystem integration have been stable.
- GUI (kge): Previously, C-k u/U mapping and SDL TEXTINPUT suppression had issues on macOS; these were debugged. The
  core root cause of “status shows Undone but no change” was fixed by moving UndoSystem::Begin/Append/commit to occur
  after buffer modifications/cursor updates so batching conditions see the correct cursor.
- Undo core exists with tree invariants, saved marker/dirty flag mirroring, batching for Insert/Delete, and Newline as a
  single-step undo.

Gaps/Risks

- Event-path unification between KEYDOWN and TEXTINPUT across platforms (macOS specifics).
- Comprehensive tests for branching, GC/limits, multi-line operations, and UTF-8 text input.
- Advanced/compound command grouping and future region operations.


2) Design Goals (emacs-like undo-tree)

- Per-buffer, non-linear undo tree: new edits after undo create a branch; existing redo branches are discarded.
- Batching: insert/backspace/paste/newline grouped into sensible units to match user expectations.
- Silent apply during undo/redo (no re-recording), using raw Buffer methods only.
- Correct saved/dirty tracking and robust pending node lifecycle (detached until commit).
- Efficient memory behavior; optional pruning limits similar to emacs (undo-limit, undo-strong-limit).
- Deterministic behavior across terminal and GUI frontends.


3) Invariants and API (must align with docs/undo.md)

- UndoTree holds root/current/saved/pending; pending is detached and only linked on commit.
- Begin(type) reuses pending only if: same type, same row, and pending->col + pending->text.size() == current cursor
  col (or prepend rules for backspace sequences); otherwise it commits and starts a new node.
- commit(): frees redo siblings from current, attaches pending as current->child, advances current, clears pending;
  nullifies saved marker if diverged.
- undo()/redo(): move current and apply the node using low-level Buffer APIs that do not trigger undo recording.
- mark_saved(): updates saved pointer and dirty flag (dirty ⇔ current != saved).
- discard_pending()/clear(): lifecycle for buffer close/reset/new file.


4) Phased Roadmap

Phase 0 — Baseline & Instrumentation (1 day)

- Audit UndoSystem against docs/undo.md invariants; ensure apply() uses only raw Buffer ops.
- Verify Begin/Append ordering across all edit commands: insert, backspace, delete, newline, paste.
- Add a temporary debug toggle (compile-time or editor flag) to log Begin/Append/commit/undo/redo, cursor(row,col), node
  sizes, and pending state. Include assertions for: pending detached, commit clears pending, redo branch freed on new
  commit, and correct batching preconditions.
- Deliverables: Short log from typing/undo/redo scenarios; instrumentation behind a macro so it can be removed.

Phase 1 — Input Path Unification & Batching Rules (1–2 days)

- Ensure all printable text insertion (terminal and GUI) flows through CommandId::InsertText and reaches UndoSystem
  Begin/Append. On SDL, handle KEYDOWN vs TEXTINPUT consistently; always suppress trailing TEXTINPUT after k-prefix
  suffix commands.
- Commit boundaries: at k-prefix entry, before Undo/Redo, on cursor movement, on focus/file ops, and before any
  non-editing command that should separate undo units.
- Batching heuristics:
    - Insert: same row, contiguous columns; Append(std::string_view) handles multi-character text (pastes, IME).
    - Backspace: prepend batching in increasing column order (store deleted text in forward order).
    - Delete (forward): contiguous at same row/col.
    - Newline: record as UndoType::Newline and immediately commit (single-step undo for line splits/joins).
- Deliverables: Manual tests pass for typing/backspace/delete/newline/paste; GUI C-k u/U work as expected on macOS.

Phase 2 — Tree Limits & GC (1 day)

- Add configurable memory/size limits for undo data (soft and strong limits like emacs). Implement pruning of oldest
  ancestors or deep redo branches while preserving recent edits. Provide stats (node count, bytes in text storage).
- Deliverables: Config hooks, tests demonstrating pruning without violating apply/undo invariants.

Phase 3 — Compound Commands & Region Ops (2–3 days)

- Introduce an optional RAII-style UndoTransaction to group multi-step commands (indent region, kill region, rectangle
  ops) into a single undo step. Internally this just sequences Begin/Append and ensures commit even on early returns.
- Support row operations (InsertRow/DeleteRow) with proper raw Buffer calls. Ensure join_lines/split_line are handled by
  Newline nodes or dedicated types if necessary.
- Deliverables: Commands updated to use transactions when appropriate; tests for region delete/indent and multi-line
  paste.

Phase 4 — Developer UX & Diagnostics (1 day)

- Add a dev command to dump the undo tree (preorder) with markers for current/saved and pending (detached). For GUI,
  optionally expose a simple ImGui debug window (behind a compile flag) that visualizes the current branch.
- Editor status improvements: show short status codes for undo/redo and when a new branch was created or redo discarded.
- Deliverables: Tree dump command; example output in docs.

Phase 5 — Comprehensive Tests & Property Checks (2–3 days)

- Unit tests (extend test_undo.cc):
    - Insert batching: type "Hello" then one undo removes all; redo restores.
    - Backspace batching: type "Hello", backspace 3×, undo → restores the 3; redo → re-applies deletion.
    - Delete batching (forward delete) with cursor not moving.
    - Newline: split a line and undo to join; join a line (via backspace at col 0) and undo to split.
    - Branching: type "abc", undo twice, type "X" → redo history discarded; ensure redo no longer restores 'b'/'c'.
    - Saved/dirty: mark_saved after typing; ensure dirty flag toggles correctly after undo/redo; saved marker tracks the
      node.
    - discard_pending: create pending by typing, then move cursor or invoke commit boundary; ensure pending is attached;
      also ensure discard on buffer close clears pending.
    - clear(): resets state with no leaks; tree pointers null.
    - UTF-8 input: insert multi-byte characters via InsertText with multi-char std::string; ensure counts/col tracking
      behave (text stored as bytes; editor col policy consistent within kte).
- Integration tests (TestFrontend):
    - Both TerminalFrontend and GUIFrontend: simulate text input and commands, including k-prefix C-k u/U.
    - Paste scenarios: multi-character insertions batched as one.
- Property tests (optional but recommended):
    - Generate random sequences of edits; record them; then apply undo until root and redo back to the end → buffer
      contents match at each step; no crashes; dirty flag transitions consistent. Seed-based to reproduce failures.
    - Redo-branch discard property: any new edit after undo must eliminate redo path; redoing should be impossible
      afterward.
- Deliverables: Tests merged and passing on CI for both frontends; failures block changes to undo core.

Phase 6 — Performance & Stress (0.5–1 day)

- Stress test with large files and long edit sequences. Target: smooth typing at 10k+ ops/minute on commodity hardware;
  memory growth bounded when GC limits enabled.
- Deliverables: Basic perf notes; optional lightweight benchmarks.


5) Acceptance Criteria

- Conformance to docs/undo.md invariants and API surface (including raw Buffer operations for apply()).
- Repro checklist passes:
    - Type text; single-step undo/redo works and respects batching.
    - Backspace/delete batching works.
    - Newline split/join are single-step undo/redo.
    - Branching works: undo, then type → redo path is discarded; no ghost redo.
    - Saved/dirty flags accurate across undo/redo and diverge/rejoin paths.
    - No pending nodes leaked on buffer close/reload; no re-recording during undo/redo.
    - Behavior identical across terminal and GUI input paths.
- Tests added for all above; CI green.


6) Concrete Work Items by File

- UndoSystem.h/cc:
    - Re-verify Begin/Append ordering; enforce batching invariants; prepend logic for backspace; immediate commit for
      newline.
    - Implement/verify apply() uses only Buffer raw methods: insert_text/delete_text/split_line/join_lines/row ops.
    - Add limits (configurable) and stats; add discard_pending safety paths.
- Buffer.h/cc:
    - Ensure raw methods exist and do not trigger undo; ensure UpdateBufferReference is correctly used when
      replacing/renaming the underlying buffer.
    - Call undo.commit() on cursor movement and non-editing commands (via Command layer integration).
- Command.cc:
    - Ensure all edit commands drive UndoSystem correctly; commit at k-prefix entry and before Undo/Redo.
    - Introduce UndoTransaction for compound commands when needed.
- GUIInputHandler.cc / TerminalInputHandler.cc / KKeymap.cc:
    - Ensure unified InsertText path; suppress SDL_TEXTINPUT when a k-prefix suffix produced a command; preserve case
      mapping.
- Tests: test_undo.cc (extend) + new tests (e.g., test_undo_branching.cc, test_undo_multiline.cc).


7) Example Test Cases (sketches)

- Branch discard after undo:
    1) InsertText("abc"); Undo(); Undo(); InsertText("X"); Redo();
       Expected: Redo is a no-op (or status indicates no redo), buffer is "aX".

- Newline split/join:
    1) InsertText("ab"); Newline(); InsertText("c"); Undo();
       Expected: single undo joins lines → buffer "abc" on one line at original join point; Redo() splits again.

- Backspace batching:
    1) InsertText("hello"); Backspace×3; Undo();
       Expected: restores "hello".

- UTF-8 insertion:
    1) InsertText("😀汉"); Undo(); Redo();
       Expected: content unchanged across cycles; no crashes.

- Saved/dirty transitions:
    1) InsertText("hi"); mark_saved(); InsertText("!"); Undo(); Redo();
       Expected: dirty false after mark_saved; dirty true after InsertText("!"); dirty returns to false after Undo();
       true again after Redo().


8) Risks & Mitigations

- SDL/macOS event ordering (KEYDOWN vs TEXTINPUT, IME): Mitigate by suppressing TEXTINPUT on mapped k-prefix suffixes;
  optionally temporarily disable SDL text input during k-prefix suffix mapping; add targeted diagnostics.
- UTF-8 width vs byte-length: Store bytes in UndoNode::text; keep column logic consistent with existing Buffer
  semantics.
- Memory growth: Add GC/limits and provide a way to clear/reduce history for huge sessions.
- Re-entrancy during apply(): Prevent public edit paths from being called; use only raw operations.


9) Nice-to-Have (post-MVP)

- Visual undo-tree navigation (emacs-like time travel and branch selection), at least as a debug tool initially.
- Persistent undo across saves (opt-in; likely out-of-scope initially).
- Time-based batching threshold (e.g., break batches after >500ms pause in typing).


10) Execution Notes for a Junior Engineer/Agentic System

- Start from Phase 0; do not skip instrumentation—assertions will catch subtle batching bugs early.
- Change one surface at a time; when adjusting Begin/Append/commit positions, re-run unit tests immediately.
- Always ensure commit boundaries before invoking commands that move the cursor/state.
- When unsure about apply(), read docs/undo.md and mirror exactly: only raw Buffer methods, never the public editing
  APIs.
- Keep diffs small and localized; add tests alongside behavior changes.

Appendix A — Minimal Developer Checklist

- [ ] Begin/Append occur after buffer mutation and cursor updates for all edit commands.
- [ ] Pending detached until commit; freed/cleared on commit/discard/clear.
- [ ] Redo branches freed on new commit after undo.
- [ ] mark_saved updates both saved pointer and dirty flag; dirty mirrors current != saved.
- [ ] apply() uses only raw Buffer methods; no recording during apply.
- [ ] Terminal and GUI both route printable input to InsertText; k-prefix mapping suppresses trailing TEXTINPUT.
- [ ] Unit and integration tests cover batching, branching, newline, saved/dirty, and UTF-8 cases.
