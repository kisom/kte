### Unit testing plan (headless, no interactive frontend)

#### Principles
- Headless-only: exercise core components directly (`PieceTable`, `Buffer`, `UndoSystem`, `OptimizedSearch`, and minimal `Editor` flows) without starting `kte` or `kge`.
- Deterministic and fast: avoid timers, GUI, environment-specific behavior; prefer in-memory operations and temporary files.
- Regression-focused: encode prior failures (save/newline mismatch, legacy `rows_` writes) as explicit tests to prevent recurrences.

#### Harness and execution
- Single binary: use target `kte_tests` (already present) to compile and run all tests under `tests/` with the minimal in-tree framework (`tests/Test.h`, `tests/TestRunner.cc`).
- No GUI/ncurses deps: link only engine sources (PieceTable/Buffer/Undo/Search/Undo* and syntax minimal set), not frontends.
- How to build/run:
  - Debug profile:
    ```
    cmake -S /Users/kyle/src/kte -B /Users/kyle/src/kte/cmake-build-debug -DBUILD_TESTS=ON && \
    cmake --build /Users/kyle/src/kte/cmake-build-debug --target kte_tests && \
    /Users/kyle/src/kte/cmake-build-debug/kte_tests
    ```
  - Release profile:
    ```
    cmake -S /Users/kyle/src/kte -B /Users/kyle/src/kte/cmake-build-release -DBUILD_TESTS=ON && \
    cmake --build /Users/kyle/src/kte/cmake-build-release --target kte_tests && \
    /Users/kyle/src/kte/cmake-build-release/kte_tests
    ```

---

### Test catalog (summary table)

The table below catalogs all unit tests defined in this plan. It is headless-only and maps directly to the suites A–H described later. “Implemented” reflects current coverage in `kte_tests`.

| Suite | ID  | Name                                      | Description (1‑line)                                                                 | Headless | Implemented |
|:-----:|:---:|:------------------------------------------|:-------------------------------------------------------------------------------------|:--------:|:-----------:|
| A | 1 | SaveAs then Save (append) | New buffer → write two lines → `SaveAs` → append → `Save`; verify exact bytes. | Yes | ✓ |
| A | 2 | Open existing then Save | Open seeded file, append, `Save`; verify overwrite bytes. | Yes | ✓ |
| A | 3 | Open non-existent then SaveAs | Start from non-existent path, insert `hello, world\n`, `SaveAs`; verify bytes. | Yes | ✓ |
| A | 4 | Trailing newline preservation | Verify saving preserves presence/absence of final `\n`. | Yes | Planned |
| A | 5 | Empty buffer saves | Empty → `SaveAs` → 0 bytes; then insert `\n` → `Save` → 1 byte. | Yes | Planned |
| A | 6 | Large file streaming | 1–4 MiB with periodic newlines; size and content integrity. | Yes | Planned |
| A | 7 | Tilde expansion | `SaveAs` with `~/...`; re-open to confirm path/content. | Yes | Planned |
| A | 8 | Error propagation | Save to unwritable path → expect failure and error message. | Yes | Planned |
| B | 1 | Insert/Delete LineCount | Basic inserts/deletes and line counting sanity. | Yes | ✓ |
| B | 2 | Line/Col conversions | `LineColToByteOffset` and reverse around boundaries. | Yes | ✓ |
| B | 3 | Delete spanning newlines | Delete ranges that cross line breaks; verify bytes/lines. | Yes | Planned |
| B | 4 | Split/Join equivalence | `split_line` followed by `join_lines` yields original bytes. | Yes | Planned |
| B | 5 | Stream vs Data equivalence | `WriteToStream` matches `GetRange`/`Data()` after edits. | Yes | Planned |
| B | 6 | UTF‑8 bytes stability | Multibyte sequences behave correctly (byte-based ops). | Yes | Planned |
| C | 1 | insert_text/delete_text | Edits at start/middle/end; `Rows()` mirrors PieceTable. | Yes | Planned |
| C | 2 | split_line/join_lines | Effects and snapshots across multiple positions. | Yes | Planned |
| C | 3 | insert_row/delete_row | Replace paragraph by row ops; verify bytes/linecount. | Yes | Planned |
| C | 4 | Cache invalidation | After each mutation, `Rows()` matches `LineCount()`. | Yes | Planned |
| D | 1 | Grouped insert undo | Contiguous typing undone/redone as a group. | Yes | Planned |
| D | 2 | Delete/Newline undo/redo | Backspace/Delete and Newline transitions across undo/redo. | Yes | Planned |
| D | 3 | Mark saved & dirty | Dirty/save markers interact correctly with undo/redo. | Yes | Planned |
| E | 1 | Search parity basic | `OptimizedSearch::find_all` vs `std::string` reference. | Yes | ✓ |
| E | 2 | Large text search | ~1 MiB random text/patterns parity. | Yes | Planned |
| F | 1 | Editor open & reload | Open via `Editor`, modify, reload, verify on-disk bytes. | Yes | Planned |
| F | 2 | Read-only toggle | Toggle and verify enforcement/behavior of saves. | Yes | Planned |
| F | 3 | Prompt lifecycle | Start/Accept/Cancel prompt doesn’t corrupt state. | Yes | Planned |
| G | 1 | Saved only newline regression | Insert text + newline; `Save` includes both bytes. | Yes | Planned |
| G | 2 | Backspace crash regression | PieceTable-backed delete/join path remains stable. | Yes | Planned |
| G | 3 | Overwrite-confirm path | Saving over existing path succeeds and is correct. | Yes | Planned |
| H | 1 | Many small edits | 10k small edits; final bytes correct within time bounds. | Yes | Planned |
| H | 2 | Consolidation equivalence | After many edits, stream vs data produce identical bytes. | Yes | Planned |

