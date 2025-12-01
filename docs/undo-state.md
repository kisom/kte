### Context recap

- The undo system is now tree‑based with batching rules and `KTE_UNDO_DEBUG` instrumentation hooks already present in
  `UndoSystem.{h,cc}`.
- GUI path uses SDL; printable input now flows exclusively via `SDL_TEXTINPUT` to `CommandId::InsertText`, while
  control/meta/movement (incl. Backspace/Delete/Newline and k‑prefix) come from `SDL_KEYDOWN`.
- Commit boundaries must be enforced at well‑defined points (movement, non‑editing commands, newline, undo/redo, etc.).

### Status summary (2025‑12‑01)

- Input‑path unification: Completed. `GUIInputHandler.cc` routes all printable characters through `SDL_TEXTINPUT → InsertText`.
  Newlines originate only from `SDL_KEYDOWN → Newline`. CR/LF are filtered out of `SDL_TEXTINPUT` payloads. Suppression
  rules prevent stray `TEXTINPUT` after meta/prefix/universal‑argument flows. Terminal input path remains consistent.
- Tests: `test_undo.cc` expanded to cover branching behavior, UTF‑8 insertion, multi‑line newline/join, and typing batching.
  All scenarios pass.
- Instrumentation: `KTE_UNDO_DEBUG` hooks exist in `UndoSystem.{h,cc}`; a CMake toggle has not yet been added.
- Commit boundaries: Undo/Redo commit boundaries are in place; newline path commits immediately by design. A final audit
  pass across movement/non‑editing commands is still pending.
- Docs: This status document updated. Further docs (instrumentation how‑to and example traces) remain pending in
  `docs/undo.md` / `docs/undo-roadmap.md`.

### Objectives

- Use the existing instrumentation to capture short traces of typing/backspacing/deleting and undo/redo.
- Unify input paths (SDL `KEYDOWN` vs `TEXTINPUT`) and lock down commit boundaries across commands.
- Extend tests to cover branching behavior, UTF‑8, and multi‑line operations.

### Plan of action

1. Enable instrumentation and make it easy to toggle
    - Add a CMake option in `CMakeLists.txt` (root project):
      `option(KTE_UNDO_DEBUG "Enable undo instrumentation logs" OFF)`.
    - When ON, add a compile definition `-DKTE_UNDO_DEBUG` to all targets that include the editor core (e.g., `kte`,
      `kge`, and test binaries).
    - Keep the default OFF so normal builds are quiet; ensure both modes compile in CI.

2. Capture short traces to validate current behavior
    - Build with `-DKTE_UNDO_DEBUG=ON` and run the GUI frontend:
        - Scenario A: type a contiguous word, then move cursor (should show `Begin(Insert)` + multiple `Append`, single
          `commit` at a movement boundary).
        - Scenario B: hold backspace to delete a run, including backspace batching (prepend rule); verify
          `Begin(Delete)` with prepended `Append` behavior, single `commit`.
        - Scenario C: forward deletes at a fixed column (anchor batching); expected single `Begin(Delete)` with same
          column.
        - Scenario D: insert newline (`Newline` node) and immediately commit; type text on the next line; undo/redo
          across the boundary.
        - Scenario E: undo chain and redo chain; then type new text and confirm redo branch gets discarded in logs.
    - Save representative trace snippets and add to `docs/undo.md` or `docs/undo-roadmap.md` for reference.

3. Input‑path unification (SDL `KEYDOWN` vs `TEXTINPUT`) — Completed 2025‑12‑01
    - In `GUIInputHandler.cc`:
        - Ensure printable characters are generated exclusively from `SDL_TEXTINPUT` and mapped to
          `CommandId::InsertText`.
        - Keep `SDL_KEYDOWN` for control/meta/movement, backspace/delete, newline, and k‑prefix handling.
        - Maintain suppression of stray `SDL_TEXTINPUT` immediately following meta/prefix or universal‑argument
          collection so no accidental text is inserted.
        - Confirm that `InsertText` path never carries `"\n"`; newline must only originate from `KEYDOWN` →
          `CommandId::Newline`.
    - If the terminal input path exists, ensure parity: printable insertions go through `InsertText`, control via key
      events, and the same commit boundaries apply.
    - Status: Implemented. See `GUIInputHandler.cc` changes; tests confirm parity with terminal path.