Legend: Implemented = ✓, Planned = to be added per Coverage roadmap.

### Test suites and cases

#### A) Filesystem I/O via Buffer
1) SaveAs then Save (append)
   - New buffer → `insert_text` two lines (explicit `\n`) → `SaveAs(tmp)` → insert a third line → `Save()`.
   - Assert file bytes equal exact expected string.
2) Open existing then Save
   - Seed a file on disk; `OpenFromFile(path)` → append line → `Save()`.
   - Assert file bytes updated exactly.
3) Open non-existent then SaveAs
   - `OpenFromFile(nonexistent)` → assert `IsFileBacked()==false` → insert `"hello, world\n"` → `SaveAs(path)`.
   - Read back exact bytes.
4) Trailing newline preservation
   - Case (a) last line without `\n`; (b) last line with `\n` → save and verify bytes unchanged.
5) Empty buffer saves
   - `SaveAs(tmp)` on empty buffer → 0-byte file. Then insert `"\n"` and `Save()` → 1-byte file.
6) Large file streaming
   - Insert ~1–4 MiB of data with periodic newlines. `SaveAs` then `Save`; verify size matches `content_.Size()` and bytes integrity.
7) Path normalization and tilde expansion
   - `SaveAs("~/.../file.txt")` → verify path expands to `$HOME` and file content round-trips with `OpenFromFile`.
8) Error propagation (guarded)
   - Attempt save into a non-writable path; expect `Save/SaveAs` returns false with non-empty error. Mark as skipped in environments lacking such path.

#### B) PieceTable semantics
1) Line counting and deletion across lines
   - Insert `"abc\n123\nxyz"` → 3 lines; delete middle line range → 2 lines; validate `GetLine` contents.
2) Position conversions
   - Validate `LineColToByteOffset` and `ByteOffsetToLineCol` at start/end of lines and EOF, especially around `\n`.
3) Delete spanning newlines
   - Remove a range that crosses line boundaries; verify resulting bytes, `LineCount` and line contents.
4) Split/join equivalence
   - Split at various columns; then join adjacent lines; verify bytes equal original.
5) WriteToStream vs materialized `Data()`
   - After multiple inserts/deletes (without forcing `Data()`), stream to `std::ostringstream`; compare with `GetRange(0, Size())`, then call `Data()` and re-compare.
6) UTF-8 bytes stability
   - Insert multibyte sequences (e.g., `"héllo"`, `"中文"`, emoji) as raw bytes; ensure line counting and conversions behave (byte-based API; no crashes/corruption).