4. Enforce and verify commit boundaries in command execution — In progress
    - Audit `Command.cc` and ensure `u->commit()` is called before executing any non‑editing command that should end a
      batch:
        - Movement commands (left/right/up/down/home/end/page).
        - Prompt accept/cancel transitions and mode switches (search prompts, replace prompts).
        - Buffer/file operations (open/switch/save/close), and focus changes.
        - Before running `Undo` or `Redo` (already present).
    - Ensure immediate commit at the end of atomic edit operations:
        - `Newline` insertion and line joins (`Delete` of newline when backspacing at column 0) should create
          `UndoType::Newline` and commit immediately (parts are already implemented; verify all call sites).
        - Pastes should be a single `Paste`/`Insert` batch per operation (depending on current design).

5. Extend automated tests (or add them if absent) — Phase 1 completed
    - Branching behavior ✓
        - Insert `"abc"`, undo twice (back to `"a"`), insert `"X"`, assert redo list is discarded, and new timeline
          continues with `aX`.
        - Navigate undo/redo along the new branch to ensure correctness.
    - UTF‑8 insertion and deletion ✓
        - Insert `"é漢"` (multi‑byte characters) via `InsertText`; verify buffer content and that a single Insert batch
          is created.
        - Undo/redo restores/removes the full insertion batch.
        - Backspace after typed UTF‑8 should remove the last inserted codepoint from the batch in a single undo step (
          current semantics are byte‑oriented in buffer ops; test to current behavior and document).
    - Multi‑line operations ✓
        - Newline splits a line: verify an `UndoType::Newline` node is created and committed immediately; undo/redo
          round‑trip.
        - Backspace at column 0 joins with previous line: record as `Newline` deletion (via `UndoType::Newline`
          inverse); undo/redo round‑trip.
    - Typing and deletion batching ✓ (typing) / Pending (delete batching)
        - Typing a contiguous word (no cursor moves) yields one `Insert` node with accumulated text.
        - Forward delete at a fixed anchor column yields one `Delete` batch. (Pending test)
        - Backspace batching uses the prepend rule when the cursor moves left. (Pending test)
    - Place tests near existing test suite files (e.g., `tests/test_undo.cc`) or create them if not present. Prefer
      using `Buffer` + `UndoSystem` directly for tight unit tests; add higher‑level integration tests as needed.

6. Documentation updates — In progress
    - In `docs/undo.md` and `docs/undo-roadmap.md`:
        - Describe how to enable instrumentation (`KTE_UNDO_DEBUG`) and an example of trace logs.
        - List batching rules and commit boundaries clearly with examples.
        - Document current UTF‑8 semantics (byte‑wise vs codepoint‑wise) and any known limitations.
    - Current status: this `undo-state.md` updated; instrumentation how‑to and example traces pending.

7. CI and build hygiene — Pending
    - Default builds: `KTE_UNDO_DEBUG` OFF.
    - Add a CI job that builds and runs tests with `KTE_UNDO_DEBUG=ON` to ensure the instrumentation path remains
      healthy.
    - Ensure no performance regressions or excessive logging in release builds.

8. Stretch goals (optional, time‑boxed) — Pending
    - IME composition: confirm that `SDL_TEXTINPUT` behavior during IME composition does not produce partial/broken
      insertions; if needed, buffer composition updates into a single commit.
    - Ensure paste operations (multi‑line/UTF‑8) remain atomic in undo history.

### How to run the tests

- Configure with `-DBUILD_TESTS=ON` and build the `test_undo` target. Run the produced binary (e.g., `./test_undo`).
  The test prints progress and uses assertions to validate behavior.

### Deliverables

- CMake toggle for instrumentation and verified logs for core scenarios. (Pending)
- Updated `GUIInputHandler.cc` solidifying `KEYDOWN` vs `TEXTINPUT` separation and suppression rules. (Completed)
- Verified commit boundaries in `Command.cc` with comments where appropriate. (In progress)
- New tests for branching, UTF‑8, and multi‑line operations; all passing. (Completed for listed scenarios)
- Docs updated with how‑to and example traces. (Pending)

### Acceptance criteria

### Current status (2025‑12‑01) vs acceptance criteria

- Short instrumentation traces match expected batching and commit behavior for typing, backspace/delete, newline, and
  undo/redo. — Pending (instrumentation toggle + capture not done)
- Printable input comes exclusively from `SDL_TEXTINPUT`; no stray inserts after meta/prefix/universal‑argument flows.
  — Satisfied (GUI path updated; terminal path consistent)
- Undo branching behaves correctly; redo is discarded upon new commits after undo. — Satisfied (tested)
- UTF‑8 and multi‑line scenarios round‑trip via undo/redo according to the documented semantics. — Satisfied (tested)
- Tests pass with `KTE_UNDO_DEBUG` both OFF and ON. — Pending (no CMake toggle yet; default OFF passes)