#### C) Buffer editing helpers and rows cache correctness
1) `insert_text`/`delete_text`
   - Apply at start/middle/end of lines; immediately call `Rows()` and validate contents/lengths mirror PieceTable.
2) `split_line` and `join_lines`
   - Verify content effects and `Rows()` snapshots for multiple positions and consecutive operations.
3) `insert_row`/`delete_row`
   - Replace a paragraph by deleting N rows then inserting N′ rows; verify bytes and `LineCount`.
4) Cache invalidation
   - After each mutation, fetch `Rows()`; assert `Nrows() == content.LineCount()` and no stale data remains.

#### D) UndoSystem semantics
1) Grouped contiguous insert undo
   - Emulate typing at a single location via repeated `insert_text`; one `undo()` should remove the whole run; `redo()` restores it.
2) Delete/newline undo/redo
   - Simulate backspace/delete (`delete_text` and `join_lines`) and newline (`split_line`); verify content transitions across `undo()`/`redo()`.
3) Mark saved and dirty flag
   - After successful save, call `UndoSystem::mark_saved()` (via existing pathways) and ensure dirty state pairing behaves as intended (at least: `SetDirty(false)` plus save does not break undo/redo).

#### E) Search algorithms
1) Parity with `std::string::find`
   - Use `OptimizedSearch::find_all` across edge cases (empty needle/text, overlaps like `"aaaaa"` vs `"aa"`, Unicode byte sequences). Compare to reference implementation.
2) Large text
   - Random ASCII text ~1 MiB; random patterns; results match reference.

#### F) Editor non-interactive flows (no frontend)
1) Open and reload
   - Through `Editor`, open file; modify the underlying `Buffer` directly; invoke reload (`Buffer::OpenFromFile` or `cmd_reload_buffer` if you bring `Command.cc` into the test target). Verify bytes match the on-disk file after reload.
2) Read-only toggle
   - Toggle `Buffer::ToggleReadOnly()`; confirm flag value changes and that subsequent saves still execute when not read-only (or, if enforcement exists, that mutations are appropriately restricted).
3) Prompt lifecycle (headless)
   - Exercise `StartPrompt` → `AcceptPrompt` → `CancelPrompt`; ensure state resets and does not corrupt buffer/editor state.

#### G) Regression tests for reported bugs
1) “Saved only newline”
   - Build buffer content via `insert_text` followed by `split_line` for newline; `Save` then validate bytes include both the text and newline.
2) Backspace crash path
   - Mimic backspace behavior using PieceTable-backed helpers (`delete_text`/`join_lines`); ensure no dependency on legacy `rows_` mutation and no memory issues.
3) Overwrite-confirm path behavior
   - Start with non-file-backed buffer named to collide with an existing file; perform `SaveAs(existing_path)` and assert success and correctness on disk (unit test bypasses interactive confirm, validating underlying write path).

#### H) Performance/stress sanity
1) Many small edits
   - 10k single-char inserts and interleaved deletes; assert final bytes; keep within conservative runtime bounds.
2) Consolidation heuristics
   - After many edits, call both `WriteToStream` and `Data()` and verify identical bytes.

---

### Coverage roadmap
- Phase 1 (already implemented and passing):
  - Buffer I/O basics (A.1–A.3), PieceTable basics (B.1–B.2), Search parity (E.1).
- Phase 2 (add next):
  - Buffer I/O edge cases (A.4–A.7), deeper PieceTable ops (B.3–B.6), Buffer helpers and cache (C.1–C.4), Undo semantics (D.1–D.2), Regression set (G.1–G.3).
- Phase 3:
  - Editor flows (F.1–F.3), performance/stress (H.1–H.2), and optional integration of `Command.cc` into the test target to exercise non-interactive command execution paths directly.

### Notes
- Use per-test temp files under the repo root or a unique temp directory; ensure cleanup after assertions.
- For HOME-dependent tests (tilde expansion), set `HOME` in the test process if not present or skip with a clear message.
- On macOS Debug, a benign allocator warning may appear; rely on process exit code for pass/fail.
